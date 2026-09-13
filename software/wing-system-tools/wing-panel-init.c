#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define IOMUXC_BASE          0x020E0000u
#define GPIO5_BASE           0x020AC000u
#define GPIO_DR_OFF          0x0000u
#define GPIO_GDIR_OFF        0x0004u

#define MUX_CSI0_DAT12       0x0054u
#define MUX_CSI0_DAT13       0x0058u
#define PAD_CSI0_DAT12       0x0368u
#define PAD_CSI0_DAT13       0x036Cu
#define SEL_UART4_RX         0x0914u

#define MUX_CSI0_VSYNC       0x0098u
#define MUX_CSI0_DAT4        0x0074u
#define MUX_PNLC_STOCK_GPIO  0x0080u
#define PAD_CSI0_VSYNC       0x03ACu
#define PAD_CSI0_DAT4        0x0388u
#define PAD_PNLC_STOCK_GPIO  0x0394u

#define UART4_MUX_MODE       0x3u
#define GPIO_MUX_MODE        0x5u
#define UART4_RX_DAISY       0x1u
#define PAD_UART_TX_STOCK    0x00000018u
#define PAD_UART_RX_STOCK    0x0000B000u
#define PAD_GPIO_STOCK       0x00000018u

#define PNLC_BOOT0_BIT       21u
#define PNLC_NRST_BIT        22u
#define PNLC_STOCK_GPIO_BIT  30u

#define PNLC_UART_PATH       "/dev/ttymxc3"
#define DEFAULT_SCRIPT_PATH  "/etc/wing-panel-init.txt"

struct mmio {
    int fd;
    size_t map_len;
    uint8_t *iomuxc;
    uint8_t *gpio5;
};

static int serial_open_raw(const char *path);

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void sleep_ms(unsigned int ms)
{
    usleep(ms * 1000u);
}

static uint32_t readl_ptr(const uint8_t *base, uint32_t off)
{
    return *(volatile const uint32_t *)(base + off);
}

static void writel_ptr(uint8_t *base, uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(base + off) = value;
}

static void mmio_init(struct mmio *mmio)
{
    const size_t map_len = 0x1000;

    printf("wing_panel_init: mmio_init starting...\n"); fflush(stdout);
    memset(mmio, 0, sizeof(*mmio));
    mmio->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mmio->fd < 0)
        die("open /dev/mem");

    mmio->map_len = map_len;
    mmio->iomuxc = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mmio->fd, IOMUXC_BASE);
    if (mmio->iomuxc == MAP_FAILED)
        die("mmap iomuxc");

    mmio->gpio5 = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mmio->fd, GPIO5_BASE);
    if (mmio->gpio5 == MAP_FAILED)
        die("mmap gpio5");
    printf("wing_panel_init: mmio_init completed successfully!\n"); fflush(stdout);
}

static void mmio_close(struct mmio *mmio)
{
    if (mmio->gpio5 && mmio->gpio5 != MAP_FAILED)
        munmap(mmio->gpio5, mmio->map_len);
    if (mmio->iomuxc && mmio->iomuxc != MAP_FAILED)
        munmap(mmio->iomuxc, mmio->map_len);
    if (mmio->fd >= 0)
        close(mmio->fd);
}

static void pinmux_uart4_and_panel_gpio(struct mmio *mmio)
{
    writel_ptr(mmio->iomuxc, PAD_CSI0_DAT12, PAD_UART_TX_STOCK);
    writel_ptr(mmio->iomuxc, MUX_CSI0_DAT12, UART4_MUX_MODE);
    writel_ptr(mmio->iomuxc, PAD_CSI0_DAT13, PAD_UART_RX_STOCK);
    writel_ptr(mmio->iomuxc, MUX_CSI0_DAT13, UART4_MUX_MODE);
    writel_ptr(mmio->iomuxc, SEL_UART4_RX, UART4_RX_DAISY);

    writel_ptr(mmio->iomuxc, MUX_CSI0_VSYNC, GPIO_MUX_MODE);
    writel_ptr(mmio->iomuxc, MUX_CSI0_DAT4, GPIO_MUX_MODE);
    writel_ptr(mmio->iomuxc, MUX_PNLC_STOCK_GPIO, GPIO_MUX_MODE);
    writel_ptr(mmio->iomuxc, PAD_CSI0_VSYNC, PAD_GPIO_STOCK);
    writel_ptr(mmio->iomuxc, PAD_CSI0_DAT4, PAD_GPIO_STOCK);
    writel_ptr(mmio->iomuxc, PAD_PNLC_STOCK_GPIO, PAD_UART_RX_STOCK);
}

static void gpio5_set_dir_output(struct mmio *mmio, uint32_t bits)
{
    uint32_t gdir = readl_ptr(mmio->gpio5, GPIO_GDIR_OFF);
    gdir |= bits;
    writel_ptr(mmio->gpio5, GPIO_GDIR_OFF, gdir);
}

static void gpio5_write_bits(struct mmio *mmio, uint32_t mask, uint32_t value)
{
    uint32_t dr = readl_ptr(mmio->gpio5, GPIO_DR_OFF);
    dr &= ~mask;
    dr |= value & mask;
    writel_ptr(mmio->gpio5, GPIO_DR_OFF, dr);
}

static void pnlc_boot_app(struct mmio *mmio)
{
    const uint32_t boot0 = 1u << PNLC_BOOT0_BIT;
    const uint32_t nrst = 1u << PNLC_NRST_BIT;
    const uint32_t stock_gpio = 1u << PNLC_STOCK_GPIO_BIT;
    const uint32_t both = boot0 | nrst | stock_gpio;

    gpio5_set_dir_output(mmio, both);

    printf("wing_panel_init: Booting STM32 application (BOOT0=0, NRST low->high)...\n");
    /* BOOT0 low, NRST low */
    gpio5_write_bits(mmio, both, 0);
    sleep_ms(50);

    /* Release NRST, keep BOOT0 low so the flashed PNLC app runs. */
    gpio5_write_bits(mmio, both, nrst);
}

static int serial_open_raw(const char *path)
{
    struct termios tio;
    int fd = open(path, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
        return -1;

    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag |= PARENB;
    tio.c_cflag &= ~PARODD;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

static uint8_t frame_checksum(const uint8_t *payload, size_t payload_len)
{
    unsigned int sum = 0;
    size_t i;

    for (i = 0; i < payload_len; ++i)
        sum = (sum + payload[i]) & 0xffu;

    return (uint8_t)(((sum & 0xffu) ^ (payload_len & 0xffu)) | 0x80u);
}

static int send_ascii_frame(int fd, char cmd, const uint8_t *payload, size_t payload_len)
{
    uint8_t buf[2 + 512 + 2];
    size_t out_len;
    size_t i;

    if (payload_len > 512)
        return -1;

    buf[0] = '*';
    buf[1] = (uint8_t)cmd;
    if (payload_len)
        memcpy(buf + 2, payload, payload_len);
    buf[2 + payload_len] = '*';
    buf[3 + payload_len] = frame_checksum(payload, payload_len);
    out_len = payload_len + 4;

    printf("wing_panel_init: TX frame: ");
    for (i = 0; i < out_len; i++) {
        printf("%02x ", buf[i]);
    }
    printf("\n");

    if (write(fd, buf, out_len) != (ssize_t)out_len)
        return -1;

    /* Read back response (wait up to 100ms) */
    usleep(50000); /* 50ms */
    uint8_t rx_buf[256];
    ssize_t n = read(fd, rx_buf, sizeof(rx_buf));
    if (n > 0) {
        printf("wing_panel_init: RX response: ");
        for (i = 0; i < (size_t)n; i++) {
            printf("%02x ", rx_buf[i]);
        }
        printf(" | ");
        for (i = 0; i < (size_t)n; i++) {
            putchar((rx_buf[i] >= 0x20 && rx_buf[i] < 0x7f) ? rx_buf[i] : '.');
        }
        printf("\n");
    } else if (n < 0) {
        printf("wing_panel_init: RX read error: %s\n", strerror(errno));
    } else {
        printf("wing_panel_init: RX response: (timeout/no data)\n");
    }

    return 0;
}

static char *trim(char *s)
{
    char *end;

    while (*s && isspace((unsigned char)*s))
        ++s;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int parse_hex_byte(const char *s, uint8_t *out)
{
    char *end;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, 16);
    if (errno || *end != '\0' || v > 0xffu)
        return -1;

    *out = (uint8_t)v;
    return 0;
}

static int parse_uint(const char *s, unsigned int *out)
{
    char *end;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, 0);
    if (errno || *end != '\0' || v > 60000u)
        return -1;

    *out = (unsigned int)v;
    return 0;
}

static void run_script(const char *path)
{
    char line[1024];
    unsigned int lineno = 0;
    FILE *fp;
    int fd;

    printf("wing_panel_init: run_script starting for %s...\n", path); fflush(stdout);
    fp = fopen(path, "r");
    if (!fp) {
        printf("wing_panel_init: failed to open script file %s: %s\n", path, strerror(errno)); fflush(stdout);
        return;
    }

    printf("wing_panel_init: opened script file successfully, opening UART %s...\n", PNLC_UART_PATH); fflush(stdout);
    fd = serial_open_raw(PNLC_UART_PATH);
    if (fd < 0) {
        fprintf(stderr, "wing_panel_init: unable to open %s for scripted commands: %s\n",
                PNLC_UART_PATH, strerror(errno));
        fclose(fp);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *tok;
        char *saveptr = NULL;
        char *cursor = trim(line);
        uint8_t payload[512];
        size_t payload_len = 0;
        char cmd;

        ++lineno;
        if (*cursor == '\0' || *cursor == '#')
            continue;

        tok = strtok_r(cursor, " \t\r\n", &saveptr);
        if (!tok)
            continue;

        if (strcmp(tok, "sleep") == 0) {
            unsigned int ms;

            tok = strtok_r(NULL, " \t\r\n", &saveptr);
            if (!tok || parse_uint(tok, &ms) != 0) {
                fprintf(stderr, "wing_panel_init:%u: expected sleep milliseconds\n", lineno);
                continue;
            }
            sleep_ms(ms);
            continue;
        }

        if (strcmp(tok, "pnlc") != 0 && strcmp(tok, "csc") != 0) {
            fprintf(stderr, "wing_panel_init:%u: expected 'pnlc', 'csc', or 'sleep'\n", lineno);
            continue;
        }

        tok = strtok_r(NULL, " \t\r\n", &saveptr);
        if (!tok || strlen(tok) != 1 || (unsigned char)tok[0] < 'A' || (unsigned char)tok[0] > 0x7f) {
            fprintf(stderr, "wing_panel_init:%u: expected single ASCII command byte\n", lineno);
            continue;
        }
        cmd = tok[0];

        while ((tok = strtok_r(NULL, " \t\r\n", &saveptr)) != NULL) {
            if (payload_len >= ARRAY_SIZE(payload)) {
                fprintf(stderr, "wing_panel_init:%u: payload too long\n", lineno);
                payload_len = 0;
                break;
            }
            if (parse_hex_byte(tok, &payload[payload_len]) != 0) {
                fprintf(stderr, "wing_panel_init:%u: bad hex byte '%s'\n", lineno, tok);
                payload_len = 0;
                break;
            }
            ++payload_len;
        }

        if (send_ascii_frame(fd, cmd, payload, payload_len) != 0) {
            fprintf(stderr, "wing_panel_init:%u: send failed for %c\n", lineno, cmd);
        } else {
            sleep_ms(20);
        }
    }

    close(fd);
    fclose(fp);
}

int main(int argc, char **argv)
{
    printf("wing_panel_init: starting...\n"); fflush(stdout);
    const char *script_path = DEFAULT_SCRIPT_PATH;
    struct mmio mmio;

    if (argc == 3 && strcmp(argv[1], "--script") == 0) {
        script_path = argv[2];
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--script /path/to/panel-init.txt]\n", argv[0]);
        return 2;
    }

    mmio_init(&mmio);
    pinmux_uart4_and_panel_gpio(&mmio);
    pnlc_boot_app(&mmio);
    mmio_close(&mmio);

    run_script(script_path);
    return 0;
}
