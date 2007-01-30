/* $XConsortium: nvreg.h /main/2 1996/10/28 05:13:41 kaleb $ */
/*
 * Copyright 1996-1997  David J. McKay
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * DAVID J. MCKAY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nvreg.h,v 1.6 2002/01/25 21:56:06 tsi Exp $ */

#ifndef __NVREG_H_
#define __NVREG_H_

//#define NV_IMAGE_PATTERN              0x18


#define NV_PRAMIN_OFFSET            0x00710000
#define NV_PRAMIN_SIZE              0x00100000

#define NV_PCRTC0_OFFSET            0x00600000
#define NV_PCRTC0_SIZE              0x00001000 /* empirical */

#define NV_PRAMDAC0_OFFSET          0x00680000
#define NV_PRAMDAC0_SIZE            0x00001000

#define NV_PFB_OFFSET               0x00100000
#define NV_PFB_SIZE                 0x00001000

#define NV_PFIFO_OFFSET             0x00002000
#define NV_PFIFO_SIZE               0x00010000

#define NV_PGRAPH_OFFSET            0x00400000
#define NV_PGRAPH_SIZE              0x00010000

#define NV_PEXTDEV_OFFSET           0x00101000
#define NV_PEXTDEV_SIZE             0x00001000

#define NV_PTIMER_OFFSET            0x00009000
#define NV_PTIMER_SIZE              0x00001000

#define NV_PVIDEO_OFFSET            0x00008000
#define NV_PVIDEO_SIZE              0x00001000

/* TODO PMC size is 0x1000, but we need to get ride of abuses first */
#define NV_PMC_OFFSET               0x00000000
#define NV_PMC_SIZE                 0x0000f000

#define NV_FIFO_OFFSET              0x00800000
#define NV_FIFO_SIZE                0x00800000

#define NV_PCIO0_OFFSET             0x00601000
#define NV_PCIO0_SIZE               0x00002000

#define NV_PDIO0_OFFSET             0x00681000
#define NV_PDIO0_SIZE               0x00002000

#define NV_PVIO_OFFSET              0x000C0000
#define NV_PVIO_SIZE                0x00008000

#define NV_PROM_OFFSET              0x00300000
#define NV_PROM_SIZE                0x00010000

/* Nvidia CRTC indexed registers */
/* VGA standard registers: - from Haiku */
#define NV_VGA_CRTCX_HTOTAL		0x00
#define NV_VGA_CRTCX_HDISPE		0x01
#define NV_VGA_CRTCX_HBLANKS		0x02
#define NV_VGA_CRTCX_HBLANKE		0x03
#define NV_VGA_CRTCX_HSYNCS		0x04
#define NV_VGA_CRTCX_HSYNCE		0x05
#define NV_VGA_CRTCX_VTOTAL		0x06
#define NV_VGA_CRTCX_OVERFLOW		0x07
#define NV_VGA_CRTCX_PRROWSCN		0x08
#define NV_VGA_CRTCX_MAXSCLIN		0x09
#define NV_VGA_CRTCX_VGACURCTRL		0x0a
#define NV_VGA_CRTCX_FBSTADDH		0x0c
#define NV_VGA_CRTCX_FBSTADDL		0x0d
#define NV_VGA_CRTCX_VSYNCS		0x10
#define NV_VGA_CRTCX_VSYNCE		0x11
#define NV_VGA_CRTCX_VDISPE		0x12
#define NV_VGA_CRTCX_PITCHL		0x13
#define NV_VGA_CRTCX_VBLANKS		0x15
#define NV_VGA_CRTCX_VBLANKE		0x16
#define NV_VGA_CRTCX_MODECTL		0x17
#define NV_VGA_CRTCX_LINECOMP		0x18
/* Extended VGA CRTC registers */
#define NV_VGA_CRTCX_REPAINT0		0x19
#define NV_VGA_CRTCX_REPAINT1		0x1a
#define NV_VGA_CRTCX_FIFO0		0x1b
#define NV_VGA_CRTCX_FIFO1		0x1c
#define NV_VGA_CRTCX_LOCK		0x1f
#define NV_VGA_CRTCX_FIFO_LWM		0x20
#define NV_VGA_CRTCX_BUFFER		0x21
#define NV_VGA_CRTCX_LSR		0x25
#define NV_VGA_CRTCX_PIXEL		0x28
#define NV_VGA_CRTCX_HEB		0x2d
#define NV_VGA_CRTCX_CURCTL2		0x2f
#define NV_VGA_CRTCX_CURCTL0		0x30
#define NV_VGA_CRTCX_CURCTL1		0x31
#define NV_VGA_CRTCX_LCD		0x33
#define NV_VGA_CRTCX_INTERLACE		0x39
#define NV_VGA_CRTCX_EXTRA		0x41
#define NV_VGA_CRTCX_OWNER		0x44
#define NV_VGA_CRTCX_SWAPPING		0x46
#define NV_VGA_CRTCX_FIFO_LWM_NV30	0x47
#define NV_VGA_CRTCX_FP_HTIMING		0x53
#define NV_VGA_CRTCX_FP_VTIMING		0x54

#define NV_PGRAPH_STATUS            (0x00000700)
#define NV_PFIFO_RAMHT              (0x00000210)
#define NV_PFB_BOOT                 (0x00000000)
#define NV_PEXTDEV_BOOT             (0x00000000)

#define NV_RAMDAC_CURSOR_POS  0x300
#define NV_RAMDAC_CURSOR_CTRL       0x320
#define NV_RAMDAC_CURSOR_DATA_LO    0x324
#define NV_RAMDAC_CURSOR_DATA_HI    0x328

#define NV_RAMDAC_NV10_CURSYNC      0x404

#define NV_RAMDAC_NVPLL             0x500
#define NV_RAMDAC_MPLL              0x504
#	define NV_RAMDAC_PLL_COEFF_MDIV     0x000000FF
#	define NV_RAMDAC_PLL_COEFF_NDIV     0x0000FF00
#	define NV_RAMDAC_PLL_COEFF_PDIV     0x00070000

#define NV_RAMDAC_VPLL              0x508
#define NV_RAMDAC_PLL_SELECT        0x50c
#define NV_RAMDAC_PLL_SELECT_DLL_BYPASS 	(1<<4)
#define NV_RAMDAC_PLL_SELECT_PLL_SOURCE_DEFAULT 	(0<<8)
#define NV_RAMDAC_PLL_SELECT_PLL_SOURCE_MPLL 		(1<<8)
#define NV_RAMDAC_PLL_SELECT_PLL_SOURCE_VPLL 		(2<<8)
#define NV_RAMDAC_PLL_SELECT_PLL_SOURCE_NVPLL 		(4<<8)
#define NV_RAMDAC_PLL_SELECT_PLL_SOURCE_ALL 		(7<<8)
#define NV_RAMDAC_PLL_SELECT_MPLL_BYPASS_FALSE 	(0<<12)
#define NV_RAMDAC_PLL_SELECT_MPLL_BYPASS_TRUE 	(1<<12)
#define NV_RAMDAC_PLL_SELECT_VS_PCLK_TV_NONE		(0<<16)
#define NV_RAMDAC_PLL_SELECT_VS_PCLK_TV_VSCLK		(1<<16)
#define NV_RAMDAC_PLL_SELECT_VS_PCLK_TV_PCLK		(2<<16)
#define NV_RAMDAC_PLL_SELECT_VS_PCLK_TV_BOTH		(3<<16)

#define NV_RAMDAC_PLL_SELECT_TVCLK_SOURCE_EXT           (0<<20)
#define NV_RAMDAC_PLL_SELECT_TVCLK_SOURCE_VIP           (1<<20)

#define NV_RAMDAC_PLL_SELECT_TVCLK_RATIO_DB1		(0<<24)
#define NV_RAMDAC_PLL_SELECT_TVCLK_RATIO_DB2		(1<<24)
#define NV_RAMDAC_PLL_SELECT_VCLK_RATIO_DB1		(0<<28)
#define NV_RAMDAC_PLL_SELECT_VCLK_RATIO_DB2		(1<<28)


#define NV_RAMDAC_PLL_SETUP_CONTROL 0x510
#define NV_RAMDAC_PLL_TEST_COUNTER  0x514
#define NV_RAMDAC_PALETTE_TEST      0x518
#define NV_RAMDAC_VPLL2             0x520
#define NV_RAMDAC_SEL_CLK           0x524
#define NV_RAMDAC_DITHER_NV11       0x528
#define NV_RAMDAC_OUTPUT            0x52c

#define NV_RAMDAC_NVPLL_B           0x570
#define NV_RAMDAC_MPLL_B            0x574
#define NV_RAMDAC_VPLL_B            0x578
#define NV_RAMDAC_VPLL2_B           0x57c

#define NV_RAMDAC_GENERAL_CONTROL   0x600
#define NV_RAMDAC_TEST_CONTROL      0x608
#define NV_RAMDAC_TEST_DATA         0x610

#define NV_RAMDAC_TV_SETUP          0x700
#define NV_RAMDAC_TV_VBLANK_START   0x704
#define NV_RAMDAC_TV_VBLANK_END     0x708
#define NV_RAMDAC_TV_HBLANK_START   0x70c
#define NV_RAMDAC_TV_HBLANK_END     0x710
#define NV_RAMDAC_TV_BLANK_COLOR    0x714
#define NV_RAMDAC_TV_VTOTAL         0x720
#define NV_RAMDAC_TV_VSYNC_START    0x724
#define NV_RAMDAC_TV_VSYNC_END      0x728
#define NV_RAMDAC_TV_HTOTAL         0x72c
#define NV_RAMDAC_TV_HSYNC_START    0x730
#define NV_RAMDAC_TV_HSYNC_END      0x734
#define NV_RAMDAC_TV_SYNC_DELAY     0x738

#define REG_DISP_END 0
#define REG_DISP_TOTAL 1
#define REG_DISP_CRTC 2
#define REG_DISP_SYNC_START 3
#define REG_DISP_SYNC_END 4
#define REG_DISP_VALID_START 5
#define REG_DISP_VALID_END 6

#define NV_RAMDAC_FP_VDISP_END      0x800
#define NV_RAMDAC_FP_VTOTAL         0x804
#define NV_RAMDAC_FP_VCRTC          0x808
#define NV_RAMDAC_FP_VSYNC_START    0x80c
#define NV_RAMDAC_FP_VSYNC_END      0x810
#define NV_RAMDAC_FP_VVALID_START   0x814
#define NV_RAMDAC_FP_VVALID_END     0x818
#define NV_RAMDAC_FP_HDISP_END      0x820
#define NV_RAMDAC_FP_HTOTAL         0x824
#define NV_RAMDAC_FP_HCRTC          0x828
#define NV_RAMDAC_FP_HSYNC_START    0x82c
#define NV_RAMDAC_FP_HSYNC_END      0x830
#define NV_RAMDAC_FP_HVALID_START   0x834
#define NV_RAMDAC_FP_HVALID_END     0x838

#define NV_RAMDAC_FP_DITHER         0x83c
#define NV_RAMDAC_FP_CHECKSUM       0x840
#define NV_RAMDAC_FP_TEST_CONTROL   0x844
#define NV_RAMDAC_FP_CONTROL        0x848

#define NV_RAMDAC_FP_DEBUG_0        0x880
#define NV_RAMDAC_FP_DEBUG_0_PWRDOWN_FPCLK (1<<28)
#define NV_RAMDAC_FP_DEBUG_0_PWRDOWN_TMDS_PLL (2<<28)
#define NV_RAMDAC_FP_DEBUG_0_PWRDOWN_BOTH (3<<28)

#define NV_RAMDAC_FP_TMDS_CONTROL   0x8b0
/* 0xff - address mask */
#define NV_RAMDAC_FP_TMDS_CONTROL_WRITE_DISABLE (1<<16)
#define NV_RAMDAC_FP_TMDS_DATA      0x8b4
/* 0xff - data mask */

#define NV_CRTC_INTR_0              0x100
#	define NV_CRTC_INTR_VBLANK           1
#define NV_CRTC_INTR_EN_0           0x140
#define NV_CRTC_START               0x800
#define NV_CRTC_CURSOR_CONFIG       0x810
#define NV_CRTC_081C                0x81c
#define NV_CRTC_0830                0x830
#define NV_CRTC_0834                0x834
#define NV_CRTC_FSEL                0x860
#define NV_CRTC_FSEL_I2C           (1<<4)
#define NV_CRTC_FSEL_TVOUT1        (1<<8)
#define NV_CRTC_FSEL_TVOUT2        (2<<8)
#define NV_CRTC_FSEL_OVERLAY       (1<<12)
#define NV_CRTC_FSEL_FPP2          (1<<16)
#define NV_CRTC_FSEL_FPP1          (2<<16)

#define NV_PFB_CFG0                 0x200
#define NV_PFB_CFG1                 0x204
#define NV_PFB_020C                 0x20C
#define NV_PFB_TILE_NV10            0x240
#define NV_PFB_TILE_SIZE_NV10       0x244
#define NV_PFB_CLOSE_PAGE2          0x33C
#define NV_PFB_TILE_NV40            0x600
#define NV_PFB_TILE_SIZE_NV40       0x604

#define NV_PGRAPH_DEBUG_0           0x080
#define NV_PGRAPH_DEBUG_1           0x084
#define NV_PGRAPH_DEBUG_2_NV04      0x088
#define NV_PGRAPH_DEBUG_2           0x620
#define NV_PGRAPH_DEBUG_3           0x08c
#define NV_PGRAPH_DEBUG_4           0x090

#define NV_PGRAPH_INTR              0x100
#define NV_PGRAPH_INTR_EN           0x140
#define NV_PGRAPH_CTX_CONTROL       0x144
#define NV_PGRAPH_CTX_CONTROL_NV04  0x170
#define NV_PGRAPH_ABS_UCLIP_XMIN    0x53C
#define NV_PGRAPH_ABS_UCLIP_YMIN    0x540
#define NV_PGRAPH_ABS_UCLIP_XMAX    0x544
#define NV_PGRAPH_ABS_UCLIP_YMAX    0x548
#define NV_PGRAPH_BETA_AND          0x608
#define NV_PGRAPH_LIMIT_VIOL_PIX    0x610

#define NV_PGRAPH_BOFFSET0          0x640
#define NV_PGRAPH_BOFFSET1          0x644
#define NV_PGRAPH_BOFFSET2          0x648

#define NV_PGRAPH_BLIMIT0           0x684
#define NV_PGRAPH_BLIMIT1           0x688
#define NV_PGRAPH_BLIMIT2           0x68c

#define NV_PGRAPH_SURFACE           0x710
#define NV_PGRAPH_STATE             0x714
#define NV_PGRAPH_FIFO              0x720

#define NV_PGRAPH_PATTERN_SHAPE     0x810

#define NV_PGRAPH_TILE              0xb00

#define NV_PVIDEO_INTR_EN           0x140
#define NV_PVIDEO_BUFFER            0x700
#define NV_PVIDEO_STOP              0x704
#define NV_PVIDEO_BASE(buff)        (0x900+(buff)*4)
#define NV_PVIDEO_LIMIT(buff)       (0x908+(buff)*4)
#define NV_PVIDEO_LUMINANCE(buff)   (0x910+(buff)*4)
#define NV_PVIDEO_CHROMINANCE(buff) (0x918+(buff)*4)
#define NV_PVIDEO_OFFSET_BUFF(buff) (0x920+(buff)*4)
#define NV_PVIDEO_SIZE_IN(buff)     (0x928+(buff)*4)
#define NV_PVIDEO_POINT_IN(buff)    (0x930+(buff)*4)
#define NV_PVIDEO_DS_DX(buff)       (0x938+(buff)*4)
#define NV_PVIDEO_DT_DY(buff)       (0x940+(buff)*4)
#define NV_PVIDEO_POINT_OUT(buff)   (0x948+(buff)*4)
#define NV_PVIDEO_SIZE_OUT(buff)    (0x950+(buff)*4)
#define NV_PVIDEO_FORMAT(buff)      (0x958+(buff)*4)
#	define NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8    (1 << 16)
#	define NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY        (1 << 20)
#	define NV_PVIDEO_FORMAT_MATRIX_ITURBT709         (1 << 24)
#define NV_PVIDEO_COLOR_KEY          0xB00

#endif


