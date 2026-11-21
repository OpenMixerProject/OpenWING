/* Linux-only test double: exercise the actual CLI with real staged FPGA/LDR bytes.
 * Wrap open, ioctl, fopen and getrandom; never maps or opens hardware. */
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "spibone_uapi.h"

enum { NONE, SS0, SS1, GPIO2, GPIO4, CDONE, CRESET };
static int kinds[1024];
static bool released, no_cs;
static unsigned offset, image_size, reset_bits;
static FILE *reference;
static bool offline_set, enabled;
static unsigned booted_core;

static bool fault(const char *name) {
    const char *value = getenv("FAULT");
    return value && !strcmp(value, name);
}

ssize_t __wrap_getrandom(void *buffer, size_t size, unsigned flags) {
    (void)flags;
    uint32_t nonce = 0x12345678;
    assert(size == sizeof nonce);
    memcpy(buffer, &nonce, size);
    return size;
}

static void decode(const char *hex, uint8_t *bytes, size_t size) {
    assert(hex && strlen(hex) == 2 * size);
    for (size_t i = 0; i < size; ++i) {
        unsigned byte;
        assert(sscanf(hex + 2*i, "%2x", &byte) == 1);
        bytes[i] = byte;
    }
}

int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...) {
    int kind = NONE;
    if (!strcmp(path, "/dev/spidev1.0")) kind = SS0;
    else if (!strcmp(path, "/dev/spibone0")) kind = SS1;
    else if (!strcmp(path, "/dev/gpiochip50")) kind = GPIO2;
    else if (!strcmp(path, "/dev/gpiochip51")) kind = GPIO4;
    if (!kind) {
        assert(strncmp(path, "/dev/", 5));
        return __real_open(path, flags);
    }
    int fd = __real_open("/dev/null", O_RDWR | O_CLOEXEC);
    assert(fd >= 0 && (size_t)fd < sizeof kinds / sizeof kinds[0]);
    kinds[fd] = kind;
    return fd;
}

FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode) {
    assert(!strcmp(path, "/sys/module/spidev/parameters/bufsiz") && !strcmp(mode, "r"));
    static char enough[] = "65536\n", small[] = "4096\n";
    char *text = fault("bufsiz") ? small : enough;
    return fmemopen(text, strlen(text), "r");
}

int __wrap_ioctl(int fd, unsigned long op, ...) {
    va_list va;
    va_start(va, op);
    void *arg = va_arg(va, void *);
    va_end(va);
    assert(fd >= 0 && (size_t)fd < sizeof kinds / sizeof kinds[0]);
    assert(access("/run/omc-wing-fpga.json", F_OK) != 0);
    int kind = kinds[fd];
    if (op == GPIO_V2_GET_LINEINFO_IOCTL) {
        struct gpio_v2_line_info *line = arg;
        if (kind == GPIO2 && line->offset == 17) strcpy(line->name, "T55_CRESET_N");
        if (kind == GPIO4 && line->offset == 26) strcpy(line->name, "T55_CDONE");
    } else if (op == GPIO_V2_GET_LINE_IOCTL) {
        struct gpio_v2_line_request *line = arg;
        assert(line->num_lines == 1);
        int pin = line->fd = __real_open("/dev/null", O_RDWR | O_CLOEXEC);
        assert(pin >= 0 && (size_t)pin < sizeof kinds / sizeof kinds[0]);
        if (kind == GPIO2) {
            assert(line->offsets[0] == 17 && line->config.flags == GPIO_V2_LINE_FLAG_OUTPUT);
            assert(offline_set && !enabled);
            puts("CRESET asserted");
            kinds[pin] = CRESET;
        } else {
            assert(kind == GPIO4 && line->offsets[0] == 26 && line->config.flags == GPIO_V2_LINE_FLAG_INPUT);
            kinds[pin] = CDONE;
        }
    } else if (op == GPIO_V2_LINE_SET_VALUES_IOCTL) {
        struct gpio_v2_line_values *v = arg;
        assert(kind == CRESET && v->mask == 1 && v->bits == 1);
        puts("CRESET released");
        released = true;
    } else if (op == GPIO_V2_LINE_GET_VALUES_IOCTL) {
        struct gpio_v2_line_values *v = arg;
        assert(kind == CDONE && v->mask == 1);
        v->bits = !fault("no-done") && (fault("stale-done") || getenv("PRELOADED") ||
                   (image_size && offset == image_size));
    } else if (op == SPIBONE_IOC_SET_STATE) {
        assert(kind == SS1 && *(uint32_t *)arg == SPIBONE_STATE_OFFLINE);
        offline_set = true;
        enabled = false;
        puts("SPIBone offline");
    } else if (op == SPIBONE_IOC_WING_ENABLE) {
        assert(kind == SS1 && offline_set && !enabled && !no_cs && image_size && offset == image_size);
        assert(*(uint32_t *)arg == strtoul(getenv("TEST_BUILD_ID"), NULL, 10));
        puts("SPIBone identity gate");
        if (fault("gate")) return -1;
        enabled = true;
    } else if (op == SPIBONE_IOC_GET_STATE) {
        assert(kind == SS1);
        *(uint32_t *)arg = enabled || getenv("PRELOADED") ? SPIBONE_STATE_ONLINE : SPIBONE_STATE_OFFLINE;
    } else if (op == SPIBONE_IOC_PEEK || op == SPIBONE_IOC_POKE) {
        assert(kind == SS1 && (enabled || getenv("PRELOADED")));
        struct spibone_wb_xfer *reg = arg;
        const char *prior = getenv("PRIOR_RESET");
        unsigned previous = prior ? strtoul(prior, NULL, 10) : 0;
        if (op == SPIBONE_IOC_POKE) {
            assert(reg->addr == 0x1814 && !reset_bits && !(reg->val & ~11u));
            assert((reg->val & previous) == previous);
            unsigned delta = reg->val ^ previous;
            assert(delta == 1 || delta == 2 || delta == 8);
            reset_bits = reg->val;
        } else {
            switch (reg->addr) {
            case 0x1800: reg->val = 0x57494e47; break;
            case 0x1804: reg->val = 0x20000; break;
            case 0x1808: reg->val = strtoul(getenv("TEST_BUILD_ID"), NULL, 10) ^ fault("wrong-build"); break;
            case 0x180c: reg->val = 15; break;
            case 0x1810: reg->val = !fault("not-ready"); break;
            case 0x1814: reg->val = reset_bits && !fault("bad-reset-readback") ? reset_bits : previous; break;
            case 0x1818: reg->val = 0; break;
            case 0x181c: reg->val = fault("audio-active"); break;
            default: assert(!"unexpected WING CSR");
            }
        }
    } else if (op == SPI_IOC_WR_MODE32) {
        uint32_t mode = *(uint32_t *)arg;
        assert(kind == SS0 || kind == SS1);
        assert(kind == SS0);
        assert(mode == SPI_MODE_0 || (kind == SS0 && mode == SPI_NO_CS));
        if (kind == SS0) no_cs = mode == SPI_NO_CS;
    } else if (op == SPI_IOC_WR_BITS_PER_WORD) {
        assert((kind == SS0 || kind == SS1) && *(uint8_t *)arg == 8);
    } else if (op == SPI_IOC_WR_MAX_SPEED_HZ) {
        assert((kind == SS0 || kind == SS1) && *(uint32_t *)arg == 2000000);
    } else if (op == SPI_IOC_MESSAGE(1)) {
        struct spi_ioc_transfer *x = arg;
        const uint8_t *tx = (const uint8_t *)(uintptr_t)x->tx_buf;
        uint8_t *rx = (uint8_t *)(uintptr_t)x->rx_buf;
        assert(kind == SS0 && x->bits_per_word == 8 && !x->cs_change);
        assert(x->speed_hz == (rx ? 10000 : 2000000));
        if (kind == SS0 && no_cs) {
            assert(released && !rx && x->len <= 1024);
            if (!reference) {
                puts("passive transfer started");
                reference = __real_fopen("/usr/share/wing-t55/wing-fpga.hex.bin", "rb");
                assert(reference && !fseek(reference, 0, SEEK_END));
                image_size = ftell(reference);
                rewind(reference);
            }
            for (unsigned i = 0; i < x->len; ++i) assert(fgetc(reference) == tx[i]);
            if (fault("short-fpga")) return x->len - 1;
            offset += x->len;
            if (offset == image_size) puts("full FPGA from byte zero");
        } else if (rx) {
            assert(booted_core && tx[0] == booted_core && x->len == 73);
            uint8_t expected[40];
            decode(getenv("WDSP_REQUEST"), expected, sizeof expected);
            assert(!memcmp(tx + 1, expected, sizeof expected));
            for (size_t i = 41; i < x->len; ++i) assert(!tx[i]);
            // First or last legal reply position; the rest of RX is zero.
            uint8_t *reply = rx + (tx[0] == 4 ? 49 : 41);
            decode(getenv("WDSP_REPLY"), reply, 24);
            if (fault("bad-crc")) reply[23] ^= 1;
            if (fault("short-reply")) return x->len - 1;
        } else {
            assert(kind == SS0 && !rx && (tx[0] == 1 || tx[0] == 2 || tx[0] == 4));
            assert(!booted_core);
            assert(reset_bits & (1u << (tx[0] - 1)));
            char filename[] = "/usr/share/wing-t55/dsp1.ldr";
            filename[sizeof filename - 6] = '0' + tx[0];
            FILE *ldr = __real_fopen(filename, "rb");
            assert(ldr);
            for (unsigned i = 1; i < x->len; ++i) assert(fgetc(ldr) == tx[i]);
            assert(fgetc(ldr) == EOF && !ferror(ldr));
            fclose(ldr);
            printf("one full SS0 message for DSP%u\n", tx[0]);
            if (fault("short-dsp")) return x->len - 1;
            booted_core = tx[0];
        }
        return x->len;
    } else assert(!"unexpected ioctl");
    return 0;
}
