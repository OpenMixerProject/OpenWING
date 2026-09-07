# OpenWING FPGA & SHARC DSP Tools & Hardware Interface

This directory contains FPGA bitstreams, HDL sources, and DSP microcode images for the **Efinix Trion T55F484 / T85F484** FPGA and the **4× Analog Devices SHARC ADSP-21489** audio DSPs on the Behringer WING.

---

## 1. Directory Contents

| File | Size | Description |
| :--- | :--- | :--- |
| `wing_hello_spi_t55.bit.bin` | 3,458,517 B | Efinix Trion T55 bitstream: responds to `0x1337` with `"Hello from Wing!"`. |
| `wing_hello_spi_t85.bit.bin` | 3,504,437 B | Efinix Trion T85 bitstream: responds to `0x1337` with `"Hello from Wing!"`. |
| `dummy_spi_responder_t55.bit.bin` | 3,458,517 B | Efinix Trion T55 bitstream: responds to `PING` with `"TRION READY\n"`. |
| `dummy_spi_responder_t85.bit.bin` | 3,504,437 B | Efinix Trion T85 bitstream: responds to `PING` with `"TRION READY\n"`. |
| `wing_debug_spi_bridge.bit.bin` | 3,458,517 B | Raw Efinity passive SPI bitstream for 4x SHARC DSP routing. |
| `wing_debug_spi_bridge_firmware.bin` | 3,458,589 B | FPGA SPI Router bitstream with Behringer 260-byte packaging header. |
| `wing_hello_spi.v` | 3,664 B | Verilog source for `wing_hello_spi` responder. |
| `wing_hello_spi.vhd` | 3,510 B | VHDL source for `wing_hello_spi` responder. |
| `dummy_spi_responder.v` | 4,256 B | Verilog source for `dummy_spi_responder`. |
| `sharc_dsp1_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #1. |
| `sharc_dsp2_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #2. |
| `sharc_dsp3_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #3. |
| `sharc_dsp4_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #4. |
| `sharc_min_boot.bin` | 1,536 B | Generic minimal 256-word SHARC ADSP-21489 boot kernel. |

All files in this directory are automatically bundled into `/usr/share/fpga/` in the OpenWING firmware image.

---

## 2. Command Line Tools

### A. `wing_fpga_uploader` (`/usr/bin/wing_fpga_uploader` & `/usr/bin/wing_upload_fpga`)

Direct MMIO hardware uploader for configuring the Efinix Trion FPGA via i.MX6 `ECSPI2` (`0x0200C000`).
Supports automatic T55 vs T85 hardware detection via `CDONE` (`GPIO4_26`):

```bash
# Auto-detect T55 / T85 and load default hello bitstream:
wing_upload_fpga

# Or run directly with explicit path:
wing_fpga_uploader /usr/share/fpga/wing_hello_spi_t55.bit.bin
```

### B. `test_wing_hello` (`/usr/bin/test_wing_hello`)

SPI verification test client communicating over `/dev/spidev1.0`:
Sends `0x1337` and reads back 16 ASCII bytes (`"Hello from Wing!"`).

```bash
test_wing_hello
# Output:
# === Wing Hello FPGA SPI Test ===
# Sending CMD 0x1337 and reading 16 bytes...
# Received raw: 00 00 48 65 6c 6c 6f 20 66 72 6f 6d 20 57 69 6e 67 21
# Response text: "Hello from Wing!"
# SUCCESS: FPGA responded with expected 'Hello from Wing!' banner!
```

### C. `wing_fpga_dsp_tool` (`/usr/bin/wing_fpga_dsp_tool`)

Low-level CLI utility for direct hardware SPI communication with the FPGA and DSPs over i.MX6 `ECSPI2` (`0x0200C000`).

```text
Usage: wing_fpga_dsp_tool [options]

Options:
  --mmio                 Direct i.MX6 ECSPI2 MMIO (0x0200C000) (default)
  -d, --dev <path>       SPI device path (spidev mode)
  -s, --speed <hz>       SPI clock speed in Hz (default: 2,000,000)
  -u, --upload <file>    Upload bitstream (.bin, .bit.bin) to FPGA
  --boot <1..4|all> <f>  Stream bootloader kernel to specified DSP or broadcast to all
  --dsp <1..4>           Target specific DSP for raw SPI transfer
  --send <hex>           Hex bytes to send to selected DSP (e.g. '00000000')
  -h, --help             Show this help message
```

---

## 3. Hardware Pin Mapping Reference

### Efinix Trion Bank 1A (Host CPU ECSPI2 & Config Interface)

| Signal | FPGA Ball | i.MX6 Net / GPIO | Description |
| :--- | :--- | :--- | :--- |
| `CCK` | `W1` | `ECSPI2_SCLK` (`EIM_CS0`) | Passive SPI Clock |
| `CDI0` | `V2` | `ECSPI2_MOSI` (`EIM_CS1`) | Serial Data In (Host MOSI) |
| `CDI1` | `V1` | `ECSPI2_MISO` (`EIM_OE`) | Serial Data Out (Host MISO) |
| `SS_N` | `V3` | `ECSPI2_SS0` (`GPIO2_26` / `EIM_RW`) | Slave Select (Active Low) |
| `CRESET_N`| `V4` | `GPIO2_17` (`EIM_A21`) | FPGA Hardware Reset (Active Low) |
| `CDONE` | `V5` | `GPIO4_26` (`DISP0_DAT5`) | Configuration Done (Active High) |
| `NSTATUS` | `F5` | `GPIO2_18` (`EIM_A20`) | Config Status (Active High) |

### Efinix Trion to Analog Devices ADSP-21489 SHARC DSPs

| Function | FPGA Ball / Net | Connected Target |
| :--- | :--- | :--- |
| `SPI_CLK` | `J2` (`GPIOL_86`) | Shared DSP SPI Clock (`SCK`) |
| `SPI_MOSI` | `J3` (`GPIOL_87`) | Shared DSP Master-Out-Slave-In (`MOSI`) |
| `SPI_MISO` | `J4` (`GPIOL_88`) | Shared DSP Master-In-Slave-Out (`MISO`) |
| `DSP_CS1_N` | `J5` (`GPIOL_89`) | DSP #1 Chip Select (`/SPISS1`) |
| `DSP_CS2_N` | `L7` (`GPIOL_97`) | DSP #2 Chip Select (`/SPISS2`) |
| `DSP_CS3_N` | `N2` (`GPIOL_99`) | DSP #3 Chip Select (`/SPISS3`) |
| `DSP_CS4_N` | `P4` (`GPIOL_103`) | DSP #4 Chip Select (`/SPISS4`) |
| `DSP_RESET_N` | `B1` (`GPIOL_70`) | Shared DSP Active-Low Reset |
