#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEV "/dev/spidev1.0"
#define SPEED_HZ 1000000

int main(int argc, char **argv) {
    uint16_t cmd = 0x1337;
    if (argc > 1) {
        cmd = (uint16_t)strtoul(argv[1], NULL, 0);
    }

    int fd = open(SPI_DEV, O_RDWR);
    if (fd < 0) {
        perror("open " SPI_DEV);
        return 1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPEED_HZ;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("ioctl config");
        close(fd);
        return 1;
    }

    uint8_t tx[18] = { (cmd >> 8) & 0xff, cmd & 0xff };
    uint8_t rx[18] = {0};

    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (uintptr_t)tx;
    tr.rx_buf = (uintptr_t)rx;
    tr.len = sizeof(tx);
    tr.speed_hz = speed;
    tr.bits_per_word = 8;

    printf("=================================================================\n");
    printf("   WING FPGA SPI VERIFICATION ('Hello from Wing!')\n");
    printf("=================================================================\n");
    printf("[*] Target: %s @ %u Hz\n", SPI_DEV, speed);
    printf("[*] Sending Command: 0x%04X\n", cmd);
    printf("    TX ->");
    for (size_t i = 0; i < sizeof(tx); ++i) printf(" %02X", tx[i]);
    printf("\n");

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("ioctl SPI_IOC_MESSAGE");
        close(fd);
        return 1;
    }
    close(fd);

    printf("    RX <-");
    for (size_t i = 0; i < sizeof(rx); ++i) printf(" %02X", rx[i]);
    printf("\n");

    char ascii[17] = {0};
    for (int i = 0; i < 16; ++i) {
        char c = (char)rx[2 + i];
        ascii[i] = (c >= 32 && c <= 126) ? c : '.';
    }
    printf("    Decoded ASCII (bytes 2..17): \"%s\"\n", ascii);

    static const char expected[] = "Hello from Wing!";
    if (cmd == 0x1337) {
        if (memcmp(rx + 2, expected, 16) == 0) {
            printf("\n=================================================================\n");
            printf("  >>> SUCCESS: RECEIVED EXACT STRING FROM FPGA: \"%s\" <<<\n", ascii);
            printf("  >>> VERIFIED: DATA ORIGINATED HARDWARE-ACCELERATED FROM FPGA! <<<\n");
            printf("=================================================================\n");
            return 0;
        } else {
            printf("\n[FAIL] Mismatch! Expected '%s', got '%s'\n", expected, ascii);
            return 1;
        }
    } else {
        int all_zero = 1;
        for (int i = 2; i < 18; ++i) if (rx[i] != 0) all_zero = 0;
        if (all_zero) {
            printf("[PASS] Non-matching command 0x%04X returned 0x00 (silence).\n", cmd);
            return 0;
        } else {
            printf("[WARN] Non-zero response for non-matching command 0x%04X\n", cmd);
            return 1;
        }
    }
}
