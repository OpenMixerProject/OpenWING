/*
 **
 ** sru_config.c source file generated on Juni 3, 2026 at 17:56:17.
 **
 ** Copyright (C) 2014-2026 Analog Devices Inc., All Rights Reserved.
 **
 ** This file is generated automatically based upon the options selected in
 ** the SRU Configuration editor. Changes to the SRU configuration should
 ** be made by changing the appropriate options rather than editing this file.
 **
 ** By default only the SRU registers with values different from their default
 ** reset value are initialized. Alternatively, macro SRU_FULL_INIT may be
 ** defined in the project settings to cause all SRU registers to be
 ** initialized.
 **
 */

#include <stdint.h>
#include <sys/platform.h>

int32_t adi_SRU_Init(void);

/*
 * Initialize the Signal Routing Unit
 */
int32_t adi_SRU_Init(void)
{
#if defined(SRU_FULL_INIT)
    /* Connections:
     *    DAI_PB07_O - SPORT1_CLK_I
     *    DAI_PB19_O - SPORT4_CLK_I
     *    DAI_PB13_O - SPORT3_CLK_I
     *    DAI_PB13_O - SPORT2_CLK_I
     *    DAI_PB03_O - SPORT0_CLK_I
     *    DAI_PB19_O - SPORT5_CLK_I
     */
    *pSRU_CLK0 = 0x252630c2uL;

    /* Connections:
     *    DAI_LOW - SRC0_CLK_OP_I
     *    DAI_LOW - SRC1_CLK_IP_I
     *    DAI_LOW - SRC2_CLK_IP_I
     *    DAI_LOW - SRC2_CLK_OP_I
     *    DAI_LOW - SRC0_CLK_IP_I
     *    DAI_LOW - SRC1_CLK_OP_I
     */
    *pSRU_CLK1 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - IDP0_CLK_I
     *    DAI_LOW - DIT_CLK_I
     *    DAI_LOW - IDP1_CLK_I
     *    DAI_LOW - SRC3_CLK_IP_I
     *    DAI_LOW - IDP2_CLK_I
     *    DAI_LOW - SRC3_CLK_OP_I
     */
    *pSRU_CLK2 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - IDP3_CLK_I
     *    DAI_LOW - DIT_HFCLK_I
     *    DAI_LOW - IDP6_CLK_I
     *    DAI_LOW - IDP5_CLK_I
     *    DAI_LOW - IDP7_CLK_I
     *    DAI_LOW - IDP4_CLK_I
     */
    *pSRU_CLK3 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - DIT_EXT_SYNC_I
     *    DAI_LOW - PCG_EXTA_I
     *    DAI_LOW - PCG_EXTB_I
     *    DAI_LOW - PCG_SYNC_CLKB_I
     *    DAI_LOW - PCG_SYNC_CLKA_I
     */
    *pSRU_CLK4 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - PCG_EXTD_I
     *    DAI_LOW - SPORT6_CLK_I
     *    DAI_LOW - PCG_SYNC_CLKD_I
     *    DAI_LOW - PCG_SYNC_CLKC_I
     *    DAI_LOW - PCG_EXTC_I
     *    DAI_LOW - SPORT7_CLK_I
     */
    *pSRU_CLK5 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_PB06_O - SPORT1_DB_I
     *    DAI_PB05_O - SPORT1_DA_I
     *    DAI_PB09_O - SPORT2_DA_I
     *    DAI_PB01_O - SPORT0_DA_I
     *    DAI_PB02_O - SPORT0_DB_I
     */
    *pSRU_DAT0 = 0x08144040uL;

    /* Connections:
     *    DAI_PB11_O - SPORT3_DA_I
     *    DAI_PB10_O - SPORT2_DB_I
     *    DAI_PB15_O - SPORT4_DA_I
     *    DAI_PB12_O - SPORT3_DB_I
     *    DAI_PB16_O - SPORT4_DB_I
     */
    *pSRU_DAT1 = 0x0f38b289uL;

    /* Connections:
     *    DAI_PB01_O - SRC1_DAT_IP_I
     *    DAI_PB18_O - SPORT5_DB_I
     *    DAI_PB01_O - SRC2_DAT_IP_I
     *    DAI_PB17_O - SPORT5_DA_I
     *    DAI_PB01_O - SRC0_DAT_IP_I
     */
    *pSRU_DAT2 = 0x00000450uL;

    /* Connections:
     *    DAI_PB01_O - SRC3_DAT_IP_I
     *    DAI_PB01_O - SRC0_TDM_OP_I
     *    DAI_PB01_O - SRC2_TDM_OP_I
     *    DAI_PB01_O - SRC3_TDM_OP_I
     *    DAI_PB01_O - SRC1_TDM_OP_I
     */
    *pSRU_DAT3 = 0x00000000uL;

    /* Connections:
     *    DAI_PB01_O - IDP2_DAT_I
     *    DAI_PB01_O - IDP3_DAT_I
     *    DAI_PB01_O - DIT_DAT_I
     *    DAI_PB01_O - IDP1_DAT_I
     *    DAI_PB01_O - IDP0_DAT_I
     */
    *pSRU_DAT4 = 0x00000000uL;

    /* Connections:
     *    DAI_PB01_O - IDP5_DAT_I
     *    DAI_PB01_O - IDP6_DAT_I
     *    DAI_PB01_O - DIR_I
     *    DAI_PB01_O - IDP7_DAT_I
     *    DAI_PB01_O - IDP4_DAT_I
     */
    *pSRU_DAT5 = 0x00000000uL;

    /* Connections:
     *    DAI_LOW - SPORT6_DB_I
     *    DAI_LOW - SPORT6_DA_I
     *    DAI_LOW - SPORT7_DA_I
     *    DAI_LOW - SPORT7_DB_I
     */
    *pSRU_DAT6 = 0x00fbefbeuL;

    /* Connections:
     *    DAI_PB01_O - INV_MISCA5_I
     *    DAI_LOW - MISCA1_I
     *    DAI_LOW - MISCA2_I
     *    DAI_LOW - DAI_INT_31_I
     *    DAI_LOW - MISCA0_I
     *    DAI_LOW - DAI_INT_30_I
     *    DAI_LOW - MISCA5_I
     *    DAI_LOW - DAI_INT_29_I
     *    DAI_LOW - MISCA3_I
     *    DAI_LOW - MISCA4_I
     *    DAI_LOW - DAI_INT_28_I
     *    DAI_PB01_O - INV_MISCA4_I
     */
    *pSRU_EXT_MISCA = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - DAI_INT_22_I
     *    DAI_LOW - DAI_INT_23_I
     *    DAI_LOW - DAI_INT_24_I
     *    DAI_LOW - DAI_INT_25_I
     *    DAI_LOW - DAI_INT_26_I
     *    DAI_LOW - DAI_INT_27_I
     */
    *pSRU_EXT_MISCB = 0x3def7bdeuL;

    /* Connections:
     *    DAI_PB14_O - SPORT3_FS_I
     *    DAI_PB08_O - SPORT1_FS_I
     *    DAI_PB20_O - SPORT5_FS_I
     *    DAI_PB14_O - SPORT2_FS_I
     *    DAI_PB04_O - SPORT0_FS_I
     *    DAI_PB20_O - SPORT4_FS_I
     */
    *pSRU_FS0 = 0x2736b4e3uL;

    /* Connections:
     *    DAI_LOW - SRC2_FS_IP_I
     *    DAI_LOW - SRC0_FS_OP_I
     *    DAI_LOW - SRC0_FS_IP_I
     *    DAI_LOW - SRC1_FS_OP_I
     *    DAI_LOW - SRC2_FS_OP_I
     *    DAI_LOW - SRC1_FS_IP_I
     */
    *pSRU_FS1 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - IDP0_FS_I
     *    DAI_LOW - SRC3_FS_IP_I
     *    DAI_LOW - PDAP_HOLD_I
     *    DAI_LOW - IDP1_FS_I
     *    DAI_LOW - SRC3_FS_OP_I
     *    DAI_LOW - IDP2_FS_I
     *    DAI_LOW - DIT_FS_I
     */
    *pSRU_FS2 = 0x3def7bdeuL;

    /* Connections:
     *    DAI_LOW - IDP3_FS_I
     *    DAI_LOW - IDP6_FS_I
     *    DAI_LOW - IDP5_FS_I
     *    DAI_LOW - IDP7_FS_I
     *    DAI_LOW - IDP4_FS_I
     */
    *pSRU_FS3 = 0x01ef7bdeuL;

    /* Connections:
     *    DAI_LOW - SPORT6_FS_I
     *    DAI_LOW - SPORT7_FS_I
     */
    *pSRU_FS4 = 0x000003deuL;

    /* Connections:
     *    DPI_LOW - SPIB_MISO_I
     *    DPI_LOW - SPIB_MOSI_I
     *    DPI_PB01_O - SPI_MOSI_I
     *    DPI_PB04_O - SPI_DS_I
     *    DPI_PB03_O - SPI_CLK_I
     *    DPI_PB02_O - SPI_MISO_I
     */
    *pSRU2_INPUT0 = 0x00021462uL;

    /* Connections:
     *    DPI_LOW - SPIB_CLK_I
     *    DPI_PB12_O - TWI_CLK_I
     *    DPI_PB10_O - UART0_RX_I
     *    DPI_PB11_O - TWI_DATA_I
     *    DPI_LOW - SPIB_DS_I
     */
    *pSRU2_INPUT1 = 0x1ac02c00uL;

    /* Connections:
     *    DPI_LOW - TIMER0_I
     *    DPI_LOW - TIMER1_I
     *    DPI_LOW - FLAG5_I
     *    DPI_LOW - FLAG4_I
     *    DPI_LOW - FLAG6_I
     */
    *pSRU2_INPUT2 = 0x00000000uL;

    /* Connections:
     *    DPI_LOW - FLAG11_I
     *    DPI_LOW - FLAG12_I
     *    DPI_LOW - FLAG10_I
     *    DPI_LOW - FLAG7_I
     *    DPI_LOW - FLAG9_I
     *    DPI_LOW - FLAG8_I
     */
    *pSRU2_INPUT3 = 0x00000000uL;

    /* Connections:
     *    DPI_LOW - MISCB2_I
     *    DPI_LOW - MISCB0_I
     *    DPI_LOW - MISCB1_I
     *    DPI_LOW - FLAG15_I
     *    DPI_LOW - FLAG13_I
     *    DPI_LOW - FLAG14_I
     */
    *pSRU2_INPUT4 = 0x00000000uL;

    /* Connections:
     *    DPI_LOW - MISCB3_I
     *    DPI_LOW - MISCB6_I
     *    DPI_LOW - MISCB7_I
     *    DPI_LOW - MISCB4_I
     *    DPI_LOW - MISCB5_I
     *    DPI_LOW - MISCB8_I
     */
    *pSRU2_INPUT5 = 0x00000000uL;

    /* Connections:
     *    SPORT0_DA_PBEN_O - DAI_PBEN01_I
     *    SPORT0_CLK_PBEN_O - DAI_PBEN03_I
     *    SPORT0_DB_PBEN_O - DAI_PBEN02_I
     *    SPORT1_DA_PBEN_O - DAI_PBEN05_I
     *    SPORT0_FS_PBEN_O - DAI_PBEN04_I
     */
    *pSRU_PBEN0 = 0x0e2482cauL;

    /* Connections:
     *    SPI_FLG0_PBEN_O - DPI_PBEN05_I
     *    DPI_LOW - DPI_PBEN04_I
     *    SPI_CLK_PBEN_O - DPI_PBEN03_I
     *    SPI_MISO_PBEN_O - DPI_PBEN02_I
     *    SPI_MOSI_PBEN_O - DPI_PBEN01_I
     */
    *pSRU2_PBEN0 = 0x0d00c28buL;

    /* Connections:
     *    SPORT2_DA_PBEN_O - DAI_PBEN09_I
     *    SPORT1_FS_PBEN_O - DAI_PBEN08_I
     *    SPORT2_DB_PBEN_O - DAI_PBEN10_I
     *    SPORT1_CLK_PBEN_O - DAI_PBEN07_I
     *    SPORT1_DB_PBEN_O - DAI_PBEN06_I
     */
    *pSRU_PBEN1 = 0x1348d30fuL;

    /* Connections:
     *    UART0_TX_PBEN_O - DPI_PBEN09_I
     *    SPI_FLG3_PBEN_O - DPI_PBEN08_I
     *    SPI_FLG2_PBEN_O - DPI_PBEN07_I
     *    SPI_FLG1_PBEN_O - DPI_PBEN06_I
     *    DPI_LOW - DPI_PBEN10_I
     */
    *pSRU2_PBEN1 = 0x002103ceuL;

    /* Connections:
     *    SPORT3_DB_PBEN_O - DAI_PBEN12_I
     *    SPORT3_DA_PBEN_O - DAI_PBEN11_I
     *    SPORT3_FS_PBEN_O - DAI_PBEN14_I
     *    SPORT3_CLK_PBEN_O - DAI_PBEN13_I
     *    SPORT4_DA_PBEN_O - DAI_PBEN15_I
     */
    *pSRU_PBEN2 = 0x1a5545d6uL;

    /* Connections:
     *    TWI_CLK_PBEN_O - DPI_PBEN12_I
     *    TIMER0_PBEN_O - DPI_PBEN13_I
     *    TIMER1_PBEN_O - DPI_PBEN14_I
     *    TWI_DATA_PBEN_O - DPI_PBEN11_I
     */
    *pSRU2_PBEN2 = 0x00185964uL;

    /* Connections:
     *    SPORT5_CLK_PBEN_O - DAI_PBEN19_I
     *    SPORT5_FS_PBEN_O - DAI_PBEN20_I
     *    SPORT4_DB_PBEN_O - DAI_PBEN16_I
     *    SPORT5_DB_PBEN_O - DAI_PBEN18_I
     *    SPORT5_DA_PBEN_O - DAI_PBEN17_I
     */
    *pSRU_PBEN3 = 0x1d71f79buL;

    /* Connections:
     *    SPORT0_DA_O - DAI_PB01_I
     *    SPORT0_DB_O - DAI_PB02_I
     *    SPORT0_CLK_O - DAI_PB03_I
     *    SPORT0_FS_O - DAI_PB04_I
     */
    *pSRU_PIN0 = 0x04c80a94uL;

    /* Connections:
     *    DPI_LOW - DPI_PB04_I
     *    SPI_FLG0_O - DPI_PB05_I
     *    SPI_MISO_O - DPI_PB02_I
     *    SPI_CLK_O - DPI_PB03_I
     *    SPI_MOSI_O - DPI_PB01_I
     */
    *pSRU2_PIN0 = 0x18017556uL;

    /* Connections:
     *    SPORT1_DA_O - DAI_PB05_I
     *    SPORT1_DB_O - DAI_PB06_I
     *    SPORT1_CLK_O - DAI_PB07_I
     *    SPORT1_FS_O - DAI_PB08_I
     */
    *pSRU_PIN1 = 0x04e84b96uL;

    /* Connections:
     *    SPI_FLG3_O - DPI_PB08_I
     *    UART0_TX_O - DPI_PB09_I
     *    SPI_FLG1_O - DPI_PB06_I
     *    SPI_FLG2_O - DPI_PB07_I
     *    DPI_LOW - DPI_PB10_I
     */
    *pSRU2_PIN1 = 0x004db699uL;

    /* Connections:
     *    SPORT3_DB_O - DAI_PB12_I
     *    SPORT2_DB_O - DAI_PB10_I
     *    SPORT3_DA_O - DAI_PB11_I
     *    SPORT2_DA_O - DAI_PB09_I
     */
    *pSRU_PIN2 = 0x03668c98uL;

    /* Connections:
     *    TIMER1_O - DPI_PB14_I
     *    TIMER0_O - DPI_PB13_I
     *    DPI_LOW - DPI_PB12_I
     *    DPI_LOW - DPI_PB11_I
     */
    *pSRU2_PIN2 = 0x00450000uL;

    /* Connections:
     *    SPORT4_DB_O - DAI_PB16_I
     *    SPORT3_CLK_O - DAI_PB13_I
     *    SPORT3_FS_O - DAI_PB14_I
     *    SPORT4_DA_O - DAI_PB15_I
     */
    *pSRU_PIN3 = 0x03a714a3uL;

    /* Connections:
     *    DAI_PB01_O - INV_DAI_PB20_I
     *    SPORT5_DA_O - DAI_PB17_I
     *    SPORT5_DB_O - DAI_PB18_I
     *    SPORT5_CLK_O - DAI_PB19_I
     *    SPORT5_FS_O - DAI_PB20_I
     *    DAI_PB01_O - INV_DAI_PB19_I
     */
    *pSRU_PIN4 = 0x05694f9euL;
#endif /* defined(SRU_FULL_INIT) */

    return 0;
}

