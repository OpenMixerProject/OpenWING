# wing-audio-clock: Si5351B Audio Master Clock Utility

Controls the Silicon Labs **Si5351B** (`U23`) audio clock generator directly from the i.MX6 CPU via hardware **I2C4** (`0x021F8000`) MMIO.

## Hardware Wiring
* **I2C Controller**: i.MX6 hardware I2C4
* **SCL**: `NANDF_WP_B` (Ball E15, Mode 9)
* **SDA**: `NANDF_CS3` (Ball D16, Mode 9)
* **Slave Address**: `0x60`
* **Outputs**:
  * `CLK0`: Audio MCLK to FPGA Bank 1A (24.576 MHz @ 48k / 22.5792 MHz @ 44.1k)
  * `CLK2`: AES50 Transceiver Reference Clock (24.576 MHz @ 48k)
  * `CLK4`: SHARC DSP Master MCLK (12.288 MHz @ 48k / 11.2896 MHz @ 44.1k)
  * `CLK5`: Secondary Audio Clock (12.288 MHz @ 48k)

## CLI Usage
```bash
# Set sample rate to 48 kHz (standard)
wing-audio-clock --set 48k

# Set sample rate to 44.1 kHz
wing-audio-clock --set 44.1k

# Check PLL lock status and active rates
wing-audio-clock --status

# Scan I2C4 bus for devices
wing-audio-clock --detect

# Dump registers of any I2C4 device (e.g. 0x68 RTC, 0x6A A2B)
wing-audio-clock --dump-dev 0x68

# Safe shutdown: Tri-state and disable all clock outputs
wing-audio-clock --stop
```

## Compilation
```bash
docker run --rm -v "$(pwd):/work" -w /work wing-usb-console-builder:trixie \
  arm-linux-gnueabihf-gcc -O2 -static -Wall wing_clock.c -o wing-clock
```
