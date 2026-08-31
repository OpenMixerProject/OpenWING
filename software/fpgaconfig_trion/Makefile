CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2

TARGET = wing_fpga_dsp_tool
SRC = ../../linux-tools/wing_fpga_dsp_tool.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) fpgaconfig_trion

.PHONY: all clean
