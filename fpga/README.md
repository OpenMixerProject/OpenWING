# OpenWING FPGA & SHARC DSP Tools & Hardware Interface

This directory contains FPGA bitstreams and DSP microcode images for the **Efinix Trion T55F484** FPGA and the **4× Analog Devices SHARC ADSP-21489** audio DSPs on the Behringer WING.

---

## 1. Directory Contents

| File | Size | Description |
| :--- | :--- | :--- |
| `wing_debug_spi_bridge_firmware.bin` | 3,458,589 B | FPGA SPI Router bitstream with Behringer 260-byte packaging header. |
| `wing_debug_spi_bridge.bit.bin` | 3,458,517 B | Raw Efinity passive SPI bitstream. |
| `sharc_dsp1_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #1. |
| `sharc_dsp2_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #2. |
| `sharc_dsp3_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #3. |
| `sharc_dsp4_welcome.bin` | 1,536 B | 256-word bootloader kernel for SHARC DSP #4. |
| `sharc_min_boot.bin` | 1,536 B | Generic minimal 256-word SHARC ADSP-21489 boot kernel. |

All files in this directory are automatically bundled into `/usr/share/fpga/` in the OpenWING firmware image.

---

## 2. Command Line Tools

### A. `wing_fpga_dsp_tool` (`/usr/bin/wing_fpga_dsp_tool`)

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

#### Examples:
```bash
# 1. Upload bitstream to FPGA
wing_fpga_dsp_tool --upload /usr/share/fpga/wing_debug_spi_bridge_firmware.bin

# 2. Bootload DSP #1 with microcode
wing_fpga_dsp_tool --boot 1 /usr/share/fpga/sharc_dsp1_welcome.bin

# 3. Broadcast bootloader to all 4 DSPs simultaneously
wing_fpga_dsp_tool --boot all /usr/share/fpga/sharc_min_boot.bin

# 4. Send 8 raw SPI bytes to DSP #3 and read returned MISO bytes
wing_fpga_dsp_tool --dsp 3 --send 0000000000000000
```

---

### B. `wing_dsp_demo` (`/usr/bin/wing_dsp_demo`)

End-to-end multi-DSP verification demo that automates:
1. FPGA bitstream configuration.
2. Individual DSP microcode bootloader upload.
3. Live SPI querying of all 4 DSPs with raw MISO hex output.

```bash
# Run the demo
wing_dsp_demo
```

---

## 3. FPGA SPI Framing Protocol

When the SPI Bridge bitstream is active in the Efinix Trion T55, the Linux host transmits 1-byte framed SPI commands:

| Byte 0 (Header) | Destination | Hardware Action |
| :--- | :--- | :--- |
| `0x00` | **FPGA Core Registers** | Reads/writes internal FPGA control registers (Magic `'WING'`, DSP resets). |
| `0x01` | **DSP #1 (ADSP-21489 #1)** | Asserts `/SPISS1` (`J5`), routes `SCK`/`MOSI` to `J2`/`J3`, routes `MISO` (`J4`) to Host. |
| `0x02` | **DSP #2 (ADSP-21489 #2)** | Asserts `/SPISS2` (`L7`), routes `SCK`/`MOSI` to `J2`/`J3`, routes `MISO` (`J4`) to Host. |
| `0x03` | **DSP #3 (ADSP-21489 #3)** | Asserts `/SPISS3` (`N2`), routes `SCK`/`MOSI` to `J2`/`J3`, routes `MISO` (`J4`) to Host. |
| `0x04` | **DSP #4 (ADSP-21489 #4)** | Asserts `/SPISS4` (`P4`), routes `SCK`/`MOSI` to `J2`/`J3`, routes `MISO` (`J4`) to Host. |
| `0x0F` | **Broadcast (All DSPs)** | Asserts all 4 chip selects simultaneously for parallel microcode streaming. |

---

## 4. Hardware Pin Mapping Reference

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
