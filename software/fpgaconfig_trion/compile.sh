#!/bin/bash
set -e
CC=${CROSS_COMPILE:-gcc}
echo "Compiling wing_fpga_dsp_tool with ${CC}..."
${CC} -Wall -Wextra -O2 ../../linux-tools/wing_fpga_dsp_tool.c -o wing_fpga_dsp_tool
echo "Build complete: wing_fpga_dsp_tool"
