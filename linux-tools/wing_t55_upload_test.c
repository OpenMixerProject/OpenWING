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
#define MAX_IMAGE_SIZE (4 * 1024 * 1024)
#define CONFIG_CHUNK_SIZE 256
#define CRESET_GPIO 49   /* i.MX6 GPIO2_17 */
#define CDONE_GPIO 122   /* i.MX6 GPIO4_26 */
#define CONFIG_CS_GPIO_BIT 26 /* i.MX6 GPIO2_26 / EIM_RW */
#define GPIO2_BASE 0x020a0000U
#define GPIO_MAP_SIZE 0x1000U
#define GPIO_DR_OFFSET 0x00U
#define GPIO_GDIR_OFFSET 0x04U
#define ECSPI2_BASE 0x0200c000U
#define ECSPI1_BASE 0x02008000U
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
#define ECSPI1_PLATFORM_DEVICE "2008000.spi"
#define RUNTIME_CS_GPIO_BIT 24 /* i.MX6 GPIO3_24 */
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
#define SW_MUX_CTL_PAD_EIM_CS1 0x140U
#define SW_PAD_CTL_PAD_EIM_CS1 0x510U
#define SW_MUX_CTL_PAD_EIM_OE 0x1d8U
#define SW_PAD_CTL_PAD_EIM_OE 0x5a8U
#define SW_MUX_CTL_PAD_EIM_RW 0x1dcU
#define SW_PAD_CTL_PAD_EIM_RW 0x5acU
#define SW_MUX_CTL_PAD_EIM_LBA 0x1d4U
#define SW_PAD_CTL_PAD_EIM_LBA 0x5a4U
#define SW_MUX_CTL_PAD_EIM_D24 0x164U
#define SW_PAD_CTL_PAD_EIM_D24 0x534U
#define SW_MUX_CTL_PAD_EIM_D25 0x168U
#define SW_PAD_CTL_PAD_EIM_D25 0x538U
#define ECSPI2_SCLK_SELECT_INPUT 0x7f4U
#define ECSPI2_MISO_SELECT_INPUT 0x7f8U
#define ECSPI2_MOSI_SELECT_INPUT 0x7fcU
#define GPIO3_BASE 0x020a4000U
#define GPIO4_BASE 0x020a8000U
#define GPIO_PSR_OFFSET 0x08U

static int configure_fpga_gpio_mux(void)
{
    volatile uint32_t *iomuxc;
    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (memory_fd < 0)
        return -1;
    iomuxc = mmap(NULL, IOMUXC_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                  memory_fd, IOMUXC_BASE);
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

static int initialize_stock_aux_gpios(void)
{
    volatile uint32_t *gpio2 = MAP_FAILED;
    volatile uint32_t *gpio3 = MAP_FAILED;
    const uint32_t gpio2_bits = (1U << 26) | (1U << 27);
    const uint32_t gpio3_bits = (1U << 24) | (1U << 25);
    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    int result = -1;

    if (memory_fd < 0)
        return -1;
    gpio2 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 memory_fd, GPIO2_BASE);
    gpio3 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 memory_fd, GPIO3_BASE);
    if (gpio2 == MAP_FAILED || gpio3 == MAP_FAILED)
        goto cleanup;
    gpio2[GPIO_GDIR_OFFSET / 4] |= gpio2_bits;
    gpio3[GPIO_GDIR_OFFSET / 4] |= gpio3_bits;
    gpio2[GPIO_DR_OFFSET / 4] |= gpio2_bits;
    gpio3[GPIO_DR_OFFSET / 4] |= gpio3_bits;
    __sync_synchronize();
    result = 0;

cleanup:
    if (gpio2 != MAP_FAILED)
        munmap((void *)gpio2, GPIO_MAP_SIZE);
    if (gpio3 != MAP_FAILED)
        munmap((void *)gpio3, GPIO_MAP_SIZE);
    close(memory_fd);
    return result;
}

static int drive_config_cs(int value)
{
    volatile uint32_t *gpio2;
    uint32_t bit = 1U << CONFIG_CS_GPIO_BIT;
    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (memory_fd < 0)
        return -1;
    gpio2 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 memory_fd, GPIO2_BASE);
    if (gpio2 == MAP_FAILED) {
        close(memory_fd);
        return -1;
    }

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

static int write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY);
    ssize_t length = (ssize_t)strlen(text);
    ssize_t result;

    if (fd < 0)
        return -1;
    result = write(fd, text, (size_t)length);
    close(fd);
    return result == length ? 0 : -1;
}

static int export_gpio(unsigned int gpio)
{
    char number[16];
    int fd;

    snprintf(number, sizeof(number), "%u", gpio);
    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0)
        return -1;
    if (write(fd, number, strlen(number)) < 0 && errno != EBUSY) {
        close(fd);
        return -1;
    }
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
    int fd;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if (read(fd, &value, 1) != 1) {
        close(fd);
        return -1;
    }
    close(fd);
    return value == '1';
}

static int configure_spi(int fd, uint32_t speed)
{
    /* Runtime CPU-to-FPGA SPI uses ECSPI1 chip-select 1 in mode 0. */
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
        return -1;
    return 0;
}

static int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t length,
                        uint32_t speed)
{
    struct spi_ioc_transfer tr;

    if (length == 0)
        return -1;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (uintptr_t)tx;
    tr.rx_buf = (uintptr_t)rx;
    tr.len = length;
    tr.speed_hz = speed;
    tr.bits_per_word = 8;
    return ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0 ? -1 : 0;
}

static int direct_spi_burst_io(volatile uint32_t *spi, const uint8_t *data,
                               uint8_t *rx, size_t length)
{
    uint32_t con;
    size_t offset;
    int timeout;

    if (length == 0 || length > CONFIG_CHUNK_SIZE)
        return -1;

    con = spi[ECSPI_CONREG / 4];
    con &= ~0xfff00008U;
    con |= (uint32_t)(length * 8U - 1U) << 20;
    spi[ECSPI_CONREG / 4] = con;
    spi[ECSPI_PERIODREG / 4] = 0x80;

    for (offset = 0; offset < length; offset += 4) {
        uint32_t word = 0;
        size_t count = length - offset < 4 ? length - offset : 4;
        for (size_t i = 0; i < count; ++i)
            word = (word << 8) | data[offset + i];
        spi[ECSPI_TXDATA / 4] = word;
    }

    spi[ECSPI_STATREG / 4] = ECSPI_TC;
    spi[ECSPI_INTREG / 4] = 0;
    spi[ECSPI_CONREG / 4] = con | ECSPI_XCH;
    /* Stock waits on the transfer-complete IRQ (STATREG.TC), not XCH.
     * XCH is only the exchange trigger and may clear before the wire transfer
     * has finished, so using it as completion can overrun the TX FIFO. */
    for (timeout = 100000; timeout > 0; --timeout) {
        if (spi[ECSPI_STATREG / 4] & ECSPI_TC)
            break;
        usleep(1);
    }
    if (timeout == 0)
        return -1;
    spi[ECSPI_STATREG / 4] = ECSPI_TC;
    for (offset = 0; offset < length; offset += 4) {
        uint32_t word = spi[ECSPI_RXDATA / 4];
        size_t count = length - offset < 4 ? length - offset : 4;
        if (rx) {
            for (size_t i = 0; i < count; ++i)
                rx[offset + i] = (uint8_t)(word >> (8U * (count - 1U - i)));
        }
    }
    return 0;
}

static int direct_spi_burst(volatile uint32_t *spi, const uint8_t *data,
                            size_t length)
{
    return direct_spi_burst_io(spi, data, NULL, length);
}

static int upload_image(const char *path, unsigned int spi_mode,
                        size_t trailing_byte_count)
{
    uint8_t buffer[CONFIG_CHUNK_SIZE];
    uint8_t trailing_clocks[CONFIG_CHUNK_SIZE];
    volatile uint32_t *spi = MAP_FAILED;
    volatile uint32_t *ccm = MAP_FAILED;
    volatile uint32_t *gpio2 = MAP_FAILED;
    volatile uint32_t *gpio4 = MAP_FAILED;
    struct stat image_stat;
    size_t total = 0;
    ssize_t count;
    int image_fd = -1;
    int memory_fd = -1;
    int result = -1;
    int cdone_seen = 0;
    int status_low_seen = 0;
    uint32_t configreg = 0x100U;

    if (spi_mode & 1U)
        configreg |= 1U;                 /* SCLK_PHA channel 0 */
    if (spi_mode & 2U)
        configreg |= (1U << 4) | (1U << 20); /* SCLK_POL + idle level */

    image_fd = open(path, O_RDONLY);
    if (image_fd < 0) {
        perror("open image");
        goto cleanup;
    }
    if (fstat(image_fd, &image_stat) < 0 || image_stat.st_size <= 0 ||
        image_stat.st_size > MAX_IMAGE_SIZE) {
        fprintf(stderr, "Unexpected image size (expected 1..%d bytes)\n",
                MAX_IMAGE_SIZE);
        goto cleanup;
    }

    memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) {
        perror("open /dev/mem for ECSPI2");
        goto cleanup;
    }
    ccm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
               memory_fd, CCM_BASE);
    spi = mmap(NULL, ECSPI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
               memory_fd, ECSPI2_BASE);
    gpio2 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 memory_fd, GPIO2_BASE);
    gpio4 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 memory_fd, GPIO4_BASE);
    if (ccm == MAP_FAILED || spi == MAP_FAILED || gpio2 == MAP_FAILED ||
        gpio4 == MAP_FAILED) {
        perror("mmap ECSPI2/CCM");
        goto cleanup;
    }
    ccm[CCM_CCGR1_OFFSET / 4] |= 0xffU;
    spi[ECSPI_CONREG / 4] = 0;
    /* Stock sets channel-0 SS_CTL; this keeps its burst framing continuous. */
    spi[ECSPI_CONFIGREG / 4] = configreg;
    spi[ECSPI_STATREG / 4] = 0xff;
    /* Stock uses PRE_DIVIDER=2. Override only for signal-margin diagnosis. */
    spi[ECSPI_CONREG / 4] = (CONFIG_PRE_DIVIDER << 12) | 0x11U;

    while ((count = read(image_fd, buffer, sizeof(buffer))) > 0) {
        size_t aligned = (size_t)count;
        size_t remainder = 0;

        /* Stock sends floor(size/4) via its 32-bit path, then 1..3 bytes. */
        if (total + (size_t)count == (size_t)image_stat.st_size) {
            aligned = (size_t)count & ~(size_t)3;
            remainder = (size_t)count - aligned;
        }
        if ((aligned && direct_spi_burst(spi, buffer, aligned) < 0) ||
            (remainder && direct_spi_burst(spi, buffer + aligned, remainder) < 0)) {
            fprintf(stderr, "direct ECSPI2 configuration transfer failed after %zu bytes\n",
                    total);
            goto cleanup;
        }
        total += (size_t)count;
        if (!status_low_seen && !(gpio2[GPIO_PSR_OFFSET / 4] & (1U << 18))) {
            printf("GPIO2_18 first observed LOW after %zu bytes\n", total);
            status_low_seen = 1;
        }
        if (!cdone_seen && (gpio4[GPIO_PSR_OFFSET / 4] & (1U << 26))) {
            printf("CDONE first observed HIGH after %zu bytes\n", total);
            cdone_seen = 1;
        }
    }
    if (count < 0) {
        perror("read image");
        goto cleanup;
    }

    /* Stock emits 16 all-ones bytes; larger counts are diagnostic only. */
    memset(trailing_clocks, 0xff, sizeof(trailing_clocks));
    for (size_t sent = 0; sent < trailing_byte_count;) {
        size_t burst = trailing_byte_count - sent;
        if (burst > sizeof(trailing_clocks))
            burst = sizeof(trailing_clocks);
        if (direct_spi_burst(spi, trailing_clocks, burst) < 0) {
            fprintf(stderr, "direct ECSPI2 trailing clocks failed\n");
            goto cleanup;
        }
        sent += burst;
    }
    printf("Uploaded %zu image bytes plus %zu trailing 0xff bytes; "
           "GPIO2_18=%s\n", total, trailing_byte_count,
           (gpio2[GPIO_PSR_OFFSET / 4] & (1U << 18)) ? "HIGH" : "LOW");
    result = 0;

cleanup:
    if (spi != MAP_FAILED)
        munmap((void *)spi, ECSPI_MAP_SIZE);
    if (ccm != MAP_FAILED)
        munmap((void *)ccm, 0x1000);
    if (gpio2 != MAP_FAILED)
        munmap((void *)gpio2, GPIO_MAP_SIZE);
    if (gpio4 != MAP_FAILED)
        munmap((void *)gpio4, GPIO_MAP_SIZE);
    if (memory_fd >= 0)
        close(memory_fd);
    if (image_fd >= 0)
        close(image_fd);
    return result;
}

static int query_responder(int spi_fd)
{
    static const uint8_t expected[] = "TRION READY\n";
    uint8_t tx[4 + sizeof(expected) - 1] = {'P', 'I', 'N', 'G'};
    uint8_t rx[sizeof(tx)];

    memset(rx, 0, sizeof(rx));
    if (spi_transfer(spi_fd, tx, rx, sizeof(tx), RUNTIME_SPEED_HZ) < 0) {
        perror("runtime SPI transfer");
        return -1;
    }

    printf("Runtime RX:");
    for (size_t i = 0; i < sizeof(rx); ++i)
        printf(" %02x", rx[i]);
    printf("\n");

    if (memcmp(rx + 4, expected, sizeof(expected) - 1) != 0) {
        fprintf(stderr, "Response mismatch\n");
        return -1;
    }
    printf("PASS: received TRION READY\\n\n");
    return 0;
}

static int query_responder_mmio(void)
{
    static const uint8_t expected[] = "TRION READY\n";
    uint8_t tx[4 + sizeof(expected) - 1] = {'P', 'I', 'N', 'G'};
    uint8_t rx[sizeof(tx)];
    volatile uint32_t *ccm = MAP_FAILED;
    volatile uint32_t *spi = MAP_FAILED;
    volatile uint32_t *gpio2 = MAP_FAILED;
    const uint32_t cs_bit = 1U << CONFIG_CS_GPIO_BIT;
    int memory_fd = -1;
    int result = -1;
    int driver_unbound = 0;

    memset(rx, 0, sizeof(rx));
    if (write_text(SPI_IMX_UNBIND, ECSPI2_PLATFORM_DEVICE) < 0) {
        perror("unbind Linux ECSPI2 driver");
        return -1;
    }
    driver_unbound = 1;
    memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0)
        goto cleanup;
    ccm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
               memory_fd, CCM_BASE);
    spi = mmap(NULL, ECSPI_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
               memory_fd, ECSPI2_BASE);
    gpio2 = mmap(NULL, GPIO_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                 memory_fd, GPIO2_BASE);
    if (ccm == MAP_FAILED || spi == MAP_FAILED || gpio2 == MAP_FAILED)
        goto cleanup;

    ccm[CCM_CCGR1_OFFSET / 4] |= 0xffU;
    gpio2[GPIO_GDIR_OFFSET / 4] |= cs_bit;
    gpio2[GPIO_DR_OFFSET / 4] |= cs_bit;

    spi[ECSPI_CONREG / 4] = 0;
    spi[ECSPI_CONFIGREG / 4] = 0x100U;
    spi[ECSPI_STATREG / 4] = 0xffU;
    /* 60 MHz / (14 + 1) / 2^2 = 1 MHz. */
    spi[ECSPI_CONREG / 4] = (14U << 12) | (2U << 8) | 0x11U;

    gpio2[GPIO_DR_OFFSET / 4] &= ~cs_bit;
    __sync_synchronize();
    if (direct_spi_burst_io(spi, tx, rx, sizeof(tx)) < 0) {
        fprintf(stderr, "direct ECSPI2 runtime transfer failed\n");
        goto cleanup;
    }
    gpio2[GPIO_DR_OFFSET / 4] |= cs_bit;
    __sync_synchronize();

    printf("Runtime MMIO RX:");
    for (size_t i = 0; i < sizeof(rx); ++i)
        printf(" %02x", rx[i]);
    printf("\n");
    if (memcmp(rx + 4, expected, sizeof(expected) - 1) != 0) {
        fprintf(stderr, "MMIO response mismatch\n");
        goto cleanup;
    }
    printf("PASS: received TRION READY\\n via direct ECSPI2\n");
    result = 0;

cleanup:
    if (gpio2 != MAP_FAILED) {
        gpio2[GPIO_DR_OFFSET / 4] |= cs_bit;
        munmap((void *)gpio2, GPIO_MAP_SIZE);
    }
    if (spi != MAP_FAILED)
        munmap((void *)spi, ECSPI_MAP_SIZE);
    if (ccm != MAP_FAILED)
        munmap((void *)ccm, 0x1000);
    if (memory_fd >= 0)
        close(memory_fd);
    if (driver_unbound && write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE) < 0) {
        perror("rebind Linux ECSPI2 driver");
        result = -1;
    }
    return result;
}

int main(int argc, char **argv)
{
    const char *image_path;
    int spi_fd;
    int cdone;
    int upload_only = 0;
    int config_cs_during_upload = 1;
    unsigned int spi_mode = 0;
    unsigned int reset_low_us = 2000;
    unsigned int reset_release_us = 5000;
    size_t trailing_byte_count = 16;

    if (configure_fpga_gpio_mux() < 0) {
        perror("configure CRESET_N/CDONE pin mux");
        return 1;
    }
    if (initialize_stock_aux_gpios() < 0) {
        perror("initialize stock aux GPIOs");
        return 1;
    }

    if (argc == 2 && strcmp(argv[1], "--query-only") == 0) {
        spi_fd = open(FPGA_SPI_DEVICE, O_RDWR);
        if (spi_fd < 0 || configure_spi(spi_fd, RUNTIME_SPEED_HZ) < 0) {
            perror("open/configure " FPGA_SPI_DEVICE);
            return 1;
        }
        cdone = query_responder(spi_fd);
        close(spi_fd);
        return cdone < 0 ? 1 : 0;
    }
    if (argc == 2 && strcmp(argv[1], "--query-mmio-only") == 0)
        return query_responder_mmio() < 0 ? 1 : 0;

    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s IMAGE [--upload-only] [--config-cs-low] "
                "[--reset-low-us N] [--reset-release-us N] "
                "[--spi-mode 0..3] [--trailing-bytes N]\n",
                argv[0]);
        return 2;
    }
    image_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--upload-only") == 0) {
            upload_only = 1;
        } else if (strcmp(argv[i], "--config-cs-low") == 0) {
            config_cs_during_upload = 0;
        } else if (strcmp(argv[i], "--reset-low-us") == 0 && i + 1 < argc) {
            reset_low_us = (unsigned int)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--reset-release-us") == 0 && i + 1 < argc) {
            reset_release_us = (unsigned int)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--spi-mode") == 0 && i + 1 < argc) {
            spi_mode = (unsigned int)strtoul(argv[++i], NULL, 0);
            if (spi_mode > 3) {
                fprintf(stderr, "SPI mode must be 0..3\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--trailing-bytes") == 0 && i + 1 < argc) {
            trailing_byte_count = (size_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "Unknown/incomplete option: %s\n", argv[i]);
            return 2;
        }
    }
    if (configure_fpga_gpio_mux() < 0) {
        perror("configure CRESET_N/CDONE pin mux");
        return 1;
    }
    if (initialize_stock_aux_gpios() < 0) {
        perror("initialize stock FPGA auxiliary GPIOs");
        return 1;
    }
    if (export_gpio(CRESET_GPIO) < 0 ||
        gpio_direction(CRESET_GPIO, "high") < 0) {
        perror("prepare CRESET_N GPIO2_17");
        return 1;
    }
    if (export_gpio(CDONE_GPIO) < 0 || gpio_direction(CDONE_GPIO, "in") < 0) {
        perror("prepare CDONE GPIO4_26");
        return 1;
    }

    if (write_text(SPI_IMX_UNBIND, ECSPI2_PLATFORM_DEVICE) < 0) {
        perror("unbind Linux ECSPI2 driver");
        return 1;
    }

    /* Stock sequence: auxiliary GPIOs high, CRESET_N low, then high. */
    if (drive_config_cs(config_cs_during_upload) < 0) {
        perror("set configuration CS GPIO2_26");
        write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
        return 1;
    }
    if (gpio_value(CRESET_GPIO, 0) < 0) {
        perror("assert CRESET_N");
        write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
        return 1;
    }
    usleep(reset_low_us);
    if (gpio_value(CRESET_GPIO, 1) < 0) {
        perror("release CRESET_N");
        write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
        return 1;
    }
    usleep(reset_release_us);

    printf("Upload setup: GPIO2_26=%s, CRESET_N low=%u us, "
           "release wait=%u us, SPI mode=%u\n",
           config_cs_during_upload ? "HIGH" : "LOW",
           reset_low_us, reset_release_us, spi_mode);
    if (upload_image(image_path, spi_mode, trailing_byte_count) < 0) {
        drive_config_cs(1);
        write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
        return 1;
    }

    /* Restore the stock idle state (also ends the --config-cs-low test). */
    if (drive_config_cs(1) < 0) {
        perror("release configuration CS GPIO2_26");
        write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
        return 1;
    }
    if (gpio_value(CRESET_GPIO, 1) < 0) {
        perror("final CRESET_N release");
        write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE);
        return 1;
    }

    if (write_text(SPI_IMX_BIND, ECSPI2_PLATFORM_DEVICE) < 0) {
        perror("rebind Linux ECSPI2 driver");
        return 1;
    }
    usleep(10000);
    spi_fd = open(FPGA_SPI_DEVICE, O_RDWR);
    if (spi_fd < 0 || configure_spi(spi_fd, RUNTIME_SPEED_HZ) < 0) {
        perror("open/configure " FPGA_SPI_DEVICE);
        return 1;
    }
    usleep(5000);
    cdone = read_gpio(CDONE_GPIO);
    printf("CDONE (GPIO4_26): %s\n", cdone == 1 ? "HIGH" : "LOW/ERROR");
    if (cdone != 1) {
        close(spi_fd);
        return 1;
    }

    if (upload_only) {
        close(spi_fd);
        return 0;
    }

    if (query_responder(spi_fd) < 0) {
        close(spi_fd);
        return 1;
    }
    close(spi_fd);
    return 0;
}
