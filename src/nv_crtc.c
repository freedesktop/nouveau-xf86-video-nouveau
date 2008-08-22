/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include "xf86.h"
#include "os.h"
#include "mibank.h"
#include "globals.h"
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86DDC.h"
#include "mipointer.h"
#include "windowstr.h"
#include <randrstr.h>
#include <X11/extensions/render.h>

#include "xf86Crtc.h"
#include "nv_include.h"

static void nv_crtc_load_state_vga(xf86CrtcPtr crtc, RIVA_HW_STATE *state);
static void nv_crtc_load_state_ext(xf86CrtcPtr crtc, RIVA_HW_STATE *state, Bool override);
static void nv_crtc_load_state_ramdac(xf86CrtcPtr crtc, RIVA_HW_STATE *state);
static void nv_crtc_save_state_ext(xf86CrtcPtr crtc, RIVA_HW_STATE *state);
static void nv_crtc_save_state_vga(xf86CrtcPtr crtc, RIVA_HW_STATE *state);
static void nv_crtc_save_state_ramdac(xf86CrtcPtr crtc, RIVA_HW_STATE *state);
static void nv_crtc_load_state_palette(xf86CrtcPtr crtc, RIVA_HW_STATE *state);
static void nv_crtc_save_state_palette(xf86CrtcPtr crtc, RIVA_HW_STATE *state);

static uint32_t NVCrtcReadCRTC(xf86CrtcPtr crtc, uint32_t reg)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);

	return NVReadCRTC(pNv, nv_crtc->head, reg);
}

static void NVCrtcWriteCRTC(xf86CrtcPtr crtc, uint32_t reg, uint32_t val)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);

	NVWriteCRTC(pNv, nv_crtc->head, reg, val);
}

static uint32_t NVCrtcReadRAMDAC(xf86CrtcPtr crtc, uint32_t reg)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);

	return NVReadRAMDAC(pNv, nv_crtc->head, reg);
}

static void NVCrtcWriteRAMDAC(xf86CrtcPtr crtc, uint32_t reg, uint32_t val)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);

	NVWriteRAMDAC(pNv, nv_crtc->head, reg, val);
}

void NVCrtcLockUnlock(xf86CrtcPtr crtc, Bool lock)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);

	if (pNv->twoHeads)
		NVSetOwner(pScrn, nv_crtc->head);
	NVLockVgaCrtc(pNv, nv_crtc->head, lock);
}

/* Even though they are not yet used, i'm adding some notes about some of the 0x4000 regs */
/* They are only valid for NV4x, appearantly reordered for NV5x */
/* gpu pll: 0x4000 + 0x4004
 * unknown pll: 0x4008 + 0x400c
 * vpll1: 0x4010 + 0x4014
 * vpll2: 0x4018 + 0x401c
 * unknown pll: 0x4020 + 0x4024
 * unknown pll: 0x4038 + 0x403c
 * Some of the unknown's are probably memory pll's.
 * The vpll's use two set's of multipliers and dividers. I refer to them as a and b.
 * 1 and 2 refer to the registers of each pair. There is only one post divider.
 * Logic: clock = reference_clock * ((n(a) * n(b))/(m(a) * m(b))) >> p
 * 1) bit 0-7: familiar values, but redirected from were? (similar to PLL_SETUP_CONTROL)
 *     bit8: A switch that turns of the second divider and multiplier off.
 *     bit12: Also a switch, i haven't seen it yet.
 *     bit16-19: p-divider
 *     but 28-31: Something related to the mode that is used (see bit8).
 * 2) bit0-7: m-divider (a)
 *     bit8-15: n-multiplier (a)
 *     bit16-23: m-divider (b)
 *     bit24-31: n-multiplier (b)
 */

/* Modifying the gpu pll for example requires:
 * - Disable value 0x333 (inverse AND mask) on the 0xc040 register.
 * This is not needed for the vpll's which have their own bits.
 */

static void nv_crtc_save_state_pll(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp = &state->crtc_reg[nv_crtc->head];
	NVPtr pNv = NVPTR(crtc->scrn);

	if (nv_crtc->head) {
		regp->vpll_a = NVReadRAMDAC(pNv, 0, NV_RAMDAC_VPLL2);
		if (pNv->twoStagePLL)
			regp->vpll_b = NVReadRAMDAC(pNv, 0, NV_RAMDAC_VPLL2_B);
	} else {
		regp->vpll_a = NVReadRAMDAC(pNv, 0, NV_RAMDAC_VPLL);
		if (pNv->twoStagePLL)
			regp->vpll_b = NVReadRAMDAC(pNv, 0, NV_RAMDAC_VPLL_B);
	}
	if (pNv->twoHeads)
		state->sel_clk = NVReadRAMDAC(pNv, 0, NV_RAMDAC_SEL_CLK);
	state->pllsel = NVReadRAMDAC(pNv, 0, NV_RAMDAC_PLL_SELECT);
	if (pNv->Architecture == NV_ARCH_40)
		state->reg580 = NVReadRAMDAC(pNv, 0, NV_RAMDAC_580);
}

static void nv_crtc_load_state_pll(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp = &state->crtc_reg[nv_crtc->head];
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	uint32_t savedc040 = 0;

	/* This sequence is important, the NV28 is very sensitive in this area. */
	/* Keep pllsel last and sel_clk first. */
	if (pNv->twoHeads) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_SEL_CLK %08X\n", state->sel_clk);
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_SEL_CLK, state->sel_clk);
	}

	if (pNv->Architecture == NV_ARCH_40) {
		savedc040 = nvReadMC(pNv, 0xc040);

		/* for vpll1 change bits 16 and 17 are disabled */
		/* for vpll2 change bits 18 and 19 are disabled */
		nvWriteMC(pNv, 0xc040, savedc040 & ~(3 << (16 + nv_crtc->head * 2)));
	}

	if (nv_crtc->head) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_VPLL2 %08X\n", regp->vpll_a);
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_VPLL2, regp->vpll_a);
		if (pNv->twoStagePLL) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_VPLL2_B %08X\n", regp->vpll_b);
			NVWriteRAMDAC(pNv, 0, NV_RAMDAC_VPLL2_B, regp->vpll_b);
		}
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_VPLL %08X\n", regp->vpll_a);
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_VPLL, regp->vpll_a);
		if (pNv->twoStagePLL) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_VPLL_B %08X\n", regp->vpll_b);
			NVWriteRAMDAC(pNv, 0, NV_RAMDAC_VPLL_B, regp->vpll_b);
		}
	}

	if (pNv->Architecture == NV_ARCH_40) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_580 %08X\n", state->reg580);
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_580, state->reg580);

		/* We need to wait a while */
		usleep(5000);
		nvWriteMC(pNv, 0xc040, savedc040);
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_PLL_SELECT %08X\n", state->pllsel);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_PLL_SELECT, state->pllsel);
}

/* Calculate extended mode parameters (SVGA) and save in a mode state structure */
static void nv_crtc_calc_state_ext(xf86CrtcPtr crtc, DisplayModePtr mode, int dot_clock)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	RIVA_HW_STATE *state = &pNv->ModeReg;
	NVCrtcRegPtr regp = &state->crtc_reg[nv_crtc->head];
	struct pll_lims pll_lim;
	int NM1 = 0xbeef, NM2 = 0, log2P = 0, VClk = 0;
	uint32_t g70_pll_special_bits = 0;
	Bool nv4x_single_stage_pll_mode = FALSE;
	uint32_t arbitration0, arbitration1;

	if (!get_pll_limits(pScrn, nv_crtc->head ? VPLL2 : VPLL1, &pll_lim))
		return;

	if (pNv->twoStagePLL || pNv->NVArch == 0x30 || pNv->NVArch == 0x35) {
		if (dot_clock < pll_lim.vco1.maxfreq && pNv->NVArch > 0x40) { /* use a single VCO */
			nv4x_single_stage_pll_mode = TRUE;
			/* Turn the second set of divider and multiplier off */
			/* Bogus data, the same nvidia uses */
			NM2 = 0x11f;
			VClk = getMNP_single(pScrn, &pll_lim, dot_clock, &NM1, &log2P);
		} else
			VClk = getMNP_double(pScrn, &pll_lim, dot_clock, &NM1, &NM2, &log2P);
	} else
		VClk = getMNP_single(pScrn, &pll_lim, dot_clock, &NM1, &log2P);

	/* Are these all the (relevant) G70 cards? */
	if (pNv->NVArch == 0x4B || pNv->NVArch == 0x46 || pNv->NVArch == 0x47 || pNv->NVArch == 0x49) {
		/* This is a big guess, but should be reasonable until we can narrow it down. */
		/* What exactly are the purpose of the upper 2 bits of pll_a and pll_b? */
		if (nv4x_single_stage_pll_mode)
			g70_pll_special_bits = 0x1;
		else
			g70_pll_special_bits = 0x3;
	}

	if (pNv->NVArch == 0x30 || pNv->NVArch == 0x35)
		/* See nvregisters.xml for details. */
		regp->vpll_a = (NM2 & (0x18 << 8)) << 13 | (NM2 & (0x7 << 8)) << 11 | log2P << 16 | NV30_RAMDAC_ENABLE_VCO2 | (NM2 & 7) << 4 | NM1;
	else
		regp->vpll_a = g70_pll_special_bits << 30 | log2P << 16 | NM1;
	regp->vpll_b = NV31_RAMDAC_ENABLE_VCO2 | NM2;

	if (nv4x_single_stage_pll_mode) {
		if (nv_crtc->head == 0)
			state->reg580 |= NV_RAMDAC_580_VPLL1_ACTIVE;
		else
			state->reg580 |= NV_RAMDAC_580_VPLL2_ACTIVE;
	} else {
		if (nv_crtc->head == 0)
			state->reg580 &= ~NV_RAMDAC_580_VPLL1_ACTIVE;
		else
			state->reg580 &= ~NV_RAMDAC_580_VPLL2_ACTIVE;
	}

	/* The NV40 seems to have more similarities to NV3x than other NV4x */
	if (pNv->NVArch < 0x41)
		state->pllsel |= NV_RAMDAC_PLL_SELECT_PLL_SOURCE_NVPLL |
				 NV_RAMDAC_PLL_SELECT_PLL_SOURCE_MPLL;
	/* The blob uses this always, so let's do the same */
	if (pNv->Architecture == NV_ARCH_40)
		state->pllsel |= NV_RAMDAC_PLL_SELECT_USE_VPLL2_TRUE;

	if (nv_crtc->head == 1) {
		if (!nv4x_single_stage_pll_mode)
			state->pllsel |= NV_RAMDAC_PLL_SELECT_VCLK2_RATIO_DB2;
		else
			state->pllsel &= ~NV_RAMDAC_PLL_SELECT_VCLK2_RATIO_DB2;
		state->pllsel |= NV_RAMDAC_PLL_SELECT_PLL_SOURCE_VPLL2;
	} else {
		if (!nv4x_single_stage_pll_mode)
			state->pllsel |= NV_RAMDAC_PLL_SELECT_VCLK_RATIO_DB2;
		else
			state->pllsel &= ~NV_RAMDAC_PLL_SELECT_VCLK_RATIO_DB2;
		state->pllsel |= NV_RAMDAC_PLL_SELECT_PLL_SOURCE_VPLL;
	}

	if ((!pNv->twoStagePLL && pNv->NVArch != 0x30 && pNv->NVArch != 0x35) || nv4x_single_stage_pll_mode)
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vpll: n %d m %d log2p %d\n", NM1 >> 8, NM1 & 0xff, log2P);
	else
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vpll: n1 %d n2 %d m1 %d m2 %d log2p %d\n", NM1 >> 8, NM2 >> 8, NM1 & 0xff, NM2 & 0xff, log2P);

	if (pNv->Architecture == NV_ARCH_04) {
		nv4UpdateArbitrationSettings(VClk, pScrn->bitsPerPixel,
					     &arbitration0, &arbitration1, pNv);

		regp->CRTC[NV_VGA_CRTCX_CURCTL0] = 0x00;
		regp->CRTC[NV_VGA_CRTCX_CURCTL1] = 0xbC;
		regp->CRTC[NV_VGA_CRTCX_CURCTL2] = 0x00000000;
	} else {
		uint32_t CursorStart = nv_crtc->head ? pNv->Cursor2->offset : pNv->Cursor->offset;

		if (((pNv->Chipset & 0xfff0) == CHIPSET_C51) ||
		    ((pNv->Chipset & 0xfff0) == CHIPSET_C512)) {
			arbitration0 = 128;
			arbitration1 = 0x0480;
		} else if (((pNv->Chipset & 0xffff) == CHIPSET_NFORCE) ||
			 ((pNv->Chipset & 0xffff) == CHIPSET_NFORCE2))
			nForceUpdateArbitrationSettings(VClk, pScrn->bitsPerPixel,
							&arbitration0,
							&arbitration1, pNv);
		else if (pNv->Architecture < NV_ARCH_30)
			nv10UpdateArbitrationSettings(VClk, pScrn->bitsPerPixel,
						      &arbitration0,
						      &arbitration1, pNv);
		else
			nv30UpdateArbitrationSettings(pNv, &arbitration0,
						      &arbitration1);

		regp->CRTC[NV_VGA_CRTCX_CURCTL0] = 0x80 | (CursorStart >> 17);
		regp->CRTC[NV_VGA_CRTCX_CURCTL1] = (CursorStart >> 11) << 2;
		regp->CRTC[NV_VGA_CRTCX_CURCTL2] = CursorStart >> 24;
	}

	if (mode->Flags & V_DBLSCAN)
		regp->CRTC[NV_VGA_CRTCX_CURCTL1] |= 2;

	regp->CRTC[NV_VGA_CRTCX_FIFO0] = arbitration0;
	regp->CRTC[NV_VGA_CRTCX_FIFO_LWM] = arbitration1 & 0xff;
	if (pNv->Architecture >= NV_ARCH_30)
		regp->CRTC[NV_VGA_CRTCX_FIFO_LWM_NV30] = arbitration1 >> 8;
}

static void
nv_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	unsigned char seq1 = 0, crtc17 = 0;
	unsigned char crtc1A;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_dpms is called for CRTC %d with mode %d.\n", nv_crtc->head, mode);

	if (nv_crtc->last_dpms == mode) /* Don't do unnecesary mode changes. */
		return;

	nv_crtc->last_dpms = mode;

	if (pNv->twoHeads)
		NVSetOwner(pScrn, nv_crtc->head);

	crtc1A = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_REPAINT1) & ~0xC0;
	switch(mode) {
		case DPMSModeStandby:
		/* Screen: Off; HSync: Off, VSync: On -- Not Supported */
		seq1 = 0x20;
		crtc17 = 0x80;
		crtc1A |= 0x80;
		break;
	case DPMSModeSuspend:
		/* Screen: Off; HSync: On, VSync: Off -- Not Supported */
		seq1 = 0x20;
		crtc17 = 0x80;
		crtc1A |= 0x40;
		break;
	case DPMSModeOff:
		/* Screen: Off; HSync: Off, VSync: Off */
		seq1 = 0x20;
		crtc17 = 0x00;
		crtc1A |= 0xC0;
		break;
	case DPMSModeOn:
	default:
		/* Screen: On; HSync: On, VSync: On */
		seq1 = 0x00;
		crtc17 = 0x80;
		break;
	}

	NVVgaSeqReset(pNv, nv_crtc->head, true);
	/* Each head has it's own sequencer, so we can turn it off when we want */
	seq1 |= (NVReadVgaSeq(pNv, nv_crtc->head, 0x01) & ~0x20);
	NVWriteVgaSeq(pNv, nv_crtc->head, 0x1, seq1);
	crtc17 |= (NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_MODECTL) & ~0x80);
	usleep(10000);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_MODECTL, crtc17);
	NVVgaSeqReset(pNv, nv_crtc->head, false);

	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_REPAINT1, crtc1A);
}

static Bool
nv_crtc_mode_fixup(xf86CrtcPtr crtc, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode)
{
	return TRUE;
}

static void
nv_crtc_mode_set_vga(xf86CrtcPtr crtc, DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];

	/* Calculate our timings */
	int horizDisplay	= (mode->CrtcHDisplay >> 3) 	- 1;
	int horizStart		= (mode->CrtcHSyncStart >> 3) 	- 1;
	int horizEnd		= (mode->CrtcHSyncEnd >> 3) 	- 1;
	int horizTotal		= (mode->CrtcHTotal >> 3)		- 5;
	int horizBlankStart	= (mode->CrtcHDisplay >> 3)		- 1;
	int horizBlankEnd	= (mode->CrtcHTotal >> 3)		- 1;
	int vertDisplay		= mode->CrtcVDisplay			- 1;
	int vertStart		= mode->CrtcVSyncStart 		- 1;
	int vertEnd		= mode->CrtcVSyncEnd			- 1;
	int vertTotal		= mode->CrtcVTotal 			- 2;
	int vertBlankStart	= mode->CrtcVDisplay 			- 1;
	int vertBlankEnd	= mode->CrtcVTotal			- 1;

	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	bool fp_output = false;
	int i;

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		NVOutputPrivatePtr nv_output = output->driver_private;

		if (output->crtc == crtc && (nv_output->dcb->type == OUTPUT_LVDS ||
					     nv_output->dcb->type == OUTPUT_TMDS))
			fp_output = true;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Mode clock: %d\n", mode->Clock);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Adjusted mode clock: %d\n", adjusted_mode->Clock);

	if (fp_output) {
		vertStart = vertTotal - 3;  
		vertEnd = vertTotal - 2;
		vertBlankStart = vertStart;
		horizStart = horizTotal - 5;
		horizEnd = horizTotal - 2;
		horizBlankEnd = horizTotal + 4;
		if (pNv->overlayAdaptor && pNv->Architecture >= NV_ARCH_10)
			/* This reportedly works around some video overlay bandwidth problems */
			horizTotal += 2;
	}

	if (mode->Flags & V_INTERLACE) 
		vertTotal |= 1;

#if 0
	ErrorF("horizDisplay: 0x%X \n", horizDisplay);
	ErrorF("horizStart: 0x%X \n", horizStart);
	ErrorF("horizEnd: 0x%X \n", horizEnd);
	ErrorF("horizTotal: 0x%X \n", horizTotal);
	ErrorF("horizBlankStart: 0x%X \n", horizBlankStart);
	ErrorF("horizBlankEnd: 0x%X \n", horizBlankEnd);
	ErrorF("vertDisplay: 0x%X \n", vertDisplay);
	ErrorF("vertStart: 0x%X \n", vertStart);
	ErrorF("vertEnd: 0x%X \n", vertEnd);
	ErrorF("vertTotal: 0x%X \n", vertTotal);
	ErrorF("vertBlankStart: 0x%X \n", vertBlankStart);
	ErrorF("vertBlankEnd: 0x%X \n", vertBlankEnd);
#endif

	/*
	* compute correct Hsync & Vsync polarity 
	*/
	if ((mode->Flags & (V_PHSYNC | V_NHSYNC))
		&& (mode->Flags & (V_PVSYNC | V_NVSYNC))) {

		regp->MiscOutReg = 0x23;
		if (mode->Flags & V_NHSYNC) regp->MiscOutReg |= 0x40;
		if (mode->Flags & V_NVSYNC) regp->MiscOutReg |= 0x80;
	} else {
		int VDisplay = mode->VDisplay;
		if (mode->Flags & V_DBLSCAN)
			VDisplay *= 2;
		if (mode->VScan > 1)
			VDisplay *= mode->VScan;
		if (VDisplay < 400)
			regp->MiscOutReg = 0xA3;		/* +hsync -vsync */
		else if (VDisplay < 480)
			regp->MiscOutReg = 0x63;		/* -hsync +vsync */
		else if (VDisplay < 768)
			regp->MiscOutReg = 0xE3;		/* -hsync -vsync */
		else
			regp->MiscOutReg = 0x23;		/* +hsync +vsync */
	}

	regp->MiscOutReg |= (mode->ClockIndex & 0x03) << 2;

	/*
	* Time Sequencer
	*/
	regp->Sequencer[0] = 0x00;
	/* 0x20 disables the sequencer */
	if (mode->Flags & V_CLKDIV2)
		regp->Sequencer[1] = 0x29;
	else
		regp->Sequencer[1] = 0x21;
	regp->Sequencer[2] = 0x0F;
	regp->Sequencer[3] = 0x00;                     /* Font select */
	regp->Sequencer[4] = 0x0E;                             /* Misc */

	/*
	* CRTC Controller
	*/
	regp->CRTC[NV_VGA_CRTCX_HTOTAL]  = Set8Bits(horizTotal);
	regp->CRTC[NV_VGA_CRTCX_HDISPE]  = Set8Bits(horizDisplay);
	regp->CRTC[NV_VGA_CRTCX_HBLANKS]  = Set8Bits(horizBlankStart);
	regp->CRTC[NV_VGA_CRTCX_HBLANKE]  = SetBitField(horizBlankEnd,4:0,4:0) 
				| SetBit(7);
	regp->CRTC[NV_VGA_CRTCX_HSYNCS]  = Set8Bits(horizStart);
	regp->CRTC[NV_VGA_CRTCX_HSYNCE]  = SetBitField(horizBlankEnd,5:5,7:7)
				| SetBitField(horizEnd,4:0,4:0);
	regp->CRTC[NV_VGA_CRTCX_VTOTAL]  = SetBitField(vertTotal,7:0,7:0);
	regp->CRTC[NV_VGA_CRTCX_OVERFLOW]  = SetBitField(vertTotal,8:8,0:0)
				| SetBitField(vertDisplay,8:8,1:1)
				| SetBitField(vertStart,8:8,2:2)
				| SetBitField(vertBlankStart,8:8,3:3)
				| SetBit(4)
				| SetBitField(vertTotal,9:9,5:5)
				| SetBitField(vertDisplay,9:9,6:6)
				| SetBitField(vertStart,9:9,7:7);
	regp->CRTC[NV_VGA_CRTCX_PRROWSCN]  = 0x00;
	regp->CRTC[NV_VGA_CRTCX_MAXSCLIN]  = SetBitField(vertBlankStart,9:9,5:5)
				| SetBit(6)
				| ((mode->Flags & V_DBLSCAN) ? 0x80 : 0x00);
	regp->CRTC[NV_VGA_CRTCX_VGACURSTART] = 0x00;
	regp->CRTC[NV_VGA_CRTCX_VGACUREND] = 0x00;
	regp->CRTC[NV_VGA_CRTCX_FBSTADDH] = 0x00;
	regp->CRTC[NV_VGA_CRTCX_FBSTADDL] = 0x00;
	regp->CRTC[0xe] = 0x00;
	regp->CRTC[0xf] = 0x00;
	regp->CRTC[NV_VGA_CRTCX_VSYNCS] = Set8Bits(vertStart);
	/* What is the meaning of bit5, it is empty in the vga spec. */
	regp->CRTC[NV_VGA_CRTCX_VSYNCE] = SetBitField(vertEnd,3:0,3:0) | SetBit(5);
	regp->CRTC[NV_VGA_CRTCX_VDISPE] = Set8Bits(vertDisplay);
	/* framebuffer can be larger than crtc scanout area. */
	regp->CRTC[NV_VGA_CRTCX_PITCHL] = pScrn->displayWidth / 8 * pScrn->bitsPerPixel / 8;
	regp->CRTC[NV_VGA_CRTCX_UNDERLINE] = 0x00;
	regp->CRTC[NV_VGA_CRTCX_VBLANKS] = Set8Bits(vertBlankStart);
	regp->CRTC[NV_VGA_CRTCX_VBLANKE] = Set8Bits(vertBlankEnd);
	/* 0x80 enables the sequencer, we don't want that */
	regp->CRTC[NV_VGA_CRTCX_MODECTL] = 0xC3 & ~0x80;
	regp->CRTC[NV_VGA_CRTCX_LINECOMP] = 0xff;

	/* 
	 * Some extended CRTC registers (they are not saved with the rest of the vga regs).
	 */

	/* framebuffer can be larger than crtc scanout area. */
	regp->CRTC[NV_VGA_CRTCX_REPAINT0] = ((pScrn->displayWidth / 8 * pScrn->bitsPerPixel / 8) & 0x700) >> 3;
	regp->CRTC[NV_VGA_CRTCX_REPAINT1] = mode->CrtcHDisplay < 1280 ? 0x04 : 0x00;
	regp->CRTC[NV_VGA_CRTCX_LSR] = SetBitField(horizBlankEnd,6:6,4:4)
				| SetBitField(vertBlankStart,10:10,3:3)
				| SetBitField(vertStart,10:10,2:2)
				| SetBitField(vertDisplay,10:10,1:1)
				| SetBitField(vertTotal,10:10,0:0);

	regp->CRTC[NV_VGA_CRTCX_HEB] = SetBitField(horizTotal,8:8,0:0) 
				| SetBitField(horizDisplay,8:8,1:1)
				| SetBitField(horizBlankStart,8:8,2:2)
				| SetBitField(horizStart,8:8,3:3);

	regp->CRTC[NV_VGA_CRTCX_EXTRA] = SetBitField(vertTotal,11:11,0:0)
				| SetBitField(vertDisplay,11:11,2:2)
				| SetBitField(vertStart,11:11,4:4)
				| SetBitField(vertBlankStart,11:11,6:6);

	if(mode->Flags & V_INTERLACE) {
		horizTotal = (horizTotal >> 1) & ~1;
		regp->CRTC[NV_VGA_CRTCX_INTERLACE] = Set8Bits(horizTotal);
		regp->CRTC[NV_VGA_CRTCX_HEB] |= SetBitField(horizTotal,8:8,4:4);
	} else
		regp->CRTC[NV_VGA_CRTCX_INTERLACE] = 0xff;  /* interlace off */

	/*
	* Graphics Display Controller
	*/
	regp->Graphics[0] = 0x00;
	regp->Graphics[1] = 0x00;
	regp->Graphics[2] = 0x00;
	regp->Graphics[3] = 0x00;
	regp->Graphics[4] = 0x00;
	regp->Graphics[5] = 0x40; /* 256 color mode */
	regp->Graphics[6] = 0x05; /* map 64k mem + graphic mode */
	regp->Graphics[7] = 0x0F;
	regp->Graphics[8] = 0xFF;

	regp->Attribute[0]  = 0x00; /* standard colormap translation */
	regp->Attribute[1]  = 0x01;
	regp->Attribute[2]  = 0x02;
	regp->Attribute[3]  = 0x03;
	regp->Attribute[4]  = 0x04;
	regp->Attribute[5]  = 0x05;
	regp->Attribute[6]  = 0x06;
	regp->Attribute[7]  = 0x07;
	regp->Attribute[8]  = 0x08;
	regp->Attribute[9]  = 0x09;
	regp->Attribute[10] = 0x0A;
	regp->Attribute[11] = 0x0B;
	regp->Attribute[12] = 0x0C;
	regp->Attribute[13] = 0x0D;
	regp->Attribute[14] = 0x0E;
	regp->Attribute[15] = 0x0F;
	regp->Attribute[16] = 0x01; /* Enable graphic mode */
	/* Non-vga */
	regp->Attribute[17] = 0x00;
	regp->Attribute[18] = 0x0F; /* enable all color planes */
	regp->Attribute[19] = 0x00;
	regp->Attribute[20] = 0x00;
}

/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static void
nv_crtc_mode_set_regs(xf86CrtcPtr crtc, DisplayModePtr mode)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];
	NVCrtcRegPtr savep = &pNv->SavedReg.crtc_reg[nv_crtc->head];
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	bool lvds_output = false, tmds_output = false;
	int i;

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		NVOutputPrivatePtr nv_output = output->driver_private;

		if (output->crtc == crtc && nv_output->dcb->type == OUTPUT_LVDS)
			lvds_output = true;
		if (output->crtc == crtc && nv_output->dcb->type == OUTPUT_TMDS)
			tmds_output = true;
	}

	/* Registers not directly related to the (s)vga mode */

	/* bit2 = 0 -> fine pitched crtc granularity */
	/* The rest disables double buffering on CRTC access */
	regp->CRTC[NV_VGA_CRTCX_BUFFER] = 0xfa;

	/* the blob sometimes sets |= 0x10 (which is the same as setting |=
	 * 1 << 30 on 0x60.830), for no apparent reason */
	regp->CRTC[NV_VGA_CRTCX_59] = 0x0;
	if (tmds_output && pNv->Architecture < NV_ARCH_40)
		regp->CRTC[NV_VGA_CRTCX_59] |= 0x1;

	/* What is the meaning of this register? */
	/* A few popular values are 0x18, 0x1c, 0x38, 0x3c */ 
	regp->CRTC[NV_VGA_CRTCX_FIFO1] = savep->CRTC[NV_VGA_CRTCX_FIFO1] & ~(1<<5);

	regp->head = 0;
	/* Except for rare conditions I2C is enabled on the primary crtc */
	if (nv_crtc->head == 0)
		regp->head |= NV_CRTC_FSEL_I2C;
	/* Set overlay to desired crtc. */
	if (pNv->overlayAdaptor) {
		NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(pNv);
		if (pPriv->overlayCRTC == nv_crtc->head)
			regp->head |= NV_CRTC_FSEL_OVERLAY;
	}

	/* This is not what nv does, but it is what the blob does (for nv4x at least) */
	/* This fixes my cursor corruption issue */
	regp->cursorConfig = 0x0;
	if(mode->Flags & V_DBLSCAN)
		regp->cursorConfig |= NV_CRTC_CURSOR_CONFIG_DOUBLE_SCAN;
	if (pNv->alphaCursor) {
		regp->cursorConfig |= NV_CRTC_CURSOR_CONFIG_32BPP |
				      NV_CRTC_CURSOR_CONFIG_64PIXELS |
				      NV_CRTC_CURSOR_CONFIG_64LINES |
				      NV_CRTC_CURSOR_CONFIG_ALPHA_BLEND;
	} else
		regp->cursorConfig |= NV_CRTC_CURSOR_CONFIG_32LINES;

	/* Unblock some timings */
	regp->CRTC[NV_VGA_CRTCX_FP_HTIMING] = 0;
	regp->CRTC[NV_VGA_CRTCX_FP_VTIMING] = 0;

	/* What is the purpose of this register? */
	/* 0x14 may be disabled? */
	regp->CRTC[NV_VGA_CRTCX_26] = 0x20;

	/* 0x00 is disabled, 0x11 is lvds, 0x22 crt and 0x88 tmds */
	if (lvds_output)
		regp->CRTC[NV_VGA_CRTCX_3B] = 0x11;
	else if (tmds_output)
		regp->CRTC[NV_VGA_CRTCX_3B] = 0x88;
	else
		regp->CRTC[NV_VGA_CRTCX_3B] = 0x22;

	/* These values seem to vary */
	/* This register seems to be used by the bios to make certain decisions on some G70 cards? */
	regp->CRTC[NV_VGA_CRTCX_SCRATCH4] = savep->CRTC[NV_VGA_CRTCX_SCRATCH4];

	regp->CRTC[NV_VGA_CRTCX_45] = 0x80;

	/* What does this do?:
	 * bit0: crtc0
	 * bit6: lvds
	 * bit7: (only in X)
	 */
	if (nv_crtc->head == 0)
		regp->CRTC[NV_VGA_CRTCX_4B] = 0x81;
	else 
		regp->CRTC[NV_VGA_CRTCX_4B] = 0x80;

	if (lvds_output)
		regp->CRTC[NV_VGA_CRTCX_4B] |= 0x40;

	/* The blob seems to take the current value from crtc 0, add 4 to that
	 * and reuse the old value for crtc 1 */
	regp->CRTC[NV_VGA_CRTCX_52] = pNv->SavedReg.crtc_reg[0].CRTC[NV_VGA_CRTCX_52];
	if (!nv_crtc->head)
		regp->CRTC[NV_VGA_CRTCX_52] += 4;

	regp->unk830 = mode->CrtcVDisplay - 3;
	regp->unk834 = mode->CrtcVDisplay - 1;

	if (pNv->twoHeads)
		/* This is what the blob does */
		regp->unk850 = NVReadCRTC(pNv, 0, NV_CRTC_0850);

	/* Never ever modify gpio, unless you know very well what you're doing */
	regp->gpio = NVReadCRTC(pNv, 0, NV_CRTC_GPIO);

	if (pNv->twoHeads)
		regp->gpio_ext = NVReadCRTC(pNv, 0, NV_CRTC_GPIO_EXT);

	regp->config = 0x2; /* HSYNC mode */

	/* Some misc regs */
	if (pNv->Architecture == NV_ARCH_40) {
		regp->CRTC[NV_VGA_CRTCX_85] = 0xFF;
		regp->CRTC[NV_VGA_CRTCX_86] = 0x1;
	}

	regp->CRTC[NV_VGA_CRTCX_PIXEL] = (pScrn->depth + 1) / 8;
	/* Enable slaved mode */
	if (lvds_output || tmds_output)
		regp->CRTC[NV_VGA_CRTCX_PIXEL] |= (1 << 7);

	/* Generic PRAMDAC regs */

	if (pNv->Architecture >= NV_ARCH_10)
		/* Only bit that bios and blob set. */
		regp->nv10_cursync = (1 << 25);

	switch (pScrn->depth) {
		case 24:
		case 15:
			regp->general = 0x00100130;
			break;
		case 16:
		default:
			regp->general = 0x00101130;
			break;
	}
	if (pNv->alphaCursor)
		/* PIPE_LONG mode, something to do with the size of the cursor? */
		regp->general |= 1 << 29;

	/* Some values the blob sets */
	regp->unk_a20 = 0x0;
	regp->unk_a24 = 0xfffff;
	regp->unk_a34 = 0x1;
}

/* this could be set in nv_output, but would require some rework of load/save */
static void
nv_crtc_mode_set_fp_regs(xf86CrtcPtr crtc, DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];
	NVCrtcRegPtr savep = &pNv->SavedReg.crtc_reg[nv_crtc->head];
	NVOutputPrivatePtr nv_output = NULL;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	bool is_fp = false;
	bool is_lvds = false;
	uint32_t mode_ratio, panel_ratio;
	int i;

	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];
		/* assuming one fp output per crtc seems ok */
		nv_output = output->driver_private;

		if (output->crtc == crtc && nv_output->dcb->type == OUTPUT_LVDS)
			is_lvds = true;
		if (is_lvds || (output->crtc == crtc && nv_output->dcb->type == OUTPUT_TMDS)) {
			is_fp = true;
			break;
		}
	}
	if (!is_fp)
		return;

	regp->fp_horiz_regs[REG_DISP_END] = adjusted_mode->HDisplay - 1;
	regp->fp_horiz_regs[REG_DISP_TOTAL] = adjusted_mode->HTotal - 1;
	if ((adjusted_mode->HSyncStart - adjusted_mode->HDisplay) >= pNv->VBIOS.digital_min_front_porch)
		regp->fp_horiz_regs[REG_DISP_CRTC] = adjusted_mode->HDisplay;
	else
		regp->fp_horiz_regs[REG_DISP_CRTC] = adjusted_mode->HSyncStart - pNv->VBIOS.digital_min_front_porch - 1;
	regp->fp_horiz_regs[REG_DISP_SYNC_START] = adjusted_mode->HSyncStart - 1;
	regp->fp_horiz_regs[REG_DISP_SYNC_END] = adjusted_mode->HSyncEnd - 1;
	regp->fp_horiz_regs[REG_DISP_VALID_START] = adjusted_mode->HSkew;
	regp->fp_horiz_regs[REG_DISP_VALID_END] = adjusted_mode->HDisplay - 1;

	regp->fp_vert_regs[REG_DISP_END] = adjusted_mode->VDisplay - 1;
	regp->fp_vert_regs[REG_DISP_TOTAL] = adjusted_mode->VTotal - 1;
	regp->fp_vert_regs[REG_DISP_CRTC] = adjusted_mode->VTotal - 5 - 1;
	regp->fp_vert_regs[REG_DISP_SYNC_START] = adjusted_mode->VSyncStart - 1;
	regp->fp_vert_regs[REG_DISP_SYNC_END] = adjusted_mode->VSyncEnd - 1;
	regp->fp_vert_regs[REG_DISP_VALID_START] = 0;
	regp->fp_vert_regs[REG_DISP_VALID_END] = adjusted_mode->VDisplay - 1;

	/*
	* bit0: positive vsync
	* bit4: positive hsync
	* bit8: enable center mode
	* bit9: enable native mode
	* bit24: 12/24 bit interface (12bit=on, 24bit=off)
	* bit26: a bit sometimes seen on some g70 cards
	* bit28: fp display enable bit
	* bit31: set for dual link LVDS
	*/

	regp->fp_control = (savep->fp_control & 0x04100000) |
			   NV_RAMDAC_FP_CONTROL_DISPEN_POS;

	/* Deal with vsync/hsync polarity */
	/* LVDS screens do set this, but modes with +ve syncs are very rare */
	if (adjusted_mode->Flags & V_PVSYNC)
		regp->fp_control |= NV_RAMDAC_FP_CONTROL_VSYNC_POS;
	if (adjusted_mode->Flags & V_PHSYNC)
		regp->fp_control |= NV_RAMDAC_FP_CONTROL_HSYNC_POS;

	if (nv_output->scaling_mode == SCALE_PANEL ||
	    nv_output->scaling_mode == SCALE_NOSCALE) /* panel needs to scale */
		regp->fp_control |= NV_RAMDAC_FP_CONTROL_MODE_CENTER;
	/* This is also true for panel scaling, so we must put the panel scale check first */
	else if (mode->HDisplay == adjusted_mode->HDisplay &&
		 mode->VDisplay == adjusted_mode->VDisplay) /* native mode */
		regp->fp_control |= NV_RAMDAC_FP_CONTROL_MODE_NATIVE;
	else /* gpu needs to scale */
		regp->fp_control |= NV_RAMDAC_FP_CONTROL_MODE_SCALE;

	if (nvReadEXTDEV(pNv, NV_PEXTDEV_BOOT_0) & NV_PEXTDEV_BOOT_0_STRAP_FP_IFACE_12BIT)
		regp->fp_control |= NV_RAMDAC_FP_CONTROL_WIDTH_12;

	if (is_lvds && pNv->VBIOS.fp.dual_link)
		regp->fp_control |= (8 << 28);

	/* Use the generic value, and enable x-scaling, y-scaling, and the TMDS enable bit */
	regp->debug_0 = 0x01101191;
	/* We want automatic scaling */
	regp->debug_1 = 0;
	/* This can override HTOTAL and VTOTAL */
	regp->debug_2 = 0;

	/* Use 20.12 fixed point format to avoid floats */
	mode_ratio = (1 << 12) * mode->HDisplay / mode->VDisplay;
	panel_ratio = (1 << 12) * adjusted_mode->HDisplay / adjusted_mode->VDisplay;
	/* if ratios are equal, SCALE_ASPECT will automatically (and correctly)
	 * get treated the same as SCALE_FULLSCREEN */
	if (nv_output->scaling_mode == SCALE_ASPECT && mode_ratio != panel_ratio) {
		uint32_t diff, scale;

		if (mode_ratio < panel_ratio) {
			/* vertical needs to expand to glass size (automatic)
			 * horizontal needs to be scaled at vertical scale factor
			 * to maintain aspect */
	
			scale = (1 << 12) * mode->VDisplay / adjusted_mode->VDisplay;
			regp->debug_1 = 1 << 12 | ((scale >> 1) & 0xfff);

			/* restrict area of screen used, horizontally */
			diff = adjusted_mode->HDisplay -
			       adjusted_mode->VDisplay * mode_ratio / (1 << 12);
			regp->fp_horiz_regs[REG_DISP_VALID_START] += diff / 2;
			regp->fp_horiz_regs[REG_DISP_VALID_END] -= diff / 2;
		}

		if (mode_ratio > panel_ratio) {
			/* horizontal needs to expand to glass size (automatic)
			 * vertical needs to be scaled at horizontal scale factor
			 * to maintain aspect */

			scale = (1 << 12) * mode->HDisplay / adjusted_mode->HDisplay;
			regp->debug_1 = 1 << 28 | ((scale >> 1) & 0xfff) << 16;
			
			/* restrict area of screen used, vertically */
			diff = adjusted_mode->VDisplay -
			       (1 << 12) * adjusted_mode->HDisplay / mode_ratio;
			regp->fp_vert_regs[REG_DISP_VALID_START] += diff / 2;
			regp->fp_vert_regs[REG_DISP_VALID_END] -= diff / 2;
		}
	}

	/* Flatpanel support needs at least a NV10 */
	if (pNv->twoHeads) {
		/* Output property. */
		if (nv_output && nv_output->dithering) {
			if (pNv->NVArch == 0x11)
				regp->dither = savep->dither | 0x00010000;
			else {
				int i;
				regp->dither = savep->dither | 0x00000001;
				for (i = 0; i < 3; i++) {
					regp->dither_regs[i] = 0xe4e4e4e4;
					regp->dither_regs[i + 3] = 0x44444444;
				}
			}
		} else {
			if (pNv->NVArch != 0x11) {
				/* reset them */
				int i;
				for (i = 0; i < 3; i++) {
					regp->dither_regs[i] = savep->dither_regs[i];
					regp->dither_regs[i + 3] = savep->dither_regs[i + 3];
				}
			}
			regp->dither = savep->dither;
		}
	} else
		regp->dither = savep->dither;
}

/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static void
nv_crtc_mode_set(xf86CrtcPtr crtc, DisplayModePtr mode,
		 DisplayModePtr adjusted_mode,
		 int x, int y)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_mode_set is called for CRTC %d.\n", nv_crtc->head);

	xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Mode on CRTC %d\n", nv_crtc->head);
	xf86PrintModeline(pScrn->scrnIndex, mode);
	if (pNv->twoHeads)
		NVSetOwner(pScrn, nv_crtc->head);

	nv_crtc_mode_set_vga(crtc, mode, adjusted_mode);

	/* calculated in output_prepare, nv40 needs it written before calculating PLLs */
	if (pNv->Architecture == NV_ARCH_40) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Writing NV_RAMDAC_SEL_CLK %08X\n", pNv->ModeReg.sel_clk);
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_SEL_CLK, pNv->ModeReg.sel_clk);
	}
	nv_crtc_mode_set_regs(crtc, mode);
	nv_crtc_mode_set_fp_regs(crtc, mode, adjusted_mode);
	nv_crtc_calc_state_ext(crtc, mode, adjusted_mode->Clock);

	NVVgaProtect(pNv, nv_crtc->head, true);
	nv_crtc_load_state_ramdac(crtc, &pNv->ModeReg);
	nv_crtc_load_state_ext(crtc, &pNv->ModeReg, FALSE);
	nv_crtc_load_state_palette(crtc, &pNv->ModeReg);
	nv_crtc_load_state_vga(crtc, &pNv->ModeReg);
	nv_crtc_load_state_pll(crtc, &pNv->ModeReg);

	NVVgaProtect(pNv, nv_crtc->head, false);

	NVCrtcSetBase(crtc, x, y);

#if X_BYTE_ORDER == X_BIG_ENDIAN
	/* turn on LFB swapping */
	{
		unsigned char tmp;

		tmp = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_SWAPPING);
		tmp |= (1 << 7);
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_SWAPPING, tmp);
	}
#endif
}

static void nv_crtc_save(xf86CrtcPtr crtc)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_save is called for CRTC %d.\n", nv_crtc->head);

	/* We just came back from terminal, so unlock */
	NVCrtcLockUnlock(crtc, FALSE);

	nv_crtc_save_state_ramdac(crtc, &pNv->SavedReg);
	nv_crtc_save_state_vga(crtc, &pNv->SavedReg);
	nv_crtc_save_state_palette(crtc, &pNv->SavedReg);
	nv_crtc_save_state_ext(crtc, &pNv->SavedReg);
	nv_crtc_save_state_pll(crtc, &pNv->SavedReg);

	/* init some state to saved value */
	pNv->ModeReg.reg580 = pNv->SavedReg.reg580;
	pNv->ModeReg.sel_clk = pNv->SavedReg.sel_clk & ~(0x5 << 16);
	pNv->ModeReg.crtc_reg[nv_crtc->head].CRTC[NV_VGA_CRTCX_LCD] = pNv->SavedReg.crtc_reg[nv_crtc->head].CRTC[NV_VGA_CRTCX_LCD];
}

static void nv_crtc_restore(xf86CrtcPtr crtc)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(pScrn);
	RIVA_HW_STATE *state;
	NVCrtcRegPtr savep;

	state = &pNv->SavedReg;
	savep = &pNv->SavedReg.crtc_reg[nv_crtc->head];

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_restore is called for CRTC %d.\n", nv_crtc->head);

	/* Just to be safe */
	NVCrtcLockUnlock(crtc, FALSE);

	NVVgaProtect(pNv, nv_crtc->head, true);
	nv_crtc_load_state_ramdac(crtc, &pNv->SavedReg);
	nv_crtc_load_state_ext(crtc, &pNv->SavedReg, TRUE);
	nv_crtc_load_state_palette(crtc, &pNv->SavedReg);
	nv_crtc_load_state_vga(crtc, &pNv->SavedReg);
	nv_crtc_load_state_pll(crtc, &pNv->SavedReg);
	NVVgaProtect(pNv, nv_crtc->head, false);

	nv_crtc->last_dpms = NV_DPMS_CLEARED;
}

static void nv_crtc_prepare(xf86CrtcPtr crtc)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_prepare is called for CRTC %d.\n", nv_crtc->head);

	/* Just in case */
	NVCrtcLockUnlock(crtc, 0);

	crtc->funcs->dpms(crtc, DPMSModeOff);

	/* Sync the engine before adjust mode */
	if (pNv->EXADriverPtr) {
		exaMarkSync(pScrn->pScreen);
		exaWaitSync(pScrn->pScreen);
	}

	NVBlankScreen(pScrn, nv_crtc->head, true);

	/* Some more preperation. */
	NVCrtcWriteCRTC(crtc, NV_CRTC_CONFIG, 0x1); /* Go to non-vga mode/out of enhanced mode */
	if (pNv->Architecture == NV_ARCH_40) {
		uint32_t reg900 = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_900);
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_900, reg900 & ~0x10000);
	}
}

static void nv_crtc_commit(xf86CrtcPtr crtc)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_commit for CRTC %d.\n", nv_crtc->head);

	crtc->funcs->dpms (crtc, DPMSModeOn);

	if (crtc->scrn->pScreen != NULL) {
		NVPtr pNv = NVPTR(crtc->scrn);

		xf86_reload_cursors (crtc->scrn->pScreen);
		if (!pNv->alphaCursor) {
			/* this works round the fact that xf86_reload_cursors
			 * will quite happily show the hw cursor when it knows
			 * the hardware can't do alpha, and the current cursor
			 * has an alpha channel
			 */
			xf86ForceHWCursor(crtc->scrn->pScreen, 1);
			xf86ForceHWCursor(crtc->scrn->pScreen, 0);
		}
	}
}

static Bool nv_crtc_lock(xf86CrtcPtr crtc)
{
	return FALSE;
}

static void nv_crtc_unlock(xf86CrtcPtr crtc)
{
}

static void
nv_crtc_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, CARD16 *blue,
					int size)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcRegPtr regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];
	int i, j;

	switch (pScrn->depth) {
	case 15:
		/* R5G5B5 */
		/* We've got 5 bit (32 values) colors and 256 registers for each color */
		for (i = 0; i < 32; i++) {
			for (j = 0; j < 8; j++) {
				regp->DAC[(i*8 + j) * 3 + 0] = red[i] >> 8;
				regp->DAC[(i*8 + j) * 3 + 1] = green[i] >> 8;
				regp->DAC[(i*8 + j) * 3 + 2] = blue[i] >> 8;
			}
		}
		break;
	case 16:
		/* R5G6B5 */
		/* First deal with the 5 bit colors */
		for (i = 0; i < 32; i++) {
			for (j = 0; j < 8; j++) {
				regp->DAC[(i*8 + j) * 3 + 0] = red[i] >> 8;
				regp->DAC[(i*8 + j) * 3 + 2] = blue[i] >> 8;
			}
		}
		/* Now deal with the 6 bit color */
		for (i = 0; i < 64; i++) {
			for (j = 0; j < 4; j++) {
				regp->DAC[(i*4 + j) * 3 + 1] = green[i] >> 8;
			}
		}
		break;
	default:
		/* R8G8B8 */
		for (i = 0; i < 256; i++) {
			regp->DAC[i * 3] = red[i] >> 8;
			regp->DAC[(i * 3) + 1] = green[i] >> 8;
			regp->DAC[(i * 3) + 2] = blue[i] >> 8;
		}
		break;
	}

	nv_crtc_load_state_palette(crtc, &pNv->ModeReg);
}

/**
 * Allocates memory for a locked-in-framebuffer shadow of the given
 * width and height for this CRTC's rotated shadow framebuffer.
 */
 
static void *
nv_crtc_shadow_allocate (xf86CrtcPtr crtc, int width, int height)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	void *offset;

	unsigned long rotate_pitch;
	int size, align = 64;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_shadow_allocate is called.\n");

	rotate_pitch = pScrn->displayWidth * (pScrn->bitsPerPixel/8);
	size = rotate_pitch * height;

	assert(nv_crtc->shadow == NULL);
	if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN,
			align, size, &nv_crtc->shadow)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to allocate memory for shadow buffer!\n");
		return NULL;
	}

	if (nv_crtc->shadow && nouveau_bo_map(nv_crtc->shadow, NOUVEAU_BO_RDWR)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"Failed to map shadow buffer.\n");
		return NULL;
	}

	offset = nv_crtc->shadow->map;
	return offset;
}

/**
 * Creates a pixmap for this CRTC's rotated shadow framebuffer.
 */
static PixmapPtr
nv_crtc_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	ScreenPtr pScreen = pScrn->pScreen;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	unsigned long rotate_pitch;
	PixmapPtr rotate_pixmap;
	struct nouveau_pixmap *nvpix;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_shadow_create is called.\n");

	if (!data)
		data = crtc->funcs->shadow_allocate (crtc, width, height);

	rotate_pitch = pScrn->displayWidth * (pScrn->bitsPerPixel/8);

	/* Create a dummy pixmap, to get a private that will be accepted by the system.*/
	rotate_pixmap = pScreen->CreatePixmap(pScreen, 
								0, /* width */
								0, /* height */
	#ifdef CREATE_PIXMAP_USAGE_SCRATCH /* there seems to have been no api bump */
								pScrn->depth,
								0);
	#else
								pScrn->depth);
	#endif /* CREATE_PIXMAP_USAGE_SCRATCH */

	if (rotate_pixmap == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Couldn't allocate shadow pixmap for rotated CRTC\n");
	}

	nvpix = exaGetPixmapDriverPrivate(rotate_pixmap);
	if (!nvpix) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No initial shadow private available for rotation.\n");
	} else {
		nvpix->bo = nv_crtc->shadow;
		nvpix->mapped = TRUE;
	}

	/* Modify the pixmap to actually be the one we need. */
	pScreen->ModifyPixmapHeader(rotate_pixmap,
					width,
					height,
					pScrn->depth,
					pScrn->bitsPerPixel,
					rotate_pitch,
					data);

	nvpix = exaGetPixmapDriverPrivate(rotate_pixmap);
	if (!nvpix || !nvpix->bo)
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No final shadow private available for rotation.\n");

	return rotate_pixmap;
}

static void
nv_crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	ScreenPtr pScreen = pScrn->pScreen;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_crtc_shadow_destroy is called.\n");

	if (rotate_pixmap) { /* This should also unmap the buffer object if relevant. */
		pScreen->DestroyPixmap(rotate_pixmap);
	}

	nv_crtc->shadow = NULL;
}

static const xf86CrtcFuncsRec nv_crtc_funcs = {
	.dpms = nv_crtc_dpms,
	.save = nv_crtc_save,
	.restore = nv_crtc_restore,
	.mode_fixup = nv_crtc_mode_fixup,
	.mode_set = nv_crtc_mode_set,
	.prepare = nv_crtc_prepare,
	.commit = nv_crtc_commit,
	.destroy = NULL, /* XXX */
	.lock = nv_crtc_lock,
	.unlock = nv_crtc_unlock,
	.set_cursor_colors = NULL, /* Alpha cursors do not need this */
	.set_cursor_position = nv_crtc_set_cursor_position,
	.show_cursor = nv_crtc_show_cursor,
	.hide_cursor = nv_crtc_hide_cursor,
	.load_cursor_argb = nv_crtc_load_cursor_argb,
	.gamma_set = nv_crtc_gamma_set,
	.shadow_create = nv_crtc_shadow_create,
	.shadow_allocate = nv_crtc_shadow_allocate,
	.shadow_destroy = nv_crtc_shadow_destroy,
};

void
nv_crtc_init(ScrnInfoPtr pScrn, int crtc_num)
{
	NVPtr pNv = NVPTR(pScrn);
	static xf86CrtcFuncsRec crtcfuncs;
	xf86CrtcPtr crtc;
	NVCrtcPrivatePtr nv_crtc;
	NVCrtcRegPtr regp = &pNv->ModeReg.crtc_reg[crtc_num];
	int i;

	crtcfuncs = nv_crtc_funcs;

	/* NV04-NV10 doesn't support alpha cursors */
	if (pNv->NVArch < 0x11) {
		crtcfuncs.set_cursor_colors = nv_crtc_set_cursor_colors;
		crtcfuncs.load_cursor_image = nv_crtc_load_cursor_image;
		crtcfuncs.load_cursor_argb = NULL;
	}
	if (pNv->NoAccel) {
		crtcfuncs.shadow_create = NULL;
		crtcfuncs.shadow_allocate = NULL;
		crtcfuncs.shadow_destroy = NULL;
	}
	
	crtc = xf86CrtcCreate(pScrn, &crtcfuncs);
	if (crtc == NULL)
		return;

	nv_crtc = xnfcalloc (sizeof (NVCrtcPrivateRec), 1);
	nv_crtc->head = crtc_num;
	nv_crtc->last_dpms = NV_DPMS_CLEARED;

	crtc->driver_private = nv_crtc;

	/* Initialise the default LUT table. */
	for (i = 0; i < 256; i++) {
		regp->DAC[i*3] = i;
		regp->DAC[(i*3)+1] = i;
		regp->DAC[(i*3)+2] = i;
	}

	NVCrtcLockUnlock(crtc, FALSE);
}

static void nv_crtc_load_state_vga(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	int i;
	NVCrtcRegPtr regp = &state->crtc_reg[nv_crtc->head];

	NVWritePVIO(pNv, nv_crtc->head, VGA_MISC_OUT_W, regp->MiscOutReg);

	for (i = 0; i < 5; i++)
		NVWriteVgaSeq(pNv, nv_crtc->head, i, regp->Sequencer[i]);

	/* Ensure CRTC registers 0-7 are unlocked by clearing bit 7 of CRTC[17] */
	NVWriteVgaCrtc(pNv, nv_crtc->head, 17, regp->CRTC[17] & ~0x80);

	for (i = 0; i < 25; i++)
		NVWriteVgaCrtc(pNv, nv_crtc->head, i, regp->CRTC[i]);

	for (i = 0; i < 9; i++)
		NVWriteVgaGr(pNv, nv_crtc->head, i, regp->Graphics[i]);

	NVSetEnablePalette(pNv, nv_crtc->head, true);
	for (i = 0; i < 21; i++)
		NVWriteVgaAttr(pNv, nv_crtc->head, i, regp->Attribute[i]);

	NVSetEnablePalette(pNv, nv_crtc->head, false);
}

static void nv_crtc_load_state_ext(xf86CrtcPtr crtc, RIVA_HW_STATE *state, Bool override)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);    
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp;
	int i;

	regp = &state->crtc_reg[nv_crtc->head];

	if (pNv->Architecture >= NV_ARCH_10) {
		if (pNv->twoHeads)
			/* setting FSEL *must* come before CRTCX_LCD, as writing CRTCX_LCD sets some
			 * bits (16 & 17) in FSEL that should not be overwritten by writing FSEL */
			NVCrtcWriteCRTC(crtc, NV_CRTC_FSEL, regp->head);

		nvWriteVIDEO(pNv, NV_PVIDEO_STOP, 1);
		nvWriteVIDEO(pNv, NV_PVIDEO_INTR_EN, 0);
		nvWriteVIDEO(pNv, NV_PVIDEO_OFFSET_BUFF(0), 0);
		nvWriteVIDEO(pNv, NV_PVIDEO_OFFSET_BUFF(1), 0);
		nvWriteVIDEO(pNv, NV_PVIDEO_LIMIT(0), pNv->VRAMPhysicalSize - 1);
		nvWriteVIDEO(pNv, NV_PVIDEO_LIMIT(1), pNv->VRAMPhysicalSize - 1);
		nvWriteVIDEO(pNv, NV_PVIDEO_UVPLANE_LIMIT(0), pNv->VRAMPhysicalSize - 1);
		nvWriteVIDEO(pNv, NV_PVIDEO_UVPLANE_LIMIT(1), pNv->VRAMPhysicalSize - 1);
		nvWriteMC(pNv, NV_PBUS_POWERCTRL_2, 0);

		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_BUFFER, regp->CRTC[NV_VGA_CRTCX_BUFFER]);
		NVCrtcWriteCRTC(crtc, NV_CRTC_CURSOR_CONFIG, regp->cursorConfig);
		NVCrtcWriteCRTC(crtc, NV_CRTC_0830, regp->unk830);
		NVCrtcWriteCRTC(crtc, NV_CRTC_0834, regp->unk834);
		if (pNv->Architecture == NV_ARCH_40) {
			NVCrtcWriteCRTC(crtc, NV_CRTC_0850, regp->unk850);
			NVCrtcWriteCRTC(crtc, NV_CRTC_GPIO_EXT, regp->gpio_ext);
		}

		if (pNv->Architecture == NV_ARCH_40) {
			uint32_t reg900 = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_900);
			if (regp->config == 0x2) /* enhanced "horizontal only" non-vga mode */
				NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_900, reg900 | 0x10000);
			else
				NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_900, reg900 & ~0x10000);
		}
	}

	NVCrtcWriteCRTC(crtc, NV_CRTC_CONFIG, regp->config);
	NVCrtcWriteCRTC(crtc, NV_CRTC_GPIO, regp->gpio);

	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_REPAINT0, regp->CRTC[NV_VGA_CRTCX_REPAINT0]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_REPAINT1, regp->CRTC[NV_VGA_CRTCX_REPAINT1]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_LSR, regp->CRTC[NV_VGA_CRTCX_LSR]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_PIXEL, regp->CRTC[NV_VGA_CRTCX_PIXEL]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_LCD, regp->CRTC[NV_VGA_CRTCX_LCD]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_HEB, regp->CRTC[NV_VGA_CRTCX_HEB]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO1, regp->CRTC[NV_VGA_CRTCX_FIFO1]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO0, regp->CRTC[NV_VGA_CRTCX_FIFO0]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO_LWM, regp->CRTC[NV_VGA_CRTCX_FIFO_LWM]);
	if (pNv->Architecture >= NV_ARCH_30)
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO_LWM_NV30, regp->CRTC[NV_VGA_CRTCX_FIFO_LWM_NV30]);

	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_CURCTL0, regp->CRTC[NV_VGA_CRTCX_CURCTL0]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_CURCTL1, regp->CRTC[NV_VGA_CRTCX_CURCTL1]);
	if (pNv->Architecture == NV_ARCH_40) /* HW bug */
		nv_crtc_fix_nv40_hw_cursor(pScrn, nv_crtc->head);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_CURCTL2, regp->CRTC[NV_VGA_CRTCX_CURCTL2]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_INTERLACE, regp->CRTC[NV_VGA_CRTCX_INTERLACE]);

	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_26, regp->CRTC[NV_VGA_CRTCX_26]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_3B, regp->CRTC[NV_VGA_CRTCX_3B]);
	NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_SCRATCH4, regp->CRTC[NV_VGA_CRTCX_SCRATCH4]);
	if (pNv->Architecture >= NV_ARCH_10) {
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_EXTRA, regp->CRTC[NV_VGA_CRTCX_EXTRA]);
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_45, regp->CRTC[NV_VGA_CRTCX_45]);
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_4B, regp->CRTC[NV_VGA_CRTCX_4B]);
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_52, regp->CRTC[NV_VGA_CRTCX_52]);
	}
	/* NV11 and NV20 stop at 0x52. */
	if (pNv->NVArch >= 0x17 && pNv->twoHeads) {
		if (override)
			for (i = 0; i < 0x10; i++)
				NVWriteVgaCrtc5758(pNv, nv_crtc->head, i, regp->CR58[i]);

		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FP_HTIMING, regp->CRTC[NV_VGA_CRTCX_FP_HTIMING]);
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FP_VTIMING, regp->CRTC[NV_VGA_CRTCX_FP_VTIMING]);

		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_59, regp->CRTC[NV_VGA_CRTCX_59]);

		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_85, regp->CRTC[NV_VGA_CRTCX_85]);
		NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_86, regp->CRTC[NV_VGA_CRTCX_86]);
	}

	if (override)
		NVCrtcWriteCRTC(crtc, NV_CRTC_START, regp->fb_start);

	/* Setting 1 on this value gives you interrupts for every vblank period. */
	NVCrtcWriteCRTC(crtc, NV_CRTC_INTR_EN_0, 0);
	NVCrtcWriteCRTC(crtc, NV_CRTC_INTR_0, NV_CRTC_INTR_VBLANK);
}

static void nv_crtc_save_state_vga(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	int i;
	NVCrtcRegPtr regp = &state->crtc_reg[nv_crtc->head];

	regp->MiscOutReg = NVReadPVIO(pNv, nv_crtc->head, VGA_MISC_OUT_R);

	for (i = 0; i < 25; i++)
		regp->CRTC[i] = NVReadVgaCrtc(pNv, nv_crtc->head, i);

	NVSetEnablePalette(pNv, nv_crtc->head, true);
	for (i = 0; i < 21; i++)
		regp->Attribute[i] = NVReadVgaAttr(pNv, nv_crtc->head, i);
	NVSetEnablePalette(pNv, nv_crtc->head, false);

	for (i = 0; i < 9; i++)
		regp->Graphics[i] = NVReadVgaGr(pNv, nv_crtc->head, i);

	for (i = 0; i < 5; i++)
		regp->Sequencer[i] = NVReadVgaSeq(pNv, nv_crtc->head, i);
}

static void nv_crtc_save_state_ext(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp;
	int i;

	regp = &state->crtc_reg[nv_crtc->head];

	regp->CRTC[NV_VGA_CRTCX_LCD] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_LCD);
	regp->CRTC[NV_VGA_CRTCX_REPAINT0] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_REPAINT0);
	regp->CRTC[NV_VGA_CRTCX_REPAINT1] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_REPAINT1);
	regp->CRTC[NV_VGA_CRTCX_LSR] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_LSR);
	regp->CRTC[NV_VGA_CRTCX_PIXEL] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_PIXEL);
	regp->CRTC[NV_VGA_CRTCX_HEB] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_HEB);
	regp->CRTC[NV_VGA_CRTCX_FIFO1] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO1);

	regp->CRTC[NV_VGA_CRTCX_FIFO0] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO0);
	regp->CRTC[NV_VGA_CRTCX_FIFO_LWM] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO_LWM);
	regp->CRTC[NV_VGA_CRTCX_BUFFER] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_BUFFER);
	if (pNv->Architecture >= NV_ARCH_30)
		regp->CRTC[NV_VGA_CRTCX_FIFO_LWM_NV30] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FIFO_LWM_NV30);
	regp->CRTC[NV_VGA_CRTCX_CURCTL0] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_CURCTL0);
	regp->CRTC[NV_VGA_CRTCX_CURCTL1] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_CURCTL1);
	regp->CRTC[NV_VGA_CRTCX_CURCTL2] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_CURCTL2);
	regp->CRTC[NV_VGA_CRTCX_INTERLACE] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_INTERLACE);

	if (pNv->Architecture >= NV_ARCH_10) {
		regp->unk830 = NVCrtcReadCRTC(crtc, NV_CRTC_0830);
		regp->unk834 = NVCrtcReadCRTC(crtc, NV_CRTC_0834);
		if (pNv->Architecture == NV_ARCH_40) {
			regp->unk850 = NVCrtcReadCRTC(crtc, NV_CRTC_0850);
			regp->gpio_ext = NVCrtcReadCRTC(crtc, NV_CRTC_GPIO_EXT);
		}
		if (pNv->twoHeads) {
			regp->head = NVCrtcReadCRTC(crtc, NV_CRTC_FSEL);
			regp->crtcOwner = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_OWNER);
		}
		regp->cursorConfig = NVCrtcReadCRTC(crtc, NV_CRTC_CURSOR_CONFIG);
	}

	regp->gpio = NVCrtcReadCRTC(crtc, NV_CRTC_GPIO);
	regp->config = NVCrtcReadCRTC(crtc, NV_CRTC_CONFIG);

	regp->CRTC[NV_VGA_CRTCX_26] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_26);
	regp->CRTC[NV_VGA_CRTCX_3B] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_3B);
	regp->CRTC[NV_VGA_CRTCX_SCRATCH4] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_SCRATCH4);
	if (pNv->Architecture >= NV_ARCH_10) {
		regp->CRTC[NV_VGA_CRTCX_EXTRA] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_EXTRA);
		regp->CRTC[NV_VGA_CRTCX_45] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_45);
		regp->CRTC[NV_VGA_CRTCX_4B] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_4B);
		regp->CRTC[NV_VGA_CRTCX_52] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_52);
	}
	/* NV11 and NV20 don't have this, they stop at 0x52. */
	if (pNv->NVArch >= 0x17 && pNv->twoHeads) {
		for (i = 0; i < 0x10; i++)
			regp->CR58[i] = NVReadVgaCrtc5758(pNv, nv_crtc->head, i);

		regp->CRTC[NV_VGA_CRTCX_59] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_59);
		regp->CRTC[NV_VGA_CRTCX_FP_HTIMING] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FP_HTIMING);
		regp->CRTC[NV_VGA_CRTCX_FP_VTIMING] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_FP_VTIMING);

		regp->CRTC[NV_VGA_CRTCX_85] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_85);
		regp->CRTC[NV_VGA_CRTCX_86] = NVReadVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_86);
	}

	regp->fb_start = NVCrtcReadCRTC(crtc, NV_CRTC_START);
}

static void nv_crtc_save_state_ramdac(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);    
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp;
	int i;

	regp = &state->crtc_reg[nv_crtc->head];

	regp->general = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_GENERAL_CONTROL);

	if (pNv->twoHeads) {
		regp->fp_control	= NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_CONTROL);
		regp->debug_0	= NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_DEBUG_0);
		regp->debug_1	= NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_DEBUG_1);
		regp->debug_2	= NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_DEBUG_2);

		regp->unk_a20 = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_A20);
		regp->unk_a24 = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_A24);
		regp->unk_a34 = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_A34);
	}

	if (pNv->NVArch == 0x11) {
		regp->dither = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_DITHER_NV11);
	} else if (pNv->twoHeads) {
		regp->dither = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_DITHER);
		for (i = 0; i < 3; i++) {
			regp->dither_regs[i] = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_850 + i * 4);
			regp->dither_regs[i + 3] = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_FP_85C + i * 4);
		}
	}
	if (pNv->Architecture >= NV_ARCH_10)
		regp->nv10_cursync = NVCrtcReadRAMDAC(crtc, NV_RAMDAC_NV10_CURSYNC);

	/* The regs below are 0 for non-flatpanels, so you can load and save them */

	for (i = 0; i < 7; i++) {
		uint32_t ramdac_reg = NV_RAMDAC_FP_HDISP_END + (i * 4);
		regp->fp_horiz_regs[i] = NVCrtcReadRAMDAC(crtc, ramdac_reg);
	}

	for (i = 0; i < 7; i++) {
		uint32_t ramdac_reg = NV_RAMDAC_FP_VDISP_END + (i * 4);
		regp->fp_vert_regs[i] = NVCrtcReadRAMDAC(crtc, ramdac_reg);
	}
}

static void nv_crtc_load_state_ramdac(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);    
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVCrtcRegPtr regp;
	int i;

	regp = &state->crtc_reg[nv_crtc->head];

	NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_GENERAL_CONTROL, regp->general);

	if (pNv->twoHeads) {
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_CONTROL, regp->fp_control);
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_DEBUG_0, regp->debug_0);
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_DEBUG_1, regp->debug_1);
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_DEBUG_2, regp->debug_2);
		if (pNv->NVArch == 0x30) { /* For unknown purposes. */
			uint32_t reg890 = NVCrtcReadRAMDAC(crtc, NV30_RAMDAC_890);
			NVCrtcWriteRAMDAC(crtc, NV30_RAMDAC_89C, reg890);
		}

		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_A20, regp->unk_a20);
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_A24, regp->unk_a24);
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_A34, regp->unk_a34);
	}

	if (pNv->NVArch == 0x11)
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_DITHER_NV11, regp->dither);
	else if (pNv->twoHeads) {
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_DITHER, regp->dither);
		for (i = 0; i < 3; i++) {
			NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_850 + i * 4, regp->dither_regs[i]);
			NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_FP_85C + i * 4, regp->dither_regs[i + 3]);
		}
	}
	if (pNv->Architecture >= NV_ARCH_10)
		NVCrtcWriteRAMDAC(crtc, NV_RAMDAC_NV10_CURSYNC, regp->nv10_cursync);

	/* The regs below are 0 for non-flatpanels, so you can load and save them */

	for (i = 0; i < 7; i++) {
		uint32_t ramdac_reg = NV_RAMDAC_FP_HDISP_END + (i * 4);
		NVCrtcWriteRAMDAC(crtc, ramdac_reg, regp->fp_horiz_regs[i]);
	}

	for (i = 0; i < 7; i++) {
		uint32_t ramdac_reg = NV_RAMDAC_FP_VDISP_END + (i * 4);
		NVCrtcWriteRAMDAC(crtc, ramdac_reg, regp->fp_vert_regs[i]);
	}
}

void NVCrtcSetBase(xf86CrtcPtr crtc, int x, int y)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);    
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	uint32_t start = (y * pScrn->displayWidth + x) * pScrn->bitsPerPixel / 8;

	if (crtc->rotatedData != NULL) /* we do not exist on the real framebuffer */
		start = nv_crtc->shadow->offset;
	else
		start += pNv->FB->offset;

	/* 30 bits addresses in 32 bits according to haiku */
	NVCrtcWriteCRTC(crtc, NV_CRTC_START, start & 0xfffffffc);

	crtc->x = x;
	crtc->y = y;
}

static void nv_crtc_save_state_palette(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(crtc->scrn);
	uint32_t mmiobase = nv_crtc->head ? NV_PDIO1_OFFSET : NV_PDIO0_OFFSET;
	int i;

	VGA_WR08(pNv->REGS, VGA_DAC_MASK + mmiobase, 0xff);
	VGA_WR08(pNv->REGS, VGA_DAC_READ_ADDR + mmiobase, 0x0);

	for (i = 0; i < 768; i++) {
		state->crtc_reg[nv_crtc->head].DAC[i] = NV_RD08(pNv->REGS, VGA_DAC_DATA + mmiobase);
		DDXMMIOH("nv_crtc_save_state_palette: head %d reg 0x%04x data 0x%02x\n", nv_crtc->head, VGA_DAC_DATA + mmiobase, state->crtc_reg[nv_crtc->head].DAC[i]);
	}

	NVSetEnablePalette(pNv, nv_crtc->head, false);
}
static void nv_crtc_load_state_palette(xf86CrtcPtr crtc, RIVA_HW_STATE *state)
{
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
	NVPtr pNv = NVPTR(crtc->scrn);
	uint32_t mmiobase = nv_crtc->head ? NV_PDIO1_OFFSET : NV_PDIO0_OFFSET;
	int i;

	VGA_WR08(pNv->REGS, VGA_DAC_MASK + mmiobase, 0xff);
	VGA_WR08(pNv->REGS, VGA_DAC_WRITE_ADDR + mmiobase, 0x0);

	for (i = 0; i < 768; i++) {
		DDXMMIOH("nv_crtc_load_state_palette: head %d reg 0x%04x data 0x%02x\n", nv_crtc->head, VGA_DAC_DATA + mmiobase, state->crtc_reg[nv_crtc->head].DAC[i]);
		NV_WR08(pNv->REGS, VGA_DAC_DATA + mmiobase, state->crtc_reg[nv_crtc->head].DAC[i]);
	}

	NVSetEnablePalette(pNv, nv_crtc->head, false);
}
