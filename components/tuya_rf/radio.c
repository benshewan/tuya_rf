/*
 * THE FOLLOWING FIRMWARE IS PROVIDED: (1) "AS IS" WITH NO WARRANTY; AND
 * (2)TO ENABLE ACCESS TO CODING INFORMATION TO GUIDE AND FACILITATE CUSTOMER.
 * CONSEQUENTLY, CMOSTEK SHALL NOT BE HELD LIABLE FOR ANY DIRECT, INDIRECT OR
 * CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE CONTENT
 * OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING INFORMATION
 * CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * Copyright (C) CMOSTEK SZ.
 */

/*!
 * @file    radio.c
 * @brief   Generic radio handlers
 *
 * @version 1.2
 * @date    Jul 17 2017
 * @author  CMOSTEK R@D
 */

#include "radio.h"
#include "cmt2300a_hal.h"
#include "cmt2300a_params_captured.h"
#include <stddef.h>
#include <stdint.h>

#define RF_CRYSTAL_HZ 26000000ULL

/* Default 433.92 MHz Frequency Bank; retuned at runtime by RF_SetFrequency(). */
uint8_t g_cmt2300aFrequencyBank[CMT2300A_FREQUENCY_BANK_SIZE] = {
    /* 0x18 RX N                              */ 0x42,
    /* 0x19 RX K[7:0]                         */ 0x71,
    /* 0x1A RX K[15:8]                        */ 0xCE,
    /* 0x1B PALDO_SEL|DIVX_CODE(001)|RX K hi  */ 0x1C,
    /* 0x1C TX N                              */ 0x42,
    /* 0x1D TX K[7:0]                         */ 0x5B,
    /* 0x1E TX K[15:8]                        */ 0x1C,
    /* 0x1F FSK_SWT|VCO_BANK(001)|TX K hi     */ 0x1C,
};

int RF_SetFrequency(uint32_t freq_hz)
{
    static const struct { uint8_t divider; uint8_t code; } candidates[] = {
        {4, 1}, {6, 5}, {8, 2}, {12, 3}, {2, 0}
    };
    uint8_t divider = 0, divx_code = 0, vco_bank = 0;
    size_t i;
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        uint64_t fvco = (uint64_t) freq_hz * candidates[i].divider;
        if (fvco >= 1680000000ULL && fvco <= 2040000000ULL) {
            divider = candidates[i].divider; divx_code = candidates[i].code; vco_bank = 1; break;
        }
    }
    if (divider == 0) {
        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            uint64_t fvco = (uint64_t) freq_hz * candidates[i].divider;
            if (fvco >= 1516000000ULL && fvco <= 1680000000ULL) {
                divider = candidates[i].divider; divx_code = candidates[i].code; vco_bank = 6; break;
            }
        }
    }
    if (divider == 0)
        return 0;

    /* TX: f_vco = freq * divider ; N = floor(f_vco / xtal) ; K = round(frac * 2^20) */
    uint64_t fvco_tx = (uint64_t) freq_hz * divider;
    uint32_t n_tx = (uint32_t) (fvco_tx / RF_CRYSTAL_HZ);
    uint64_t rem_tx = fvco_tx - (uint64_t) n_tx * RF_CRYSTAL_HZ;
    uint32_t k_tx = (uint32_t) ((rem_tx * (1ULL << 20) + RF_CRYSTAL_HZ / 2) / RF_CRYSTAL_HZ);

    /* RX: low-IF offset f_xtal/92 (only relevant if the receiver is enabled) */
    uint64_t fvco_rx = ((uint64_t) freq_hz + RF_CRYSTAL_HZ / 92) * divider;
    uint32_t n_rx = (uint32_t) (fvco_rx / RF_CRYSTAL_HZ);
    uint64_t rem_rx = fvco_rx - (uint64_t) n_rx * RF_CRYSTAL_HZ;
    uint32_t k_rx = (uint32_t) ((rem_rx * (1ULL << 20) + RF_CRYSTAL_HZ / 2) / RF_CRYSTAL_HZ);

    g_cmt2300aFrequencyBank[0] = (uint8_t) n_rx;
    g_cmt2300aFrequencyBank[1] = (uint8_t) (k_rx & 0xFF);
    g_cmt2300aFrequencyBank[2] = (uint8_t) ((k_rx >> 8) & 0xFF);
    g_cmt2300aFrequencyBank[3] = (g_cmt2300aFrequencyBank[3] & 0x80)
                               | (uint8_t) (divx_code << 4)
                               | (uint8_t) ((k_rx >> 16) & 0x0F);
    g_cmt2300aFrequencyBank[4] = (uint8_t) n_tx;
    g_cmt2300aFrequencyBank[5] = (uint8_t) (k_tx & 0xFF);
    g_cmt2300aFrequencyBank[6] = (uint8_t) ((k_tx >> 8) & 0xFF);
    g_cmt2300aFrequencyBank[7] = (g_cmt2300aFrequencyBank[7] & 0x80)
                               | (uint8_t) (vco_bank << 4)
                               | (uint8_t) ((k_tx >> 16) & 0x0F);
    return 1;
}


int RF_Init(void)
{
    uint8_t tmp;
    
    CMT2300A_InitGpio();
	CMT2300A_Init();
    
    /* Config registers */
    CMT2300A_ConfigRegBank(CMT2300A_CMT_BANK_ADDR       , g_cmt2300aCmtBank       , CMT2300A_CMT_BANK_SIZE       );
    CMT2300A_ConfigRegBank(CMT2300A_SYSTEM_BANK_ADDR    , g_cmt2300aSystemBank    , CMT2300A_SYSTEM_BANK_SIZE    );
    CMT2300A_ConfigRegBank(CMT2300A_FREQUENCY_BANK_ADDR , g_cmt2300aFrequencyBank , CMT2300A_FREQUENCY_BANK_SIZE );
    CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR , g_cmt2300aDataRateBank  , CMT2300A_DATA_RATE_BANK_SIZE );
    CMT2300A_ConfigRegBank(CMT2300A_BASEBAND_BANK_ADDR  , g_cmt2300aBasebandBank  , CMT2300A_BASEBAND_BANK_SIZE  );
    CMT2300A_ConfigRegBank(CMT2300A_TX_BANK_ADDR        , g_cmt2300aTxBank        , CMT2300A_TX_BANK_SIZE        );
    
    // xosc_aac_code[2:0] = 2
    tmp = (~0x07) & CMT2300A_ReadReg(CMT2300A_CUS_CMT10);
    CMT2300A_WriteReg(CMT2300A_CUS_CMT10, tmp|0x02);
    
	if(false==CMT2300A_IsExist()) 
	{
        //CMT2300A not found!
        return -1;
    }
    else
	{
        return 0;
    }
}

int StartTx() {
     if (RF_Init()!=0) {
        return 1;
	}
    CMT2300A_WriteReg(CMT2300A_CUS_SYS2,0); //???? 
    CMT2300A_ConfigGpio(CMT2300A_GPIO1_SEL_DOUT | CMT2300A_GPIO3_SEL_DIN | CMT2300A_GPIO2_SEL_INT2);
	CMT2300A_EnableTxDin(true);    
	CMT2300A_ConfigTxDin(CMT2300A_TX_DIN_SEL_GPIO1);
	CMT2300A_EnableTxDinInvert(false); 
	CMT2300A_GoSleep();
	CMT2300A_GoStby();
	if (CMT2300A_GoTx()) {
        return 0;
    } else {
        return 2;
    }
}
 

int StartRx() {
    if (RF_Init()!=0) {
        return 1;
	}

    CMT2300A_ConfigGpio (CMT2300A_GPIO1_SEL_INT1 | CMT2300A_GPIO2_SEL_INT2 | CMT2300A_GPIO3_SEL_DOUT);
	CMT2300A_ConfigInterrupt(CMT2300A_INT_SEL_TX_DONE, CMT2300A_INT_SEL_PKT_OK);
	CMT2300A_EnableInterrupt(CMT2300A_MASK_TX_DONE_EN | CMT2300A_MASK_PKT_DONE_EN);

	CMT2300A_WriteReg(CMT2300A_CUS_SYS2 , 0);

	CMT2300A_EnableFifoMerge(true);

	CMT2300A_WriteReg(CMT2300A_CUS_PKT29, 0x20); 

	CMT2300A_GoSleep();
	CMT2300A_GoStby();
	CMT2300A_ConfigGpio (CMT2300A_GPIO1_SEL_DCLK | CMT2300A_GPIO2_SEL_DOUT | CMT2300A_GPIO3_SEL_INT2);
	CMT2300A_ConfigInterrupt(CMT2300A_INT_SEL_SYNC_OK | CMT2300A_INT_SEL_SL_TMO, CMT2300A_INT_SEL_PKT_OK);
    CMT2300A_EnableFifoMerge(true);
	CMT2300A_ClearInterruptFlags();
	CMT2300A_ClearRxFifo();
	
    CMT2300A_GoRx();

	//CMT2300_IsExist()
	int rssi=CMT2300A_GetRssiDBm();
	//stream->print("rssi dbm ");
	//stream->println(rssi);

	CMT2300A_ClearInterruptFlags();
    return 0;
}