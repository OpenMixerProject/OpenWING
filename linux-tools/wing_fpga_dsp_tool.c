/* ============================================================================
 * OpenWING FPGA & SHARC DSP Management Tool
 * Direct Hardware SPI Bus Interface for Efinix FPGA & ADSP-21489 SHARC DSPs
 * ============================================================================
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define DEFAULT_SPI_DEV       "/dev/spidev0.1"
#define DEFAULT_SPI_SPEED_HZ  2000000 // 2 MHz
#define FIFO_CHUNK_SIZE       1024

// i.MX6 MMIO Register Bases
#define CCM_BASE              0x020C4000
#define CCM_CCGR1_OFF         0x6C
#define IOMUXC_BASE           0x020E0000
#define GPIO2_BASE            0x020A0000
#define GPIO3_BASE            0x020A4000
#define GPIO5_BASE            0x020AC000
#define ECSPI2_BASE           0x0200C000

#define ECSPI_RXDATA_OFF      0x00
#define ECSPI_TXDATA_OFF      0x04
#define ECSPI_CONREG_OFF      0x08
#define ECSPI_CONFIGREG_OFF   0x0C
#define ECSPI_INTREG_OFF      0x10
#define ECSPI_PERIODREG_OFF   0x18
#define ECSPI_STATREG_OFF     0x1C

#define ECSPI_XCH_BIT         (1u << 2)
#define ECSPI_TC_BIT          (1u << 7)

typedef struct {
    int mem_fd;
    int spi_fd;
    bool use_mmio;
    uint8_t *iomuxc;
    uint8_t *gpio2;
    uint8_t *gpio3;
    uint8_t *gpio5;
    uint8_t *ecspi2;
} wing_hw_ctx_t;

static inline void writel(uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}
static inline uint32_t readl(uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static int hw_init_mmio(wing_hw_ctx_t *ctx) {
    ctx->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (ctx->mem_fd < 0) return -1;

    uint8_t *ccm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->mem_fd, CCM_BASE);
    if (ccm != MAP_FAILED) {
        uint32_t ccgr1 = readl(ccm, CCM_CCGR1_OFF);
        ccgr1 |= 0x0000000Cu;
        writel(ccm, CCM_CCGR1_OFF, ccgr1);
        munmap(ccm, 0x1000);
    }

    ctx->iomuxc = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->mem_fd, IOMUXC_BASE);
    ctx->gpio2  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->mem_fd, GPIO2_BASE);
    ctx->gpio3  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->mem_fd, GPIO3_BASE);
    ctx->gpio5  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->mem_fd, GPIO5_BASE);
    ctx->ecspi2 = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->mem_fd, ECSPI2_BASE);

    if (ctx->iomuxc == MAP_FAILED || ctx->ecspi2 == MAP_FAILED) {
        return -1;
    }

    static const struct { uint32_t off; uint32_t val; } pads[] = {
        { 0x05ac, 0x0001b0b0 }, { 0x01dc, 0x00000005 },
        { 0x05a4, 0x0001b0b0 }, { 0x01d4, 0x00000005 },
        { 0x0534, 0x0001b0b0 }, { 0x0164, 0x00000005 },
        { 0x0538, 0x0001b0b0 }, { 0x0168, 0x00000005 },
        { 0x050c, 0x0001b0b0 }, { 0x013c, 0x00000002 }, { 0x07f4, 0x00000002 },
        { 0x05a8, 0x0001b0b0 }, { 0x01d8, 0x00000002 }, { 0x07f8, 0x00000002 },
        { 0x0510, 0x0001b0b0 }, { 0x0140, 0x00000002 }, { 0x07fc, 0x00000002 },
    };
    for (size_t i = 0; i < sizeof(pads)/sizeof(pads[0]); i++) {
        writel(ctx->iomuxc, pads[i].off, pads[i].val);
    }

    writel(ctx->ecspi2, ECSPI_CONREG_OFF, 0);
    writel(ctx->ecspi2, ECSPI_CONREG_OFF, 0x00000011u | (7u << 20));
    writel(ctx->ecspi2, ECSPI_CONFIGREG_OFF, 0x00000100u);
    writel(ctx->ecspi2, ECSPI_PERIODREG_OFF, 0);

    ctx->use_mmio = true;
    return 0;
}

static void hw_close(wing_hw_ctx_t *ctx) {
    if (ctx->use_mmio) {
        if (ctx->ecspi2 && ctx->ecspi2 != MAP_FAILED) munmap(ctx->ecspi2, 0x4000);
        if (ctx->gpio5 && ctx->gpio5 != MAP_FAILED) munmap(ctx->gpio5, 0x1000);
        if (ctx->gpio3 && ctx->gpio3 != MAP_FAILED) munmap(ctx->gpio3, 0x1000);
        if (ctx->gpio2 && ctx->gpio2 != MAP_FAILED) munmap(ctx->gpio2, 0x1000);
        if (ctx->iomuxc && ctx->iomuxc != MAP_FAILED) munmap(ctx->iomuxc, 0x1000);
        if (ctx->mem_fd >= 0) close(ctx->mem_fd);
    }
    if (ctx->spi_fd >= 0) close(ctx->spi_fd);
}

static uint8_t ecspi2_transfer_byte(wing_hw_ctx_t *ctx, uint8_t tx_byte) {
    uint32_t con = readl(ctx->ecspi2, ECSPI_CONREG_OFF);
    writel(ctx->ecspi2, ECSPI_STATREG_OFF, ECSPI_TC_BIT);
    writel(ctx->ecspi2, ECSPI_TXDATA_OFF, tx_byte);
    writel(ctx->ecspi2, ECSPI_INTREG_OFF, ECSPI_TC_BIT);
    writel(ctx->ecspi2, ECSPI_CONREG_OFF, con | ECSPI_XCH_BIT);

    int timeout = 10000;
    while ((readl(ctx->ecspi2, ECSPI_STATREG_OFF) & ECSPI_TC_BIT) == 0 && --timeout > 0) {
        usleep(1);
    }
    writel(ctx->ecspi2, ECSPI_INTREG_OFF, 0);
    return (uint8_t)(readl(ctx->ecspi2, ECSPI_RXDATA_OFF) & 0xFF);
}

static int hw_xfer(wing_hw_ctx_t *ctx, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t speed_hz) {
    if (ctx->use_mmio) {
        for (size_t i = 0; i < len; i++) {
            uint8_t txb = tx ? tx[i] : 0x00;
            uint8_t rxb = ecspi2_transfer_byte(ctx, txb);
            if (rx) rx[i] = rxb;
        }
        return 0;
    } else {
        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = (unsigned long)tx;
        tr.rx_buf = (unsigned long)rx;
        tr.len = len;
        tr.speed_hz = speed_hz ? speed_hz : DEFAULT_SPI_SPEED_HZ;
        tr.bits_per_word = 8;
        return (ioctl(ctx->spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) ? -1 : 0;
    }
}

int upload_bitstream(wing_hw_ctx_t *ctx, const char *filepath, uint32_t speed_hz) {
    printf("[*] Uploading FPGA Bitstream to Efinix Trion (%s mode)...\n",
           ctx->use_mmio ? "ECSPI2 0x0200C000 MMIO" : "spidev");

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] Could not open file '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    struct stat st;
    fstat(fileno(f), &st);
    size_t file_size = st.st_size;

    if (file_size == 3458589 || file_size == 3458521) {
        fseek(f, 260, SEEK_SET);
        file_size -= 260;
    }

    uint8_t buffer[FIFO_CHUNK_SIZE];
    size_t total_sent = 0;
    const int bar_width = 50;

    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (hw_xfer(ctx, buffer, NULL, n, speed_hz) < 0) {
            fprintf(stderr, "\n[ERROR] SPI transmission error at byte %zu\n", total_sent);
            fclose(f);
            return -1;
        }
        total_sent += n;
        int progress = (int)((double)total_sent / file_size * bar_width);
        printf("\r[");
        for (int i = 0; i < bar_width; i++) printf("%s", (i < progress) ? "=" : " ");
        printf("] %zu/%zu B (%.1f%%)", total_sent, file_size, (double)total_sent / file_size * 100.0);
        fflush(stdout);
    }

    memset(buffer, 0, 100);
    hw_xfer(ctx, buffer, NULL, 100, speed_hz);

    printf("\n[+] FPGA Bitstream successfully uploaded! (%zu bytes transmitted)\n", total_sent);
    fclose(f);
    return 0;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --mmio                 Use direct i.MX6 ECSPI2 MMIO (0x0200C000) (default)\n");
    printf("  -d, --dev <path>       SPI device path for spidev mode (e.g. /dev/spidev0.1)\n");
    printf("  -s, --speed <hz>       SPI speed in Hz (default: %d Hz)\n", DEFAULT_SPI_SPEED_HZ);
    printf("  -u, --upload <file>    Upload bitstream (.bin, .bit.bin) to FPGA\n");
    printf("  --boot <1..4|all> <file> Stream bootloader kernel to specific DSP or all\n");
    printf("  --dsp <1..4>           Target specific DSP for raw SPI transfer\n");
    printf("  --send <hex>           Hex bytes to send to selected DSP (e.g. '00000000')\n");
    printf("  -h, --help             Show this help message\n");
}

int main(int argc, char *argv[]) {
    const char *spi_dev = DEFAULT_SPI_DEV;
    uint32_t speed_hz = DEFAULT_SPI_SPEED_HZ;
    const char *upload_file = NULL;
    const char *boot_target_str = NULL;
    const char *boot_file = NULL;
    const char *send_hex_str = NULL;
    int target_dsp = 0;
    bool force_spidev = false;

    static struct option long_options[] = {
        {"mmio",       no_argument,       0, 1000},
        {"dev",        required_argument, 0, 'd'},
        {"speed",      required_argument, 0, 's'},
        {"upload",     required_argument, 0, 'u'},
        {"dsp",        required_argument, 0, 1003},
        {"send",       required_argument, 0, 1004},
        {"boot-all",   required_argument, 0, 1005},
        {"boot",       required_argument, 0, 1006},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:s:u:h", long_options, NULL)) != -1) {
        switch (c) {
            case 1000: force_spidev = false; break;
            case 'd': spi_dev = optarg; force_spidev = true; break;
            case 's': speed_hz = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'u': upload_file = optarg; break;
            case 1003: target_dsp = atoi(optarg); break;
            case 1004: send_hex_str = optarg; break;
            case 1005: boot_target_str = "all"; boot_file = optarg; break;
            case 1006: 
                boot_target_str = optarg;
                if (optind < argc && argv[optind][0] != '-') {
                    boot_file = argv[optind++];
                }
                break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    wing_hw_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_fd = -1;
    ctx.spi_fd = -1;

    if (!force_spidev) {
        if (hw_init_mmio(&ctx) != 0) {
            ctx.spi_fd = open(spi_dev, O_RDWR);
        }
    } else {
        ctx.spi_fd = open(spi_dev, O_RDWR);
    }

    if (upload_file) {
        int ret = upload_bitstream(&ctx, upload_file, speed_hz);
        hw_close(&ctx);
        return ret;
    }
    if (boot_file) {
        uint8_t target = 0x0F;
        if (boot_target_str && strcmp(boot_target_str, "all") != 0) {
            target = (uint8_t)atoi(boot_target_str);
        }
        printf("[*] Streaming bootloader to Target 0x%02X (%s)...\n",
               target, (target == 0x0F) ? "Broadcast All DSPs" : "Individual DSP");
        FILE *bf = fopen(boot_file, "rb");
        if (!bf) {
            fprintf(stderr, "[ERROR] Could not open %s: %s\n", boot_file, strerror(errno));
            hw_close(&ctx);
            return 1;
        }
        uint8_t buf[256];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), bf)) > 0) {
            uint8_t tx_chunk[257];
            tx_chunk[0] = target;
            memcpy(&tx_chunk[1], buf, r);
            hw_xfer(&ctx, tx_chunk, NULL, r + 1, speed_hz);
        }
        fclose(bf);
        printf("[+] Boot stream complete.\n");
        hw_close(&ctx);
        return 0;
    }
    if (target_dsp && send_hex_str) {
        size_t hlen = strlen(send_hex_str);
        size_t blen = hlen / 2;
        uint8_t *tx = malloc(blen + 1);
        uint8_t *rx = malloc(blen + 1);
        tx[0] = (uint8_t)target_dsp;
        for (size_t i = 0; i < blen; i++) {
            auto int nib(char ch) {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                return 0;
            };
            tx[i + 1] = (uint8_t)((nib(send_hex_str[i * 2]) << 4) | nib(send_hex_str[i * 2 + 1]));
        }
        hw_xfer(&ctx, tx, rx, blen + 1, speed_hz);
        printf("[+] Live MISO from DSP #%d (Target 0x%02X):\n    HEX: ", target_dsp, target_dsp);
        for (size_t i = 1; i <= blen; i++) printf("%02X ", rx[i]);
        printf("\n    ASCII: ");
        for (size_t i = 1; i <= blen; i++) printf("%c", (rx[i] >= 32 && rx[i] <= 126) ? rx[i] : '.');
        printf("\n");
        free(tx);
        free(rx);
        hw_close(&ctx);
        return 0;
    }

    print_usage(argv[0]);
    hw_close(&ctx);
    return 0;
}
