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
 * @file    radio.h
 * @brief   Generic radio handlers
 *
 * @version 1.2
 * @date    Jul 17 2017
 * @author  CMOSTEK R@D
 */
 
#ifndef __RADIO_H
#define __RADIO_H

#include "cmt2300a.h"

#ifdef __cplusplus 
extern "C" { 
#endif

int StartTx(void);
int StartRx(void);

/* Recompute the Frequency Bank (0x18-0x1F) for a target carrier in Hz.
 * Returns 1 if the frequency is achievable, 0 otherwise (bank left unchanged).
 * The new values are written to the chip on the next RF_Init()/StartTx(). */
int RF_SetFrequency(uint32_t freq_hz);

#ifdef __cplusplus 
} 
#endif

#endif
