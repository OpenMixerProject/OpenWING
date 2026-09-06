#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

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
#define ECSPI_STATREG_OFF     0x18
#define ECSPI_PERIODREG_OFF   0x1C

#define ECSPI_XCH_BIT         (1u << 2)
#define ECSPI_TC_BIT          (1u << 7)

static inline void writel(uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}
static inline uint32_t readl(uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

int main(void) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { perror("open /dev/mem"); return 1; }

    uint8_t *ccm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, CCM_BASE);
    if (ccm != MAP_FAILED) {
        writel(ccm, CCM_CCGR1_OFF, readl(ccm, CCM_CCGR1_OFF) | 0x0Cu);
        munmap(ccm, 0x1000);
    }

    uint8_t *iomuxc = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, IOMUXC_BASE);
    uint8_t *ecspi2 = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, ECSPI2_BASE);

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
        writel(iomuxc, pads[i].off, pads[i].val);
    }

    writel(ecspi2, ECSPI_CONREG_OFF, 0);
    writel(ecspi2, ECSPI_CONREG_OFF, 0x00000011u | (7u << 20));
    writel(ecspi2, ECSPI_CONFIGREG_OFF, 0x00000100u);
    writel(ecspi2, ECSPI_PERIODREG_OFF, 0);

    auto uint8_t xfer_byte(uint8_t tx) {
        uint32_t con = readl(ecspi2, ECSPI_CONREG_OFF);
        writel(ecspi2, ECSPI_STATREG_OFF, ECSPI_TC_BIT);
        writel(ecspi2, ECSPI_TXDATA_OFF, tx);
        writel(ecspi2, ECSPI_INTREG_OFF, ECSPI_TC_BIT);
        writel(ecspi2, ECSPI_CONREG_OFF, con | ECSPI_XCH_BIT);
        int timeout = 10000;
        while ((readl(ecspi2, ECSPI_STATREG_OFF) & ECSPI_TC_BIT) == 0 && --timeout > 0) {
            usleep(1);
        }
        writel(ecspi2, ECSPI_INTREG_OFF, 0);
        return (uint8_t)(readl(ecspi2, ECSPI_RXDATA_OFF) & 0xFF);
    };

    printf("====================================================\n");
    printf("        RAW HARDWARE SPI BUS & MISO PROBING         \n");
    printf("====================================================\n");

    for (uint8_t target = 0x00; target <= 0x04; target++) {
        uint8_t tx[8] = { target, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        uint8_t rx[8] = { 0 };

        for (int i = 0; i < 8; i++) {
            rx[i] = xfer_byte(tx[i]);
        }

        printf("Target 0x%02X (%s):\n", target,
               target == 0 ? "FPGA Registers" :
               target == 1 ? "DSP #1" :
               target == 2 ? "DSP #2" :
               target == 3 ? "DSP #3" : "DSP #4");
        printf("   TX -> ");
        for (int i = 0; i < 8; i++) printf("%02X ", tx[i]);
        printf("\n   RX <- ");
        for (int i = 0; i < 8; i++) printf("%02X ", rx[i]);
        printf("\n\n");
    }

    close(mem_fd);
    return 0;
}
