# Open Source Operating System for the Behringer WING Audio Mixing Console

This repository contains software to load and start the Linux-Kernel on the Behringer WING and some userland tools.
The main logic of our custom firmware lives in its own repository at [OpenMixerControl](https://github.com/OpenMixerProject/OpenMixerControl)

# Progress-O-Meter

Description | State
-----|-------
Linux boots | ✔️ May 2026
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

# Hardware

## CPU

<img width="93" height="96" alt="Screenshot_2026-06-06-01-28-27-64_92460851df6f172a4592fca41cc2d2e6" src="https://github.com/user-attachments/assets/1299e5a3-b6a4-4dac-8973-80b8576476b5" />

i.MX 6 series 32-bit MPU, single ARM Cortex-A9 core, 1GHz, MAPBGA 624 (With VPU, GPU, MLB, EPDC)

- Product Page: https://www.nxp.com/part/MCIMX6S8DVM10AC
- Datasheet: https://www.nxp.com/docs/en/data-sheet/IMX6SDLCEC.pdf


# Social, Web, Community

- [OpenMixerProject on Github](https://github.com/OpenMixerProject)
- [OpenX32 Website](https://openx32.com/)
- [Discourse Community](https://discourse.openmixerproject.de/)
- [Sponsor us](https://buymeacoffee.com/chrisnoeding)
