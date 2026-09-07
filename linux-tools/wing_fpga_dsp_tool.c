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

/* Standard-Standardwerte für Linux spidev und Datentransfer */
#define DEFAULT_SPI_DEVICE_PATH  "/dev/spidev1.0"
#define DEFAULT_SPI_SPEED_HZ     2000000 // 2 MHz Standard-Takt
#define FIFO_CHUNK_SIZE          1024    // Puffergröße für das Senden in Häppchen

/* i.MX6 MMIO Register Basisadressen */
#define CCM_BASE_ADDRESS         0x020C4000 // Clock Controller Module
#define CCM_CCGR1_OFFSET         0x6C       // Clock Gating Register 1
#define IOMUXC_BASE_ADDRESS      0x020E0000 // IO Multiplexer Controller
#define GPIO2_BASE_ADDRESS       0x020A0000 // General Purpose IO Bank 2
#define GPIO3_BASE_ADDRESS       0x020A4000 // General Purpose IO Bank 3
#define GPIO4_BASE_ADDRESS       0x020A8000 // General Purpose IO Bank 4
#define GPIO5_BASE_ADDRESS       0x020AC000 // General Purpose IO Bank 5
#define ECSPI2_BASE_ADDRESS      0x0200C000 // Enhanced CSPI Interface 2

/* Relative Offsets für i.MX6 GPIO Controller Register */
#define GPIO_DR_OFFSET           0x00 // Data Register (Ausgabe / Pegel lesen)
#define GPIO_GDIR_OFFSET         0x04 // Direction Register (0 = Input, 1 = Output)
#define GPIO_PSR_OFFSET          0x08 // Pad Status Register (Eingangspegel lesen)

/* ECSPI Register Offsets relative zur Base-Adresse */
#define ECSPI_RXDATA_OFFSET      0x00 // Empfangsdaten-Register
#define ECSPI_TXDATA_OFFSET      0x04 // Sendedaten-Register
#define ECSPI_CONREG_OFFSET      0x08 // Steuerungsregister (Control Register)
#define ECSPI_CONFIGREG_OFFSET   0x0C // Konfigurationsregister
#define ECSPI_INTREG_OFFSET      0x10 // Interrupt-Steuerregister
#define ECSPI_STATREG_OFFSET     0x18 // Statusregister
#define ECSPI_PERIODREG_OFFSET   0x1C // Sample Period / Waveform Steuerungsregister

/* Bitmasken für ECSPI Status & Steuerung */
#define ECSPI_XCH_START_BIT      (1u << 2) // Startet den Datenaustausch
#define ECSPI_TC_COMPLETE_BIT    (1u << 7) // Transfer Complete Flag

/**
 * @brief Hardware-Kontextstruktur zur Speicherung von File-Deskriptoren
 * und Mapped Memory (MMIO) Adressen.
 */
typedef struct {
    int memory_file_descriptor;    // File-Deskriptor für /dev/mem
    int spi_file_descriptor;       // File-Deskriptor für Linux spidev Fallback
    bool is_using_mmio;            // Status-Flag: direktes MMIO aktiv?
    uint8_t *iomuxc_base_ptr;      // Virtual Base Pointer für IOMUXC
    uint8_t *gpio2_base_ptr;       // Virtual Base Pointer für GPIO Bank 2
    uint8_t *gpio3_base_ptr;       // Virtual Base Pointer für GPIO Bank 3
    uint8_t *gpio4_base_ptr;       // Virtual Base Pointer für GPIO Bank 4
    uint8_t *gpio5_base_ptr;       // Virtual Base Pointer für GPIO Bank 5
    uint8_t *ecspi2_base_ptr;      // Virtual Base Pointer für ECSPI2
} wing_hardware_context_t;

/**
 * @brief Schreibt ein 32-Bit Word in ein MMIO Register.
 */
static inline void mmio_write_32(uint8_t *base_ptr, uint32_t register_offset, uint32_t value) {
    *(volatile uint32_t *)(base_ptr + register_offset) = value;
}

/**
 * @brief Liest ein 32-Bit Word aus einem MMIO Register.
 */
static inline uint32_t mmio_read_32(uint8_t *base_ptr, uint32_t register_offset) {
    return *(volatile uint32_t *)(base_ptr + register_offset);
}

/**
 * @brief Hilfsfunktion zur Ermittlung des Mmapped-Speicherzeigers für eine GPIO Bank.
 */
static uint8_t* get_gpio_bank_ptr(wing_hardware_context_t *hw_context, uint8_t gpio_bank) {
    switch (gpio_bank) {
        case 2: return hw_context->gpio2_base_ptr;
        case 3: return hw_context->gpio3_base_ptr;
        case 4: return hw_context->gpio4_base_ptr;
        case 5: return hw_context->gpio5_base_ptr;
        default: return NULL;
    }
}

/**
 * @brief Setzt den Pegel eines bestimmten GPIO Pins (Konfiguriert den Pin automatisch als Ausgang).
 * 
 * @param hw_context Zeiger auf den Hardware-Kontext
 * @param gpio_bank Nummer der GPIO Bank (2, 3, 4 oder 5)
 * @param gpio_pin Pin-Nummer innerhalb der Bank (0..31)
 * @param value Zielpegel (true = High, false = Low)
 * @return int 0 bei Erfolg, -1 bei Fehler (z.B. falsche Bank oder kein MMIO)
 */
int hardware_gpio_set_pin(wing_hardware_context_t *hw_context, uint8_t gpio_bank, uint8_t gpio_pin, bool value) {
    if (!hw_context->is_using_mmio || gpio_pin > 31) return -1;
    
    uint8_t *bank_ptr = get_gpio_bank_ptr(hw_context, gpio_bank);
    if (!bank_ptr || bank_ptr == MAP_FAILED) return -1;

    // Output-Wert im Data Register (DR) setzen
    uint32_t dr = mmio_read_32(bank_ptr, GPIO_DR_OFFSET);
    if (value) {
        dr |= (1u << gpio_pin);
    } else {
        dr &= ~(1u << gpio_pin);
    }
    mmio_write_32(bank_ptr, GPIO_DR_OFFSET, dr);

    return 0;
}

/**
 * @brief Liest den logischen Pegel eines GPIO Pins ein.
 * 
 * @param hw_context Zeiger auf den Hardware-Kontext
 * @param gpio_bank Nummer der GPIO Bank (2, 3, 4 oder 5)
 * @param gpio_pin Pin-Nummer innerhalb der Bank (0..31)
 * @return int Logischer Pegel (0 oder 1) oder -1 bei Fehler
 */
int hardware_gpio_get_pin(wing_hardware_context_t *hw_context, uint8_t gpio_bank, uint8_t gpio_pin) {
    if (!hw_context->is_using_mmio || gpio_pin > 31) return -1;

    uint8_t *bank_ptr = get_gpio_bank_ptr(hw_context, gpio_bank);
    if (!bank_ptr || bank_ptr == MAP_FAILED) return -1;

    // Aktuellen Pegel aus dem Pad Status Register (PSR) auslesen
    uint32_t psr = mmio_read_32(bank_ptr, GPIO_PSR_OFFSET);
    return (psr & (1u << gpio_pin)) ? 1 : 0;
}

/**
 * @brief Initialisiert die i.MX6 MMIO-Register für direkten Hardware-Zugriff via /dev/mem.
 */
static int hardware_init_mmio(wing_hardware_context_t *hw_context) {
    hw_context->memory_file_descriptor = open("/dev/mem", O_RDWR | O_SYNC);
    if (hw_context->memory_file_descriptor < 0) {
        return -1;
    }

    uint8_t *ccm_base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, 
                             hw_context->memory_file_descriptor, CCM_BASE_ADDRESS);
    if (ccm_base != MAP_FAILED) {
        uint32_t clock_gate_val = mmio_read_32(ccm_base, CCM_CCGR1_OFFSET);
        clock_gate_val |= 0x0000000Cu; // ECSPI2 Clock freischalten
        mmio_write_32(ccm_base, CCM_CCGR1_OFFSET, clock_gate_val);
        munmap(ccm_base, 0x1000);
    }

    hw_context->iomuxc_base_ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, hw_context->memory_file_descriptor, IOMUXC_BASE_ADDRESS);
    hw_context->gpio2_base_ptr  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, hw_context->memory_file_descriptor, GPIO2_BASE_ADDRESS);
    hw_context->gpio3_base_ptr  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, hw_context->memory_file_descriptor, GPIO3_BASE_ADDRESS);
    hw_context->gpio4_base_ptr  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, hw_context->memory_file_descriptor, GPIO4_BASE_ADDRESS);
    hw_context->gpio5_base_ptr  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, hw_context->memory_file_descriptor, GPIO5_BASE_ADDRESS);
    hw_context->ecspi2_base_ptr = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, hw_context->memory_file_descriptor, ECSPI2_BASE_ADDRESS);

    if (hw_context->iomuxc_base_ptr == MAP_FAILED || hw_context->ecspi2_base_ptr == MAP_FAILED) {
        return -1;
    }

    typedef struct {
        uint32_t register_offset;
        uint32_t register_value;
    } pad_config_t;

    static const pad_config_t pad_configurations[] = {
        { 0x05ac, 0x0001b0b0 }, { 0x01dc, 0x00000005 }, // GPIO2_26
        { 0x05a4, 0x0001b0b0 }, { 0x01d4, 0x00000005 }, // GPIO2_27
        { 0x0534, 0x0001b0b0 }, { 0x0164, 0x00000005 }, // GPIO3_24
        { 0x0538, 0x0001b0b0 }, { 0x0168, 0x00000005 }, // GPIO3_25
        { 0x050c, 0x0001b0b0 }, { 0x013c, 0x00000002 }, { 0x07f4, 0x00000002 }, // SPI2_SCLK
        { 0x05a8, 0x0001b0b0 }, { 0x01d8, 0x00000002 }, { 0x07f8, 0x00000002 }, // SPI2_MISO
        { 0x0510, 0x0001b0b0 }, { 0x0140, 0x00000002 }, { 0x07fc, 0x00000002 }, // SPI2_MOSI
        { 0x04f4, 0x0001b0b0 }, { 0x0124, 0x00000005 }, /* CRESET_N: GPIO2_17 */
        { 0x0410, 0x0001b008 }, { 0x00fc, 0x00000015 }, /* CDONE: GPIO4_26 mit SION bit */
        { 0x04f0, 0x00013008 }, { 0x0120, 0x00000005 }, /* FPGA STATUS: GPIO2_18 (Pull-Down) */
    };

    size_t pad_count = sizeof(pad_configurations) / sizeof(pad_configurations[0]);
    for (size_t i = 0; i < pad_count; i++) {
        mmio_write_32(hw_context->iomuxc_base_ptr, pad_configurations[i].register_offset, pad_configurations[i].register_value);
    }

    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_CONREG_OFFSET, 0);
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_CONREG_OFFSET, 0x00000011u | (7u << 20));
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_CONFIGREG_OFFSET, 0x00000100u);
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_PERIODREG_OFFSET, 0);

    hw_context->is_using_mmio = true;
    return 0;
}

static void hardware_close(wing_hardware_context_t *hw_context) {
    if (hw_context->is_using_mmio) {
        if (hw_context->ecspi2_base_ptr && hw_context->ecspi2_base_ptr != MAP_FAILED) munmap(hw_context->ecspi2_base_ptr, 0x4000);
        if (hw_context->gpio5_base_ptr  && hw_context->gpio5_base_ptr  != MAP_FAILED) munmap(hw_context->gpio5_base_ptr, 0x1000);
        if (hw_context->gpio4_base_ptr  && hw_context->gpio4_base_ptr  != MAP_FAILED) munmap(hw_context->gpio4_base_ptr, 0x1000);
        if (hw_context->gpio3_base_ptr  && hw_context->gpio3_base_ptr  != MAP_FAILED) munmap(hw_context->gpio3_base_ptr, 0x1000);
        if (hw_context->gpio2_base_ptr  && hw_context->gpio2_base_ptr  != MAP_FAILED) munmap(hw_context->gpio2_base_ptr, 0x1000);
        if (hw_context->iomuxc_base_ptr && hw_context->iomuxc_base_ptr != MAP_FAILED) munmap(hw_context->iomuxc_base_ptr, 0x1000);
        if (hw_context->memory_file_descriptor >= 0) close(hw_context->memory_file_descriptor);
    }
    if (hw_context->spi_file_descriptor >= 0) close(hw_context->spi_file_descriptor);
}

static uint8_t ecspi2_transfer_single_byte(wing_hardware_context_t *hw_context, uint8_t transmit_byte) {
    uint32_t control_reg = mmio_read_32(hw_context->ecspi2_base_ptr, ECSPI_CONREG_OFFSET);
    
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_STATREG_OFFSET, ECSPI_TC_COMPLETE_BIT);
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_TXDATA_OFFSET, transmit_byte);
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_INTREG_OFFSET, ECSPI_TC_COMPLETE_BIT);
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_CONREG_OFFSET, control_reg | ECSPI_XCH_START_BIT);

    int timeout_counter = 10000;
    while ((mmio_read_32(hw_context->ecspi2_base_ptr, ECSPI_STATREG_OFFSET) & ECSPI_TC_COMPLETE_BIT) == 0 && --timeout_counter > 0) {
        usleep(1);
    }
    
    mmio_write_32(hw_context->ecspi2_base_ptr, ECSPI_INTREG_OFFSET, 0);
    return (uint8_t)(mmio_read_32(hw_context->ecspi2_base_ptr, ECSPI_RXDATA_OFFSET) & 0xFF);
}

static int hardware_spi_transfer(wing_hardware_context_t *hw_context, 
                                 const uint8_t *tx_buffer, 
                                 uint8_t *rx_buffer, 
                                 size_t transfer_length, 
                                 uint32_t speed_hz) {
    if (hw_context->is_using_mmio) {
        for (size_t index = 0; index < transfer_length; index++) {
            uint8_t tx_byte = tx_buffer ? tx_buffer[index] : 0x00;
            uint8_t rx_byte = ecspi2_transfer_single_byte(hw_context, tx_byte);
            if (rx_buffer) rx_buffer[index] = rx_byte;
        }
        return 0;
    } else {
        struct spi_ioc_transfer spi_transfer;
        memset(&spi_transfer, 0, sizeof(spi_transfer));
        spi_transfer.tx_buf = (unsigned long)tx_buffer;
        spi_transfer.rx_buf = (unsigned long)rx_buffer;
        spi_transfer.len = transfer_length;
        spi_transfer.speed_hz = speed_hz ? speed_hz : DEFAULT_SPI_SPEED_HZ;
        spi_transfer.bits_per_word = 8;
        
        return (ioctl(hw_context->spi_file_descriptor, SPI_IOC_MESSAGE(1), &spi_transfer) < 0) ? -1 : 0;
    }
}

int upload_bitstream(wing_hardware_context_t *hw_context, const char *filepath, uint32_t speed_hz) {
    printf("[*] Uploading FPGA Bitstream to Efinix Trion (%s mode)...\n",
           hw_context->is_using_mmio ? "ECSPI2 0x0200C000 MMIO" : "spidev");

    FILE *bitstream_file = fopen(filepath, "rb");
    if (!bitstream_file) {
        fprintf(stderr, "[ERROR] Could not open file '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    struct stat file_status;
    fstat(fileno(bitstream_file), &file_status);
    size_t total_file_size = file_status.st_size;

    if (total_file_size == 3458589 || total_file_size == 3458521) {
        fseek(bitstream_file, 260, SEEK_SET);
        total_file_size -= 260;
    }

    uint8_t chunk_buffer[FIFO_CHUNK_SIZE];
    size_t total_bytes_sent = 0;
    const int progress_bar_width = 50;

    // Steuerleitungne für DSP-CS auf 1 -> alle aus
    hardware_gpio_set_pin(hw_context, 2, 26, true);
    hardware_gpio_set_pin(hw_context, 2, 27, true);
    hardware_gpio_set_pin(hw_context, 3, 24, true);
    hardware_gpio_set_pin(hw_context, 3, 25, true);

    // FPGA Status prüfen

    bool fpga_done = hardware_gpio_get_pin(hw_context, 4, 26);
    bool fpga_status = hardware_gpio_get_pin(hw_context, 2, 18);
        
    printf("\n[+] ## FPGA status: %d", fpga_status);
    printf("\n[+] ## FPGA done: %d", fpga_done);

    // CRESET_N low setzen
    hardware_gpio_set_pin(hw_context, 2, 17, false);
    printf("\n[+] FPGA reset LOW");

    // 2ms warten
    
    usleep(2000);

    // CRESET_N high setzen
    hardware_gpio_set_pin(hw_context, 2, 17, true);
    printf("\n[+] FPGA reset HIGH");

    // 5ms warten
    
    usleep(5000);

    // FPGA Status prüfen

    fpga_done = hardware_gpio_get_pin(hw_context, 4, 26);
    fpga_status = hardware_gpio_get_pin(hw_context, 2, 18);
        
    printf("\n[+] ## FPGA status: %d", fpga_status);
    printf("\n[+] ## FPGA done: %d", fpga_done);
    printf("\n");

    // Bitstream übertragen
    size_t bytes_read;
    while ((bytes_read = fread(chunk_buffer, 1, sizeof(chunk_buffer), bitstream_file)) > 0) {
        if (hardware_spi_transfer(hw_context, chunk_buffer, NULL, bytes_read, speed_hz) < 0) {
            fprintf(stderr, "\n[ERROR] SPI transmission error at byte %zu\n", total_bytes_sent);
            fclose(bitstream_file);
            return -1;
        }
        total_bytes_sent += bytes_read;
        
        int current_progress = (int)((double)total_bytes_sent / total_file_size * progress_bar_width);
        printf("\r[");
        for (int i = 0; i < progress_bar_width; i++) printf("%s", (i < current_progress) ? "=" : " ");
        printf("] %zu/%zu B (%.1f%%)", total_bytes_sent, total_file_size, (double)total_bytes_sent / total_file_size * 100.0);
        fflush(stdout);
    }

    // 1000 Nullwerte als Padding am Ende übertragen

    memset(chunk_buffer, 0, 1000);
    hardware_spi_transfer(hw_context, chunk_buffer, NULL, 1000, speed_hz);

    printf("\n[+] FPGA Bitstream uploaded! (%zu bytes transmitted)\n", total_bytes_sent);
    fclose(bitstream_file);

    // 5 ms warten

    usleep(5000);

    // FPGA Status prüfen

    fpga_done = hardware_gpio_get_pin(hw_context, 4, 26);
    fpga_status = hardware_gpio_get_pin(hw_context, 2, 18);
        
    printf("\n[+] ## FPGA status: %d", fpga_status);
    printf("\n[+] ## FPGA done: %d", fpga_done);

    // FPGA Magic auslesen

    uint8_t rx_magic_buffer[FIFO_CHUNK_SIZE];
    memset(chunk_buffer, 0, 100);
    memset(rx_magic_buffer, 0, 100);
    hardware_spi_transfer(hw_context, chunk_buffer, rx_magic_buffer, 10, speed_hz);

    rx_magic_buffer[10] = '\0';
    printf("\n[+] FPGA MAGIC (10): %s", rx_magic_buffer);

    
    return 0;
}

static void print_usage(const char *executable_name) {
    printf("Usage: %s [options]\n\n", executable_name);
    printf("Options:\n");
    printf("  --mmio                 Use direct i.MX6 ECSPI2 MMIO (0x0200C000) (default)\n");
    printf("  -d, --dev <path>       SPI device path for spidev mode (e.g. /dev/spidev0.1)\n");
    printf("  -s, --speed <hz>       SPI speed in Hz (default: %d Hz)\n", DEFAULT_SPI_SPEED_HZ);
    printf("  -u, --upload <file>    Upload bitstream (.bin, .bit.bin) to FPGA\n");
    printf("  --boot <1..4|all> <file> Stream bootloader kernel to specific DSP or all\n");
    printf("  --dsp <1..4>           Target specific DSP for raw SPI transfer\n");
    printf("  --send <hex>           Hex bytes to send to selected DSP (e.g. '00000000')\n");
    printf("  --gpio-set <bank>      Set GPIO output (requires --pin and --val)\n");
    printf("  --gpio-get <bank>      Read GPIO input level (requires --pin)\n");
    printf("  --pin <0..31>          GPIO pin index\n");
    printf("  --val <0|1>            Value to write to GPIO\n");
    printf("  -h, --help             Show this help message\n");
}

int main(int argc, char *argv[]) {
    const char *spi_device_path = DEFAULT_SPI_DEVICE_PATH;
    uint32_t spi_speed_hz = DEFAULT_SPI_SPEED_HZ;
    const char *upload_filepath = NULL;
    const char *boot_target_string = NULL;
    const char *boot_filepath = NULL;
    const char *hex_send_string = NULL;
    int target_dsp_index = 0;
    bool force_spidev_mode = false;

    // Variablen für GPIO Steuerung
    int gpio_set_bank = -1;
    int gpio_get_bank = -1;
    int gpio_pin_num = -1;
    int gpio_val = -1;

    static struct option long_options[] = {
        {"mmio",       no_argument,       0, 1000},
        {"dev",        required_argument, 0, 'd'},
        {"speed",      required_argument, 0, 's'},
        {"upload",     required_argument, 0, 'u'},
        {"dsp",        required_argument, 0, 1003},
        {"send",       required_argument, 0, 1004},
        {"boot-all",   required_argument, 0, 1005},
        {"boot",       required_argument, 0, 1006},
        {"gpio-set",   required_argument, 0, 1007},
        {"gpio-get",   required_argument, 0, 1008},
        {"pin",        required_argument, 0, 1009},
        {"val",        required_argument, 0, 1010},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int option_char;
    while ((option_char = getopt_long(argc, argv, "d:s:u:h", long_options, NULL)) != -1) {
        switch (option_char) {
            case 1000: force_spidev_mode = false; break;
            case 'd': spi_device_path = optarg; force_spidev_mode = true; break;
            case 's': spi_speed_hz = (uint32_t)strtoul(optarg, NULL, 10); break;
            case 'u': upload_filepath = optarg; break;
            case 1003: target_dsp_index = atoi(optarg); break;
            case 1004: hex_send_string = optarg; break;
            case 1005: boot_target_string = "all"; boot_filepath = optarg; break;
            case 1006: 
                boot_target_string = optarg;
                if (optind < argc && argv[optind][0] != '-') {
                    boot_filepath = argv[optind++];
                }
                break;
            case 1007: gpio_set_bank = atoi(optarg); break;
            case 1008: gpio_get_bank = atoi(optarg); break;
            case 1009: gpio_pin_num  = atoi(optarg); break;
            case 1010: gpio_val      = atoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    wing_hardware_context_t hw_context;
    memset(&hw_context, 0, sizeof(hw_context));
    hw_context.memory_file_descriptor = -1;
    hw_context.spi_file_descriptor = -1;

    if (!force_spidev_mode) {
        if (hardware_init_mmio(&hw_context) != 0) {
            hw_context.spi_file_descriptor = open(spi_device_path, O_RDWR);
        }
    } else {
        hw_context.spi_file_descriptor = open(spi_device_path, O_RDWR);
    }

    // 3. Bitstream-Upload
    if (upload_filepath) {
        upload_bitstream(&hw_context, upload_filepath, spi_speed_hz);
        hardware_close(&hw_context);
        return 0;
    }

    // 4. Bootloader-Streaming für DSPs
    if (boot_filepath) {
        uint8_t dsp_target_address = 0x0F;
        if (boot_target_string && strcmp(boot_target_string, "all") != 0) {
            dsp_target_address = (uint8_t)atoi(boot_target_string);
        }
        
        printf("[*] Streaming bootloader to Target 0x%02X (%s)...\n",
               dsp_target_address, (dsp_target_address == 0x0F) ? "Broadcast All DSPs" : "Individual DSP");
        
        FILE *boot_file = fopen(boot_filepath, "rb");
        if (!boot_file) {
            fprintf(stderr, "[ERROR] Could not open %s: %s\n", boot_filepath, strerror(errno));
            hardware_close(&hw_context);
            return 1;
        }

        uint8_t file_chunk_buffer[256];
        size_t bytes_read;
        while ((bytes_read = fread(file_chunk_buffer, 1, sizeof(file_chunk_buffer), boot_file)) > 0) {
            uint8_t transmit_chunk[257];
            transmit_chunk[0] = dsp_target_address;
            memcpy(&transmit_chunk[1], file_chunk_buffer, bytes_read);
            hardware_spi_transfer(&hw_context, transmit_chunk, NULL, bytes_read + 1, spi_speed_hz);
        }
        
        fclose(boot_file);
        printf("[+] Boot stream complete.\n");
        hardware_close(&hw_context);
        return 0;
    }

    // 5. Raw Hex-Daten an DSP senden
    if (target_dsp_index && hex_send_string) {
        size_t hex_string_length = strlen(hex_send_string);
        size_t byte_length = hex_string_length / 2;
        
        uint8_t *tx_buffer = malloc(byte_length + 1);
        uint8_t *rx_buffer = malloc(byte_length + 1);
        
        tx_buffer[0] = (uint8_t)target_dsp_index;

        for (size_t i = 0; i < byte_length; i++) {
            auto int parse_nibble(char character) {
                if (character >= '0' && character <= '9') return character - '0';
                if (character >= 'A' && character <= 'F') return character - 'A' + 10;
                if (character >= 'a' && character <= 'f') return character - 'a' + 10;
                return 0;
            };
            tx_buffer[i + 1] = (uint8_t)((parse_nibble(hex_send_string[i * 2]) << 4) | 
                                          parse_nibble(hex_send_string[i * 2 + 1]));
        }

        hardware_spi_transfer(&hw_context, tx_buffer, rx_buffer, byte_length + 1, spi_speed_hz);
        
        printf("[+] Live MISO from DSP #%d (Target 0x%02X):\n    HEX: ", target_dsp_index, target_dsp_index);
        for (size_t i = 1; i <= byte_length; i++) {
            printf("%02X ", rx_buffer[i]);
        }
        printf("\n    ASCII: ");
        for (size_t i = 1; i <= byte_length; i++) {
            printf("%c", (rx_buffer[i] >= 32 && rx_buffer[i] <= 126) ? rx_buffer[i] : '.');
        }
        printf("\n");

        free(tx_buffer);
        free(rx_buffer);
        hardware_close(&hw_context);
        return 0;
    }

    hardware_close(&hw_context);
    return 0;
}