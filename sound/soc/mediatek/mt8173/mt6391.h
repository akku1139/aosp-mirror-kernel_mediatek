/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MT6391_H__
#define __MT6391_H__

#include <linux/types.h>

/*  digital pmic register definition */
#define AFE_PMICDIG_AUDIO_BASE      (0x4000)
#define AFE_UL_DL_CON0              (AFE_PMICDIG_AUDIO_BASE+0x0000)
#define AFE_DL_SRC2_CON0_H          (AFE_PMICDIG_AUDIO_BASE+0x0002)
#define AFE_DL_SRC2_CON0_L          (AFE_PMICDIG_AUDIO_BASE+0x0004)
#define AFE_DL_SDM_CON0             (AFE_PMICDIG_AUDIO_BASE + 0x0006)
#define AFE_DL_SDM_CON1             (AFE_PMICDIG_AUDIO_BASE + 0x0008)
#define AFE_UL_SRC_CON0_H           (AFE_PMICDIG_AUDIO_BASE + 0x000A)
#define AFE_UL_SRC_CON0_L           (AFE_PMICDIG_AUDIO_BASE + 0x000C)
#define AFE_UL_SRC_CON1_H           (AFE_PMICDIG_AUDIO_BASE + 0x000E)
#define AFE_UL_SRC_CON1_L           (AFE_PMICDIG_AUDIO_BASE + 0x0010)
#define ANA_AFE_TOP_CON0            (AFE_PMICDIG_AUDIO_BASE + 0x0012)
#define ANA_AUDIO_TOP_CON0          (AFE_PMICDIG_AUDIO_BASE + 0x0014)
#define AFE_DL_SRC_MON0             (AFE_PMICDIG_AUDIO_BASE + 0x0016)
#define AFE_DL_SDM_TEST0            (AFE_PMICDIG_AUDIO_BASE + 0x0018)
#define AFE_MON_DEBUG0              (AFE_PMICDIG_AUDIO_BASE + 0x001A)
#define AFUNC_AUD_CON0              (AFE_PMICDIG_AUDIO_BASE + 0x001C)
#define AFUNC_AUD_CON1              (AFE_PMICDIG_AUDIO_BASE + 0x001E)
#define AFUNC_AUD_CON2              (AFE_PMICDIG_AUDIO_BASE + 0x0020)
#define AFUNC_AUD_CON3              (AFE_PMICDIG_AUDIO_BASE + 0x0022)
#define AFUNC_AUD_CON4              (AFE_PMICDIG_AUDIO_BASE + 0x0024)
#define AFUNC_AUD_MON0              (AFE_PMICDIG_AUDIO_BASE + 0x0026)
#define AFUNC_AUD_MON1              (AFE_PMICDIG_AUDIO_BASE + 0x0028)
#define AUDRC_TUNE_MON0             (AFE_PMICDIG_AUDIO_BASE + 0x002A)
#define AFE_UP8X_FIFO_CFG0          (AFE_PMICDIG_AUDIO_BASE + 0x002C)
#define AFE_UP8X_FIFO_LOG_MON0      (AFE_PMICDIG_AUDIO_BASE + 0x002E)
#define AFE_UP8X_FIFO_LOG_MON1      (AFE_PMICDIG_AUDIO_BASE + 0x0030)
#define AFE_DL_DC_COMP_CFG0         (AFE_PMICDIG_AUDIO_BASE + 0x0032)
#define AFE_DL_DC_COMP_CFG1         (AFE_PMICDIG_AUDIO_BASE + 0x0034)
#define AFE_DL_DC_COMP_CFG2         (AFE_PMICDIG_AUDIO_BASE + 0x0036)
#define AFE_PMIC_NEWIF_CFG0         (AFE_PMICDIG_AUDIO_BASE + 0x0038)
#define AFE_PMIC_NEWIF_CFG1         (AFE_PMICDIG_AUDIO_BASE + 0x003A)
#define AFE_PMIC_NEWIF_CFG2         (AFE_PMICDIG_AUDIO_BASE + 0x003C)
#define AFE_PMIC_NEWIF_CFG3         (AFE_PMICDIG_AUDIO_BASE + 0x003E)
#define AFE_SGEN_CFG0               (AFE_PMICDIG_AUDIO_BASE + 0x0040)
#define AFE_SGEN_CFG1               (AFE_PMICDIG_AUDIO_BASE + 0x0042)


void mt6391_set_reg(uint32_t offset, uint32_t value, uint32_t mask);
uint32_t mt6391_get_reg(uint32_t offset);
void mt6391_debug_log_print(void);

#endif
