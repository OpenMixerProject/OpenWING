# fpga-pin-monitor: 252-Pin FPGA Telemetry & Signal Analyzer

Monitors 252 GPIO pins on the Behringer WING Efinix Trion FPGA (`T55F484` / `T85F484`) simultaneously. Provides transition counting, logic-level inventory, 32-sample waveform capture, and differential signal detection.

## Hardware Interface
* **Host Interface**: i.MX6 `ECSPI2` (`/dev/spidev1.0`)
* **SPI Pins**: `W1` (SCK), `V2` (MOSI), `V1` (MISO)
* **Framing**: Self-synchronizing `0xA55A` preamble; no chip-select input
* **Monitored Pins**: 252 pins across Bank 1, Bank 2, Bank 3D, and Bank 4.

On the measured WING variant C (`T55F484`), the i.MX6 SS0–SS3 pads reach `J2`, `V6`, `K4`, and `G1`, respectively. The monitor samples these as probe inputs, not as chip selects. `V3` remains excluded to preserve existing probe IDs.

## CLI Usage (`test_all_pins`)
```bash
# Plug-in detector: outputs ONLY the newly appeared signals
/tmp/test_all_pins --diff --time 3

# Scan all 252 pins for active transitions over 3 seconds
/tmp/test_all_pins --time 3

# Full static DC inventory of all 252 pins (logic levels, transitions)
/tmp/test_all_pins --dump
```
