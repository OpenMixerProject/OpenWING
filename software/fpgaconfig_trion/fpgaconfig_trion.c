// FPGA Configuration-tool for the Efinix Trion T55/T85 FPGA in the Behringer WING
// v0.0.1, 06.06.2026
//
// This software reads a *.hex file from Efinix Efinity and sends it using
// the SPI-connection /dev/spidevX.X of the imx6 main-controller to the FPGA
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

// SPI configuration for i.MX6
#define SPI_DEVICE "/dev/spidev2.0" // TODO: check which spidevice we have to use
#define SPI_SPEED_HZ 10000000 // 10 MHz for testing, maybe more is possible
#define FIFO_SIZE 4096

// ----------------------------------------------
// HELPER-FUNCTIONS
// ----------------------------------------------

// returns the size of the given bitstream-file in bytes
long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1; // an error occured
}

// this functions convert the ASCII-HEX-files of
// Efinix Efinity into real binary
uint8_t hexchar_to_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') { // we are accepting lowercase as well
        return c - 'a' + 10;
    }
    return 0; // error for the case we do not have a valid hex-value
}

uint8_t hexchars_to_uint8(char high, char low) {
    return (hexchar_to_nibble(high) << 4) | hexchar_to_nibble(low);
}

// ----------------------------------------------
// END OF HELPER-FUNCTIONS
// ----------------------------------------------

// configures a Efinix Trion T55/T85 via SPI
// accepts path to bitstream-hex-file
// returns 0 if sucecssul, -1 on errors
int configure_trion_spi(const char *bitstream_path) {
    int spi_fd = -1;
    FILE *bitstream_file = NULL;
    struct spi_ioc_transfer tr = {0};
    uint8_t tx_buffer[FIFO_SIZE];
    uint8_t rx_buffer[sizeof(tx_buffer)]; // not used here, but necessary
    int ret = 0;
    uint8_t buf[1024];
    uint16_t offset = 0;
    size_t bytes_read;

    uint8_t spiMode = SPI_MODE_0; // TODO: check if Trion uses MODE 0 like other FPGAs
    uint8_t spiBitsPerWord = 8;
    uint32_t spiSpeed = SPI_SPEED_HZ;

    fprintf(stdout, "FPGA Configuration Tool v0.0.1\n");

    fprintf(stdout, "  Connecting to SPI...\n");
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        perror("Error: Could not open SPI-device");
        ret = -1; goto cleanup_gpio;
    }

    // SPI-Modus (0 = CPOL=0, CPHA=0; Xilinx FPGAs oft Modus 0 oder 2)
    ioctl(spi_fd, SPI_IOC_WR_MODE, &spiMode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &spiBitsPerWord);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spiSpeed);
    fprintf(stdout, "  SPI-Bus '%s' initialized. (Mode %d, Speed %d Hz).\n", SPI_DEVICE, spiMode, spiSpeed);


    // read bitstream-file. Efinity exports an ASCII-Hex-File that we have to read line by line
    // and convert into binary before sending
    fprintf(stdout, "Configuring Efinix Trion T55/T85...\n");

    // calculating the binary-size of the bitstream. The file is an ASCII-HEX file
    // with line-breaks but we are calculating binary-wise. So we have to take care of this
    long file_size = get_file_size(bitstream_path) / 3;

    // now prepare the FPGA for receiving the bitstream
    fprintf(stdout, "  Setting Init-Sequence: RESET# HIGH -> RESET# LOW -> CS# LOW -> RESET# HIGH and start upload...\n");
    int fd_reset = open("/sys/class/leds/reset_fpga/brightness", O_WRONLY);
    int fd_cs = open("/sys/class/leds/cs_fpga/brightness", O_WRONLY);
    write(fd_reset, "1", 1);
    write(fd_cs, "1", 1);
    usleep(500);
    write(fd_reset, "0", 1);
    usleep(500);
    write(fd_cs, "0", 1);
    usleep(500);
    write(fd_reset, "1", 1);
    usleep(500);
    close(fd_reset);
    close(fd_cs);
    usleep(500);

    // prepare SPI-buffers
    tr.tx_buf = (unsigned long)tx_buffer;
    tr.rx_buf = (unsigned long)rx_buffer;
    tr.bits_per_word = spiBitsPerWord;
    tr.speed_hz = spiSpeed;

    // now send the data
    int lines_read = 0;
    char* line = NULL;
    size_t len = 0;
    size_t fifo_pos = 0;
    ssize_t read;
    FILE *inf = fopen(bitstream_path, "r");
    bool safe_mode = 0;
    fprintf(stdout, "Send bitstream '%s' to FPGA...\n", bitstream_path);
    long total_bytes_sent = 0;
    int progress_bar_width = 50;
    while ((read = getline(&line, &len, inf)) != -1) {
        lines_read++;

        // If in safe mode, use lower clock speed for 300 bytes to ensure proper reading of sync pattern
        if((safe_mode == 1) && (lines_read > 300)) {
            safe_mode = 0;
            printf("Now using full clockspeed\n");
        }

        // convert two HEX-Chars into a single binary-byte
        tx_buffer[fifo_pos++] = hexchars_to_uint8(line[0], line[1]); // high-nibble, low-nibble

        // if FIFO is full, transmit data
        if (fifo_pos == FIFO_SIZE) {
            tr.len = FIFO_SIZE;
            ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
            if (ret < 0) {
                perror("Error: SPI-transmission failed");
                goto cleanup;
            }

            // calculate progress-bar
            total_bytes_sent += FIFO_SIZE;
            int progress = (int)((double)total_bytes_sent / file_size * progress_bar_width);
            printf("\r[");
            for (int i = 0; i < progress_bar_width; ++i) {
                if (i < progress) {
                    printf("█");
                }else{
                    printf(" ");
                }
            }
            printf("] %ld/%ld Bytes (%.2f%%)", total_bytes_sent, file_size, (double)total_bytes_sent / file_size * 100);
            fflush(stdout);
        }
    }

    // take care of remaining bytes in buffer
    if (fifo_pos > 0) {
        tr.len = fifo_pos;
        ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
        if (ret < 0) {
            perror("Error: SPI-transmission failed");
            goto cleanup;
        }

        // calculate progress-bar
        total_bytes_sent += fifo_pos;
        int progress = (int)((double)total_bytes_sent / file_size * progress_bar_width);
        printf("\r[");
        for (int i = 0; i < progress_bar_width; ++i) {
            if (i < progress) {
                printf("█");
            }else{
                printf(" ");
            }
        }
        printf("] %ld/%ld Bytes (%.2f%%)", total_bytes_sent, file_size, (double)total_bytes_sent / file_size * 100);
        fflush(stdout);
    }

    // send 1000 zeros for extra clock cycle
    for (uint16_t i = 0; i < 1000; i++) {
        tx_buffer[i] = 0;
    }
    tr.len = 1000;
    ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        perror("Error: SPI-transmission failed");
        goto cleanup;
    }

    fprintf(stdout, "\n\nBitstream transmitted.\n");

cleanup:
    // deassert CS#
    usleep(500);
    fd_cs = open("/sys/class/leds/cs_fpga/brightness", O_WRONLY);
    write(fd_cs, "1", 1);
    close(fd_cs);

    if (bitstream_file) fclose(bitstream_file);
    if (spi_fd >= 0) close(spi_fd);

cleanup_gpio:
    return ret;
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <bitstream.hex>\n", argv[0]);
        return 1;
    }

    return configure_trion_spi(argv[1]);
}
