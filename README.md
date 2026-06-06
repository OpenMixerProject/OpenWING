# Open Source Operating System for the Behringer WING Audio Mixing Console

This repository contains software to load and start the Linux-Kernel on the Behringer WING and some userland tools. Currently (June 2026) it is Work in Progress.

The main logic of our custom firmware lives in its own repository at [OpenMixerControl](https://github.com/OpenMixerProject/OpenMixerControl)

# Hardware

## CPU

<img width="931" height="969" alt="Screenshot_2026-06-06-01-28-27-64_92460851df6f172a4592fca41cc2d2e6" src="https://github.com/user-attachments/assets/1299e5a3-b6a4-4dac-8973-80b8576476b5" />

i.MX 6 series 32-bit MPU, ARM Cortex-A9 core, 1GHz, MAPBGA 624

Datasheet: https://www.nxp.com/docs/en/data-sheet/IMX6SDLCEC.pdf

## Current State (June 2026)

### OS and Devices

<img width="3000" height="4000" alt="IMG_20260601_073527" src="https://github.com/user-attachments/assets/17ea719a-ec58-4a85-a6ef-fc72c6bbbfd8" />

- Linux boots 🐧
- Screen, Touch and Network is working
- Buttons, Encoders and Faders are basically working, needs more mapping
- LCDs are understood
<img width="3000" height="4000" alt="IMG_20260605_050305" src="https://github.com/user-attachments/assets/dab998ef-cbfc-46bb-8f70-65311aed5435" />

- USB works

### Software

- OpenMixerControl starts

<img width="3000" height="4000" alt="IMG_20260604_020730" src="https://github.com/user-attachments/assets/51cb410b-7d2b-4a9d-9ea8-ab00bf1b646f" />

## TODOs

- understand the Scribbe Strip LCDs
- finish groundworks on surface input and output (Buttons, Encoders, Faders, LCDs, LEDs)
- get the Trion FPGA working
- get the four DSPs working
- get the Audiosystem working (ADDA-Chips, digital audio interfaces like StageConnect, AES/EBU, AES50)
- optimize the build chain and dial in the build parameter for max performance
- add usefull tools to the OS
- port OpenMixerControl to use the hardware of the WING

# Social, Web, Community

- [OpenMixerProject on Github](https://github.com/OpenMixerProject)
- [OpenX32 Website](https://openx32.com/)
- [Discourse Community](https://discourse.openmixerproject.de/)
- [Sponsor us](https://buymeacoffee.com/chrisnoeding)
