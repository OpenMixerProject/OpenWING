#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("==================================================================\n");
    printf("        BEHRINGER WING - 4x SHARC ADSP-21489 HARDWARE DEMO        \n");
    printf("==================================================================\n\n");

    // 1. Upload FPGA Bitstream
    printf("[1/3] Uploading FPGA Bitstream to Efinix Trion T55...\n");
    system("/usr/bin/wing_fpga_dsp_tool --upload /usr/share/fpga/wing_debug_spi_bridge_firmware.bin");
    printf("\n");

    // 2. Bootload all 4 DSPs
    printf("[2/3] Streaming microcode kernel to all 4 DSPs...\n");
    for (int i = 1; i <= 4; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "/usr/bin/wing_fpga_dsp_tool --boot %d /usr/share/fpga/sharc_dsp%d_welcome.bin", i, i);
        system(cmd);
        usleep(20000);
    }
    printf("\n");

    // 3. Query DSPs over SPI
    printf("[3/3] Querying live MISO responses from all 4 DSPs...\n");
    for (int i = 1; i <= 4; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "/usr/bin/wing_fpga_dsp_tool --dsp %d --send 0000000000000000000000000000000000000000", i);
        system(cmd);
        printf("\n");
    }

    printf("==================================================================\n");
    printf("                     HARDWARE DEMO COMPLETE                       \n");
    printf("==================================================================\n");
    return 0;
}
