/*
 * ============================================================================
 * wing_clock.c - Behringer WING Si5351B Audio Master Clock Generator Utility
 * ============================================================================
 *
 * Hardware Overview:
 * ------------------
 * This utility controls the Silicon Labs Si5351B I2C clock generator (IC U23)
 * on the Behringer WING console mainboard. The Si5351B synthesizes all primary
 * audio master clocks (MCLK, AES50 reference clocks, and SHARC DSP clocks)
 * from a local 25.000 MHz crystal reference.
 *
 * Architecture & Hardware Interface:
 * ----------------------------------
 * - Host SoC: NXP i.MX6 Solo (ARM Cortex-A9)
 * - Bus: i.MX6 I2C4 Hardware Controller at physical MMIO base 0x021F8000
 * - Pin Multiplexing (IOMUXC):
 *     * SCL: Ball B18 (Pad NANDF_WP_B)
 *            - MUX Register: 0x020E02A8 -> Mux Mode 9 (I2C4_SCL), SION bit set (0x19)
 *            - Pad Register: 0x020E0690 -> 0x00010808 (Open Drain, 47 kOhm Pull-up)
 *            - Input Select: 0x020E0880 -> Select Input Mode 2
 *     * SDA: Ball B17 (Pad NANDF_CS3)
 *            - MUX Register: 0x020E0280 -> Mux Mode 9 (I2C4_SDA), SION bit set (0x19)
 *            - Pad Register: 0x020E0668 -> 0x00010808 (Open Drain, 47 kOhm Pull-up)
 *            - Input Select: 0x020E0884 -> Select Input Mode 2
 * - Clock Gating (CCM):
 *     * Register CCGR1 (0x020C406C) bitfield cg4 (bits 9:8) & cg11 (bits 23:22)
 * - Target Device: Silicon Labs Si5351B
 *     * 7-bit I2C Slave Address: 0x60 (Write: 0xC0, Read: 0xC1)
 *
 * Clock Frequencies & Routing:
 * ----------------------------
 * 1. 48.000 kHz Mode (Standard Production Sampling Rate):
 *    - PLL_A VCO: 786.4320 MHz = 25.0 MHz * (31 + 457280 / 1000000)
 *    - CLK0: 24.5760 MHz (512 * Fs, MultiSynth0 = /32)  -> FPGA Audio Master Clock
 *    - CLK2: 24.5760 MHz (512 * Fs, MultiSynth2 = /32)  -> AES50 Transceiver Reference
 *    - CLK4: 12.2880 MHz (256 * Fs, MultiSynth3 = /16, R_DIV=/4 -> /64) -> ADSP-21489 SHARC DSPs
 *    - CLK5: 12.2880 MHz (256 * Fs, MultiSynth4 = /16, R_DIV=/4 -> /64) -> Secondary / Aux Audio Clock
 *
 * 2. 44.100 kHz Mode (Legacy / CD Sampling Rate):
 *    - PLL_A VCO: 722.5344 MHz = 25.0 MHz * (28 + 901376 / 1000000)
 *    - CLK0: 22.5792 MHz (512 * Fs, MultiSynth0 = /32)  -> FPGA Audio Master Clock
 *    - CLK2: 22.5792 MHz (512 * Fs, MultiSynth2 = /32)  -> AES50 Transceiver Reference
 *    - CLK4: 11.2896 MHz (256 * Fs, MultiSynth3 = /16, R_DIV=/4 -> /64) -> ADSP-21489 SHARC DSPs
 *    - CLK5: 11.2896 MHz (256 * Fs, MultiSynth4 = /16, R_DIV=/4 -> /64) -> Secondary / Aux Audio Clock
 *
 * MultiSynth Parameter Calculation (Silicon Labs AN619):
 * ------------------------------------------------------
 * The Si5351 MultiSynth fractional divider satisfies:
 *   Divider = a + (b / c)
 * Parameters programmed into registers:
 *   P1 = 128 * a + floor(128 * b / c) - 512
 *   P2 = 128 * b - c * floor(128 * b / c)
 *   P3 = c
 * ============================================================================
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

/* ============================================================================
 * i.MX6 MMIO Register Base Addresses
 * ============================================================================
 */
#define IOMUXC_BASE     0x020E0000  /* Pin Multiplexing & Pad Configuration */
#define CCM_BASE        0x020C4000  /* Clock Controller Module */
#define I2C4_BASE       0x021F8000  /* I2C Controller 4 */

/*
 * i.MX6 I2C Controller Register Offsets (16-bit registers on 32-bit alignment)
 */
#define I2C_IADR        0x00        /* I2C Address Register (Slave Mode) */
#define I2C_IFDR        0x04        /* I2C Frequency Divider Register (Baud Rate) */
#define I2C_I2CR        0x08        /* I2C Control Register */
#define I2C_I2SR        0x0C        /* I2C Status Register */
#define I2C_I2DR        0x10        /* I2C Data I/O Register */

/*
 * I2CR (I2C Control Register) Bitmasks
 */
#define I2CR_IEN        (1u << 7)   /* I2C Enable: 1 = Controller Active */
#define I2CR_IIEN       (1u << 6)   /* I2C Interrupt Enable */
#define I2CR_MSTA       (1u << 5)   /* Master/Slave Mode: 1 = Master (generates START), 0 = Slave (generates STOP) */
#define I2CR_MTX        (1u << 4)   /* Transmit/Receive Mode: 1 = Transmit, 0 = Receive */
#define I2CR_TXAK       (1u << 3)   /* Transfer Acknowledge: 1 = Send NAK (no ack), 0 = Send ACK */
#define I2CR_RSTA       (1u << 2)   /* Repeat START: 1 = Generate Repeated START condition */

/*
 * I2SR (I2C Status Register) Bitmasks
 */
#define I2SR_ICF        (1u << 7)   /* Data Transfer Complete (1 = Transfer complete) */
#define I2SR_IAAS       (1u << 6)   /* Addressed as a Slave */
#define I2SR_IBB        (1u << 5)   /* Bus Busy (1 = Bus busy with transaction) */
#define I2SR_IAL        (1u << 4)   /* Arbitration Lost */
#define I2SR_SRW        (1u << 2)   /* Slave Read/Write */
#define I2SR_IIF        (1u << 1)   /* I2C Interrupt Flag (1 = Byte transferred / event pending) */
#define I2SR_RXAK       (1u << 0)   /* Received Acknowledge: 0 = ACK received, 1 = NAK received */

/*
 * Silicon Labs Si5351B 7-bit Slave Address
 */
#define SI5351_ADDR     0x60

/* Mapped register pointers */
static volatile uint8_t *iomuxc = NULL;
static volatile uint8_t *ccm    = NULL;
static volatile uint8_t *i2c4   = NULL;

/*
 * ----------------------------------------------------------------------------
 * Helper: 32-bit & 16-bit MMIO Memory Accessors
 * ----------------------------------------------------------------------------
 */
static inline void writel_reg(volatile uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}

static inline uint32_t readl_reg(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static inline void writeh_reg(volatile uint8_t *base, uint32_t off, uint16_t val) {
    *(volatile uint16_t *)(base + off) = val;
}

static inline uint16_t readh_reg(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint16_t *)(base + off);
}

/*
 * ----------------------------------------------------------------------------
 * map_hardware()
 * Maps physical memory addresses of IOMUXC, CCM, and I2C4 via /dev/mem
 * into the process address space.
 * ----------------------------------------------------------------------------
 */
static bool map_hardware(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open(/dev/mem)");
        return false;
    }

    iomuxc = (volatile uint8_t *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, IOMUXC_BASE);
    ccm    = (volatile uint8_t *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CCM_BASE);
    i2c4   = (volatile uint8_t *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, I2C4_BASE);

    close(fd);

    if (iomuxc == MAP_FAILED || ccm == MAP_FAILED || i2c4 == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed for hardware registers.\n");
        return false;
    }

    return true;
}

/*
 * ----------------------------------------------------------------------------
 * init_i2c4_hardware()
 * Configures the SoC clock gate, pin multiplexing, and initializes the
 * i.MX6 I2C4 controller registers for 100 kHz standard mode operation.
 * ----------------------------------------------------------------------------
 */
static void init_i2c4_hardware(void) {
    /*
     * Step 1: Clock Gating
     * Enable I2C4 clock gate in CCM (CCGR1: bits 8-9 for i2c4; also ensure bits 22-23 are set)
     */
    uint32_t ccgr1 = readl_reg(ccm, 0x6C);
    writel_reg(ccm, 0x6C, ccgr1 | (3u << 8) | (3u << 22));

    /*
     * Step 2: Configure IOMUX Pin Multiplexing
     * Matches the stock Behringer Wing boot initialization:
     * - SCL: NANDF_WP_B -> MUX 0x2A8 mode 9 (SION=1), PAD 0x690 = 0x10808 (Open Drain, 47k Pull-up)
     * - SDA: NANDF_CS3  -> MUX 0x280 mode 9 (SION=1), PAD 0x668 = 0x10808 (Open Drain, 47k Pull-up)
     * Both input daisy-chain registers (0x880 and 0x884) set to Mode 2.
     */
    writel_reg(iomuxc, 0x690, 0x00010808);
    writel_reg(iomuxc, 0x2A8, 0x00000019); /* Mode 9, SION=1 */
    writel_reg(iomuxc, 0x880, 0x00000002); /* SCL SELECT_INPUT = 2 */

    writel_reg(iomuxc, 0x668, 0x00010808);
    writel_reg(iomuxc, 0x280, 0x00000019); /* Mode 9, SION=1 */
    writel_reg(iomuxc, 0x884, 0x00000002); /* SDA SELECT_INPUT = 2 */

    /*
     * Step 3: Controller Initialization & Reset
     * - Disable module and clear status register
     * - Program clock divider: IFDR = 0x15 (~100 kHz bus clock)
     * - Enable I2C controller module (IEN)
     */
    writeh_reg(i2c4, I2C_I2CR, 0x0000);  /* Disable controller */
    writeh_reg(i2c4, I2C_I2SR, 0x0000);  /* Clear interrupt/status flags */
    writeh_reg(i2c4, I2C_IFDR, 0x0015);  /* Prescaler divider (~100 kHz) */
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN); /* Enable module */
    usleep(1000);
}

/*
 * ----------------------------------------------------------------------------
 * i2c_wait_iif()
 * Polls the IIF (Interrupt Flag) in I2SR to verify that byte transmission
 * has completed on the I2C bus. Automatically clears the flag when set.
 * ----------------------------------------------------------------------------
 */
static bool i2c_wait_iif(void) {
    /* Fast bus-read polling (at 100 kHz, a byte transfer takes ~90 microseconds) */
    for (int i = 0; i < 5000; i++) {
        uint16_t sr = readh_reg(i2c4, I2C_I2SR);
        if (sr & I2SR_IIF) {
            writeh_reg(i2c4, I2C_I2SR, sr & ~I2SR_IIF); /* Clear interrupt flag */
            return true;
        }
    }
    /* Fallback with short sleeps up to 2 ms */
    for (int i = 0; i < 20; i++) {
        usleep(100);
        uint16_t sr = readh_reg(i2c4, I2C_I2SR);
        if (sr & I2SR_IIF) {
            writeh_reg(i2c4, I2C_I2SR, sr & ~I2SR_IIF);
            return true;
        }
    }
    return false;
}

/*
 * ----------------------------------------------------------------------------
 * i2c_stop()
 * Generates an I2C STOP condition on the bus by switching from Master to Slave.
 * ----------------------------------------------------------------------------
 */
static void i2c_stop(void) {
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN); /* Clearing MSTA generates STOP */
    usleep(200);
}

/*
 * ----------------------------------------------------------------------------
 * i2c_write_byte()
 * Transmits a single byte to an 8-bit register on the target I2C device.
 * Sequence: START -> [SlaveAddr + W] -> [Register] -> [Data] -> STOP
 * ----------------------------------------------------------------------------
 */
static bool i2c_write_byte(uint8_t slave_addr, uint8_t reg, uint8_t data) {
    writeh_reg(i2c4, I2C_I2SR, 0x0000);
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN | I2CR_MSTA | I2CR_MTX);

    /* 1. Send 7-bit Slave Address with Write bit (0) */
    writeh_reg(i2c4, I2C_I2DR, (slave_addr << 1) | 0);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 2. Send Target Register Address */
    writeh_reg(i2c4, I2C_I2DR, reg);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 3. Send Data Payload Byte */
    writeh_reg(i2c4, I2C_I2DR, data);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    i2c_stop();
    return true;
}

/*
 * ----------------------------------------------------------------------------
 * i2c_write_burst()
 * Writes multiple consecutive bytes starting at `start_reg` via auto-increment.
 * Sequence: START -> [SlaveAddr + W] -> [StartReg] -> [Data0]..[DataN] -> STOP
 * ----------------------------------------------------------------------------
 */
static bool i2c_write_burst(uint8_t slave_addr, uint8_t start_reg, const uint8_t *data, size_t len) {
    writeh_reg(i2c4, I2C_I2SR, 0x0000);
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN | I2CR_MSTA | I2CR_MTX);

    /* 1. Send Slave Address (Write) */
    writeh_reg(i2c4, I2C_I2DR, (slave_addr << 1) | 0);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 2. Send Starting Register Address */
    writeh_reg(i2c4, I2C_I2DR, start_reg);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 3. Stream Data Bytes sequentially */
    for (size_t i = 0; i < len; i++) {
        writeh_reg(i2c4, I2C_I2DR, data[i]);
        if (!i2c_wait_iif()) { i2c_stop(); return false; }
        if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }
    }

    i2c_stop();
    return true;
}

/*
 * ----------------------------------------------------------------------------
 * i2c_read_byte()
 * Reads a single byte from a specified register on the target I2C device.
 * Sequence: START -> [SlaveAddr + W] -> [Register] -> Repeated START ->
 *           [SlaveAddr + R] -> Switch to RX (NAK) -> Dummy Read -> Read Byte -> STOP
 * ----------------------------------------------------------------------------
 */
static bool i2c_read_byte(uint8_t slave_addr, uint8_t reg, uint8_t *val) {
    writeh_reg(i2c4, I2C_I2SR, 0x0000);
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN | I2CR_MSTA | I2CR_MTX);

    /* 1. Send Slave Address (Write mode to select register) */
    writeh_reg(i2c4, I2C_I2DR, (slave_addr << 1) | 0);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 2. Send Register Address */
    writeh_reg(i2c4, I2C_I2DR, reg);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 3. Issue Repeated START condition with Read bit (1) */
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN | I2CR_MSTA | I2CR_MTX | I2CR_RSTA);
    writeh_reg(i2c4, I2C_I2DR, (slave_addr << 1) | 1);
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    if (readh_reg(i2c4, I2C_I2SR) & I2SR_RXAK) { i2c_stop(); return false; }

    /* 4. Switch to Receive Mode and assert TXAK (NAK) since only 1 byte is expected */
    writeh_reg(i2c4, I2C_I2CR, I2CR_IEN | I2CR_MSTA | I2CR_TXAK);
    (void)readh_reg(i2c4, I2C_I2DR); /* Dummy read triggers bus clocking */

    /* 5. Wait for byte reception */
    if (!i2c_wait_iif()) { i2c_stop(); return false; }
    i2c_stop();

    /* 6. Retrieve data byte from register */
    *val = (uint8_t)readh_reg(i2c4, I2C_I2DR);
    return true;
}

/*
 * ----------------------------------------------------------------------------
 * pack_ms_params()
 * Encodes Silicon Labs MultiSynth divider parameters (a + b/c) into the
 * 8-byte register structure required by Si5351B PLLs and MultiSynths.
 *
 * Parameters:
 *   a     : Integer component of divider
 *   b     : Fractional numerator
 *   c     : Fractional denominator
 *   r_div : Additional output R divider (0 = /1, 1 = /2, 2 = /4, 4 = /16, etc.)
 *   out   : 8-byte output buffer
 *
 * Silicon Labs Register Format (AN619 Section 3.2):
 *   Reg 0: P3[15:8]
 *   Reg 1: P3[7:0]
 *   Reg 2: R_DIV[2:0] in bits [6:4], P1[17:16] in bits [1:0]
 *   Reg 3: P1[15:8]
 *   Reg 4: P1[7:0]
 *   Reg 5: P3[19:16] in bits [7:4], P2[19:16] in bits [3:0]
 *   Reg 6: P2[15:8]
 *   Reg 7: P2[7:0]
 * ----------------------------------------------------------------------------
 */
static void pack_ms_params(uint32_t a, uint32_t b, uint32_t c, uint8_t r_div, uint8_t *out) {
    uint32_t p1, p2, p3;
    if (b == 0) {
        /* Integer division mode */
        p1 = 128 * a - 512;
        p2 = 0;
        p3 = 1;
    } else {
        /* Fractional division mode */
        uint64_t b128 = (uint64_t)b * 128ULL;
        uint32_t div = (uint32_t)(b128 / c);
        p1 = 128 * a + div - 512;
        p2 = (uint32_t)(b128 - ((uint64_t)c * div));
        p3 = c;
    }

    out[0] = (uint8_t)((p3 >> 8) & 0xFF);
    out[1] = (uint8_t)(p3 & 0xFF);
    out[2] = (uint8_t)(((r_div & 0x07) << 4) | ((p1 >> 16) & 0x03));
    out[3] = (uint8_t)((p1 >> 8) & 0xFF);
    out[4] = (uint8_t)(p1 & 0xFF);
    out[5] = (uint8_t)((((p3 >> 16) & 0x0F) << 4) | ((p2 >> 16) & 0x0F));
    out[6] = (uint8_t)((p2 >> 8) & 0xFF);
    out[7] = (uint8_t)(p2 & 0xFF);
}

/*
 * ----------------------------------------------------------------------------
 * set_audio_rate()
 * Reconfigures PLL_A, MultiSynth dividers, and output drivers to synthesize
 * all audio master clocks for either 48.0 kHz or 44.1 kHz sample rate.
 * ----------------------------------------------------------------------------
 */
static bool set_audio_rate(uint32_t rate_hz) {
    bool is_48k = (rate_hz == 48000 || rate_hz == 48);
    bool is_44k = (rate_hz == 44100 || rate_hz == 44);

    if (!is_48k && !is_44k) {
        fprintf(stderr, "[-] Unsupported sample rate: %u Hz (Supported: 48000 / 48k, 44100 / 44.1k)\n", rate_hz);
        return false;
    }

    printf("=================================================================\n");
    printf("     BEHRINGER WING — Si5351B AUDIO CLOCK GENERATOR\n");
    printf("     Setting Sample Rate: %s Hz\n", is_48k ? "48000 (48 kHz)" : "44100 (44.1 kHz)");
    printf("=================================================================\n");

    /*
     * Step 1: Temporarily disable clock outputs (Reg 3 = 0xFF)
     * Tri-stating outputs avoids spurious glitch clocks during PLL lock.
     */
    printf("[1/6] Tri-stating clock outputs during PLL reconfiguration (Reg 3 = 0xFF)...\n");
    if (!i2c_write_byte(SI5351_ADDR, 3, 0xFF)) {
        fprintf(stderr, "[-] I2C write failed at Reg 3.\n");
        return false;
    }

    /*
     * Step 2: Ensure crystal load capacitance = 10 pF and fanout enable
     * Reg 183 = 0x92 (XTAL_CL = 10 pF, matches console crystal)
     * Reg 187 = 0xD0 (Fanout Enable)
     */
    i2c_write_byte(SI5351_ADDR, 183, 0x92);
    i2c_write_byte(SI5351_ADDR, 187, 0xD0);

    /*
     * Step 3: Program PLL_A Multiplier (Reg 0x22..0x29, 8 bytes)
     * 48.0 kHz:
     *   Target F_pll = 786.4320 MHz = 25 MHz * (31 + 457280 / 1000000)
     * 44.1 kHz:
     *   Target F_pll = 722.5344 MHz = 25 MHz * (28 + 901376 / 1000000)
     */
    uint8_t pll_params[8];
    if (is_48k) {
        printf("[2/6] Programming PLL-A for 786.432 MHz (Mult: 31 + 457280/1000000)...\n");
        pack_ms_params(31, 457280, 1000000, 0, pll_params);
    } else {
        printf("[2/6] Programming PLL-A for 722.5344 MHz (Mult: 28 + 901376/1000000)...\n");
        pack_ms_params(28, 901376, 1000000, 0, pll_params);
    }
    if (!i2c_write_burst(SI5351_ADDR, 0x22, pll_params, 8)) {
        fprintf(stderr, "[-] I2C write failed at PLL_A (Reg 0x22).\n");
        return false;
    }

    /*
     * Step 4: Program MultiSynth Dividers
     * - MS0 (CLK0 - Audio MCLK): div 32 -> 24.5760 MHz (48k) / 22.5792 MHz (44.1k)
     * - MS2 (CLK2 - AES50 Ref):  div 32 -> 24.5760 MHz (48k) / 22.5792 MHz (44.1k)
     * - MS3 (CLK4 - DSP MCLK):   div 16 with R_DIV=4 (total /64) -> 12.2880 MHz (48k) / 11.2896 MHz (44.1k)
     * - MS4 (CLK5 - Aux Clock):  div 16 with R_DIV=4 (total /64) -> 12.2880 MHz (48k) / 11.2896 MHz (44.1k)
     */
    printf("[3/6] Programming MultiSynth dividers (MS0/MS2: /32, MS3/MS4: /64)...\n");
    uint8_t ms_div32[8];
    pack_ms_params(32, 0, 1, 0, ms_div32);
    i2c_write_burst(SI5351_ADDR, 0x2A, ms_div32, 8); /* MS0 (Reg 0x2A) */
    i2c_write_burst(SI5351_ADDR, 0x3A, ms_div32, 8); /* MS2 (Reg 0x3A) */

    uint8_t ms_div16[8];
    pack_ms_params(16, 0, 1, 0, ms_div16);
    i2c_write_burst(SI5351_ADDR, 0x4A, ms_div16, 8); /* MS3 (Reg 0x4A) */
    i2c_write_burst(SI5351_ADDR, 0x52, ms_div16, 8); /* MS4 (Reg 0x52) */

    /*
     * Step 5: Configure Output Controls & Drive Strength
     * - Reg 16 (CLK0): 0x4F -> Powered on, integer mode, MS0 source, 8 mA drive
     * - Reg 18 (CLK2): 0x4F -> Powered on, integer mode, MS2 source, 8 mA drive
     * - Reg 20 (CLK4): 0x6F -> Powered on, integer mode, MS3 source, R_DIV=4 (/64), 8 mA drive
     * - Reg 21 (CLK5): 0x6F -> Powered on, integer mode, MS4 source, R_DIV=4 (/64), 8 mA drive
     */
    printf("[4/6] Configuring clock output drivers (8 mA drive)...\n");
    i2c_write_byte(SI5351_ADDR, 16, 0x4F);
    i2c_write_byte(SI5351_ADDR, 18, 0x4F);
    i2c_write_byte(SI5351_ADDR, 20, 0x6F);
    i2c_write_byte(SI5351_ADDR, 21, 0x6F);

    /*
     * Step 6: Soft-Reset PLLs and Re-enable Clock Outputs
     * Reg 177 = 0xAC (resets both PLL-A and PLL-B state machines)
     */
    printf("[5/6] Resetting PLL-A & PLL-B (Reg 177 = 0xAC)...\n");
    i2c_write_byte(SI5351_ADDR, 177, 0xAC);
    usleep(10000); /* 10 ms wait for PLL lock */

    /* Un-mute active clock outputs: Reg 3 = 0xCA (Enables CLK0, CLK2, CLK4, CLK5) */
    printf("[6/6] Un-muting active clock outputs (Reg 3 = 0xCA)...\n");
    i2c_write_byte(SI5351_ADDR, 3, 0xCA);

    /* Read device status (Reg 0) to verify PLL lock */
    uint8_t status = 0;
    i2c_read_byte(SI5351_ADDR, 0, &status);

    printf("\n[+] SUCCESS! %s Clock Configuration Active!\n", is_48k ? "48 kHz" : "44.1 kHz");
    printf("    ---------------------------------------------------------\n");
    printf("    Output Pin | Function             | Frequency\n");
    printf("    -----------+----------------------+----------------------\n");
    if (is_48k) {
        printf("    CLK0       | Audio MCLK (FPGA)    | 24.5760 MHz (512 * Fs)\n");
        printf("    CLK2       | AES50 Transceiver Ref| 24.5760 MHz (512 * Fs)\n");
        printf("    CLK4       | SHARC DSP Master MCLK| 12.2880 MHz (256 * Fs)\n");
        printf("    CLK5       | Secondary Audio Clock| 12.2880 MHz (256 * Fs)\n");
    } else {
        printf("    CLK0       | Audio MCLK (FPGA)    | 22.5792 MHz (512 * Fs)\n");
        printf("    CLK2       | AES50 Transceiver Ref| 22.5792 MHz (512 * Fs)\n");
        printf("    CLK4       | SHARC DSP Master MCLK| 11.2896 MHz (256 * Fs)\n");
        printf("    CLK5       | Secondary Audio Clock| 11.2896 MHz (256 * Fs)\n");
    }
    printf("    ---------------------------------------------------------\n");
    printf("    Status: 0x%02X (LOL_A=%d, LOL_B=%d, LOS=%d)\n\n",
           status, (status >> 5) & 1, (status >> 6) & 1, (status >> 4) & 1);

    return true;
}

/*
 * ----------------------------------------------------------------------------
 * cmd_detect()
 * Probes the I2C4 bus across 7-bit addresses (0x03..0x77) to detect responsive
 * hardware devices (such as Si5351B at 0x60).
 * ----------------------------------------------------------------------------
 */
static void cmd_detect(void) {
    printf("[*] Scanning I2C4 bus (0x01..0x7F)...\n");
    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        /* Send START and address byte (write) */
        writeh_reg(i2c4, I2C_I2SR, 0x0000);
        writeh_reg(i2c4, I2C_I2CR, I2CR_IEN | I2CR_MSTA | I2CR_MTX);
        writeh_reg(i2c4, I2C_I2DR, (addr << 1) | 0);
        if (i2c_wait_iif()) {
            uint16_t sr = readh_reg(i2c4, I2C_I2SR);
            if (!(sr & I2SR_RXAK)) {
                /* ACK received! Device present */
                printf("    [+] Found active I2C device at 0x%02X!\n", addr);
                found++;
            }
        }
        i2c_stop();
        usleep(500);
    }
    if (found == 0) {
        printf("    [-] No I2C devices responded on I2C4 (pins NANDF_WP_B / NANDF_CS3).\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 * cmd_status()
 * Reads Si5351B status register (Reg 0), output enable mask (Reg 3),
 * and parses PLL_A parameters to display live lock state and frequency rate.
 * ----------------------------------------------------------------------------
 */
static void cmd_status(void) {
    uint8_t status = 0, oeb = 0;
    if (!i2c_read_byte(SI5351_ADDR, 0, &status) || !i2c_read_byte(SI5351_ADDR, 3, &oeb)) {
        fprintf(stderr, "[-] Failed to read Si5351B status.\n");
        return;
    }

    /* Read PLL_A parameters from Reg 0x22..0x29 */
    uint8_t p_raw[8];
    for (int i = 0; i < 8; i++) {
        i2c_read_byte(SI5351_ADDR, 0x22 + i, &p_raw[i]);
    }

    /* Reconstruct P1, P2, P3 according to AN619 */
    uint32_t p1 = ((uint32_t)(p_raw[2] & 0x03) << 16) | ((uint32_t)p_raw[3] << 8) | p_raw[4];
    uint32_t p2 = ((uint32_t)(p_raw[5] & 0x0F) << 16) | ((uint32_t)p_raw[6] << 8) | p_raw[7];
    uint32_t p3 = ((uint32_t)(p_raw[5] & 0xF0) << 12) | ((uint32_t)p_raw[0] << 8) | p_raw[1];

    printf("=================================================================\n");
    printf("         BEHRINGER WING — Si5351B HARDWARE STATUS\n");
    printf("=================================================================\n");
    printf("Device Address : 0x%02X\n", SI5351_ADDR);
    printf("Device Rev     : 0x%02X\n", status & 0x03);
    printf("PLL-A Lock     : %s\n", (status & (1 << 5)) ? "UNLOCKED / LOSS" : "LOCKED (OK)");
    printf("PLL-B Lock     : %s\n", (status & (1 << 6)) ? "UNLOCKED / LOSS" : "LOCKED (OK)");
    printf("Outputs Mask   : 0x%02X (0=Active, 1=Disabled)\n", oeb);
    printf("P1=%u, P2=%u, P3=%u\n", p1, p2, p3);

    /* P1 parameter identifies the active sampling rate */
    if (p1 == 3514) {
        printf("Current Rate   : 48.0 kHz (Audio MCLK: 24.576 MHz / 12.288 MHz)\n");
    } else if (p1 == 3187) {
        printf("Current Rate   : 44.1 kHz (Audio MCLK: 22.5792 MHz / 11.2896 MHz)\n");
    } else {
        printf("Current Rate   : Custom / Unprogrammed (P1=%u)\n", p1);
    }
    printf("=================================================================\n");
}

/*
 * ----------------------------------------------------------------------------
 * cmd_dump()
 * Dumps core registers of the Si5351B clock generator with human-readable
 * descriptions.
 * ----------------------------------------------------------------------------
 */
static void cmd_dump(void) {
    printf("=================================================================\n");
    printf("            Si5351B CLOCK GENERATOR REGISTER DUMP\n");
    printf("=================================================================\n");
    printf("REG   VALUE   DESCRIPTION\n");
    printf("-----------------------------------------------------------------\n");

    uint8_t val = 0;
    for (int r = 0; r <= 25; r++) {
        if (i2c_read_byte(SI5351_ADDR, r, &val)) {
            const char *desc = "";
            if (r == 0) desc = "Device Status / Rev";
            else if (r == 1) desc = "Interrupt Status";
            else if (r == 3) desc = "Output Enable Control (OEB)";
            else if (r >= 16 && r <= 23) desc = "CLKx Control Register";
            printf("0x%02X  0x%02X    %s\n", r, val, desc);
        }
    }

    for (int r = 177; r <= 187; r++) {
        if (i2c_read_byte(SI5351_ADDR, r, &val)) {
            const char *desc = "";
            if (r == 177) desc = "PLL Soft Reset";
            else if (r == 183) desc = "Crystal Load Capacitance";
            else if (r == 187) desc = "Fanout Enable";
            printf("0x%02X  0x%02X    %s\n", r, val, desc);
        }
    }
}

/*
 * ----------------------------------------------------------------------------
 * cmd_dump_dev()
 * Formats a 64-byte hex register matrix from any given I2C device address.
 * ----------------------------------------------------------------------------
 */
static void cmd_dump_dev(uint8_t addr) {
    printf("=================================================================\n");
    printf("         BEHRINGER WING — I2C DEVICE 0x%02X REGISTER DUMP\n", addr);
    printf("=================================================================\n");
    printf("     00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F\n");
    for (int row = 0; row < 4; row++) {
        printf("%02X: ", row * 16);
        for (int col = 0; col < 16; col++) {
            uint8_t r = row * 16 + col;
            uint8_t val = 0;
            if (i2c_read_byte(addr, r, &val)) {
                printf("%02X ", val);
            } else {
                printf("-- ");
            }
            if (col == 7) printf(" ");
        }
        printf("\n");
    }
    printf("=================================================================\n");
}

/*
 * ----------------------------------------------------------------------------
 * cmd_stop()
 * Tri-states all clock outputs (safe power-down mode).
 * ----------------------------------------------------------------------------
 */
static void cmd_stop(void) {
    printf("[*] Shutting down all Si5351B clock outputs (safe power-down)...\n");
    if (i2c_write_byte(SI5351_ADDR, 3, 0xFF)) {
        printf("[+] All clock outputs tri-stated/disabled (Reg 3 = 0xFF).\n");
    } else {
        fprintf(stderr, "[-] I2C write failed.\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 * print_usage()
 * Displays command-line argument help.
 * ----------------------------------------------------------------------------
 */
static void print_usage(const char *prog) {
    printf("Behringer Wing Si5351B Clock Controller\n");
    printf("Usage: %s [command]\n\n", prog);
    printf("Commands:\n");
    printf("  --set <rate>    Set sample rate: 48000, 48k, 44100, 44.1k\n");
    printf("  --status        Show current sample rate and PLL lock status\n");
    printf("  --detect        Probe I2C4 bus for Si5351B (0x60)\n");
    printf("  --dump          Dump Si5351B internal register map\n");
    printf("  --stop          Tri-state and disable all clock outputs\n\n");
    printf("Examples:\n");
    printf("  %s --set 48k    # Activate 48 kHz master clocks (24.576 MHz)\n", prog);
    printf("  %s --set 44.1k  # Activate 44.1 kHz master clocks (22.5792 MHz)\n", prog);
    printf("  %s --status     # Check current clock frequency\n", prog);
}

/*
 * ----------------------------------------------------------------------------
 * main()
 * Entry point and command dispatcher.
 * ----------------------------------------------------------------------------
 */
int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    /* Map physical hardware memory pages */
    if (!map_hardware()) {
        return 1;
    }

    /* Configure IOMUX and I2C4 registers */
    init_i2c4_hardware();

    if (strcmp(argv[1], "--set") == 0 || strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--rate") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[-] Missing rate argument. Use: 48k, 48000, 44.1k, 44100\n");
            return 1;
        }
        const char *r = argv[2];
        uint32_t rate = 0;
        if (strcasecmp(r, "48k") == 0 || strcasecmp(r, "48khz") == 0 || strcmp(r, "48000") == 0 || strcmp(r, "48") == 0) {
            rate = 48000;
        } else if (strcasecmp(r, "44.1k") == 0 || strcasecmp(r, "44.1khz") == 0 || strcasecmp(r, "44k") == 0 || strcmp(r, "44100") == 0 || strcmp(r, "44") == 0) {
            rate = 44100;
        } else {
            rate = (uint32_t)atoi(r);
        }
        return set_audio_rate(rate) ? 0 : 1;
    } else if (strcmp(argv[1], "--init") == 0) {
        /* Default initialization to 48 kHz */
        return set_audio_rate(48000) ? 0 : 1;
    } else if (strcmp(argv[1], "--status") == 0) {
        cmd_status();
    } else if (strcmp(argv[1], "--detect") == 0) {
        cmd_detect();
    } else if (strcmp(argv[1], "--dump") == 0) {
        cmd_dump();
    } else if (strcmp(argv[1], "--dump-dev") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[-] Missing address argument (e.g. 0x68, 0x6a, 0x6b)\n");
            return 1;
        }
        uint8_t a = (uint8_t)strtoul(argv[2], NULL, 0);
        cmd_dump_dev(a);
    } else if (strcmp(argv[1], "--stop") == 0) {
        cmd_stop();
    } else {
        print_usage(argv[0]);
    }

    return 0;
}
