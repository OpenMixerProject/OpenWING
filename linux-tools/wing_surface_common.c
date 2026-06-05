#include "wing_surface_common.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

#include "wing_control_names.h"

#define IOMUXC_BASE              0x020E0000u
#define GPIO2_BASE               0x020A0000u
#define GPIO3_BASE               0x020A4000u
#define GPIO5_BASE               0x020AC000u
#define ECSPI2_BASE              0x0200C000u
#define CCM_BASE                 0x020C4000u
#define CCM_CCGR1_OFF            0x006Cu
#define GPIO_DR_OFF              0x0000u
#define GPIO_GDIR_OFF            0x0004u
#define ECSPI_RXDATA_OFF         0x0000u
#define ECSPI_TXDATA_OFF         0x0004u
#define ECSPI_CONREG_OFF         0x0008u
#define ECSPI_CONFIGREG_OFF      0x000Cu
#define ECSPI_INTREG_OFF         0x0010u
#define ECSPI_STATREG_OFF        0x0018u
#define ECSPI_PERIODREG_OFF      0x001Cu
#define ECSPI_TC_BIT             0x80u
#define ECSPI_XCH_BIT            0x04u
#define CSC_LIGHT_DEFAULT_VALUE  0x0000602fu
#define CSC_LIGHT_VALUE_MASK     0x0000602fu
#define MUX_KEY_COL1             0x005Cu
#define MUX_KEY_ROW1             0x0060u
#define PAD_KEY_COL1             0x0370u
#define PAD_KEY_ROW1             0x0374u
#define SEL_UART5_RX             0x091Cu
#define UART5_ALT_MODE           0x3u
#define UART5_RX_DAISY_STOCK     0x1u
#define PAD_UART_TX_STOCK        0x00000018u
#define PAD_UART_RX_STOCK        0x0000B000u
#define CSC_RESET_BIT            23u
#define CSC_BOOT_BIT             20u
#define CSC_RESET_MASK           ((1u << CSC_RESET_BIT) | (1u << CSC_BOOT_BIT))
#define MUX_CSI0_DAT12           0x0054u
#define MUX_CSI0_DAT13           0x0058u
#define PAD_CSI0_DAT12           0x0368u
#define PAD_CSI0_DAT13           0x036Cu
#define SEL_UART4_RX             0x0914u
#define MUX_CSI0_VSYNC           0x0098u
#define MUX_CSI0_DAT4            0x0074u
#define MUX_PNLC_STOCK_GPIO      0x0080u
#define PAD_CSI0_VSYNC           0x03ACu
#define PAD_CSI0_DAT4            0x0388u
#define PAD_PNLC_STOCK_GPIO      0x0394u
#define UART4_MUX_MODE           0x4u
#define UART4_STOCK_MUX_MODE     0x3u
#define GPIO_MUX_MODE            0x5u
#define UART4_RX_DAISY_STOCK     0x1u
#define PAD_UART_TX_STOCK_PNLC   0x00000018u
#define PAD_UART_RX_STOCK_PNLC   0x0000B000u
#define PAD_GPIO_DEFAULT         0x0001B0B0u
#define PAD_GPIO_STOCK_PNLC      0x00000018u
#define PNLC_BOOT0_BIT           21u
#define PNLC_NRST_BIT            22u
#define PNLC_STOCK_GPIO_BIT      30u
#define PNLC_RESET_MASK          ((1u << PNLC_BOOT0_BIT) | (1u << PNLC_NRST_BIT) | \
                                  (1u << PNLC_STOCK_GPIO_BIT))

struct wing_csc_latch_mmio {
    int mem_fd;
    size_t map_len;
    uint8_t *iomuxc;
    uint8_t *gpio2;
    uint8_t *gpio3;
    uint8_t *ecspi2;
};

static uint32_t csc_latch_state[2];

static uint32_t readl_ptr(uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static void writel_ptr(uint8_t *base, uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(base + off) = value;
}

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    while (len) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static unsigned long long monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (unsigned long long)ts.tv_sec * 1000000ull +
           (unsigned long long)ts.tv_nsec / 1000ull;
}

void wing_parser_reset(struct wing_frame_parser *parser)
{
    memset(parser, 0, sizeof(*parser));
}

uint8_t wing_frame_checksum(const uint8_t *payload, size_t len)
{
    unsigned int sum = 0;

    for (size_t i = 0; i < len; ++i)
        sum = (sum + payload[i]) & 0xffu;
    return (uint8_t)(((sum & 0xffu) ^ (len & 0xffu)) | 0x80u);
}

void wing_parser_feed(struct wing_frame_parser *parser, const char *source, uint8_t byte,
                      wing_frame_cb cb, void *user)
{
    if (!parser->in_frame) {
        if (byte == WING_FRAME_STAR) {
            parser->in_frame = 1;
            parser->after_star = 1;
            parser->have_cmd = 0;
            parser->len = 0;
        }
        return;
    }

    if (parser->after_star) {
        parser->after_star = 0;
        if (byte == WING_FRAME_STAR) {
            parser->have_cmd = 0;
            parser->len = 0;
            parser->after_star = 1;
        } else if (byte == 0x40) {
            if (parser->len < sizeof(parser->payload))
                parser->payload[parser->len++] = WING_FRAME_STAR;
        } else if (byte & 0x80) {
            if (parser->have_cmd && cb)
                cb(user, source, parser->cmd, parser->payload, parser->len, byte);
            wing_parser_reset(parser);
        } else if (!parser->have_cmd) {
            parser->cmd = byte;
            parser->have_cmd = 1;
        } else if (parser->len + 2 <= sizeof(parser->payload)) {
            parser->payload[parser->len++] = WING_FRAME_STAR;
            parser->payload[parser->len++] = byte;
        }
        return;
    }

    if (byte == WING_FRAME_STAR) {
        parser->after_star = 1;
    } else if (!parser->have_cmd) {
        parser->cmd = byte;
        parser->have_cmd = 1;
    } else if (parser->len < sizeof(parser->payload)) {
        parser->payload[parser->len++] = byte;
    }
}

int wing_parse_uint(const char *s, unsigned int max, unsigned int *out)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(s, &end, 0);
    if (errno || *end != '\0' || value > max)
        return -1;
    *out = (unsigned int)value;
    return 0;
}

int wing_parse_hex_byte(const char *s, uint8_t *out)
{
    unsigned int value;

    if (wing_parse_uint(s, 0xffu, &value) != 0)
        return -1;
    *out = (uint8_t)value;
    return 0;
}

static int baud_to_speed(unsigned int baud, speed_t *speed)
{
    switch (baud) {
        case 9600: *speed = B9600; return 0;
        case 19200: *speed = B19200; return 0;
        case 38400: *speed = B38400; return 0;
        case 57600: *speed = B57600; return 0;
        case 115200: *speed = B115200; return 0;
        default: return -1;
    }
}

int wing_serial_open(const char *path, unsigned int baud)
{
    struct termios tio;
    speed_t speed;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_SYNC);

    if (fd < 0)
        return -1;
    if (baud_to_speed(baud, &speed) != 0) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    tio.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    tio.c_cflag &= ~(CSIZE|PARENB|CSTOPB);
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int wing_serial_open_pnlc(const char *path)
{
    struct termios tio;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_SYNC);

    if (fd < 0)
        return -1;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cflag = CS8 | CREAD | CLOCAL;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int wing_send_frame(int fd, uint8_t cmd, const uint8_t *payload, size_t len)
{
    uint8_t out[2 + 512 * 2 + 2];
    size_t out_len = 0;

    if (len > 512)
        return -1;
    out[out_len++] = WING_FRAME_STAR;
    if (cmd == WING_FRAME_STAR) {
        out[out_len++] = WING_FRAME_STAR;
        out[out_len++] = 0x40;
    } else {
        out[out_len++] = cmd;
    }
    for (size_t i = 0; i < len; ++i) {
        if (payload[i] == WING_FRAME_STAR) {
            out[out_len++] = WING_FRAME_STAR;
            out[out_len++] = 0x40;
        } else {
            out[out_len++] = payload[i];
        }
    }
    out[out_len++] = WING_FRAME_STAR;
    uint8_t chk = wing_frame_checksum(payload, len);
    if (chk == WING_FRAME_STAR) {
        out[out_len++] = WING_FRAME_STAR;
        out[out_len++] = 0x40;
    } else {
        out[out_len++] = chk;
    }
    return write_all(fd, out, out_len);
}

int wing_init_csc_transport(void)
{
    const size_t map_len = 0x1000;
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    uint8_t *iomuxc;
    uint8_t *gpio5;
    uint32_t value;

    if (mem_fd < 0)
        return -1;
    iomuxc = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, IOMUXC_BASE);
    if (iomuxc == MAP_FAILED) {
        close(mem_fd);
        return -1;
    }
    gpio5 = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, GPIO5_BASE);
    if (gpio5 == MAP_FAILED) {
        munmap(iomuxc, map_len);
        close(mem_fd);
        return -1;
    }

    writel_ptr(iomuxc, PAD_KEY_COL1, PAD_UART_TX_STOCK);
    writel_ptr(iomuxc, MUX_KEY_COL1, UART5_ALT_MODE);
    writel_ptr(iomuxc, PAD_KEY_ROW1, PAD_UART_RX_STOCK);
    writel_ptr(iomuxc, MUX_KEY_ROW1, UART5_ALT_MODE);
    writel_ptr(iomuxc, SEL_UART5_RX, UART5_RX_DAISY_STOCK);
    value = readl_ptr(gpio5, GPIO_GDIR_OFF);
    writel_ptr(gpio5, GPIO_GDIR_OFF, value | CSC_RESET_MASK);
    value = readl_ptr(gpio5, GPIO_DR_OFF);
    writel_ptr(gpio5, GPIO_DR_OFF, value & ~CSC_RESET_MASK);
    usleep(50000);
    value = readl_ptr(gpio5, GPIO_DR_OFF);
    value |= (1u << CSC_RESET_BIT);
    value &= ~(1u << CSC_BOOT_BIT);
    writel_ptr(gpio5, GPIO_DR_OFF, value);
    usleep(500000);

    munmap(gpio5, map_len);
    munmap(iomuxc, map_len);
    close(mem_fd);
    return 0;
}

static void wing_csc_latch_configure_pads(uint8_t *iomuxc)
{
    static const struct {
        uint32_t off;
        uint32_t value;
    } writes[] = {
        { 0x05ac, 0x0001b0b0 }, { 0x01dc, 0x00000005 },
        { 0x05a4, 0x0001b0b0 }, { 0x01d4, 0x00000005 },
        { 0x0534, 0x0001b0b0 }, { 0x0164, 0x00000005 },
        { 0x0538, 0x0001b0b0 }, { 0x0168, 0x00000005 },
        { 0x050c, 0x0001b0b0 }, { 0x013c, 0x00000002 }, { 0x07f4, 0x00000002 },
        { 0x05a8, 0x0001b0b0 }, { 0x01d8, 0x00000002 }, { 0x07f8, 0x00000002 },
        { 0x0510, 0x0001b0b0 }, { 0x0140, 0x00000002 }, { 0x07fc, 0x00000002 },
        { 0x04f4, 0x0001b0b0 }, { 0x0124, 0x00000005 },
        { 0x0410, 0x0001b008 }, { 0x00fc, 0x00000015 },
        { 0x04f0, 0x00013008 }, { 0x0120, 0x00000005 },
    };

    for (size_t i = 0; i < sizeof(writes) / sizeof(writes[0]); ++i)
        writel_ptr(iomuxc, writes[i].off, writes[i].value);
}

static void wing_csc_latch_gpio_init(struct wing_csc_latch_mmio *mmio)
{
    uint32_t value;

    value = readl_ptr(mmio->gpio2, GPIO_DR_OFF);
    value |= 0x0c000000u | 0x00020000u;
    writel_ptr(mmio->gpio2, GPIO_DR_OFF, value);
    value = readl_ptr(mmio->gpio2, GPIO_GDIR_OFF);
    value |= 0x0c000000u | 0x00020000u;
    writel_ptr(mmio->gpio2, GPIO_GDIR_OFF, value);

    value = readl_ptr(mmio->gpio3, GPIO_DR_OFF);
    value &= ~0x03000000u;
    writel_ptr(mmio->gpio3, GPIO_DR_OFF, value);
    value = readl_ptr(mmio->gpio3, GPIO_GDIR_OFF);
    value |= 0x03000000u;
    writel_ptr(mmio->gpio3, GPIO_GDIR_OFF, value);
}

static void wing_csc_latch_spi_init(uint8_t *ecspi2)
{
    writel_ptr(ecspi2, ECSPI_CONREG_OFF, 0);
    writel_ptr(ecspi2, ECSPI_CONREG_OFF, 0x00000011u);
    writel_ptr(ecspi2, ECSPI_CONFIGREG_OFF, 0x00000100u);
    writel_ptr(ecspi2, ECSPI_PERIODREG_OFF, 0);
}

static int wing_csc_latch_mmio_open(struct wing_csc_latch_mmio *mmio)
{
    memset(mmio, 0, sizeof(*mmio));
    mmio->mem_fd = -1;
    mmio->map_len = 0x4000;
    mmio->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mmio->mem_fd < 0)
        return -1;

    // Enable ECSPI2 clock gate via CCM CCGR1
    uint8_t *ccm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mmio->mem_fd, CCM_BASE);
    if (ccm == MAP_FAILED)
        goto fail;
    uint32_t ccgr1 = readl_ptr(ccm, CCM_CCGR1_OFF);
    ccgr1 |= 0x0000000Cu;
    writel_ptr(ccm, CCM_CCGR1_OFF, ccgr1);
    munmap(ccm, 0x1000);

    mmio->iomuxc = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mmio->mem_fd,
                        IOMUXC_BASE);
    if (mmio->iomuxc == MAP_FAILED)
        goto fail;
    mmio->gpio2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mmio->mem_fd,
                       GPIO2_BASE);
    if (mmio->gpio2 == MAP_FAILED)
        goto fail;
    mmio->gpio3 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mmio->mem_fd,
                       GPIO3_BASE);
    if (mmio->gpio3 == MAP_FAILED)
        goto fail;
    mmio->ecspi2 = mmap(NULL, mmio->map_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                        mmio->mem_fd, ECSPI2_BASE);
    if (mmio->ecspi2 == MAP_FAILED)
        goto fail;

    wing_csc_latch_configure_pads(mmio->iomuxc);
    wing_csc_latch_gpio_init(mmio);
    wing_csc_latch_spi_init(mmio->ecspi2);
    return 0;

fail:
    if (mmio->ecspi2 && mmio->ecspi2 != MAP_FAILED)
        munmap(mmio->ecspi2, mmio->map_len);
    if (mmio->gpio3 && mmio->gpio3 != MAP_FAILED)
        munmap(mmio->gpio3, 0x1000);
    if (mmio->gpio2 && mmio->gpio2 != MAP_FAILED)
        munmap(mmio->gpio2, 0x1000);
    if (mmio->iomuxc && mmio->iomuxc != MAP_FAILED)
        munmap(mmio->iomuxc, 0x1000);
    close(mmio->mem_fd);
    return -1;
}

static void wing_csc_latch_mmio_close(struct wing_csc_latch_mmio *mmio)
{
    if (mmio->ecspi2 && mmio->ecspi2 != MAP_FAILED)
        munmap(mmio->ecspi2, mmio->map_len);
    if (mmio->gpio3 && mmio->gpio3 != MAP_FAILED)
        munmap(mmio->gpio3, 0x1000);
    if (mmio->gpio2 && mmio->gpio2 != MAP_FAILED)
        munmap(mmio->gpio2, 0x1000);
    if (mmio->iomuxc && mmio->iomuxc != MAP_FAILED)
        munmap(mmio->iomuxc, 0x1000);
    if (mmio->mem_fd >= 0)
        close(mmio->mem_fd);
}

static void wing_csc_latch_select(struct wing_csc_latch_mmio *mmio)
{
    uint32_t value;

    value = readl_ptr(mmio->gpio2, GPIO_DR_OFF);
    value &= ~0x04000000u;
    value |= 0x08000000u;
    writel_ptr(mmio->gpio2, GPIO_DR_OFF, value);
    value = readl_ptr(mmio->gpio3, GPIO_DR_OFF);
    value |= 0x03000000u;
    writel_ptr(mmio->gpio3, GPIO_DR_OFF, value);
}

static void wing_csc_latch_deselect(struct wing_csc_latch_mmio *mmio)
{
    uint32_t value;

    value = readl_ptr(mmio->gpio2, GPIO_DR_OFF);
    value |= 0x0c000000u;
    writel_ptr(mmio->gpio2, GPIO_DR_OFF, value);
    value = readl_ptr(mmio->gpio3, GPIO_DR_OFF);
    value |= 0x03000000u;
    writel_ptr(mmio->gpio3, GPIO_DR_OFF, value);
}

static int wing_csc_latch_transfer(struct wing_csc_latch_mmio *mmio, const uint32_t *words,
                                   size_t count)
{
    uint32_t con;
    unsigned long long deadline;

    if (count == 0 || count > 64)
        return -1;
    wing_csc_latch_select(mmio);

    con = readl_ptr(mmio->ecspi2, ECSPI_CONREG_OFF);
    con &= ~0xfff00008u;
    con |= (uint32_t)((count << 25) - 0x00100000u);
    writel_ptr(mmio->ecspi2, ECSPI_CONREG_OFF, con);
    writel_ptr(mmio->ecspi2, ECSPI_STATREG_OFF, ECSPI_TC_BIT);
    for (size_t i = 0; i < count; ++i)
        writel_ptr(mmio->ecspi2, ECSPI_TXDATA_OFF, words[i]);
    writel_ptr(mmio->ecspi2, ECSPI_INTREG_OFF, ECSPI_TC_BIT);
    writel_ptr(mmio->ecspi2, ECSPI_CONREG_OFF, con | ECSPI_XCH_BIT);

    deadline = monotonic_us() + 500000ull;
    while ((readl_ptr(mmio->ecspi2, ECSPI_STATREG_OFF) & ECSPI_TC_BIT) == 0) {
        if (monotonic_us() > deadline) {
            wing_csc_latch_deselect(mmio);
            errno = ETIMEDOUT;
            return -1;
        }
    }
    writel_ptr(mmio->ecspi2, ECSPI_INTREG_OFF, 0);
    for (size_t i = 0; i < count; ++i)
        (void)readl_ptr(mmio->ecspi2, ECSPI_RXDATA_OFF);
    wing_csc_latch_deselect(mmio);
    return 0;
}

static int wing_csc_latch_write_state(struct wing_csc_latch_mmio *mmio, unsigned int port,
                                      uint32_t state)
{
    uint32_t words[2];

    if (port > 1)
        return -1;
    words[0] = 0xa8000000u | port;
    words[1] = state;
    wing_csc_latch_spi_init(mmio->ecspi2);
    return wing_csc_latch_transfer(mmio, words, sizeof(words) / sizeof(words[0]));
}

int wing_csc_latch_update(unsigned int port, uint32_t value, uint32_t mask)
{
    struct wing_csc_latch_mmio mmio;
    uint32_t next;
    int rc;

    if (port > 1) {
        errno = EINVAL;
        return -1;
    }
    next = (csc_latch_state[port] & ~mask) | value;
    if (next == csc_latch_state[port])
        return 0;
    if (wing_csc_latch_mmio_open(&mmio) != 0)
        return -1;
    rc = wing_csc_latch_write_state(&mmio, port, next);
    wing_csc_latch_mmio_close(&mmio);
    if (rc == 0)
        csc_latch_state[port] = next;
    return rc;
}

int wing_enable_csc_lights(uint32_t value)
{
    if (value > CSC_LIGHT_VALUE_MASK) {
        errno = EINVAL;
        return -1;
    }

    if (wing_csc_latch_update(0, 0, 0x380001bfu) != 0)
        return -1;
    usleep(2000);
    if (wing_csc_latch_update(0, 0x00040000u, 0x00000040u) != 0)
        return -1;
    if (wing_csc_latch_update(0, value, CSC_LIGHT_VALUE_MASK) != 0)
        return -1;
    if (wing_csc_latch_update(0, 0x00000040u, 0) != 0)
        return -1;
    usleep(2000);
    if (wing_csc_latch_update(0, 0x00000080u, 0) != 0 ||
        wing_csc_latch_update(0, 0x08000000u, 0) != 0 ||
        wing_csc_latch_update(0, 0x10000000u, 0) != 0 ||
        wing_csc_latch_update(0, 0x20000000u, 0) != 0 ||
        wing_csc_latch_update(0, 0x00000100u, 0) != 0)
        return -1;
    return 0;
}

static void configure_pnlc_pinmux(uint8_t *iomuxc)
{
    writel_ptr(iomuxc, PAD_CSI0_DAT12, PAD_UART_TX_STOCK_PNLC);
    writel_ptr(iomuxc, MUX_CSI0_DAT12, UART4_STOCK_MUX_MODE);
    writel_ptr(iomuxc, PAD_CSI0_DAT13, PAD_UART_RX_STOCK_PNLC);
    writel_ptr(iomuxc, MUX_CSI0_DAT13, UART4_STOCK_MUX_MODE);
    writel_ptr(iomuxc, SEL_UART4_RX, UART4_RX_DAISY_STOCK);
    writel_ptr(iomuxc, MUX_CSI0_VSYNC, GPIO_MUX_MODE);
    writel_ptr(iomuxc, MUX_CSI0_DAT4, GPIO_MUX_MODE);
    writel_ptr(iomuxc, MUX_PNLC_STOCK_GPIO, GPIO_MUX_MODE);
    writel_ptr(iomuxc, PAD_CSI0_VSYNC, PAD_GPIO_STOCK_PNLC);
    writel_ptr(iomuxc, PAD_CSI0_DAT4, PAD_GPIO_STOCK_PNLC);
    writel_ptr(iomuxc, PAD_PNLC_STOCK_GPIO, PAD_UART_RX_STOCK_PNLC);
}

int wing_configure_pnlc_transport(void)
{
    const size_t map_len = 0x1000;
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    uint8_t *iomuxc;

    if (mem_fd < 0)
        return -1;
    iomuxc = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, IOMUXC_BASE);
    if (iomuxc == MAP_FAILED) {
        close(mem_fd);
        return -1;
    }
    configure_pnlc_pinmux(iomuxc);
    munmap(iomuxc, map_len);
    close(mem_fd);
    return 0;
}

int wing_init_pnlc_transport(void)
{
    const size_t map_len = 0x1000;
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    uint8_t *iomuxc;
    uint8_t *gpio5;
    uint32_t value;

    if (mem_fd < 0)
        return -1;
    iomuxc = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, IOMUXC_BASE);
    if (iomuxc == MAP_FAILED) {
        close(mem_fd);
        return -1;
    }
    gpio5 = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, GPIO5_BASE);
    if (gpio5 == MAP_FAILED) {
        munmap(iomuxc, map_len);
        close(mem_fd);
        return -1;
    }

    configure_pnlc_pinmux(iomuxc);

    value = readl_ptr(gpio5, GPIO_GDIR_OFF);
    writel_ptr(gpio5, GPIO_GDIR_OFF, value | PNLC_RESET_MASK);
    value = readl_ptr(gpio5, GPIO_DR_OFF);
    writel_ptr(gpio5, GPIO_DR_OFF, value & ~PNLC_RESET_MASK);
    usleep(50000);
    value = readl_ptr(gpio5, GPIO_DR_OFF);
    value |= (1u << PNLC_NRST_BIT);
    value &= ~(1u << PNLC_BOOT0_BIT);
    writel_ptr(gpio5, GPIO_DR_OFF, value);
    usleep(500000);

    munmap(gpio5, map_len);
    munmap(iomuxc, map_len);
    close(mem_fd);
    return 0;
}

static int send_zero_payload(int fd, uint8_t cmd, size_t len)
{
    uint8_t payload[64];

    if (len > sizeof(payload))
        return -1;
    memset(payload, 0, len);
    return wing_send_frame(fd, cmd, payload, len);
}

static int send_u_level(int fd, uint8_t id, uint16_t value, uint8_t mode)
{
    uint8_t payload[4] = { id, (uint8_t)((value >> 8) & 0xffu),
                           (uint8_t)(value & 0xffu), mode };
    return wing_send_frame(fd, 'U', payload, sizeof(payload));
}

int wing_send_stock_baseline(int fd)
{
    static const struct {
        uint8_t cmd;
        size_t len;
    } blocks[] = {
        { 'B', 9 }, { 'L', 10 }, { 'l', 13 }, { 'C', 36 }, { 'M', 26 },
    };

    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
        if (send_zero_payload(fd, blocks[i].cmd, blocks[i].len) != 0)
            return -1;
        usleep(20000);
    }
    for (uint8_t id = 0x38; id <= 0x3f; ++id) {
        if (send_u_level(fd, id, 50, 0x08) != 0)
            return -1;
        usleep(20000);
    }
    return 0;
}

int wing_enable_pnlc_touch(int fd)
{
    uint8_t payload[1] = { 1 };
    return wing_send_frame(fd, 'I', payload, sizeof(payload));
}

int wing_enable_pnlc_backlight(int fd)
{
    uint8_t payload[4] = { 0x60, 0x60, 0x60, 0x60 };
    return wing_send_frame(fd, 'B', payload, sizeof(payload));
}

void wing_surface_init_state(struct wing_surface *surface)
{
    memset(surface, 0, sizeof(*surface));
    surface->csc_fd = -1;
    surface->pnlc_fd = -1;
    surface->csc_led_brightness = 50;
    surface->csc_lamp_brightness = 50;
    surface->csc_glow_brightness = 50;
    surface->csc_patch_brightness = 50;
}

int wing_surface_open(struct wing_surface *surface, const char *csc_device,
                      unsigned int csc_baud, const char *pnlc_device, int open_pnlc)
{
    wing_surface_init_state(surface);
    if (wing_init_csc_transport() != 0)
        return -1;
    surface->csc_fd = wing_serial_open(csc_device, csc_baud);
    if (surface->csc_fd < 0)
        return -1;
    if (wing_enable_csc_lights(CSC_LIGHT_DEFAULT_VALUE) == 0)
        surface->csc_lights_enabled = 1;
    if (wing_send_frame(surface->csc_fd, 'H', NULL, 0) != 0 ||
        wing_send_stock_baseline(surface->csc_fd) != 0)
        return -1;

    if (open_pnlc) {
        surface->pnlc_fd = wing_serial_open_pnlc(pnlc_device);
        if (surface->pnlc_fd >= 0) {
            /*
             * Do not reset/reconfigure PNLC here. The boot-time panel init has
             * already brought the STM32 application up; resetting it from this
             * console can leave touch reporting dead until the full init runs
             * again. Match wing-draw/pnlc_raw_dump and only enable reports.
             */
            (void)wing_enable_pnlc_touch(surface->pnlc_fd);
            usleep(100000);
            tcflush(surface->pnlc_fd, TCIFLUSH);
        }
    }
    return 0;
}

int wing_surface_csc_lights(struct wing_surface *surface, uint32_t value)
{
    if (wing_enable_csc_lights(value) != 0)
        return -1;
    surface->csc_lights_enabled = 1;
    return 0;
}

int wing_surface_csc_brightness(struct wing_surface *surface, int led, int lamp, int glow, int patch)
{
    if (surface->csc_fd < 0)
        return -1;

    if (led >= 0) {
        if (led > 100) return -1;
        surface->csc_led_brightness = (uint8_t)led;
        if (send_u_level(surface->csc_fd, 0x38, surface->csc_led_brightness, 0x08) != 0)
            return -1;
    }
    if (lamp >= 0) {
        if (lamp > 100) return -1;
        surface->csc_lamp_brightness = (uint8_t)lamp;
        if (send_u_level(surface->csc_fd, 0x3D, surface->csc_lamp_brightness, 0x08) != 0)
            return -1;
    }
    if (glow >= 0) {
        if (glow > 100) return -1;
        surface->csc_glow_brightness = (uint8_t)glow;
        if (send_u_level(surface->csc_fd, 0x3E, surface->csc_glow_brightness, 0x08) != 0)
            return -1;
    }
    if (patch >= 0) {
        if (patch > 100) return -1;
        surface->csc_patch_brightness = (uint8_t)patch;
        if (send_u_level(surface->csc_fd, 0x3F, surface->csc_patch_brightness, 0x08) != 0)
            return -1;
    }

    return 0;
}

int wing_surface_csc_latch(struct wing_surface *surface, unsigned int port, uint32_t value,
                           uint32_t mask)
{
    (void)surface;
    return wing_csc_latch_update(port, value, mask);
}

void wing_surface_close(struct wing_surface *surface)
{
    if (surface->csc_fd >= 0)
        close(surface->csc_fd);
    if (surface->pnlc_fd >= 0)
        close(surface->pnlc_fd);
    surface->csc_fd = -1;
    surface->pnlc_fd = -1;
}

static void pack_led_states(const uint8_t *states, unsigned int count, uint8_t *payload,
                            size_t len)
{
    memset(payload, 0, len);
    for (unsigned int i = 0; i < count; ++i)
        payload[i / 4] |= (uint8_t)((states[i] & 0x03u) << ((i % 4u) * 2u));
}

int wing_surface_fader(struct wing_surface *surface, unsigned int fader_1_based,
                       unsigned int value)
{
    uint8_t payload[3];

    if (surface->csc_fd < 0 || fader_1_based < 1 || fader_1_based > 13 || value > 4095)
        return -1;
    payload[0] = (uint8_t)(fader_1_based - 1u);
    payload[1] = (uint8_t)(value & 0xffu);
    payload[2] = (uint8_t)((value >> 8) & 0x0fu);
    return wing_send_frame(surface->csc_fd, 'F', payload, sizeof(payload));
}

static void normalize_name(const char *in, char *out, size_t out_len)
{
    size_t j = 0;

    for (size_t i = 0; in[i] && j + 1 < out_len; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c))
            out[j++] = (char)tolower(c);
        else if (c == '-' || c == ' ' || c == '/')
            out[j++] = '_';
        else if (c == '_')
            out[j++] = '_';
    }
    out[j] = '\0';
}

static int led_name_to_id(const char *name, unsigned int *id)
{
    char want[96];

    normalize_name(name, want, sizeof(want));
    if (strncmp(want, "fader_", 6) == 0) {
        char *end;
        unsigned long n = strtoul(want + 6, &end, 10);
        if (n >= 1 && n <= 13 && (*end == '\0' || strcmp(end, "_select") == 0 ||
                                  strcmp(end, "_solo") == 0 || strcmp(end, "_mute") == 0)) {
            unsigned int base = (unsigned int)((n - 1) * 3);
            if (*end == '\0' || strcmp(end, "_select") == 0)
                *id = base;
            else if (strcmp(end, "_solo") == 0)
                *id = base + 1;
            else
                *id = base + 2;
            return 0;
        }
    }
    if (strcmp(want, "master") == 0 || strcmp(want, "master_select") == 0) {
        *id = 36;
        return 0;
    }
    if (strcmp(want, "master_solo") == 0) {
        *id = 37;
        return 0;
    }
    if (strcmp(want, "master_mute") == 0) {
        *id = 38;
        return 0;
    }
    for (unsigned int i = 0; i <= 0x4c; ++i) {
        const char *candidate = wing_csc_button_name(i);
        char norm[96];
        if (!candidate)
            continue;
        normalize_name(candidate, norm, sizeof(norm));
        if (strcmp(norm, want) == 0) {
            *id = i;
            return 0;
        }
    }
    return -1;
}

struct pnlc_led_name {
    const char *name;
    const char *color;
    unsigned int id;
};

static const struct pnlc_led_name pnlc_led_names[] = {
    { "home", "white", 0 },
    { "home", "green", 1 },
    { "effects", "white", 2 },
    { "effects", "orange", 3 },
    { "meters", "white", 4 },
    { "meters", "orange", 5 },
    { "routing", "white", 6 },
    { "routing", "orange", 7 },
    { "setup", "white", 8 },
    { "setup", "orange", 9 },
    { "library", "white", 10 },
    { "library", "orange", 11 },
    { "utility", "white", 12 },
    { "utility", "orange", 13 },
    { "select", "white", 14 },
    { "clr_solo", "white", 16 },
    { "clr_solo", "yellow", 17 },
};

static int pnlc_led_name_to_id(const char *name, const char *color, unsigned int *id)
{
    char want_name[96];
    char want_color[32];

    normalize_name(name, want_name, sizeof(want_name));
    if (color)
        normalize_name(color, want_color, sizeof(want_color));
    else
        want_color[0] = '\0';

    for (size_t i = 0; i < sizeof(pnlc_led_names) / sizeof(pnlc_led_names[0]); ++i) {
        if (strcmp(want_name, pnlc_led_names[i].name) != 0)
            continue;
        if (want_color[0] == '\0' || strcmp(want_color, pnlc_led_names[i].color) == 0) {
            *id = pnlc_led_names[i].id;
            return 0;
        }
    }

    if (want_color[0] == '\0') {
        char combined[128];
        for (size_t i = 0; i < sizeof(pnlc_led_names) / sizeof(pnlc_led_names[0]); ++i) {
            snprintf(combined, sizeof(combined), "%s_%s", pnlc_led_names[i].name,
                     pnlc_led_names[i].color);
            if (strcmp(want_name, combined) == 0) {
                *id = pnlc_led_names[i].id;
                return 0;
            }
        }
    }

    return -1;
}

static int send_pnlc_leds(struct wing_surface *surface)
{
    uint8_t payload[3] = { 0, 0, 0 };

    if (surface->pnlc_fd < 0)
        return -1;
    for (unsigned int i = 0; i < 18; ++i) {
        if (surface->pnlc_led_states[i])
            payload[i / 8] |= (uint8_t)(1u << (i % 8));
    }
    return wing_send_frame(surface->pnlc_fd, 'L', payload, sizeof(payload));
}

int wing_surface_led(struct wing_surface *surface, const char *name, int on)
{
    return wing_surface_led_color(surface, name, NULL, on);
}

int wing_surface_led_color(struct wing_surface *surface, const char *name, const char *color,
                           int on)
{
    unsigned int id;

    if (pnlc_led_name_to_id(name, color, &id) == 0) {
        surface->pnlc_led_states[id] = on ? 1 : 0;
        return send_pnlc_leds(surface);
    }

    if (color && strcmp(color, "default") != 0)
        return -1;

    if (surface->csc_fd >= 0 && led_name_to_id(name, &id) == 0) {
        if (!surface->csc_lights_enabled &&
            wing_surface_csc_lights(surface, CSC_LIGHT_DEFAULT_VALUE) != 0)
            return -1;
        if (id < 36) {
            uint8_t payload[9];
            surface->strip_led_states[id] = on ? 3 : 0;
            pack_led_states(surface->strip_led_states, 36, payload, sizeof(payload));
            return wing_send_frame(surface->csc_fd, 'B', payload, sizeof(payload));
        }
        if (id >= 40 && id < 92) {
            uint8_t payload[13];
            surface->layer_led_states[id - 40] = on ? 3 : 0;
            pack_led_states(surface->layer_led_states, 52, payload, sizeof(payload));
            return wing_send_frame(surface->csc_fd, 'l', payload, sizeof(payload));
        }
    }
    return -1;
}

int wing_surface_csc_leds_all(struct wing_surface *surface, int on)
{
    if (surface->csc_fd < 0)
        return -1;

    if (!surface->csc_lights_enabled &&
        wing_surface_csc_lights(surface, CSC_LIGHT_DEFAULT_VALUE) != 0)
        return -1;

    // 1. Update strip_led_states (0 to 35)
    uint8_t payload_B[9];
    for (int i = 0; i < 36; ++i) {
        surface->strip_led_states[i] = on ? 3 : 0;
    }
    pack_led_states(surface->strip_led_states, 36, payload_B, sizeof(payload_B));
    if (wing_send_frame(surface->csc_fd, 'B', payload_B, sizeof(payload_B)) != 0)
        return -1;

    // 2. Update layer_led_states (0 to 51, mapped to pins 40-91)
    uint8_t payload_l[13];
    for (int i = 0; i < 52; ++i) {
        surface->layer_led_states[i] = on ? 3 : 0;
    }
    pack_led_states(surface->layer_led_states, 52, payload_l, sizeof(payload_l));
    if (wing_send_frame(surface->csc_fd, 'l', payload_l, sizeof(payload_l)) != 0)
        return -1;

    return 0;
}

int wing_surface_raw_csc(struct wing_surface *surface, uint8_t cmd, const uint8_t *payload,
                         size_t len)
{
    return surface->csc_fd < 0 ? -1 : wing_send_frame(surface->csc_fd, cmd, payload, len);
}

int wing_surface_raw_pnlc(struct wing_surface *surface, uint8_t cmd, const uint8_t *payload,
                          size_t len)
{
    return surface->pnlc_fd < 0 ? -1 : wing_send_frame(surface->pnlc_fd, cmd, payload, len);
}

static void append_hex(char *out, size_t out_len, const uint8_t *payload, size_t len)
{
    size_t used = strlen(out);

    for (size_t i = 0; i < len && used + 4 < out_len; ++i)
        used += (size_t)snprintf(out + used, out_len - used, "%s%02x", i ? " " : "", payload[i]);
}

static void append_raw_frame(char *out, size_t out_len, uint8_t cmd, const uint8_t *payload,
                             size_t len, uint8_t check)
{
    uint8_t frame[2 + 512 * 2 + 2];
    size_t frame_len = 0;

    frame[frame_len++] = WING_FRAME_STAR;
    frame[frame_len++] = cmd;
    for (size_t i = 0; i < len && frame_len + 2 < sizeof(frame); ++i) {
        if (payload[i] == WING_FRAME_STAR) {
            frame[frame_len++] = WING_FRAME_STAR;
            frame[frame_len++] = 0x40;
        } else {
            frame[frame_len++] = payload[i];
        }
    }
    frame[frame_len++] = WING_FRAME_STAR;
    frame[frame_len++] = check;
    strncat(out, " raw=[", out_len - strlen(out) - 1);
    append_hex(out, out_len, frame, frame_len);
    strncat(out, "]", out_len - strlen(out) - 1);
}

static int payload_is_printable(const uint8_t *payload, size_t len)
{
    if (len == 0)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        if (payload[i] < 0x20 || payload[i] >= 0x7f)
            return 0;
    }
    return 1;
}

void wing_describe_frame(char *out, size_t out_len, const char *source, uint8_t cmd,
                         const uint8_t *payload, size_t len, uint8_t check)
{
    uint8_t expect = wing_frame_checksum(payload, len);
    char printable = (cmd >= 0x20 && cmd < 0x7f) ? (char)cmd : '.';
    int unknown = 0;

    out[0] = '\0';
    if (check != expect) {
        snprintf(out, out_len, "%s UNKNOWN bad-check cmd=%c payload=[", source, printable);
        append_hex(out, out_len, payload, len);
        snprintf(out + strlen(out), out_len - strlen(out), "] check=%02x expected=%02x",
                 check, expect);
        append_raw_frame(out, out_len, cmd, payload, len, check);
        return;
    }

    if (cmd == 'f' && len == 3) {
        unsigned int id = payload[0];
        unsigned int value = (unsigned int)payload[1] | ((unsigned int)payload[2] << 8);
        const char *name = wing_csc_fader_name(id);
        unknown = id > 12 || !name;
        snprintf(out, out_len, "%s%s fader id=0x%02x name=%s fader=%u value=%u",
                 unknown ? "UNKNOWN " : "", source, id, name ? name : "?", id + 1u, value);
    } else if (cmd == 'b' && len == 2) {
        unsigned int id = payload[0];
        unsigned int shown_id = id;
        const char *name;
        if (strcmp(source, "PNLC") == 0 && shown_id > 6)
            shown_id = 25 - shown_id;
        name = strcmp(source, "PNLC") == 0 ? wing_pnlc_button_name(shown_id) :
                                             wing_csc_button_name(shown_id);
        unknown = !name;
        snprintf(out, out_len, "%s%s button id=0x%02x name=%s state=%u %s",
                 unknown ? "UNKNOWN " : "", source, shown_id, name ? name : "?", payload[1],
                 payload[1] ? "pressed" : "released");
    } else if (cmd == 'w' && len == 2) {
        const char *name = wing_csc_potentiometer_name(payload[0]);
        unknown = !name;
        snprintf(out, out_len, "%s%s potentiometer id=0x%02x name=%s value=%u",
                 unknown ? "UNKNOWN " : "", source, payload[0], name ? name : "?", payload[1]);
    } else if (cmd == 'v' && len == 2) {
        const char *name = wing_csc_encoder_name(payload[0]);
        int delta = (payload[1] & 0x80) ? (int)payload[1] - 0x100 : (int)payload[1];
        unknown = !name;
        snprintf(out, out_len, "%s%s encoder id=0x%02x name=%s delta=%d",
                 unknown ? "UNKNOWN " : "", source, payload[0], name ? name : "?", delta);
    } else if (cmd == 'p' && len == 5) {
        unsigned int raw_x = (unsigned int)payload[1] | ((unsigned int)payload[2] << 8);
        unsigned int raw_y = (unsigned int)payload[3] | ((unsigned int)payload[4] << 8);
        snprintf(out, out_len, "%s touchscreen slot=%u phase=0x%02x x=%u y=%u",
                 source, payload[0] & 0x0fu, payload[0] & 0xf0u, raw_x, raw_y);
    } else if (cmd == 'h' && payload_is_printable(payload, len)) {
        size_t copy_len = len < 96 ? len : 95;
        char text[96];
        memcpy(text, payload, copy_len);
        text[copy_len] = '\0';
        snprintf(out, out_len, "%s heartbeat version=\"%s\"", source, text);
    } else if (cmd == 't' && len == 2) {
        snprintf(out, out_len, "%s touch id=0x%02x state=%u %s",
                 source, payload[0], payload[1], payload[1] ? "pressed" : "released");
    } else if (cmd == 'h' && len == 2) {
        snprintf(out, out_len, "%s control/touch id=0x%02x state=%u",
                 source, payload[0], payload[1]);
    } else if (cmd == 'u' && len == 3) {
        unsigned int value = (unsigned int)payload[1] | ((unsigned int)payload[2] << 8);
        snprintf(out, out_len, "%s control value id=0x%02x value=%u", source, payload[0], value);
    } else if (cmd >= 'A' && cmd <= 'P') {
        unsigned int slot = cmd - 'A';
        if (len >= 5) {
            uint8_t type = payload[0];
            uint8_t display = payload[1];
            uint16_t style = payload[2] | (payload[3] << 8);
            uint8_t len1 = payload[4];
            if (type == 'U' && len >= 5 + len1) {
                char txt[256];
                size_t copy_len = len1 < sizeof(txt) - 1 ? len1 : sizeof(txt) - 1;
                memcpy(txt, &payload[5], copy_len);
                txt[copy_len] = '\0';
                snprintf(out, out_len, "%s scribble slot=%u text1=\"%s\" style=%u inverted=%u",
                         source, slot, txt, style, display);
            } else if (type == 'V' && len >= 6 + len1) {
                uint8_t len2 = payload[5 + len1];
                if (len >= 6 + len1 + len2) {
                    char txt1[256];
                    char txt2[256];
                    size_t copy_len1 = len1 < sizeof(txt1) - 1 ? len1 : sizeof(txt1) - 1;
                    size_t copy_len2 = len2 < sizeof(txt2) - 1 ? len2 : sizeof(txt2) - 1;
                    memcpy(txt1, &payload[5], copy_len1);
                    txt1[copy_len1] = '\0';
                    memcpy(txt2, &payload[6 + len1], copy_len2);
                    txt2[copy_len2] = '\0';
                    snprintf(out, out_len, "%s scribble slot=%u text1=\"%s\" text2=\"%s\" style=%u inverted=%u",
                             source, slot, txt1, txt2, style, display);
                } else {
                    unknown = 1;
                }
            } else if (type == 'B' && len >= 5 + len1) {
                snprintf(out, out_len, "%s scribble slot=%u bitmap len=%u style=%u inverted=%u",
                         source, slot, len1, style, display);
            } else {
                unknown = 1;
            }
        } else {
            unknown = 1;
        }
    } else if (cmd == 'T') {
        if (len >= 6) {
            unsigned int slot = payload[0];
            uint8_t type = payload[1];
            uint8_t display = payload[2];
            uint16_t style = payload[3] | (payload[4] << 8);
            uint8_t len1 = payload[5];
            if (type == 'U' && len >= 6 + len1) {
                char txt[256];
                size_t copy_len = len1 < sizeof(txt) - 1 ? len1 : sizeof(txt) - 1;
                memcpy(txt, &payload[6], copy_len);
                txt[copy_len] = '\0';
                snprintf(out, out_len, "%s scribble_explicit slot=%u text1=\"%s\" style=%u inverted=%u",
                         source, slot, txt, style, display);
            } else if (type == 'V' && len >= 7 + len1) {
                uint8_t len2 = payload[6 + len1];
                if (len >= 7 + len1 + len2) {
                    char txt1[256];
                    char txt2[256];
                    size_t copy_len1 = len1 < sizeof(txt1) - 1 ? len1 : sizeof(txt1) - 1;
                    size_t copy_len2 = len2 < sizeof(txt2) - 1 ? len2 : sizeof(txt2) - 1;
                    memcpy(txt1, &payload[6], copy_len1);
                    txt1[copy_len1] = '\0';
                    memcpy(txt2, &payload[7 + len1], copy_len2);
                    txt2[copy_len2] = '\0';
                    snprintf(out, out_len, "%s scribble_explicit slot=%u text1=\"%s\" text2=\"%s\" style=%u inverted=%u",
                             source, slot, txt1, txt2, style, display);
                } else {
                    unknown = 1;
                }
            } else {
                unknown = 1;
            }
        } else {
            unknown = 1;
        }
    } else {
        unknown = 1;
        snprintf(out, out_len, "UNKNOWN %s cmd=%c payload=[", source, printable);
        append_hex(out, out_len, payload, len);
        strncat(out, "]", out_len - strlen(out) - 1);
    }

    if (unknown)
        append_raw_frame(out, out_len, cmd, payload, len, check);
}

int wing_surface_scribble_text(struct wing_surface *surface, unsigned int slot, int explicit_addr,
                              int inverted, uint16_t style, const char *text1, const char *text2)
{
    uint8_t payload[512];
    size_t payload_len = 0;
    uint8_t display = inverted ? 1 : 0;
    uint8_t style_low = style & 0xffu;
    uint8_t style_high = (style >> 8) & 0xffu;

    if (surface->csc_fd < 0 || slot > 15) {
        errno = EINVAL;
        return -1;
    }

    if (explicit_addr) {
        payload[payload_len++] = (uint8_t)slot;
    }

    if (text2 == NULL) {
        // Mode 'U'
        payload[payload_len++] = 'U';
        payload[payload_len++] = display;
        payload[payload_len++] = style_low;
        payload[payload_len++] = style_high;
        size_t len1 = text1 ? strlen(text1) : 0;
        if (len1 > 255) len1 = 255;
        if (payload_len + 1 + len1 > sizeof(payload)) {
            errno = EINVAL;
            return -1;
        }
        payload[payload_len++] = (uint8_t)len1;
        if (len1 > 0) {
            memcpy(&payload[payload_len], text1, len1);
            payload_len += len1;
        }
    } else {
        // Mode 'V'
        payload[payload_len++] = 'V';
        payload[payload_len++] = display;
        payload[payload_len++] = style_low;
        payload[payload_len++] = style_high;
        size_t len1 = text1 ? strlen(text1) : 0;
        if (len1 > 255) len1 = 255;
        size_t len2 = strlen(text2);
        if (len2 > 255) len2 = 255;
        if (payload_len + 2 + len1 + len2 > sizeof(payload)) {
            errno = EINVAL;
            return -1;
        }
        payload[payload_len++] = (uint8_t)len1;
        if (len1 > 0) {
            memcpy(&payload[payload_len], text1, len1);
            payload_len += len1;
        }
        payload[payload_len++] = (uint8_t)len2;
        if (len2 > 0) {
            memcpy(&payload[payload_len], text2, len2);
            payload_len += len2;
        }
    }

    if (explicit_addr) {
        return wing_send_frame(surface->csc_fd, 'T', payload, payload_len);
    } else {
        uint8_t cmd = (uint8_t)('A' + slot);
        return wing_send_frame(surface->csc_fd, cmd, payload, payload_len);
    }
}

int wing_surface_scribble_bitmap(struct wing_surface *surface, unsigned int slot,
                                int inverted, uint16_t style, const uint8_t *bitmap, size_t bitmap_len)
{
    uint8_t payload[512];
    size_t payload_len = 0;
    uint8_t display = inverted ? 1 : 0;
    uint8_t style_low = style & 0xffu;
    uint8_t style_high = (style >> 8) & 0xffu;

    if (surface->csc_fd < 0 || slot > 15 || bitmap_len > 255) {
        errno = EINVAL;
        return -1;
    }

    payload[payload_len++] = 'B';
    payload[payload_len++] = display;
    payload[payload_len++] = style_low;
    payload[payload_len++] = style_high;
    payload[payload_len++] = (uint8_t)bitmap_len;

    if (payload_len + bitmap_len > sizeof(payload)) {
        errno = EINVAL;
        return -1;
    }

    if (bitmap_len > 0 && bitmap != NULL) {
        memcpy(&payload[payload_len], bitmap, bitmap_len);
        payload_len += bitmap_len;
    }


    uint8_t cmd = (uint8_t)('A' + slot);
    return wing_send_frame(surface->csc_fd, cmd, payload, payload_len);
}

int wing_surface_csc_text(struct wing_surface *surface, int style, const char *line1, const char *line2)
{
    uint8_t payload[512];
    size_t len = 0;

    if (surface->pnlc_fd < 0)
        return -1;

    payload[len++] = 2; // Mode 2
    payload[len++] = (uint8_t)style;

    size_t l1 = strlen(line1);
    if (len + l1 + 1 > sizeof(payload))
        return -1;
    memcpy(payload + len, line1, l1 + 1);
    len += l1 + 1;

    size_t l2 = strlen(line2);
    if (len + l2 + 1 > sizeof(payload))
        return -1;
    memcpy(payload + len, line2, l2 + 1);
    len += l2 + 1;

    return wing_send_frame(surface->pnlc_fd, 'D', payload, len);
}

int wing_surface_csc_meters(struct wing_surface *surface, const uint8_t *levels)
{
    uint8_t payload[9];

    if (surface->pnlc_fd < 0)
        return -1;

    payload[0] = 4; // Mode 4
    memcpy(payload + 1, levels, 8);

    return wing_send_frame(surface->pnlc_fd, 'D', payload, sizeof(payload));
}

int wing_surface_csc_meter_update(struct wing_surface *surface, int index, int value)
{
    uint8_t payload[2];

    if (surface->pnlc_fd < 0)
        return -1;

    payload[0] = (uint8_t)index;
    payload[1] = (uint8_t)value;

    return wing_send_frame(surface->pnlc_fd, 'v', payload, sizeof(payload));
}
