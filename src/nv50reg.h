/*
 * Copyright 2007 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NV50REG_H_
#define __NV50REG_H_

/* Bit 28 also does something, although i don't know what. */
#define NV50_CONNECTOR_HOTPLUG_INTR	0x0000E050
	#define NV50_CONNECTOR_HOTPLUG_INTR_PLUG_I2C0		(1 << 0)
	#define NV50_CONNECTOR_HOTPLUG_INTR_PLUG_I2C1		(2 << 0)
	#define NV50_CONNECTOR_HOTPLUG_INTR_UNPLUG_I2C0	(1 << 16)
	#define NV50_CONNECTOR_HOTPLUG_INTR_UNPLUG_I2C1	(2 << 16)

/* Writing 0x7FFF7FFF seems to be the way to acknowledge an interrupt. */
/* There is also bit 7 and bit 23, but i also don't know what they do. */
#define NV50_CONNECTOR_HOTPLUG_CTRL		0x0000E054
	#define NV50_CONNECTOR_HOTPLUG_CTRL_PLUG_I2C0			(1 << 0)
	#define NV50_CONNECTOR_HOTPLUG_CTRL_PLUG_I2C1			(2 << 0)
	#define NV50_CONNECTOR_HOTPLUG_CTRL_UNPLUG_I2C0		(1 << 16)
	#define NV50_CONNECTOR_HOTPLUG_CTRL_UNPLUG_I2C1		(2 << 16)

/* This works for DVI->VGA adapters as well, providing the adapter supports it. */
/* It's unknown if bits exist for i2c port > 1. */
/* Renamed to avoid confusion with other instances of CONNECTED. */
#define NV50_CONNECTOR_HOTPLUG_STATE			0x0000E104
	#define NV50_CONNECTOR_HOTPLUG_STATE_I2C0_DETECT_PIN	(1 << 2)
	#define NV50_CONNECTOR_HOTPLUG_STATE_I2C1_DETECT_PIN	(1 << 6)

#define NV50_PCONNECTOR_I2C_PORT_0			0x0000e138
#define NV50_PCONNECTOR_I2C_PORT_1			0x0000e150
#define NV50_PCONNECTOR_I2C_PORT_2			0x0000e168
#define NV50_PCONNECTOR_I2C_PORT_3			0x0000e180
#define NV50_PCONNECTOR_I2C_PORT_4			0x0000e240
#define NV50_PCONNECTOR_I2C_PORT_5			0x0000e258

/* 0x00610024 is the state register to read, all it's bits also exist in 0x0061002C in the form of interrupt switches. */
#define NV50_DISPLAY_SUPERVISOR		0x00610024
	#define NV50_DISPLAY_SUPERVISOR_CRTC0			(1 << 2)
	#define NV50_DISPLAY_SUPERVISOR_CRTC1			(2 << 2)
	#define NV50_DISPLAY_SUPERVISOR_CRTCn			(3 << 2)
	#define NV50_DISPLAY_SUPERVISOR_CLK_MASK		(7 << 4)
	#define NV50_DISPLAY_SUPERVISOR_CLK_UPDATE		(2 << 4)

/* Two vblank interrupts arrive per blanking period, it could be rise and fall, i do not know. */
/* If a vblank interrupt arrives, check NV50_DISPLAY_SUPERVISOR, it will show for which crtc (or both) it is. */
/* Note that one crtc bit will always show, maybe it's updated when a vblank occurs? */
/* Once modesetting goes into the kernel, we can ditch NV50CheckWriteVClk() and do it with interrupts. */
/* Up until then, realise that most interrupts are not handled properly yet (and will stall your machine). */
/* Bit 8 and 9 also exist for sure, but their purpose is unknown. */
#define NV50_DISPLAY_SUPERVISOR_INTR	0x0061002C
	#define NV50_DISPLAY_INTR_VBLANK_CRTC0		(1 << 2)
	#define NV50_DISPLAY_INTR_VBLANK_CRTC1		(1 << 3)
	#define NV50_DISPLAY_INTR_UNK1		(1 << 4)
	#define NV50_DISPLAY_INTR_CLK_UPDATE	(2 << 4)
	#define NV50_DISPLAY_INTR_UNK4		(4 << 4)

#define NV50_DISPLAY_UNK30_CTRL	0x00610030
	#define NV50_DISPLAY_UNK30_CTRL_UPDATE_VCLK0	(1 << 9)
	#define NV50_DISPLAY_UNK30_CTRL_UPDATE_VCLK1	(1 << 10)
	#define NV50_DISPLAY_UNK30_CTRL_PENDING			(1 << 31)

#define NV50_DISPLAY_UNK50_CTRL	0x00610050
	#define NV50_DISPLAY_UNK50_CTRL_CRTC0_ACTIVE		(2 << 0)
	#define NV50_DISPLAY_UNK50_CTRL_CRTC0_MASK		(3 << 0)
	#define NV50_DISPLAY_UNK50_CTRL_CRTC1_ACTIVE		(2 << 8)
	#define NV50_DISPLAY_UNK50_CTRL_CRTC1_MASK		(3 << 8)

/* I really don't know what this does, except that it's only revelant at start. */
#define NV50_DISPLAY_UNK200_CTRL	0x00610200

/* bit3 always activates itself, and bit 4 is some kind of switch. */
#define NV50_CRTC0_CURSOR_CTRL2	0x00610270
	#define NV50_CRTC_CURSOR_CTRL2_ON				(1 << 0)
	#define NV50_CRTC_CURSOR_CTRL2_OFF				(0 << 0)
	#define NV50_CRTC_CURSOR_CTRL2_STATUS_MASK		(3 << 16)
	#define NV50_CRTC_CURSOR_CTRL2_STATUS_ACTIVE		(1 << 16)
#define NV50_CRTC1_CURSOR_CTRL2	0x00610280

#define NV50_DISPLAY_CTRL_STATE		0x00610300
	#define NV50_DISPLAY_CTRL_STATE_DISABLE		(0 << 0)
	#define NV50_DISPLAY_CTRL_STATE_ENABLE		(1 << 0)
	#define NV50_DISPLAY_CTRL_STATE_PENDING		(1 << 31)
#define NV50_DISPLAY_CTRL_VAL		0x00610304

#define NV50_DISPLAY_UNK_380		0x00610380
/* Clamped to 256 MiB */
#define NV50_DISPLAY_RAM_AMOUNT	0x00610384
#define NV50_DISPLAY_UNK_388		0x00610388
#define NV50_DISPLAY_UNK_38C		0x0061038C

/* The registers in this range are normally accessed through display commands, with an offset of 0x540 for crtc1. */
/* They also seem duplicated into the next register as well. */
#define NV50_CRTC0_CLUT_MODE_VAL					0x00610A24
#define NV50_CRTC0_SCALE_CTRL_VAL					0x00610A50
#define NV50_CRTC0_CURSOR_CTRL_VAL					0x00610A58
#define NV50_CRTC0_DEPTH_VAL						0x00610AC8
#define NV50_CRTC0_CLOCK_VAL						0x00610AD0
#define NV50_CRTC0_COLOR_CTRL_VAL					0x00610AE0
#define NV50_CRTC0_SYNC_START_TO_BLANK_END_VAL		0x00610AE8
#define NV50_CRTC0_MODE_UNK1_VAL					0x00610AF0
#define NV50_CRTC0_DISPLAY_TOTAL_VAL				0x00610AF8
#define NV50_CRTC0_SYNC_DURATION_VAL				0x00610B00
/* For some reason this displayed the maximum framebuffer size for crtc0/dfp. */
/* It was correct for crtc1/afp. */
#define NV50_CRTC0_FB_SIZE_VAL						0x00610B18
#define NV50_CRTC0_FB_PITCH_VAL						0x00610B20
#define NV50_CRTC0_FB_POS_VAL						0x00610B28
#define NV50_CRTC0_SCALE_CENTER_OFFSET_VAL			0x00610B38
#define NV50_CRTC0_REAL_RES_VAL					0x00610B40
/* I can't be 100% about the order of these two, as setting them differently locks up the card. */
#define NV50_CRTC0_SCALE_RES1_VAL					0x00610B48
#define NV50_CRTC0_SCALE_RES2_VAL					0x00610B50

/* Some registers are based on extrapolation. */
#define NV50_DAC0_MODE_CTRL_VAL					0x00610B58
#define NV50_DAC1_MODE_CTRL_VAL					0x00610B60
#define NV50_DAC2_MODE_CTRL_VAL					0x00610B68
#define NV50_SOR0_MODE_CTRL_VAL					0x00610B70
#define NV50_SOR1_MODE_CTRL_VAL					0x00610B78

#define NV50_DAC0_MODE_CTRL2_VAL					0x00610BDC
#define NV50_DAC1_MODE_CTRL2_VAL					0x00610BE4
#define NV50_DAC2_MODE_CTRL2_VAL					0x00610BEC

#define NV50_CRTC1_CLUT_MODE_VAL					0x00610F64
#define NV50_CRTC1_SCALE_CTRL_VAL					0x00610F90
#define NV50_CRTC1_CURSOR_CTRL_VAL					0x00610F98
#define NV50_CRTC1_DEPTH_VAL						0x00611008
#define NV50_CRTC1_CLOCK_VAL						0x00611010
#define NV50_CRTC1_COLOR_CTRL_VAL					0x00611020
#define NV50_CRTC1_SYNC_START_TO_BLANK_END_VAL		0x00611028
#define NV50_CRTC1_MODE_UNK1_VAL					0x00611030
#define NV50_CRTC1_DISPLAY_TOTAL_VAL				0x00611038
#define NV50_CRTC1_SYNC_DURATION_VAL				0x00611040
#define NV50_CRTC1_FB_SIZE_VAL						0x00611058
#define NV50_CRTC1_FB_PITCH_VAL						0x00611060
#define NV50_CRTC1_FB_POS_VAL						0x00611068
#define NV50_CRTC1_SCALE_CENTER_OFFSET_VAL			0x00611078
#define NV50_CRTC1_REAL_RES_VAL					0x00611080
/* I can't be 100% about the order of these two, as setting them differently locks up the card. */
#define NV50_CRTC1_SCALE_RES1_VAL					0x00611088
#define NV50_CRTC1_SCALE_RES2_VAL					0x00611090

/* These CLK_CTRL names are a bit of a guess, i do have my reasons though. */
/* These connected indicators exist for crtc, dac and sor. */
#define NV50_CRTC0_CLK_CTRL1	0x00614100
	#define NV50_CRTC_CLK_CTRL1_CONNECTED		(3 << 9)
/* These are probably redrirected from 0x4000 range (very similar regs to nv40, maybe different order) */
#define NV50_CRTC0_VPLL_A		0x00614104
#define NV50_CRTC0_VPLL_B		0x00614108
#define NV50_CRTC0_CLK_CTRL2	0x00614200

/* These control some special modes, like dual link dvi, maybe they need another name? */
#define NV50_DAC0_CLK_CTRL2	0x00614280
#define NV50_SOR0_CLK_CTRL2	0x00614300

#define NV50_CRTC1_CLK_CTRL1	0x00614900
#define NV50_CRTC1_VPLL_A		0x00614904
#define NV50_CRTC1_VPLL_B		0x00614908
#define NV50_CRTC1_CLK_CTRL2	0x00614A00

#define NV50_DAC1_CLK_CTRL2	0x00614A80
#define NV50_SOR1_CLK_CTRL2	0x00614B00

#define NV50_DAC2_CLK_CTRL2	0x00615280

#define NV50_DAC0_DPMS_CTRL	0x0061A004
	#define	NV50_DAC_DPMS_CTRL_HSYNC_OFF		(1 << 0)
	#define	NV50_DAC_DPMS_CTRL_VSYNC_OFF		(1 << 2)
	#define	NV50_DAC_DPMS_CTRL_BLANKED		(1 << 4)
	#define	NV50_DAC_DPMS_CTRL_OFF			(1 << 6)
	/* Some cards also use bit 22, why exactly is unknown. */
	/* It seems that 1, 4 and 5 are present at bit0. bit4. bit16, bit20. */
	/* No idea what the symmetry means precisely. */
	#define	NV50_DAC_DPMS_CTRL_DEFAULT_STATE	(21 << 16)
	#define	NV50_DAC_DPMS_CTRL_PENDING		(1 << 31)
#define NV50_DAC0_LOAD_CTRL	0x0061A00C
	#define	NV50_DAC_LOAD_CTRL_ACTIVE			(1 << 20)
	#define	NV50_DAC_LOAD_CTRL_PRESENT		(7 << 27)
	/* this a bit of a guess, as load detect is very fast */
	#define	NV50_DAC_LOAD_CTRL_DONE			(1 << 31)
/* These connected indicators exist for crtc, dac and sor. */
/* The upper 4 bits seem to be some kind indicator. */
/* The purpose of bit 1 is unknown, maybe some kind of reset? */
#define NV50_DAC0_CLK_CTRL1	0x0061A010
	#define NV50_DAC_CLK_CTRL1_CONNECTED		(3 << 9)
#define NV50_DAC1_DPMS_CTRL	0x0061A804
#define NV50_DAC1_LOAD_CTRL	0x0061A80C
#define NV50_DAC1_CLK_CTRL1	0x0061A810
#define NV50_DAC2_DPMS_CTRL	0x0061B004
#define NV50_DAC2_LOAD_CTRL	0x0061B00C
#define NV50_DAC2_CLK_CTRL1	0x0061B010

/* both SOR_DPMS and DAC_DPMS have a bit28, whose purpose is unknown atm. */
#define NV50_SOR0_DPMS_CTRL	0x0061C004
	#define	NV50_SOR_DPMS_CTRL_MODE_ON		(1 << 0)
	#define	NV50_SOR_DPMS_CTRL_PENDING		(1 << 31)
/* These connected indicators exist for crtc, dac and sor. */
/* I don't know what bit27 does, it doesn't seem extremely important. */
#define NV50_SOR0_CLK_CTRL1	0x0061C008
	#define NV50_SOR_CLK_CTRL1_CONNECTED		(3 << 9)
/* Seems to be a default state, nothing that can RE'd in great detail. */
#define NV50_SOR0_UNK00C		0x0061C00C
#define NV50_SOR0_UNK010		0x0061C010
#define NV50_SOR0_UNK014		0x0061C014
#define NV50_SOR0_UNK018		0x0061C018

#define NV50_SOR0_DPMS_STATE	0x0061C030
	#define NV50_SOR_DPMS_STATE_ACTIVE			(3 << 16) /* this does not show if DAC is active */
	#define NV50_SOR_DPMS_STATE_BLANKED		(8 << 16)
	#define NV50_SOR_DPMS_STATE_WAIT			(1 << 28)

#define NV50_SOR1_DPMS_CTRL	0x0061C804
#define NV50_SOR1_CLK_CTRL1	0x0061C808
/* Seems to be a default state, nothing that can RE'd in any great detail. */
#define NV50_SOR1_UNK00C		0x0061C80C
#define NV50_SOR1_UNK010		0x0061C810
#define NV50_SOR1_UNK014		0x0061C814
#define NV50_SOR1_UNK018		0x0061C818

#define NV50_SOR1_DPMS_STATE	0x0061C830

/* A few things seem to exist in the 0x0064XXXX range, but not much. */
/* Each of these corresponds to a range in 0x006102XX. */
/* The blob writes zero to these regs. */
/* 0x00610200-0x0061020C, 0x00610200 seems special from all the rest. */
#define NV50_UNK_640000				0x00640000
/* 0x00610210-0x0061021C */
#define NV50_UNK_641000				0x00641000
/* 0x00610220-0x0061022C */
/* Seems tv-out related somehow, the other two show up always. */
#define NV50_UNK_642000				0x00642000
/* 0x00610230-0x0061023C and 0x00610240-0x0061024C seem to be similar. */
/* I think the correlation goes for all 0x0064X000, up to and including 6. */

/* Write 0 to process the new position, seem to be write only registers. */
#define NV50_CRTC0_CURSOR_POS_CTRL		0x00647080
#define NV50_CRTC0_CURSOR_POS			0x00647084
#define NV50_CRTC1_CURSOR_POS_CTRL		0x00648080
#define NV50_CRTC1_CURSOR_POS			0x00648084

/* These things below are so called "commands" */
#define NV50_UPDATE_DISPLAY		0x80
#define NV50_UNK84				0x84
#define NV50_UNK88				0x88

#define NV50_DAC0_MODE_CTRL		0x400
	#define NV50_DAC_MODE_CTRL_OFF			(0 << 0)
	#define NV50_DAC_MODE_CTRL_CRTC0			(1 << 0)
	#define NV50_DAC_MODE_CTRL_CRTC1			(1 << 1)
#define NV50_DAC1_MODE_CTRL		0x480
#define NV50_DAC2_MODE_CTRL		0x500

#define NV50_DAC0_MODE_CTRL2		0x404
	#define NV50_DAC_MODE_CTRL2_NHSYNC			(1 << 0)
	#define NV50_DAC_MODE_CTRL2_NVSYNC			(2 << 0)
#define NV50_DAC1_MODE_CTRL2		0x484
#define NV50_DAC2_MODE_CTRL2		0x504

#define NV50_SOR0_MODE_CTRL		0x600
	#define NV50_SOR_MODE_CTRL_OFF			(0 << 0)
	#define NV50_SOR_MODE_CTRL_CRTC0			(1 << 0)
	#define NV50_SOR_MODE_CTRL_CRTC1			(1 << 1)
	#define NV50_SOR_MODE_CTRL_LVDS			(0 << 8)
	#define NV50_SOR_MODE_CTRL_TMDS			(1 << 8)
	#define NV50_SOR_MODE_CTRL_TMDS_DUAL_LINK	(4 << 8)
	#define NV50_SOR_MODE_CTRL_NHSYNC			(1 << 12)
	#define NV50_SOR_MODE_CTRL_NVSYNC			(2 << 12)
#define NV50_SOR1_MODE_CTRL		0x640

#define NV50_CRTC0_UNK800			0x800
#define NV50_CRTC0_CLOCK			0x804
#define NV50_CRTC0_INTERLACE		0x808

/* 0x810 is a reasonable guess, nothing more. */
#define NV50_CRTC0_DISPLAY_START				0x810
#define NV50_CRTC0_DISPLAY_TOTAL				0x814
#define NV50_CRTC0_SYNC_DURATION				0x818
#define NV50_CRTC0_SYNC_START_TO_BLANK_END		0x81C
#define NV50_CRTC0_MODE_UNK1					0x820
#define NV50_CRTC0_MODE_UNK2					0x824

#define NV50_CRTC0_UNK82C						0x82C

/* You can't have a palette in 8 bit mode (=OFF) */
#define NV50_CRTC0_CLUT_MODE		0x840
	#define NV50_CRTC0_CLUT_MODE_BLANK		0x00000000
	#define NV50_CRTC0_CLUT_MODE_OFF		0x80000000
	#define NV50_CRTC0_CLUT_MODE_ON		0xC0000000
#define NV50_CRTC0_CLUT_OFFSET		0x844

/* Anyone know what part of the chip is triggered here precisely? */
#define NV84_CRTC0_BLANK_UNK1		0x85C
	#define NV84_CRTC0_BLANK_UNK1_BLANK	0x0
	#define NV84_CRTC0_BLANK_UNK1_UNBLANK	0x1

#define NV50_CRTC0_FB_OFFSET		0x860

#define NV50_CRTC0_FB_SIZE			0x868
#define NV50_CRTC0_FB_PITCH		0x86C

#define NV50_CRTC0_DEPTH			0x870
	#define NV50_CRTC0_DEPTH_8BPP		0x1E00
	#define NV50_CRTC0_DEPTH_15BPP	0xE900
	#define NV50_CRTC0_DEPTH_16BPP	0xE800
	#define NV50_CRTC0_DEPTH_24BPP	0xCF00

/* I'm openminded to better interpretations. */
/* This is an educated guess. */
/* NV50 has RAMDAC and TMDS offchip, so it's unlikely to be that. */
#define NV50_CRTC0_BLANK_CTRL		0x874
	#define NV50_CRTC0_BLANK_CTRL_BLANK	0x0
	#define NV50_CRTC0_BLANK_CTRL_UNBLANK	0x1

#define NV50_CRTC0_CURSOR_CTRL	0x880
	#define NV50_CRTC0_CURSOR_CTRL_SHOW	0x85000000
	#define NV50_CRTC0_CURSOR_CTRL_HIDE	0x05000000

#define NV50_CRTC0_CURSOR_OFFSET	0x884

/* Anyone know what part of the chip is triggered here precisely? */
#define NV84_CRTC0_BLANK_UNK2		0x89C
	#define NV84_CRTC0_BLANK_UNK2_BLANK	0x0
	#define NV84_CRTC0_BLANK_UNK2_UNBLANK	0x1

#define NV50_CRTC0_DITHERING_CTRL	0x8A0
	#define NV50_CRTC0_DITHERING_CTRL_ON	0x11
	#define NV50_CRTC0_DITHERING_CTRL_OFF	0x0

#define NV50_CRTC0_SCALE_CTRL		0x8A4
	#define	NV50_CRTC0_SCALE_CTRL_SCALER_INACTIVE	(0 << 0)
	/* It doesn't seem to be needed, hence i wonder what it does precisely. */
	#define	NV50_CRTC0_SCALE_CTRL_SCALER_ACTIVE	(9 << 0)
#define NV50_CRTC0_COLOR_CTRL		0x8A8
	#define NV50_CRTC_COLOR_CTRL_MODE_COLOR		(4 << 16)

#define NV50_CRTC0_FB_POS			0x8C0
#define NV50_CRTC0_REAL_RES		0x8C8

/* Added a macro, because the signed stuff can cause you problems very quickly. */
#define NV50_CRTC0_SCALE_CENTER_OFFSET		0x8D4
	#define NV50_CRTC_SCALE_CENTER_OFFSET_VAL(x, y) ((((unsigned)y << 16) & 0xFFFF0000) | (((unsigned)x) & 0x0000FFFF))
/* Both of these are needed, otherwise nothing happens. */
#define NV50_CRTC0_SCALE_RES1		0x8D8
#define NV50_CRTC0_SCALE_RES2		0x8DC

#define NV50_CRTC1_UNK800			0xC00
#define NV50_CRTC1_CLOCK			0xC04
#define NV50_CRTC1_INTERLACE		0xC08

/* 0xC10 is a reasonable guess, nothing more. */
#define NV50_CRTC1_DISPLAY_START				0xC10
#define NV50_CRTC1_DISPLAY_TOTAL				0xC14
#define NV50_CRTC1_SYNC_DURATION				0xC18
#define NV50_CRTC1_SYNC_START_TO_BLANK_END		0xC1C
#define NV50_CRTC1_MODE_UNK1					0xC20
#define NV50_CRTC1_MODE_UNK2					0xC24

#define NV50_CRTC1_CLUT_MODE		0xC40
	#define NV50_CRTC1_CLUT_MODE_BLANK		0x00000000
	#define NV50_CRTC1_CLUT_MODE_OFF		0x80000000
	#define NV50_CRTC1_CLUT_MODE_ON		0xC0000000
#define NV50_CRTC1_CLUT_OFFSET		0xC44

/* Anyone know what part of the chip is triggered here precisely? */
#define NV84_CRTC1_BLANK_UNK1		0xC5C
	#define NV84_CRTC1_BLANK_UNK1_BLANK	0x0
	#define NV84_CRTC1_BLANK_UNK1_UNBLANK	0x1

#define NV50_CRTC1_FB_OFFSET		0xC60

#define NV50_CRTC1_FB_SIZE			0xC68
#define NV50_CRTC1_FB_PITCH		0xC6C

#define NV50_CRTC1_DEPTH			0xC70
	#define NV50_CRTC1_DEPTH_8BPP		0x1E00
	#define NV50_CRTC1_DEPTH_15BPP	0xE900
	#define NV50_CRTC1_DEPTH_16BPP	0xE800
	#define NV50_CRTC1_DEPTH_24BPP	0xCF00

/* I'm openminded to better interpretations. */
#define NV50_CRTC1_BLANK_CTRL		0xC74
	#define NV50_CRTC1_BLANK_CTRL_BLANK	0x0
	#define NV50_CRTC1_BLANK_CTRL_UNBLANK	0x1

#define NV50_CRTC1_CURSOR_CTRL		0xC80
	#define NV50_CRTC1_CURSOR_CTRL_SHOW	0x85000000
	#define NV50_CRTC1_CURSOR_CTRL_HIDE	0x05000000

#define NV50_CRTC1_CURSOR_OFFSET	0xC84

/* Anyone know what part of the chip is triggered here precisely? */
#define NV84_CRTC1_BLANK_UNK2		0xC9C
	#define NV84_CRTC1_BLANK_UNK2_BLANK	0x0
	#define NV84_CRTC1_BLANK_UNK2_UNBLANK	0x1

#define NV50_CRTC1_DITHERING_CTRL	0xCA0
	#define NV50_CRTC1_DITHERING_CTRL_ON	0x11
	#define NV50_CRTC1_DITHERING_CTRL_OFF	0x0

#define NV50_CRTC1_SCALE_CTRL		0xCA4
#define NV50_CRTC1_COLOR_CTRL		0xCA8

#define NV50_CRTC1_FB_POS			0xCC0
#define NV50_CRTC1_REAL_RES		0xCC8

#define NV50_CRTC1_SCALE_CENTER_OFFSET		0xCD4
/* Both of these are needed, otherwise nothing happens. */
#define NV50_CRTC1_SCALE_RES1		0xCD8
#define NV50_CRTC1_SCALE_RES2		0xCDC

/* misc stuff */
#define NV50_I2C_START		0x7
#define NV50_I2C_STOP		0x3

#endif /* __NV50REG_H_ */
