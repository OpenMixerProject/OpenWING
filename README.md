# Open Source Operating System for the Behringer WING Audio Mixing Console

This repository contains software to load and start the Linux-Kernel on the Behringer WING and some userland tools.
The main logic of our custom firmware lives in its own repository at [OpenMixerControl](https://github.com/OpenMixerProject/OpenMixerControl)

# Progress-O-Meter

Description | State
-----|-------
Linux boots | ✔️ May 2026
Stable Networking | 👀 In Progress
Scribble Strip LCDs | ✔️ June 2026
Surface Input and Output (Buttons, Encoders, Faders, LCDs, LEDs, Touchscreen) | ✔️ June 2026
OpenMixerControl ported to iMX6 | ✔️ June 2026
Trion FPGA Firmware Upload | ✔️ September 2026
Implement FPGA Firmware (TDM Audio Routing) | 
DSP Firmware Upload | 👀 In Progress
Implement DSP1 Firmware (Input) |
Implement DSP2 Firmware (Bus) |
Implement DSP3 Firmware (FX1) |
Implement DSP4 Firmware (FX2) |
ADDA-Chips |
StageConnect |
AES/EBU | 
AES50 |

# OS and Devices

- Linux boots 🐧
- USB works
- Screen, Touch and Network is working
- Buttons, Encoders and Faders are basically working, needs more mapping
- LCDs are understood 

<img width="300" height="400" alt="IMG_20260601_073527" src="https://github.com/user-attachments/assets/17ea719a-ec58-4a85-a6ef-fc72c6bbbfd8" />
<img width="300" height="400" alt="IMG_20260605_050305" src="https://github.com/user-attachments/assets/dab998ef-cbfc-46bb-8f70-65311aed5435" />

# Software

- OpenMixerControl starts and can be "used", but without the WING audio hardware

<img width="300" height="400" alt="IMG_20260607_182549" src="https://github.com/user-attachments/assets/a6c39231-d984-4066-8214-f5afbd509c15" />
<img width="400" height="300" alt="IMG_20260607_174217" src="https://github.com/user-attachments/assets/50e9e0c2-e282-453f-a69b-497ceb20157a" />
<img width="300" height="400" alt="IMG_20260607_182642" src="https://github.com/user-attachments/assets/13924386-fbdd-4a5d-8371-0ff2e37143da" />

# Sponsors

[Analog Devices](https://www.analog.com) sponsored us a licence for CrossCore Embedded Studio - many thanks!

<img width="200" alt="grafik" src="https://github.com/user-attachments/assets/5e1ba20c-b4e0-454a-b10e-205d58897fa8" />

# Hardware

## CPU

<img width="93" height="96" alt="Screenshot_2026-06-06-01-28-27-64_92460851df6f172a4592fca41cc2d2e6" src="https://github.com/user-attachments/assets/1299e5a3-b6a4-4dac-8973-80b8576476b5" />

i.MX 6 series 32-bit MPU, single ARM Cortex-A9 core, 1GHz, MAPBGA 624 (With VPU, GPU, MLB, EPDC)

- Product Page: https://www.nxp.com/part/MCIMX6S8DVM10AC
- Datasheet: https://www.nxp.com/docs/en/data-sheet/IMX6SDLCEC.pdf

## Stage the matched WING DSP loaders

Pass the reviewed OpenAudioDSP `wing-wdsp-v1-stock-tdm16-21489` build directory
to `python3 tools/stage_wing_dsp.py DSP_BUILD NEW_OUTPUT_DIRECTORY`. The input
must include `SHA256SUMS`, `provenance.txt`, `dsp_board_profile.json`, and all
four raw ADSP-21489 SPI loaders. The tool checks the pinned bytes for DSP1
(input), DSP2 (bus), DSP3 (FX1), and DSP4 (FX2), and the stock-direction TDM16
profile (`3b29934…`). Rebuilding with different bytes requires review of the
new pins; regenerating `SHA256SUMS` does not make a different set match.

The output is a staging directory only. It does not install or boot anything,
and neither DSP firmware nor vendor loader binaries are distributed here.

## Manual T55 DSP bring-up

Build with `WING_PROFILE=t55-spibone ./compile.sh`. Set `WING_T55_FPGA_DIR`
to the matching schema-2 T55 FPGA image, manifest, three generated CSR files
and board profile; `WING_T55_DSP_DIR` to the loader build above;
`WING_T55_OMC_DIR` to the matched static ARM hard-float OMC artifact with
checksums and provenance; and `OPENAUDIOFPGA_DIR` to the reviewed WING
SPIBone source checkout. The default Linux build is unchanged.

The T55 image does not load the FPGA, boot a DSP, or start OMC at startup.
On a muted bench only, with external FPGA clocks independently verified,
`wing-t55 load --bench-muted --clocks-verified` loads the volatile fabric.
Then `wing-t55 boot-dsp {1|2|4} --bench-muted` boots one explicitly selected
core (substitute its number). DSP3 is blocked: its reset is not qualified
and A9 must remain an input. The tool uses SS0 (`/dev/spidev1.0`) for the
target-prefixed loader and WDSP v1 IDENTIFY, and SS1 (`/dev/spibone0`) for
identity, reset and safety register checks. It never enables FS, VALID or
unmutes the mixer. The reset/loader delays and electrical behavior still
need bench validation; an armed identity reply is not audio readiness.

OMC and the loader coordinate through `/run/wing-t55.lock` (shared for OMC's
session, exclusive for load/boot). The root-only atomic receipt is
`/run/omc-wing-fpga.json` on boot-local tmpfs. It binds the packaged manifest
path and SHA-256, bitstream SHA-256, build ID, and SS1 endpoint and chip
select. `dsp_runtime_mask` starts at zero. Bit `n − 1` is added only after
DSP `n` returns an armed, CRC- and nonce-verified WDSP v1 IDENTIFY reply
and the live reset state matches the new mask. A failed mutating operation
leaves no receipt; reload the fabric before retrying. The packaged manifest
records SS0 as the WDSP v1 runtime endpoint.

The build stages SPIBone from the selected OpenAudioFPGA commit, based on
Mala's [OpenAudioFPGA history](https://github.com/malarisch/OpenAudioFPGA/commit/c56ef3ecdd7b2c2c0b7844788cd40c45964d87c4)
and [SPIBone/Wishbone implementation](https://github.com/malarisch/OpenAudioFPGA/commit/7d2869acdcff4a0a1952b74bf9b5fe45e37ff7dd).
Driver source and notices are copied byte-for-byte. The UAPI adds the WING
opt-in ioctl to Mala's baseline and retains `GPL-2.0-or-later WITH
Linux-syscall-note`; the rest of the driver retains its original notices.

# Social, Web, Community

- [OpenMixerProject on Github](https://github.com/OpenMixerProject)
- [OpenX32 Website](https://openx32.com/)
- [Discourse Community](https://discourse.openmixerproject.de/)
- [Sponsor us](https://buymeacoffee.com/chrisnoeding)
