#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define FPGA_SPI_DEVICE "/dev/spidev1.0"
#define RUNTIME_SPEED_HZ 1000000U
#define MAX_IMAGE_SIZE (8 * 1024 * 1024)
#define CONFIG_CHUNK_SIZE 256

#define CRESET_GPIO 49          /* i.MX6 GPIO2_17 */
#define CDONE_GPIO 122          /* i.MX6 GPIO4_26 */
#define STATUS_GPIO 50          /* i.MX6 GPIO2_18 / EIM_A20 */
#define CONFIG_CS_GPIO_BIT 26   /* i.MX6 GPIO2_26 / EIM_RW */

#define GPIO2_BASE 0x020a0000U
#define GPIO4_BASE 0x020a8000U
#define GPIO_MAP_SIZE 0x1000U
#define GPIO_DR_OFFSET 0x00U
#define GPIO_GDIR_OFFSET 0x04U
#define GPIO_PSR_OFFSET 0x08U

#define ECSPI2_BASE 0x0200c000U
#define ECSPI_MAP_SIZE 0x1000U
#define CCM_BASE 0x020c4000U
#define CCM_CCGR1_OFFSET 0x6cU

#define ECSPI_RXDATA 0x00U
#define ECSPI_TXDATA 0x04U
#define ECSPI_CONREG 0x08U
#define ECSPI_CONFIGREG 0x0cU
#define ECSPI_INTREG 0x10U
#define ECSPI_STATREG 0x18U
#define ECSPI_PERIODREG 0x1cU
#define ECSPI_XCH (1U << 2)
#define ECSPI_TC (1U << 7)

#ifndef CONFIG_PRE_DIVIDER
#define CONFIG_PRE_DIVIDER 2U
#endif

#define SPI_IMX_UNBIND "/sys/bus/platform/drivers/spi_imx/unbind"
#define SPI_IMX_BIND "/sys/bus/platform/drivers/spi_imx/bind"
#define ECSPI2_PLATFORM_DEVICE "200c000.spi"

#define IOMUXC_BASE 0x020e0000U
#define IOMUXC_SIZE 0x1000U
#define SW_MUX_CTL_PAD_EIM_A21 0x124U
#define SW_PAD_CTL_PAD_EIM_A21 0x4f4U
#define SW_MUX_CTL_PAD_DISP0_DAT5 0x0fcU
#define SW_PAD_CTL_PAD_DISP0_DAT5 0x410U
#define SW_MUX_CTL_PAD_EIM_A20 0x120U
#define SW_PAD_CTL_PAD_EIM_A20 0x4f0U
#define SW_MUX_CTL_PAD_EIM_CS0 0x13cU
#define SW_PAD_CTL_PAD_EIM_CS0 0x50cU
#define ECSPI2_SCLK_SELECT_INPUT 0x7f4U
#define SW_MUX_CTL_PAD_EIM_CS1 0x140U
#define SW_PAD_CTL_PAD_EIM_CS1 0x510U
#define ECSPI2_MOSI_SELECT_INPUT 0x7fcU
#define SW_MUX_CTL_PAD_EIM_OE 0x1d8U
#define SW_PAD_CTL_PAD_EIM_OE 0x5a8U
#define ECSPI2_MISO_SELECT_INPUT 0x7f8U
#define SW_MUX_CTL_PAD_EIM_RW 0x1dcU
#define SW_PAD_CTL_PAD_EIM_RW 0x5acU
#define SW_MUX_CTL_PAD_EIM_LBA 0x1d4U
#define SW_PAD_CTL_PAD_EIM_LBA 0x5a4U
#define SW_MUX_CTL_PAD_EIM_D24 0x164U
#define SW_PAD_CTL_PAD_EIM_D24 0x534U
#define SW_MUX_CTL_PAD_EIM_D25 0x168U
#define SW_PAD_CTL_PAD_EIM_D25 0x538U

#define DEFAULT_T55_PATH "/usr/share/fpga/wing_hello_spi_t55.bit.bin"
#define DEFAULT_T85_PATH "/usr/share/fpga/wing_hello_spi_t85.bit.bin"

static int write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t len = strlen(text);
    ssize_t res = write(fd, text, len);
    close(fd);
    return res == len ? 0 : -1;
}

static int export_gpio(unsigned int gpio)
{
    char number[16];
    snprintf(number, sizeof(number), "%u", gpio);
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    write(fd, number, strlen(number));
    close(fd);
    return 0;
}

static int gpio_direction(unsigned int gpio, const char *direction)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", gpio);
    return write_text(path, direction);
}

static int gpio_value(unsigned int gpio, int value)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
    return write_text(path, value ? "1" : "0");
}

static int read_gpio(unsigned int gpio)
{
    char path[96];
    char value;
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    if (read(fd, &value, 1) != 1) {
        close(fd);
        return -1;
    }
    close(fd);
    return value == '1';
}

static int configure_fpga_gpio_mux(void)
{
    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) return -1;
    volatile uint32_t *iomuxc = mmap(NULL, IOMUXC_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fd, IOMUXC_BASE);
    if (iomuxc == MAP_FAILED) {
        close(memory_fd);
        return -1;
    }

    iomuxc[SW_MUX_CTL_PAD_EIM_A21 / 4] = 5;       /* CRESET_N: GPIO2_17 */
    iomuxc[SW_PAD_CTL_PAD_EIM_A21 / 4] = 0x1b0b0;
    iomuxc[SW_MUX_CTL_PAD_DISP0_DAT5 / 4] = 0x15; /* CDONE with SION */
    iomuxc[SW_PAD_CTL_PAD_DISP0_DAT5 / 4] = 0x1b008;
    iomuxc[SW_MUX_CTL_PAD_EIM_A20 / 4] = 5;
    iomuxc[SW_PAD_CTL_PAD_EIM_A20 / 4] = 0x13008;
    iomuxc[SW_MUX_CTL_PAD_EIM_CS0 / 4] = 2;       /* ECSPI2_SCLK */
    iomuxc[SW_PAD_CTL_PAD_EIM_CS0 / 4] = 0x1b0b0;
    iomuxc[ECSPI2_SCLK_SELECT_INPUT / 4] = 2;
    iomuxc[SW_MUX_CTL_PAD_EIM_CS1 / 4] = 2;       /* ECSPI2_MOSI */
    iomuxc[SW_PAD_CTL_PAD_EIM_CS1 / 4] = 0x1b0b0;
    iomuxc[ECSPI2_MOSI_SELECT_INPUT / 4] = 2;
    iomuxc[SW_MUX_CTL_PAD_EIM_OE / 4] = 2;        /* ECSPI2_MISO */
    iomuxc[SW_PAD_CTL_PAD_EIM_OE / 4] = 0x1b0b0;
    iomuxc[ECSPI2_MISO_SELECT_INPUT / 4] = 2;
    iomuxc[SW_MUX_CTL_PAD_EIM_RW / 4] = 5;        /* GPIO2_26 */
    iomuxc[SW_PAD_CTL_PAD_EIM_RW / 4] = 0x1b0b0;
    iomuxc[SW_MUX_CTL_PAD_EIM_LBA / 4] = 5;       /* GPIO2_27 */
    iomuxc[SW_PAD_CTL_PAD_EIM_LBA / 4] = 0x1b0b0;
    iomuxc[SW_MUX_CTL_PAD_EIM_D24 / 4] = 5;       /* GPIO3_24 */
    iomuxc[SW_PAD_CTL_PAD_EIM_D24 / 4] = 0x1b0b0;
    iomuxc[SW_MUX_CTL_PAD_EIM_D25 / 4] = 5;       /* GPIO3_25 */
    iomuxc[SW_PAD_CTL_PAD_EIM_D25 / 4] = 0x1b0b0;
    __sync_synchronize();

    munmap((void *)iomuxc, IOMUXC_SIZE);
    close(memory_fd);
    return 0;
}

static int drive_config_cs(int value)
{
    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) return -1;
    volatile uint32_t *gpio2 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fd, GPIO2_BASE);
    if (gpio2 == MAP_FAILED) {
        close(memory_fd);
        return -1;
    }
    uint32_t bit = 1U << CONFIG_CS_GPIO_BIT;
    gpio2[GPIO_GDIR_OFFSET / 4] |= bit;
    if (value)
        gpio2[GPIO_DR_OFFSET / 4] |= bit;
    else
        gpio2[GPIO_DR_OFFSET / 4] &= ~bit;
    __sync_synchronize();
    munmap((void *)gpio2, GPIO_MAP_SIZE);
    close(memory_fd);
    return 0;
}

static int direct_spi_burst(volatile uint32_t *spi, const uint8_t *data, size_t length)
{
    uint32_t con = spi[ECSPI_CONREG / 4];
    con &= ~0xfff00008U;
    con |= (uint32_t)(length * 8U - 1U) << 20;
    spi[ECSPI_CONREG / 4] = con;
    spi[ECSPI_PERIODREG / 4] = 0x80;

    for (size_t offset = 0; offset < length; offset += 4) {
        uint32_t word = 0;
        size_t count = length - offset < 4 ? length - offset : 4;
        for (size_t i = 0; i < count; ++i)
            word |= (uint32_t)data[offset + i] << (8U * (count - 1U - i));
        spi[ECSPI_TXDATA / 4] = word;
    }

    spi[ECSPI_STATREG / 4] = ECSPI_TC;
    spi[ECSPI_CONREG / 4] = con | ECSPI_XCH;

    int timeout = 50000;
    while (!(spi[ECSPI_STATREG / 4] & ECSPI_TC)) {
        if (--timeout == 0) return -1;
        usleep(1);
    }
    while (!(spi[ECSPI_STATREG / 4] & (1U << 3))) {
        (void)spi[ECSPI_RXDATA / 4];
    }
    return 0;
}

static int upload_raw_stream(const char *image_path)
{
    int image_fd = open(image_path, O_RDONLY);
    if (image_fd < 0) {
        fprintf(stderr, "[ERROR] Cannot open bitstream '%s': %s\n", image_path, strerror(errno));
        return -1;
    }

    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) {
        close(image_fd);
        return -1;
    }

    volatile uint32_t *ccm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fd, CCM_BASE);
    volatile uint32_t *spi = mmap(NULL, ECSPI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fd, ECSPI2_BASE);
    if (ccm == MAP_FAILED || spi == MAP_FAILED) {
        if (ccm != MAP_FAILED) munmap((void *)ccm, 0x1000);
        close(memory_fd);
        close(image_fd);
        return -1;
    }

    ccm[CCM_CCGR1_OFFSET / 4] |= 0xffU;
    spi[ECSPI_CONREG / 4] = 0;
    spi[ECSPI_CONFIGREG / 4] = 0x100U; // Channel 0 SS_CTL
    spi[ECSPI_STATREG / 4] = 0xff;
    spi[ECSPI_CONREG / 4] = (CONFIG_PRE_DIVIDER << 12) | 0x11U;

    uint8_t buffer[CONFIG_CHUNK_SIZE];
    ssize_t count;
    size_t total = 0;

    struct stat st;
    fstat(image_fd, &st);

    while ((count = read(image_fd, buffer, sizeof(buffer))) > 0) {
        size_t aligned = (size_t)count;
        size_t rem = 0;
        if (total + (size_t)count == (size_t)st.st_size) {
            aligned = (size_t)count & ~3U;
            rem = (size_t)count - aligned;
        }
        if ((aligned && direct_spi_burst(spi, buffer, aligned) < 0) ||
            (rem && direct_spi_burst(spi, buffer + aligned, rem) < 0)) {
            munmap((void *)spi, ECSPI_MAP_SIZE);
            munmap((void *)ccm, 0x1000);
            close(memory_fd);
            close(image_fd);
            return -1;
        }
        total += (size_t)count;
    }

    // 16 trailing 0xFF clocks to complete internal Trion startup state machine
    uint8_t trailing[16];
    memset(trailing, 0xff, sizeof(trailing));
    direct_spi_burst(spi, trailing, sizeof(trailing));

    munmap((void *)spi, ECSPI_MAP_SIZE);
    munmap((void *)ccm, 0x1000);
    close(memory_fd);
    close(image_fd);
    return 0;
}

static int perform_upload(const char *image_path)
{
    // 1. Reset pulse CRESET_N (GPIO2_17)
    drive_config_cs(1);
    gpio_value(CRESET_GPIO, 0);
    usleep(2000); // 2ms
    gpio_value(CRESET_GPIO, 1);
    usleep(5000); // 5ms wait for Trion ready

    // 2. Stream bitstream
    if (upload_raw_stream(image_path) < 0) {
        return -1;
    }

    usleep(5000);
    return read_gpio(CDONE_GPIO); // Returns 1 (HIGH) or 0 (LOW)
}

static int verify_hello(void)
{
    int fd = open(FPGA_SPI_DEVICE, O_RDWR);
    if (fd < 0) return -1;

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = RUNTIME_SPEED_HZ;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    uint8_t tx[18] = { 0x13, 0x37 };
    uint8_t rx[18] = { 0 };

    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (uintptr_t)tx;
    tr.rx_buf = (uintptr_t)rx;
    tr.len = sizeof(tx);
    tr.speed_hz = speed;
    tr.bits_per_word = 8;

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    if (memcmp(rx + 2, "Hello from Wing!", 16) == 0) {
        printf("[+] Runtime SPI Verification PASS: Received 'Hello from Wing!'\n");
        return 0;
    } else {
        printf("[*] Runtime SPI response: ");
        for (int i = 0; i < 18; i++) printf("%02x ", rx[i]);
        printf("\n");
        return 1;
    }
}

int main(int argc, char **argv)
{
    const char *target_device = "auto";
    const char *custom_path = NULL;
    int do_verify = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            target_device = argv[++i];
        } else if (strcmp(argv[i], "-t55") == 0) {
            target_device = "t55";
        } else if (strcmp(argv[i], "-t85") == 0) {
            target_device = "t85";
        } else if (strcmp(argv[i], "--no-verify") == 0) {
            do_verify = 0;
        } else if (argv[i][0] != '-') {
            custom_path = argv[i];
        }
    }

    printf("=================================================================\n");
    printf("   BEHRINGER WING FPGA UPLOADER (Auto-Detection via CDONE Pin)\n");
    printf("=================================================================\n");

    configure_fpga_gpio_mux();
    export_gpio(CRESET_GPIO);
    gpio_direction(CRESET_GPIO, "high");
    export_gpio(CDONE_GPIO);
    gpio_direction(CDONE_GPIO, "in");

    // Unbind kernel ECSPI2 driver during passive bitstream upload
    write_text(SPI_IMX_UNBIND, ECSPI2_PLATFORM_DEVICE);

    int cdone = 0;
    const char *detected_platform = NULL;

    if (custom_path) {
        printf("[*] Uploading custom bitstream: %s\n", custom_path);
        cdone = perform_upload(custom_path);
        if (cdone == 1) detected_platform = "Custom Image (CDONE=HIGH)";
    } else if (strcasecmp(target_device, "t55") == 0) {
        printf("[*] Target explicitly set to T55. Uploading %s...\n", DEFAULT_T55_PATH);
        cdone = perform_upload(DEFAULT_T55_PATH);
        if (cdone == 1) detected_platform = "Efinix Trion T55F484";
    } else if (strcasecmp(target_device, "t85") == 0) {
        printf("[*] Target explicitly set to T85. Uploading %s...\n", DEFAULT_T85_PATH);
        cdone = perform_upload(DEFAULT_T85_PATH);
        if (cdone == 1) detected_platform = "Efinix Trion T85F484";
    } else {
        // AUTO-DETECTION MODE:
        // 1. Try T55 bitstream first
        printf("[*] Auto-detecting FPGA platform via hardware CDONE pin (GPIO4_26)...\n");
        printf("[*] Step 1: Testing T55 payload (%s)...\n", DEFAULT_T55_PATH);
        cdone = perform_upload(DEFAULT_T55_PATH);
        if (cdone == 1) {
            detected_platform = "Efinix Trion T55F484";
            printf("[+] CDONE went HIGH! Silicon identified as Efinix Trion T55F484.\n");
        } else {
            printf("[-] CDONE remained LOW (T55 rejected). Step 2: Testing T85 payload (%s)...\n", DEFAULT_T85_PATH);
            cdone = perform_upload(DEFAULT_T85_PATH);
            if (cdone == 1) {
                detected_platform = "Efinix Trion T85F484";
                printf("[+] CDONE went HIGH! Silicon identified as Efinix Trion T85F484.\n");
            }
        }
    }

    // Re-bind Linux ECSPI2 driver
    drive_config_cs(1);
    write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
    usleep(10000);

    if (cdone != 1) {
        fprintf(stderr, "\n[FAIL] Configuration FAILED! CDONE (GPIO4_26) remained LOW.\n");
        return 1;
    }

    printf("\n=================================================================\n");
    printf("   SUCCESS: FPGA CONFIGURED! Platform: %s\n", detected_platform);
    printf("   Pin CDONE (GPIO4_26): HIGH\n");
    printf("=================================================================\n");

    if (do_verify) {
        verify_hello();
    }
    return 0;
}
