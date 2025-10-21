/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2025 Richard Genoud, Bootlin <richard.genoud@bootlin.com>
 */

#ifndef _SUNXI_PWM_SUN50I_H616_H
#define _SUNXI_PWM_

#ifndef __ASSEMBLY__
#include <linux/bitops.h>
#endif

/* PWM IRQ Enable Register */
#define SUNXI_PWM_IER			0x0

/* PWM IRQ Status Register */
#define SUNXI_PWM_ISR			0x4

/* PWM Capture IRQ Enable Register */
#define SUNXI_PWM_CIER			0x10

/* PWM Capture IRQ Status Register */
#define SUNXI_PWM_CISR			0x14

/* PWM01 Clock Configuration Register */
#define SUNXI_PWM_CCR01R		0x20

/* PWM23 Clock Configuration Register */
#define SUNXI_PWM_CCR23R		0x24

/* PWM45 Clock Configuration Register */
#define SUNXI_PWM_CCR45R		0x28

/* PWM01 Dead Zone Control Register */
#define SUNXI_PWM_DZCR01R		0x30

/* PWM23 Dead Zone Control Register */
#define SUNXI_PWM_DZCR23R		0x34

/* PWM45 Dead Zone Control Register */
#define SUNXI_PWM_DZCR45R		0x38

/* PWM Enable Register */
#define SUNXI_PWM_ENR			0x40

/* PWM Capture Enable Register */
#define SUNXI_PWM_CER			0x44

/* PWM Control Register */
#define SUNXI_PWM_CTRL_REG(x)		(0x60 + (x) * 0x20)

/* PWM Period Register */
#define SUNXI_PWM_PERIOD_REG(x)		(0x64 + (x) * 0x20)

/* PWM Count Register */
#define SUNXI_PWM_COUNT_REG(x)		(0x68 + (x) * 0x20)

/* PWM Capture Control Register */
#define SUNXI_PWM_CCR(x)		(0x6c + (x) * 0x20)

/* PWM Capture Rise Lock Register */
#define SUNXI_PWM_CRLR(x)		(0x70 + (x) * 0x20)

/* PWM Capture Fall Lock Register */
#define SUNXI_PWM_CFLR(x)		(0x74 + (x) * 0x20)


/* PWM Period bit field */
#define PWM_ENTIRE_CYCLE(x)		(((x) - 1) << 16)
#define PWM_ACTIVE_CYCLE(x)		((x) - 1)

/* PWM Clock Configuration bit field */
#define PMW_CLK_DIV_M(x)		(x)
#define PWM_CLK_GATING			BIT(4)
#define PWM23_CLK_SRC_BYPASS_TO_PWM2	BIT(5)
#define PWM45_CLK_SRC_BYPASS_TO_PWM4	BIT(5)
#define PWM01_CLK_SRC_BYPASS_TO_PWM1	BIT(6)
#define PWM23_CLK_SRC_BYPASS_TO_PWM3	BIT(6)
#define PWM45_CLK_SRC_BYPASS_TO_PWM5	BIT(6)
#define PWM_CLK_SRC_APB1		BIT(7)

/* PWM Enable bit field */
#define PWM_ENABLE(x)			BIT(x)

#endif
