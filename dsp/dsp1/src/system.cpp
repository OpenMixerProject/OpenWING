#include "system.h"

void systemPllSetBypass(bool bypass) {
	int i;

	if (bypass) {
		// enable bypass-mode
		*pPMCTL |= PLLBP;
	}else{
		// wait at least 4096 CLKIN cycles
		for (i=0; i<4096; i++) { asm("nop;"); }
		// disable bypass-mode
		*pPMCTL &= ~PLLBP;
	}

	// wait 16 CCLK cycles
	for (i=0; i<16; i++) { asm("nop;"); }
}

void systemPllInit() {
	/*
	CLKIN -> INPUT CLOCK DIVIDER -> PHASE DETECT -> LOOP FILTER -> VCO -> POST DIVIDER -> CCLK/PCLK/SDCLK/MLBCLK/LCLK
										/\								|
										|								|
										|_______________________________|

	21489 supports up to 450MHz

	CLK_CFG1 = Pin  5 =
	CLK_CFG0 = Pin 22 =

	CLK_CFG[1:0]:
	===================
	00 8x
	01 32x
	10 16x
	11 reserved

	CLKIN is fed by CLK from Si5351
	*/

	systemPllSetBypass(true);
	*pPMCTL |= INDIV; // see page 23-3 in Processor Hardware Manual
	systemPllSetBypass(false);

	// Step 2: set PLL-configuration to the desired values
	// ================================================================
	systemPllSetBypass(true);
	*pPMCTL = (INDIV | PLLM1 | SDCKR2 | PLLBP | DIVEN); // DIVEN is self-clearing, so next line should be not necessary!
	*pPMCTL = (INDIV | PLLM1 | SDCKR2 | PLLBP); // disable DIVEN (DIVEN is WriteOnly, so set the whole setting without DIVEN)
	systemPllSetBypass(false);
}

void systemSruInit(void) {
	// setting up pins for LED-control via Flag-signals
	// =======================================
	// flags 4 to 15 are supported for DPI. Flags 0 to 3 not available on the DPI
	SRU(FLAG7_O, DPI_PB07_I); // connect output of Flag7 to input of DPI-PinBuffer 7 (LED on DPI7)
	SRU(HIGH, DPI_PBEN07_I); // set Pin-Buffer to output (HIGH=Output, LOW=Input)
	sysreg_bit_set(sysreg_FLAGS, FLG7O); // set flag-pins
	sysreg_bit_clr(sysreg_FLAGS, FLG7); // clear flag-pins
}
