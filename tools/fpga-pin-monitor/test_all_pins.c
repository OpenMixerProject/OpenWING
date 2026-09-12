/*
 * ============================================================================
 * test_all_pins.c - Behringer WING 252-Pin FPGA Telemetry & Logic Analyzer
 * ============================================================================
 *
 * Overview:
 * ---------
 * This utility interfaces with the `all_pin_monitor` bitstream loaded onto the
 * Efinix Trion FPGA (T55F484 or T85F484). It turns the FPGA into a 252-channel
 * hardware logic analyzer and signal signature scanner.
 *
 * All non-SPI GPIO pins on the FPGA (across Banks 1, 2, 3, and 4) are sampled
 * simultaneously on every clock cycle.
 *
 * Telemetry Features per Pin:
 *   - Live DC logic level (0 or 1)
 *   - Latch indicators: Saw HIGH, Saw LOW, Active Toggling
 *   - 16-bit transition / edge counter (pulse count)
 *   - 16-bit HIGH duration counter (for exact duty cycle calculation)
 *   - 8-bit minimum observed pulse width (cycles)
 *   - 32-sample consecutive digital waveform snapshot
 *
 * SPI Protocol (0xA55A Fixed 8-Byte Frames, Mode 0, MSB First):
 *   - Write Frame: [0xA5, 0x5A, RegAddr[6:0], Data[31:24], Data[23:16], Data[15:8], Data[7:0], 0x00]
 *   - Read Frame : [0xA5, 0x5A, 0x80 | RegAddr[6:0], RX[31:24], RX[23:16], RX[15:8], RX[7:0], 0x00]
 *
 * Register Map:
 *   - 0x00: FPGA Magic ID (0x50494E53 = "PINS")
 *   - 0x01: Build Version (0x20260908)
 *   - 0x02: Total Pin Count (252) / Write 1 to clear counters
 *   - 0x03: Select Pin Index (0..251)
 *   - 0x04: Telemetry Word 0: [Live, SawHigh, SawLow, Toggling, EdgeCount[15:0]]
 *   - 0x05: Telemetry Word 1: [HighCount[15:0], MinPulse[7:0], 0x00]
 *   - 0x06: Telemetry Word 2: Waveform History (32 samples)
 *   - 0x10..0x17: 8x 32-bit Activity Bitmasks (1 = toggling pin)
 *   - 0x20..0x27: 8x 32-bit Live DC Level Bitmasks
 *
 * Operating Modes:
 *   - Full Scan (`test_all_pins`): Samples for N seconds, classifies active signals.
 *   - Differential Mode (`--diff`): Compares baseline vs. cable plugged-in state
 *     to instantly identify AES50 or expansion port pins.
 *   - Full Dump (`--dump`): Dumps static DC levels and states for all 252 pins.
 * ============================================================================
 */

#define VERSION "v1.2"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEV "/dev/spidev1.0"
#define NUM_PINS 252

/*
 * Pin Metadata mapping FPGA pin index to Package Ball, Efinix Resource, and I/O Bank
 */
struct pin_info {
    int index;
    const char *ball;
    const char *resource;
    const char *bank;
};

static const struct pin_info PINS[NUM_PINS] = {
    { 0, "A10", "GPIOT_TXP25", "2C" },
    { 1, "A11", "GPIOT_RXP01", "2D" },
    { 2, "A12", "GPIOT_RXP11", "2E" },
    { 3, "A13", "GPIOT_RXN11", "2E" },
    { 4, "A14", "GPIOT_RXN15", "2E" },
    { 5, "A15", "GPIOT_RXN18", "2E" },
    { 6, "A16", "GPIOT_RXP20", "2F" },
    { 7, "A17", "GPIOT_RXP21", "2F" },
    { 8, "A18", "GPIOR_167", "3D_TR_BR" },
    { 9, "A19", "GPIOR_169", "3D_TR_BR" },
    { 10, "A2", "GPIOT_TXP05", "2A" },
    { 11, "A20", "GPIOR_174", "3D_TR_BR" },
    { 12, "A21", "GPIOR_176", "3D_TR_BR" },
    { 13, "A3", "GPIOT_TXN06", "2A" },
    { 14, "A4", "GPIOT_TXP06", "2A" },
    { 15, "A5", "GPIOT_TXP07", "2A" },
    { 16, "A6", "GPIOT_TXP12", "2B" },
    { 17, "A7", "GPIOT_TXP14", "2B" },
    { 18, "A8", "GPIOT_TXN18", "2B" },
    { 19, "A9", "GPIOT_TXP23", "2C" },
    { 20, "AA1", "GPIOB_TXP05", "4F" },
    { 21, "AA10", "GPIOB_TXN26", "4D" },
    { 22, "AA11", "GPIOB_RXN00", "4C" },
    { 23, "AA12", "GPIOB_RXP05", "4C" },
    { 24, "AA13", "GPIOB_RXN10", "4B" },
    { 25, "AA15", "GPIOB_RXN20", "4A" },
    { 26, "AA16", "GPIOB_RXP20", "4A" },
    { 27, "AA2", "GPIOB_TXN06", "4F" },
    { 28, "AA5", "GPIOB_TXP08", "4F" },
    { 29, "AA6", "GPIOB_TXN14", "4E" },
    { 30, "AA7", "GPIOB_TXN16", "4E" },
    { 31, "AA8", "GPIOB_TXN22", "4D" },
    { 32, "AA9", "GPIOB_TXN24", "4D" },
    { 33, "AB10", "GPIOB_TXP26", "4D" },
    { 34, "AB11", "GPIOB_RXP00", "4C" },
    { 35, "AB12", "GPIOB_RXN05", "4C" },
    { 36, "AB13", "GPIOB_RXP10", "4B" },
    { 37, "AB14", "GPIOB_RXN14", "4B" },
    { 38, "AB15", "GPIOB_RXP14", "4B" },
    { 39, "AB16", "GPIOB_RXP28", "4A" },
    { 40, "AB17", "GPIOB_RXN28", "4A" },
    { 41, "AB2", "GPIOB_TXP06", "4F" },
    { 42, "AB3", "GPIOB_TXP07", "4F" },
    { 43, "AB4", "GPIOB_TXN07", "4F" },
    { 44, "AB5", "GPIOB_TXN08", "4F" },
    { 45, "AB6", "GPIOB_TXP14", "4E" },
    { 46, "AB7", "GPIOB_TXP16", "4E" },
    { 47, "AB8", "GPIOB_TXP22", "4D" },
    { 48, "AB9", "GPIOB_TXP24", "4D" },
    { 49, "B1", "GPIOT_TXN04", "2A" },
    { 50, "B10", "GPIOT_TXN25", "2C" },
    { 51, "B11", "GPIOT_RXN01", "2D" },
    { 52, "B13", "GPIOT_RXP13", "2E" },
    { 53, "B14", "GPIOT_RXP15", "2E" },
    { 54, "B15", "GPIOT_RXP18", "2E" },
    { 55, "B16", "GPIOT_RXN20", "2F" },
    { 56, "B17", "GPIOT_RXN21", "2F" },
    { 57, "B19", "GPIOR_172", "3D_TR_BR" },
    { 58, "B2", "GPIOT_TXN05", "2A" },
    { 59, "B20", "GPIOR_175", "3D_TR_BR" },
    { 60, "B21", "GPIOR_177", "3D_TR_BR" },
    { 61, "B22", "GPIOR_180", "3D_TR_BR" },
    { 62, "B5", "GPIOT_TXN07", "2A" },
    { 63, "B6", "GPIOT_TXN12", "2B" },
    { 64, "B7", "GPIOT_TXN14", "2B" },
    { 65, "B8", "GPIOT_TXP18", "2B" },
    { 66, "B9", "GPIOT_TXN23", "2C" },
    { 67, "C1", "GPIOT_TXP04", "2A" },
    { 68, "C11", "GPIOT_RXP04", "2D" },
    { 69, "C12", "GPIOT_RXN04", "2D" },
    { 70, "C13", "GPIOT_RXN13", "2E" },
    { 71, "C15", "GPIOT_RXN23", "2F" },
    { 72, "C16", "GPIOT_RXP23", "2F" },
    { 73, "C18", "GPIOR_166", "3D_TR_BR" },
    { 74, "C19", "GPIOR_171", "3D_TR_BR" },
    { 75, "C2", "GPIOT_TXN02", "2A" },
    { 76, "C21", "GPIOR_178", "3D_TR_BR" },
    { 77, "C22", "GPIOR_182", "3D_TR_BR" },
    { 78, "C3", "GPIOT_TXP03", "2A" },
    { 79, "C4", "GPIOT_TXN03", "2A" },
    { 80, "C6", "GPIOT_TXP10", "2B" },
    { 81, "C7", "GPIOT_TXN10", "2B" },
    { 82, "D1", "GPIOT_TXP01", "2A" },
    { 83, "D10", "GPIOT_TXP24", "2C" },
    { 84, "D11", "GPIOT_RXN02", "2D" },
    { 85, "D14", "GPIOT_RXN19", "2E" },
    { 86, "D15", "GPIOT_RXN22", "2F" },
    { 87, "D18", "GPIOR_168", "3D_TR_BR" },
    { 88, "D19", "GPIOR_173", "3D_TR_BR" },
    { 89, "D2", "GPIOT_TXP02", "2A" },
    { 90, "D20", "GPIOR_179", "3D_TR_BR" },
    { 91, "D21", "GPIOR_184", "3D_TR_BR" },
    { 92, "D22", "GPIOR_183", "3D_TR_BR" },
    { 93, "D9", "GPIOT_TXN21", "2C" },
    { 94, "E1", "GPIOT_TXN01", "2A" },
    { 95, "E10", "GPIOT_TXN24", "2C" },
    { 96, "E11", "GPIOT_RXP02", "2D" },
    { 97, "E12", "GPIOT_RXN09", "2D" },
    { 98, "E13", "GPIOT_RXN14", "2E" },
    { 99, "E14", "GPIOT_RXP19", "2E" },
    { 100, "E15", "GPIOT_RXP22", "2F" },
    { 101, "E16", "GPIOT_RXN28", "2F" },
    { 102, "E19", "GPIOR_170", "3D_TR_BR" },
    { 103, "E2", "GPIOL_157", "1F_1G" },
    { 104, "E21", "GPIOR_181", "3D_TR_BR" },
    { 105, "E22", "GPIOR_185", "3D_TR_BR" },
    { 106, "E3", "GPIOL_160", "1F_1G" },
    { 107, "E4", "GPIOL_164", "1F_1G" },
    { 108, "E5", "GPIOL_162", "1F_1G" },
    { 109, "E6", "GPIOT_TXN11", "2B" },
    { 110, "E7", "GPIOT_TXP13", "2B" },
    { 111, "E8", "GPIOT_TXP16", "2B" },
    { 112, "E9", "GPIOT_TXP21", "2C" },
    { 113, "F1", "GPIOL_151", "1F_1G" },
    { 114, "F10", "GPIOT_TXP26", "2C" },
    { 115, "F12", "GPIOT_RXP09", "2D" },
    { 116, "F13", "GPIOT_RXP14", "2E" },
    { 117, "F15", "GPIOT_RXN24", "2F" },
    { 118, "F16", "GPIOT_RXP28", "2F" },
    { 119, "F17", "GPIOT_RXN29", "2F" },
    { 120, "F4", "GPIOL_156", "1F_1G" },
    { 121, "F5", "GPIOL_150", "1F_1G" },
    { 122, "F6", "GPIOT_TXP11", "2B" },
    { 123, "F7", "GPIOT_TXN13", "2B" },
    { 124, "F8", "GPIOT_TXN16", "2B" },
    { 125, "G1", "GPIOL_136", "1F_1G" },
    { 126, "G10", "GPIOT_TXN26", "2C" },
    { 127, "G11", "GPIOT_RXP03", "2D" },
    { 128, "G12", "GPIOT_RXN08", "2D" },
    { 129, "G13", "GPIOT_RXN12", "2E" },
    { 130, "G14", "GPIOT_RXP16", "2E" },
    { 131, "G15", "GPIOT_RXP24", "2F" },
    { 132, "G17", "GPIOT_RXP29", "2F" },
    { 133, "G2", "GPIOL_142", "1F_1G" },
    { 134, "G3", "GPIOL_141", "1F_1G" },
    { 135, "G4", "GPIOL_138", "1F_1G" },
    { 136, "G5", "GPIOL_147", "1F_1G" },
    { 137, "G6", "GPIOL_134", "1F_1G" },
    { 138, "G7", "GPIOT_TXP15", "2B" },
    { 139, "G8", "GPIOT_TXN20", "2C" },
    { 140, "G9", "GPIOT_TXP22", "2C" },
    { 141, "H1", "GPIOL_133", "1F_1G" },
    { 142, "H11", "GPIOT_RXN03", "2D" },
    { 143, "H12", "GPIOT_RXP08", "2D" },
    { 144, "H13", "GPIOT_RXP12", "2E" },
    { 145, "H14", "GPIOT_RXN16", "2E" },
    { 146, "H16", "GPIOT_RXN27", "2F" },
    { 147, "H3", "GPIOL_131", "1F_1G" },
    { 148, "H4", "GPIOL_121", "1F_1G" },
    { 149, "H6", "GPIOL_123", "1F_1G" },
    { 150, "H7", "GPIOT_TXN15", "2B" },
    { 151, "H8", "GPIOT_TXP20", "2C" },
    { 152, "H9", "GPIOT_TXN22", "2C" },
    { 153, "J1", "GPIOL_119", "1F_1G" },
    { 154, "J16", "GPIOT_RXP27", "2F" },
    { 155, "J2", "GPIOL_117", "1D_1E" },
    { 156, "J3", "GPIOL_91", "1D_1E" },
    { 157, "J4", "GPIOL_88", "1D_1E" },
    { 158, "J5", "GPIOL_85", "1D_1E" },
    { 159, "J7", "GPIOL_115", "1D_1E" },
    { 160, "J8", "GPIOL_116", "1D_1E" },
    { 161, "K1", "GPIOL_87", "1D_1E" },
    { 162, "K15", "GPIOR_188", "3D_TR_BR" },
    { 163, "K2", "GPIOL_89", "1D_1E" },
    { 164, "K4", "GPIOL_79", "1D_1E" },
    { 165, "K5", "GPIOL_81", "1D_1E" },
    { 166, "K6", "GPIOL_77", "1D_1E" },
    { 167, "K7", "GPIOL_76", "1D_1E" },
    { 168, "K8", "GPIOL_75", "1D_1E" },
    { 169, "L1", "GPIOL_68", "1B_1C" },
    { 170, "L15", "GPIOR_187", "3D_TR_BR" },
    { 171, "L2", "GPIOL_69", "1B_1C" },
    { 172, "L3", "GPIOL_73", "1D_1E" },
    { 173, "L4", "GPIOL_74", "1D_1E" },
    { 174, "L5", "GPIOL_71", "1D_1E" },
    { 175, "L6", "GPIOL_72", "1D_1E" },
    { 176, "L7", "GPIOL_70", "1D_1E" },
    { 177, "M1", "GPIOL_65", "1B_1C" },
    { 178, "M15", "GPIOR_186", "3D_TR_BR" },
    { 179, "M3", "GPIOL_67", "1B_1C" },
    { 180, "M4", "GPIOL_64", "1B_1C" },
    { 181, "M6", "GPIOL_66", "1B_1C" },
    { 182, "M7", "GPIOL_58", "1B_1C" },
    { 183, "N1", "GPIOL_62", "1B_1C" },
    { 184, "N2", "GPIOL_63", "1B_1C" },
    { 185, "N4", "GPIOL_56", "1B_1C" },
    { 186, "N5", "GPIOL_53", "1B_1C" },
    { 187, "N7", "GPIOL_40", "1B_1C" },
    { 188, "P1", "GPIOL_52", "1B_1C" },
    { 189, "P2", "GPIOL_50", "1B_1C" },
    { 190, "P3", "GPIOL_51", "1B_1C" },
    { 191, "P4", "GPIOL_36", "1B_1C" },
    { 192, "P5", "GPIOL_47", "1B_1C" },
    { 193, "P6", "GPIOL_45", "1B_1C" },
    { 194, "P7", "GPIOL_24", "1B_1C" },
    { 195, "P8", "GPIOL_22", "1B_1C" },
    { 196, "R1", "GPIOL_49", "1B_1C" },
    { 197, "R10", "GPIOB_RXN01", "4C" },
    { 198, "R12", "GPIOB_RXP09", "4C" },
    { 199, "R13", "GPIOB_RXN15", "4B" },
    { 200, "R14", "GPIOB_RXP15", "4B" },
    { 201, "R15", "GPIOB_RXN21", "4A" },
    { 202, "R2", "GPIOL_15", "1B_1C" },
    { 203, "R3", "GPIOL_17", "1B_1C" },
    { 204, "R4", "GPIOL_19", "1B_1C" },
    { 205, "R6", "GPIOL_20", "1B_1C" },
    { 206, "R7", "GPIOL_18", "1B_1C" },
    { 207, "R8", "GPIOL_16", "1B_1C" },
    { 208, "T10", "GPIOB_RXP01", "4C" },
    { 209, "T12", "GPIOB_RXN09", "4C" },
    { 210, "T13", "GPIOB_RXP12", "4B" },
    { 211, "T14", "GPIOB_RXP18", "4B" },
    { 212, "T15", "GPIOB_RXP21", "4A" },
    { 213, "T4", "GPIOL_13", "1A" },
    { 214, "T6", "GPIOL_14", "1B_1C" },
    { 215, "T8", "GPIOB_TXP18", "4E" },
    { 216, "U10", "GPIOB_TXN23", "4D" },
    { 217, "U11", "GPIOB_RXP06", "4C" },
    { 218, "U12", "GPIOB_RXN06", "4C" },
    { 219, "U13", "GPIOB_RXN12", "4B" },
    { 220, "U14", "GPIOB_RXN18", "4B" },
    { 221, "U4", "GPIOL_11", "1A" },
    { 222, "U5", "GPIOL_05", "1A" },
    { 223, "U6", "GPIOL_12", "1A" },
    { 224, "U7", "GPIOB_TXN15", "4E" },
    { 225, "U8", "GPIOB_TXN18", "4E" },
    { 226, "U9", "GPIOB_TXP23", "4D" },
    { 227, "V10", "GPIOB_TXN25", "4D" },
    { 228, "V11", "GPIOB_RXN03", "4C" },
    { 229, "V12", "GPIOB_RXP08", "4C" },
    { 230, "V14", "GPIOB_RXP19", "4B" },
    { 231, "V15", "GPIOB_RXP29", "4A" },
    { 232, "V6", "GPIOL_04", "1A" },
    { 233, "V7", "GPIOB_TXP15", "4E" },
    { 234, "V8", "GPIOB_TXN17", "4E" },
    { 235, "V9", "GPIOB_TXP19", "4E" },
    { 236, "W10", "GPIOB_TXP25", "4D" },
    { 237, "W11", "GPIOB_RXP03", "4C" },
    { 238, "W12", "GPIOB_RXN08", "4C" },
    { 239, "W14", "GPIOB_RXN19", "4B" },
    { 240, "W15", "GPIOB_RXN29", "4A" },
    { 241, "W2", "GPIOB_TXP04", "4F" },
    { 242, "W3", "GPIOB_TXN04", "4F" },
    { 243, "W4", "GPIOB_TXN03", "4F" },
    { 244, "W8", "GPIOB_TXP17", "4E" },
    { 245, "W9", "GPIOB_TXN19", "4E" },
    { 246, "Y1", "GPIOB_TXN05", "4F" },
    { 247, "Y13", "GPIOB_RXN16", "4B" },
    { 248, "Y14", "GPIOB_RXP16", "4B" },
    { 249, "Y4", "GPIOB_TXP03", "4F" },
    { 250, "Y6", "GPIOB_TXP13", "4E" },
    { 251, "Y7", "GPIOB_TXN13", "4E" },
};

static int spi_fd = -1;

/*
 * ----------------------------------------------------------------------------
 * read_reg()
 * Reads a 32-bit register from the FPGA telemetry engine over SPI.
 * Frame format: [0xA5, 0x5A, 0x80 | (addr & 0x7F), 0, 0, 0, 0, 0]
 * Response: rx[3..6] contains the 32-bit big-endian register value.
 * ----------------------------------------------------------------------------
 */
static uint32_t read_reg(uint8_t reg_addr) {
    uint8_t tx[8] = { 0xA5, 0x5A, (uint8_t)(0x80 | (reg_addr & 0x7F)), 0, 0, 0, 0, 0 };
    uint8_t rx[8] = { 0 };
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 8,
        .speed_hz = 2000000,
        .bits_per_word = 8,
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI read error");
        return 0;
    }
    return ((uint32_t)rx[3] << 24) | ((uint32_t)rx[4] << 16) | ((uint32_t)rx[5] << 8) | rx[6];
}

/*
 * ----------------------------------------------------------------------------
 * write_reg()
 * Writes a 32-bit value to a register on the FPGA telemetry engine.
 * Frame format: [0xA5, 0x5A, addr & 0x7F, val[31:24], val[23:16], val[15:8], val[7:0], 0x00]
 * ----------------------------------------------------------------------------
 */
static void write_reg(uint8_t reg_addr, uint32_t val) {
    uint8_t tx[8] = {
        0xA5,
        0x5A,
        (uint8_t)(0x00 | (reg_addr & 0x7F)),
        (uint8_t)((val >> 24) & 0xFF),
        (uint8_t)((val >> 16) & 0xFF),
        (uint8_t)((val >> 8) & 0xFF),
        (uint8_t)(val & 0xFF),
        0x00
    };
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = 0,
        .len = 8,
        .speed_hz = 2000000,
        .bits_per_word = 8,
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI write error");
    }
}

/*
 * ----------------------------------------------------------------------------
 * clear_counters()
 * Clears edge counters, duration timers, and latch flags in the FPGA.
 * ----------------------------------------------------------------------------
 */
static void clear_counters(void) {
    write_reg(0x02, 0x01);
}

/*
 * Telemetry snapshot for a single monitored pin
 */
struct pin_telemetry {
    int live;             /* Instantaneous digital level (0 or 1) */
    int saw_high;         /* Latched 1 if pin was HIGH at any point */
    int saw_low;          /* Latched 1 if pin was LOW at any point */
    int toggling;         /* Latched 1 if both HIGH and LOW observed */
    uint16_t edge_cnt;    /* Count of transitions (clamped to 65535) */
    uint16_t high_cnt;    /* Count of cycles pin was HIGH */
    uint8_t min_pulse;    /* Minimum observed pulse width in clock cycles */
    uint32_t history;     /* 32 consecutive digital samples (waveform) */
};

/*
 * ----------------------------------------------------------------------------
 * read_pin_telemetry()
 * Selects pin `idx` (via register 0x03) and reads words 0x04, 0x05, 0x06.
 * ----------------------------------------------------------------------------
 */
static void read_pin_telemetry(int idx, struct pin_telemetry *t) {
    write_reg(0x03, (uint32_t)idx);
    uint32_t w0 = read_reg(0x04);
    uint32_t w1 = read_reg(0x05);
    uint32_t w2 = read_reg(0x06);

    t->live = (w0 >> 31) & 1;
    t->saw_high = (w0 >> 30) & 1;
    t->saw_low = (w0 >> 29) & 1;
    t->toggling = (w0 >> 28) & 1;
    t->edge_cnt = w0 & 0xFFFF;

    t->high_cnt = (w1 >> 16) & 0xFFFF;
    t->min_pulse = (w1 >> 8) & 0xFF;

    t->history = w2;
}

/*
 * ----------------------------------------------------------------------------
 * get_activity_masks()
 * Reads the 8x 32-bit bulk activity registers (0x10..0x17).
 * Bit i in the composite 256-bit mask indicates that pin i has toggled.
 * ----------------------------------------------------------------------------
 */
static void get_activity_masks(uint32_t masks[8]) {
    for (int i = 0; i < 8; i++) {
        masks[i] = read_reg(0x10 + i);
    }
}

/*
 * ----------------------------------------------------------------------------
 * format_history_bits()
 * Formats a 32-bit waveform snapshot into a 32-character binary string ('0'/'1').
 * ----------------------------------------------------------------------------
 */
static void format_history_bits(uint32_t val, char str[33]) {
    for (int i = 31; i >= 0; i--) {
        str[31 - i] = ((val >> i) & 1) ? '1' : '0';
    }
    str[32] = '\0';
}

/*
 * ----------------------------------------------------------------------------
 * classify_signal()
 * Heuristically categorizes the signal based on transition count and duty cycle:
 *   - Static DC High / Low
 *   - High-Frequency Clock (~50% duty, >60k pulses)
 *   - Symmetric Audio Clock (45%..55% duty)
 *   - Strobe / Frame Sync Pulse (<=5% or >=95% duty)
 *   - Serial Data / TDM Audio Stream
 * ----------------------------------------------------------------------------
 */
static const char *classify_signal(const struct pin_telemetry *t, int total_samples) {
    if (t->edge_cnt == 0) {
        return t->live ? "STATIC HIGH (3.3V / Pull-Up)" : "STATIC LOW (GND / 0V)";
    }
    float duty = 0.0f;
    if (t->edge_cnt > 0 && total_samples > 0) {
        duty = (float)t->high_cnt / (float)total_samples * 100.0f;
    }
    if (t->edge_cnt >= 60000) {
        if (duty >= 40.0f && duty <= 60.0f) return "HIGH-FREQ CLOCK (~50% Duty)";
        return "HIGH-FREQ TOGGLE";
    }
    if (duty >= 45.0f && duty <= 55.0f) {
        return "SYMMETRIC CLOCK (~50% Duty)";
    }
    if (duty <= 5.0f || duty >= 95.0f) {
        return "FRAME SYNC / STROBE PULSE";
    }
    return "SERIAL DATA / TDM STREAM";
}

/*
 * ----------------------------------------------------------------------------
 * run_scan()
 * Performs a comprehensive scan across all 252 pins over `duration_sec` seconds.
 * ----------------------------------------------------------------------------
 */
static void run_scan(int duration_sec) {
    printf("\n========================================================================================\n");
    printf("         BEHRINGER WING — 252-PIN FULL FPGA TELEMETRY & CLOCK SIGNATURE SCAN\n");
    printf("========================================================================================\n");

    uint32_t magic = read_reg(0x00);
    uint32_t ver = read_reg(0x01);
    uint32_t pin_cnt = read_reg(0x02);

    char magic_str[5] = {0};
    magic_str[0] = (magic >> 24) & 0xFF;
    magic_str[1] = (magic >> 16) & 0xFF;
    magic_str[2] = (magic >> 8) & 0xFF;
    magic_str[3] = magic & 0xFF;

    printf("FPGA Magic Signature : '%s' (0x%08X)\n", magic_str, magic);
    printf("FPGA Build Version   : 0x%08X\n", ver);
    printf("Total Pins Monitored : %u (ALL non-SPI GPIOs on Trion T55/T85)\n\n", pin_cnt);

    if (magic != 0x50494E53) {
        printf("[!] WARNING: Magic does not match 'PINS'.\n\n");
    }

    printf("[*] Clearing transition counters and sampling all 252 pins (%ds)...\n", duration_sec);
    clear_counters();

    struct timeval start, now;
    gettimeofday(&start, NULL);
    int sample_clocks = 0;
    while (1) {
        gettimeofday(&now, NULL);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
        if (elapsed >= (double)duration_sec) break;
        read_reg(0x00);
        sample_clocks += 64;
        usleep(50);
    }

    uint32_t masks[8];
    get_activity_masks(masks);

    printf("\n========================================================================================\n");
    printf("IDX   BALL   RESOURCE         BANK     LIVE  PULSES   DUTY%%  MIN_PW  WAVEFORM (32-SAMPLE)   CLASSIFICATION\n");
    printf("----------------------------------------------------------------------------------------\n");

    int active_pins = 0;
    for (int i = 0; i < NUM_PINS; i++) {
        int slice = i / 32;
        int bit = i % 32;
        if ((masks[slice] >> bit) & 1) {
            struct pin_telemetry t;
            read_pin_telemetry(i, &t);

            char wave[33];
            format_history_bits(t.history, wave);

            float duty = 0.0f;
            if (sample_clocks > 0) {
                duty = (float)t.high_cnt / (float)(sample_clocks > 65535 ? 65535 : sample_clocks) * 100.0f;
            }
            const char *sig_class = classify_signal(&t, sample_clocks);

            printf("%3d   %-6s %-16s %-8s %d     %5u   %5.1f%%   %3u   %s  %s\n",
                   i, PINS[i].ball, PINS[i].resource, PINS[i].bank,
                   t.live, t.edge_cnt, duty, t.min_pulse, wave, sig_class);
            active_pins++;
        }
    }

    if (active_pins == 0) {
        printf("  (No toggling or active signals detected across any of the 252 pins)\n");
    }
    printf("----------------------------------------------------------------------------------------\n");
    printf("Total Active Toggling Pins: %d / %d\n\n", active_pins, NUM_PINS);
}

static void run_diff(int duration_sec)
{
    printf("\n========================================================================================\n");
    printf("     BEHRINGER WING — AES50 DIFFERENTIAL PLUG-IN DETECTOR (PORT ISOLATION)\n");
    printf("========================================================================================\n");
    printf("IDX   BALL   RESOURCE         BANK     LIVE  PULSES   DUTY%%  MIN_PW  WAVEFORM (32-SAMPLE)   CLASSIFICATION\n");
    printf("----------------------------------------------------------------------------------------\n");


    while(true)
    {

        /* 1 Second Baseline measurement */
        printf("B");
        fflush(stdout);
        clear_counters();

        struct timeval start, now;
        gettimeofday(&start, NULL);
        while (1) {
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
            if (elapsed >= 1.0) break;
            read_reg(0x00);
            usleep(50);
        }

        uint32_t base_masks[8];
        get_activity_masks(base_masks);

        int base_active = 0;
        for (int i = 0; i < NUM_PINS; i++) {
            if ((base_masks[i / 32] >> (i % 32)) & 1) base_active++;
        }

        /* x Seconds Sample active state */
        clear_counters();
        printf("S");
        fflush(stdout);

        gettimeofday(&start, NULL);
        int sample_clocks = 0;
        while (1) {
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
            if (elapsed >= (double)duration_sec) break;
            read_reg(0x00);
            sample_clocks += 64;
            usleep(50);
        }

        uint32_t new_masks[8];
        get_activity_masks(new_masks);

        /* Print changed Signals */
        int new_signals = 0;
        for (int i = 0; i < NUM_PINS; i++)
        {
            int was_active = (base_masks[i / 32] >> (i % 32)) & 1;
            int is_active = (new_masks[i / 32] >> (i % 32)) & 1;

            if (is_active && !was_active)
            {
                struct pin_telemetry t;
                read_pin_telemetry(i, &t);
                char wave[33];
                format_history_bits(t.history, wave);
                float duty = 0.0f;
                if (sample_clocks > 0) {
                    duty = (float)t.high_cnt / (float)(sample_clocks > 65535 ? 65535 : sample_clocks) * 100.0f;
                }
                const char *sig_class = classify_signal(&t, sample_clocks);
                printf("\n%3d   %-6s %-16s %-8s %d     %5u   %5.1f%%   %3u   %s  %s\n",
                    i, PINS[i].ball, PINS[i].resource, PINS[i].bank,
                    t.live, t.edge_cnt, duty, t.min_pulse, wave, sig_class);
                new_signals++;
            }
        }
        fflush(stdout);
    }
}

/*
 * ----------------------------------------------------------------------------
 * run_dump_all()
 * Iterates through all 252 pins and dumps DC voltage state (0 or 1),
 * saw_high / saw_low latches, and transition edge counts.
 * ----------------------------------------------------------------------------
 */
static void run_dump_all(void) {
    printf("\n========================================================================================\n");
    printf("                  FULL 252-PIN STATIC DC LEVEL & STATE INVENTORY\n");
    printf("========================================================================================\n");
    printf("IDX   BALL   RESOURCE         BANK     DC_LEVEL  SAW_HIGH  SAW_LOW  EDGES\n");
    printf("----------------------------------------------------------------------------------------\n");
    for (int i = 0; i < NUM_PINS; i++) {
        struct pin_telemetry t;
        read_pin_telemetry(i, &t);
        printf("%3d   %-6s %-16s %-8s    %d        %d        %d       %u\n",
               i, PINS[i].ball, PINS[i].resource, PINS[i].bank,
               t.live, t.saw_high, t.saw_low, t.edge_cnt);
    }
}

int main(int argc, char **argv)
{
    printf("\n========================================================================================\n");
    printf("\n  Behringer WING FPGA PIN Detector - %s\n", VERSION);
    printf("\n========================================================================================\n");

    const char *dev = SPI_DEV;
    int duration = 2;
    bool dump_all = false;
    bool diff_mode = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--dump") == 0 || strcmp(argv[i], "-d") == 0)
        {
            dump_all = true;
        }
        else if (strcmp(argv[i], "--diff") == 0)
        {
            diff_mode = true;
        }
        else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc)
        {
            duration = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc)
        {
            dev = argv[++i];
        }
    }

    spi_fd = open(dev, O_RDWR);
    if (spi_fd < 0)
    {
        fprintf(stderr, "[ERROR] Cannot open %s: %s\n", dev, strerror(errno));
        return 1;
    }

    uint8_t mode = 0;
    uint8_t bits = 8;
    uint32_t speed = 2000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    if (dump_all)
    {
        run_dump_all();
    }
    else if (diff_mode)
    {
        run_diff(duration);
    }
    else
    {
        run_scan(duration);
    }

    close(spi_fd);
    return 0;
}