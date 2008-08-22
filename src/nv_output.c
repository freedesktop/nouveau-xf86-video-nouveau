/*
 * Copyright 2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * Copyright 2007-2008 Stuart Bennett
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
#include "X11/Xatom.h"

#include "xf86Crtc.h"
#include "nv_include.h"

const char *OutputType[] = {
    "None",
    "VGA",
    "DVI",
    "LVDS",
    "S-video",
    "Composite",
};

const char *MonTypeName[7] = {
    "AUTO",
    "NONE",
    "CRT",
    "LVDS",
    "TMDS",
    "CTV",
    "STV"
};

static int nv_output_ramdac_offset(xf86OutputPtr output)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	int offset = 0;

	if (nv_output->dcb->or & (8 | OUTPUT_C))
		offset += 0x68;
	if (nv_output->dcb->or & (8 | OUTPUT_B))
		offset += 0x2000;

	return offset;
}

static void dpms_update_fp_control(xf86OutputPtr output, int mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	NVPtr pNv = NVPTR(output->scrn);
	NVCrtcPrivatePtr nv_crtc;
	NVCrtcRegPtr regp;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(output->scrn);
	int i;

	if (mode == DPMSModeOn) {
		nv_crtc = output->crtc->driver_private;
		regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];

		nv_crtc->fp_users |= 1 << nv_output->dcb->index;
		NVWriteRAMDAC(pNv, nv_crtc->head, NV_RAMDAC_FP_CONTROL, regp->fp_control & ~0x20000022);
	} else
		for (i = 0; i <= pNv->twoHeads; i++) {
			nv_crtc = xf86_config->crtc[i]->driver_private;
			regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];

			nv_crtc->fp_users &= ~(1 << nv_output->dcb->index);
			if (!nv_crtc->fp_users) {
				/* cut the FP output */
				regp->fp_control |= 0x20000022;
				NVWriteRAMDAC(pNv, nv_crtc->head, NV_RAMDAC_FP_CONTROL, regp->fp_control);
			}
		}
}

static void nv_digital_output_prepare_sel_clk(xf86OutputPtr output);

static void
nv_lvds_output_dpms(xf86OutputPtr output, int mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_lvds_output_dpms is called with mode %d.\n", mode);

	if (nv_output->last_dpms == mode)
		return;
	nv_output->last_dpms = mode;

	if (nv_output->dcb->lvdsconf.use_power_scripts) {
		xf86CrtcPtr crtc = output->crtc;
		/* when removing an output, crtc may not be set, but PANEL_OFF must still be run */
		int head = nv_get_digital_bound_head(pNv, nv_output->dcb->or);
		int pclk = nv_output->native_mode->Clock;

		if (crtc)
			head = ((NVCrtcPrivatePtr)crtc->driver_private)->head;

		if (mode == DPMSModeOn)
			call_lvds_script(pScrn, nv_output->dcb, head, LVDS_PANEL_ON, pclk);
		else
			call_lvds_script(pScrn, nv_output->dcb, head, LVDS_PANEL_OFF, pclk);
	}

	dpms_update_fp_control(output, mode);

	if (mode == DPMSModeOn)
		nv_digital_output_prepare_sel_clk(output);
	else {
		pNv->ModeReg.sel_clk = NVReadRAMDAC(pNv, 0, NV_RAMDAC_SEL_CLK);
		pNv->ModeReg.sel_clk &= ~0xf0;
	}
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_SEL_CLK, pNv->ModeReg.sel_clk);
}

static void
nv_analog_output_dpms(xf86OutputPtr output, int mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(output->scrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_analog_output_dpms is called with mode %d.\n", mode);

	if (nv_output->last_dpms == mode)
		return;
	nv_output->last_dpms = mode;

	if (pNv->twoHeads) {
		uint32_t outputval = NVReadRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + nv_output_ramdac_offset(output));

		if (mode == DPMSModeOff)
			NVWriteRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + nv_output_ramdac_offset(output),
				      outputval & ~NV_RAMDAC_OUTPUT_DAC_ENABLE);
		else if (mode == DPMSModeOn)
			NVWriteRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + nv_output_ramdac_offset(output),
				      outputval | NV_RAMDAC_OUTPUT_DAC_ENABLE);
	}
}

static void
nv_tmds_output_dpms(xf86OutputPtr output, int mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(output->scrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,"nv_tmds_output_dpms is called with mode %d.\n", mode);

	if (nv_output->last_dpms == mode)
		return;
	nv_output->last_dpms = mode;

	dpms_update_fp_control(output, mode);

	if (nv_output->dcb->location != LOC_ON_CHIP) {
		NVCrtcPrivatePtr nv_crtc;
		int i;

		if (mode == DPMSModeOn) {
			nv_crtc = output->crtc->driver_private;
			NVWriteVgaCrtc(pNv, nv_crtc->head, NV_VGA_CRTCX_LCD,
				       pNv->ModeReg.crtc_reg[nv_crtc->head].CRTC[NV_VGA_CRTCX_LCD]);
		} else
			for (i = 0; i <= pNv->twoHeads; i++)
				NVWriteVgaCrtc(pNv, i, NV_VGA_CRTCX_LCD,
					       NVReadVgaCrtc(pNv, i, NV_VGA_CRTCX_LCD) & ~((nv_output->dcb->or << 4) & 0x30));
	}
}

static void nv_output_save(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVOutputPrivatePtr nv_output = output->driver_private;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_save is called.\n");

	if (pNv->twoHeads && nv_output->dcb->type == OUTPUT_ANALOG)
		nv_output->restore.output = NVReadRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + nv_output_ramdac_offset(output));
	if (nv_output->dcb->type == OUTPUT_TMDS || nv_output->dcb->type == OUTPUT_LVDS)
		nv_output->restore.head = nv_get_digital_bound_head(pNv, nv_output->dcb->or);
}

static uint32_t nv_get_clock_from_crtc(ScrnInfoPtr pScrn, RIVA_HW_STATE *state, uint8_t crtc)
{
	NVPtr pNv = NVPTR(pScrn);
	struct pll_lims pll_lim;
	uint32_t vplla = state->crtc_reg[crtc].vpll_a;
	uint32_t vpllb = state->crtc_reg[crtc].vpll_b;
	bool nv40_single = pNv->Architecture == 0x40 &&
			   ((!crtc && state->reg580 & NV_RAMDAC_580_VPLL1_ACTIVE) ||
			    (crtc && state->reg580 & NV_RAMDAC_580_VPLL2_ACTIVE));

	if (!get_pll_limits(pScrn, crtc ? VPLL2 : VPLL1, &pll_lim))
		return 0;

	return nv_decode_pll_highregs(pNv, vplla, vpllb, nv40_single, pll_lim.refclk);
}

static void nv_output_restore(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVOutputPrivatePtr nv_output = output->driver_private;
	int head = nv_output->restore.head;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_restore is called.\n");

	if (pNv->twoHeads && nv_output->dcb->type == OUTPUT_ANALOG)
		NVWriteRAMDAC(pNv, 0,
			      NV_RAMDAC_OUTPUT + nv_output_ramdac_offset(output),
			      nv_output->restore.output);
	if (nv_output->dcb->type == OUTPUT_LVDS)
		call_lvds_script(pScrn, nv_output->dcb, head, LVDS_PANEL_ON,
				 nv_output->native_mode->Clock);
	if (nv_output->dcb->type == OUTPUT_TMDS) {
		int clock = nv_get_clock_from_crtc(pScrn, &pNv->SavedReg, head);

		run_tmds_table(pScrn, nv_output->dcb, head, clock);
	}

	nv_output->last_dpms = NV_DPMS_CLEARED;
}

static int nv_output_mode_valid(xf86OutputPtr output, DisplayModePtr mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	NVPtr pNv = NVPTR(output->scrn);

	if (!output->doubleScanAllowed && mode->Flags & V_DBLSCAN)
		return MODE_NO_DBLESCAN;
	if (!output->interlaceAllowed && mode->Flags & V_INTERLACE)
		return MODE_NO_INTERLACE;

	if (nv_output->dcb->type == OUTPUT_ANALOG) {
		if (mode->Clock > (pNv->twoStagePLL ? 400000 : 350000))
			return MODE_CLOCK_HIGH;
		if (mode->Clock < 12000)
			return MODE_CLOCK_LOW;
	}
	if (nv_output->dcb->type == OUTPUT_LVDS || nv_output->dcb->type == OUTPUT_TMDS)
		/* No modes > panel's native res */
		if (mode->HDisplay > nv_output->native_mode->HDisplay ||
		    mode->VDisplay > nv_output->native_mode->VDisplay)
			return MODE_PANEL;
	if (nv_output->dcb->type == OUTPUT_TMDS) {
		if (nv_output->dcb->duallink_possible) {
			if (mode->Clock > 330000) /* 2x165 MHz */
				return MODE_CLOCK_HIGH;
		} else {
			if (mode->Clock > 165000) /* 165 MHz */
				return MODE_CLOCK_HIGH;
		}
	}

	return MODE_OK;
}

static Bool
nv_output_mode_fixup(xf86OutputPtr output, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_mode_fixup is called.\n");

	/* For internal panels and gpu scaling on DVI we need the native mode */
	if (nv_output->dcb->type == OUTPUT_LVDS ||
	    (nv_output->dcb->type == OUTPUT_TMDS && nv_output->scaling_mode != SCALE_PANEL)) {
		adjusted_mode->HDisplay = nv_output->native_mode->HDisplay;
		adjusted_mode->HSkew = nv_output->native_mode->HSkew;
		adjusted_mode->HSyncStart = nv_output->native_mode->HSyncStart;
		adjusted_mode->HSyncEnd = nv_output->native_mode->HSyncEnd;
		adjusted_mode->HTotal = nv_output->native_mode->HTotal;
		adjusted_mode->VDisplay = nv_output->native_mode->VDisplay;
		adjusted_mode->VScan = nv_output->native_mode->VScan;
		adjusted_mode->VSyncStart = nv_output->native_mode->VSyncStart;
		adjusted_mode->VSyncEnd = nv_output->native_mode->VSyncEnd;
		adjusted_mode->VTotal = nv_output->native_mode->VTotal;
		adjusted_mode->Clock = nv_output->native_mode->Clock;
		adjusted_mode->Flags = nv_output->native_mode->Flags;

		xf86SetModeCrtc(adjusted_mode, INTERLACE_HALVE_V);
	}

	return TRUE;
}

static void
nv_output_mode_set(xf86OutputPtr output, DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVCrtcPrivatePtr nv_crtc = output->crtc->driver_private;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_mode_set is called.\n");

	if (pNv->twoHeads && nv_output->dcb->type == OUTPUT_ANALOG)
		/* bit 16-19 are bits that are set on some G70 cards,
		 * but don't seem to have much effect */
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + nv_output_ramdac_offset(output),
			      nv_crtc->head << 8 | NV_RAMDAC_OUTPUT_DAC_ENABLE);
	if (nv_output->dcb->type == OUTPUT_TMDS)
		run_tmds_table(pScrn, nv_output->dcb, nv_crtc->head, adjusted_mode->Clock);
	else if (nv_output->dcb->type == OUTPUT_LVDS)
		call_lvds_script(pScrn, nv_output->dcb, nv_crtc->head, LVDS_RESET, adjusted_mode->Clock);

	/* This could use refinement for flatpanels, but it should work this way */
	if (pNv->NVArch < 0x44)
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL + nv_output_ramdac_offset(output), 0xf0000000);
	else
		NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL + nv_output_ramdac_offset(output), 0x00100000);
}

static xf86MonPtr
nv_get_edid(xf86OutputPtr output)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	xf86MonPtr ddc_mon;

	if (nv_output->pDDCBus == NULL)
		return NULL;

	ddc_mon = xf86OutputGetEDID(output, nv_output->pDDCBus);
	if (!ddc_mon)
		return NULL;

	if (ddc_mon->features.input_type && nv_output->dcb->type == OUTPUT_ANALOG)
		goto invalid;

	if (!ddc_mon->features.input_type && (nv_output->dcb->type == OUTPUT_TMDS ||
					      nv_output->dcb->type == OUTPUT_LVDS))
		goto invalid;

	return ddc_mon;

invalid:
	xfree(ddc_mon);
	return NULL;
}

static Bool
nv_ddc_detect(xf86OutputPtr output)
{
	xf86MonPtr m = nv_get_edid(output);

	if (m == NULL)
		return FALSE;

	xfree(m);
	return TRUE;
}

static Bool
nv_load_detect(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVOutputPrivatePtr nv_output = output->driver_private;
	NVPtr pNv = NVPTR(pScrn);
	uint32_t testval, regoffset = nv_output_ramdac_offset(output);
	uint32_t saved_powerctrl_2 = 0, saved_powerctrl_4 = 0, saved_routput, saved_rtest_ctrl, temp;
	int present = 0;

#define RGB_TEST_DATA(r,g,b) (r << 0 | g << 10 | b << 20)
	testval = RGB_TEST_DATA(0x140, 0x140, 0x140); /* 0x94050140 */
	if (pNv->VBIOS.dactestval)
		testval = pNv->VBIOS.dactestval;

	saved_rtest_ctrl = NVReadRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL + regoffset);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL + regoffset, saved_rtest_ctrl & ~0x00010000);

	if (pNv->NVArch >= 0x17) {
		saved_powerctrl_2 = nvReadMC(pNv, NV_PBUS_POWERCTRL_2);

		nvWriteMC(pNv, NV_PBUS_POWERCTRL_2, saved_powerctrl_2 & 0xd7ffffff);
		if (regoffset == 0x68) {
			saved_powerctrl_4 = nvReadMC(pNv, NV_PBUS_POWERCTRL_4);
			nvWriteMC(pNv, NV_PBUS_POWERCTRL_4, saved_powerctrl_4 & 0xffffffcf);
		}
	}

	usleep(4000);

	saved_routput = NVReadRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + regoffset);
	/* nv driver and nv31 use 0xfffffeee
	 * nv34 and 6600 use 0xfffffece */
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + regoffset, saved_routput & 0xfffffece);
	usleep(1000);

	temp = NVReadRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + regoffset);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + regoffset, temp | 1);

	/* no regoffset on purpose */
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_DATA, 1 << 31 | testval);
	temp = NVReadRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL, temp | 0x1000);
	usleep(1000);

	present = NVReadRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL + regoffset) & (1 << 28);

	/* no regoffset on purpose */
	temp = NVReadRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL, temp & 0xffffefff);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_DATA, 0);

	/* bios does something more complex for restoring, but I think this is good enough */
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_OUTPUT + regoffset, saved_routput);
	NVWriteRAMDAC(pNv, 0, NV_RAMDAC_TEST_CONTROL + regoffset, saved_rtest_ctrl);
	if (pNv->NVArch >= 0x17) {
		if (regoffset == 0x68)
			nvWriteMC(pNv, NV_PBUS_POWERCTRL_4, saved_powerctrl_4);
		nvWriteMC(pNv, NV_PBUS_POWERCTRL_2, saved_powerctrl_2);
	}

	if (present) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Load detected on output %c\n", '@' + ffs(nv_output->dcb->or));
		return TRUE;
	}

	return FALSE;
}

static xf86OutputStatus
nv_tmds_output_detect(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_tmds_output_detect is called.\n");

	if (nv_ddc_detect(output))
		return XF86OutputStatusConnected;

	return XF86OutputStatusDisconnected;
}


static xf86OutputStatus
nv_analog_output_detect(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_analog_output_detect is called.\n");

	if (nv_ddc_detect(output))
		return XF86OutputStatusConnected;

	/* we don't have a load det function for early cards */
	if (!pNv->twoHeads || pNv->NVArch == 0x11)
		return XF86OutputStatusUnknown;
	else if (pNv->twoHeads && nv_load_detect(output))
		return XF86OutputStatusConnected;

	return XF86OutputStatusDisconnected;
}

static DisplayModePtr
nv_output_get_modes(xf86OutputPtr output, xf86MonPtr mon)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVOutputPrivatePtr nv_output = output->driver_private;
	DisplayModePtr ddc_modes;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_get_modes is called.\n");

	xf86OutputSetEDID(output, mon);

	ddc_modes = xf86OutputGetEDIDModes(output);

	if (nv_output->dcb->type == OUTPUT_TMDS || nv_output->dcb->type == OUTPUT_LVDS) {
		int max_h_active = 0, max_v_active = 0;
		int i;
		DisplayModePtr mode;

		for (i = 0; i < DET_TIMINGS; i++) {
			/* We only look at detailed timings atm */
			if (mon->det_mon[i].type != DT)
				continue;
			/* Selecting only based on width ok? */
			if (mon->det_mon[i].section.d_timings.h_active > max_h_active) {
				max_h_active = mon->det_mon[i].section.d_timings.h_active;
				max_v_active = mon->det_mon[i].section.d_timings.v_active;
			}
		}
		if (!(max_h_active && max_v_active)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No EDID detailed timings available, bailing out.\n");
			return NULL;
		}

		if (nv_output->native_mode) {
			xfree(nv_output->native_mode);
			nv_output->native_mode = NULL;
		}

		for (mode = ddc_modes; mode != NULL; mode = mode->next) {
			if (mode->HDisplay == max_h_active &&
				mode->VDisplay == max_v_active) {
				/* Take the preferred mode when it exists. */
				if (mode->type & M_T_PREFERRED) {
					nv_output->native_mode = xf86DuplicateMode(mode);
					break;
				}
				/* Find the highest refresh mode otherwise. */
				if (!nv_output->native_mode || (mode->VRefresh > nv_output->native_mode->VRefresh)) {
					if (nv_output->native_mode)
						xfree(nv_output->native_mode);
					mode->type |= M_T_PREFERRED;
					nv_output->native_mode = xf86DuplicateMode(mode);
				}
			}
		}
	}

	if (nv_output->dcb->type == OUTPUT_LVDS) {
		static bool dual_link_correction_done = false;

		if (!dual_link_correction_done) {
			parse_lvds_manufacturer_table(pScrn, &NVPTR(pScrn)->VBIOS, nv_output->native_mode->Clock);
			dual_link_correction_done = true;
		}
	}

	return ddc_modes;
}

static DisplayModePtr
nv_output_get_ddc_modes(xf86OutputPtr output)
{
	xf86MonPtr ddc_mon;
	ScrnInfoPtr pScrn = output->scrn;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_get_ddc_modes is called.\n");

	ddc_mon = nv_get_edid(output);

	if (ddc_mon == NULL)
		return NULL;

	return nv_output_get_modes(output, ddc_mon);
}

static void
nv_output_destroy (xf86OutputPtr output)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_destroy is called.\n");

	if (nv_output) {
		if (nv_output->native_mode)
			xfree(nv_output->native_mode);
		xfree(output->driver_private);
	}
}

static void nv_digital_output_prepare_sel_clk(xf86OutputPtr output)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	NVPtr pNv = NVPTR(output->scrn);
	NVRegPtr state = &pNv->ModeReg;
	NVCrtcPrivatePtr nv_crtc = output->crtc->driver_private;
	uint32_t bits1618 = nv_output->dcb->or & OUTPUT_A ? 0x10000 : 0x40000;

	if (nv_output->dcb->location != LOC_ON_CHIP)
		return;

	/* SEL_CLK is only used on the primary ramdac
	 * It toggles spread spectrum PLL output and sets the bindings of PLLs
	 * to heads on digital outputs
	 */
	if (nv_crtc->head)
		state->sel_clk |= bits1618;
	else
		state->sel_clk &= ~bits1618;

	/* nv30:
	 *	bit 0		NVClk spread spectrum on/off
	 *	bit 2		MemClk spread spectrum on/off
	 * 	bit 4		PixClk1 spread spectrum on/off toggle
	 * 	bit 6		PixClk2 spread spectrum on/off toggle
	 *
	 * nv40 (observations from bios behaviour and mmio traces):
	 * 	bits 4&6	as for nv30
	 * 	bits 5&7	head dependent as for bits 4&6, but do not appear with 4&6;
	 * 			maybe a different spread mode
	 * 	bits 8&10	seen on dual-link dvi outputs, purpose unknown (set by POST scripts)
	 * 	The logic behind turning spread spectrum on/off in the first place,
	 * 	and which bit-pair to use, is unclear on nv40 (for earlier cards, the fp table
	 * 	entry has the necessary info)
	 */
	if (nv_output->dcb->type == OUTPUT_LVDS && pNv->SavedReg.sel_clk & 0xf0) {
		int shift = (pNv->SavedReg.sel_clk & 0x50) ? 0 : 1;

		state->sel_clk &= ~0xf0;
		state->sel_clk |= (nv_crtc->head ? 0x40 : 0x10) << shift;
	}
}

static void
nv_output_prepare(xf86OutputPtr output)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(output->scrn);
	NVCrtcPrivatePtr nv_crtc = output->crtc->driver_private;
	NVCrtcRegPtr regp = &pNv->ModeReg.crtc_reg[nv_crtc->head];

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_prepare is called.\n");

	output->funcs->dpms(output, DPMSModeOff);

	/* calculate some output specific CRTC regs now, so that they can be written in nv_crtc_set_mode */
	if (nv_output->dcb->type == OUTPUT_LVDS || nv_output->dcb->type == OUTPUT_TMDS)
		nv_digital_output_prepare_sel_clk(output);

	/* Some NV4x have unknown values (0x3f, 0x50, 0x54, 0x6b, 0x79, 0x7f etc.) which we don't alter */
	if (!(regp->CRTC[NV_VGA_CRTCX_LCD] & 0x44)) {
		if (nv_output->dcb->type == OUTPUT_LVDS || nv_output->dcb->type == OUTPUT_TMDS) {
			regp->CRTC[NV_VGA_CRTCX_LCD] &= ~0x30;
			regp->CRTC[NV_VGA_CRTCX_LCD] |= 0x3;
			if (nv_crtc->head == 0)
				regp->CRTC[NV_VGA_CRTCX_LCD] |= 0x8;
			else
				regp->CRTC[NV_VGA_CRTCX_LCD] &= ~0x8;
			if (nv_output->dcb->location != LOC_ON_CHIP)
				regp->CRTC[NV_VGA_CRTCX_LCD] |= (nv_output->dcb->or << 4) & 0x30;
		} else
			regp->CRTC[NV_VGA_CRTCX_LCD] = 0;
	}
}

static void
nv_output_commit(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	xf86CrtcPtr crtc = output->crtc;
	NVOutputPrivatePtr nv_output = output->driver_private;
	NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_output_commit is called.\n");

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Output %s is running on CRTC %d using output %c\n", output->name, nv_crtc->head, '@' + ffs(nv_output->dcb->or));

	output->funcs->dpms(output, DPMSModeOn);
}

static const xf86OutputFuncsRec nv_analog_output_funcs = {
    .dpms = nv_analog_output_dpms,
    .save = nv_output_save,
    .restore = nv_output_restore,
    .mode_valid = nv_output_mode_valid,
    .mode_fixup = nv_output_mode_fixup,
    .mode_set = nv_output_mode_set,
    .detect = nv_analog_output_detect,
    .get_modes = nv_output_get_ddc_modes,
    .destroy = nv_output_destroy,
    .prepare = nv_output_prepare,
    .commit = nv_output_commit,
};

/*
 * Several scaling modes exist, let the user choose.
 */
#define SCALING_MODE_NAME "SCALING_MODE"
static const struct {
	char *name;
	enum scaling_modes mode;
} scaling_mode[] = {
	{ "panel", SCALE_PANEL },
	{ "fullscreen", SCALE_FULLSCREEN },
	{ "aspect", SCALE_ASPECT },
	{ "noscale", SCALE_NOSCALE },
	{ NULL, SCALE_INVALID}
};
static Atom scaling_mode_atom;

#define DITHERING_MODE_NAME "DITHERING"
static Atom dithering_atom;

static void
nv_digital_output_create_resources(xf86OutputPtr output)
{
	NVOutputPrivatePtr nv_output = output->driver_private;
	ScrnInfoPtr pScrn = output->scrn;
	INT32 dithering_range[2] = { 0, 1 };
	int error, i;

	/*
	 * Setup scaling mode property.
	 */
	scaling_mode_atom = MakeAtom(SCALING_MODE_NAME, sizeof(SCALING_MODE_NAME) - 1, TRUE);

	error = RRConfigureOutputProperty(output->randr_output,
					scaling_mode_atom, TRUE, FALSE, FALSE,
					0, NULL);

	if (error != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"RRConfigureOutputProperty error, %d\n", error);
	}

	char *existing_scale_name = NULL;
	for (i = 0; scaling_mode[i].name; i++)
		if (scaling_mode[i].mode == nv_output->scaling_mode)
			existing_scale_name = scaling_mode[i].name;

	error = RRChangeOutputProperty(output->randr_output, scaling_mode_atom,
					XA_STRING, 8, PropModeReplace, 
					strlen(existing_scale_name),
					existing_scale_name, FALSE, TRUE);

	if (error != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Failed to set scaling mode, %d\n", error);
	}

	/*
	 * Setup dithering property.
	 */
	dithering_atom = MakeAtom(DITHERING_MODE_NAME, sizeof(DITHERING_MODE_NAME) - 1, TRUE);

	error = RRConfigureOutputProperty(output->randr_output,
					dithering_atom, TRUE, TRUE, FALSE,
					2, dithering_range);

	if (error != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"RRConfigureOutputProperty error, %d\n", error);
	}

	/* promote bool into int32 to make RandR DIX and big endian happy */
	int32_t existing_dither = nv_output->dithering;
	error = RRChangeOutputProperty(output->randr_output, dithering_atom,
					XA_INTEGER, 32, PropModeReplace, 1,
					&existing_dither, FALSE, TRUE);

	if (error != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Failed to set dithering mode, %d\n", error);
	}
}

static Bool
nv_digital_output_set_property(xf86OutputPtr output, Atom property,
				RRPropertyValuePtr value)
{
	NVOutputPrivatePtr nv_output = output->driver_private;

	if (property == scaling_mode_atom) {
		int32_t ret;
		char *name = NULL;

		if (value->type != XA_STRING || value->format != 8)
			return FALSE;

		name = (char *) value->data;

		/* Match a string to a scaling mode */
		ret = nv_scaling_mode_lookup(name, value->size);
		if (ret == SCALE_INVALID)
			return FALSE;

		/* LVDS must always use gpu scaling. */
		if (ret == SCALE_PANEL && nv_output->dcb->type == OUTPUT_LVDS)
			return FALSE;

		nv_output->scaling_mode = ret;
	} else if (property == dithering_atom) {
		if (value->type != XA_INTEGER || value->format != 32)
			return FALSE;

		int32_t val = *(int32_t *) value->data;

		if (val < 0 || val > 1)
			return FALSE;

		nv_output->dithering = val;
	}

	return TRUE;
}

static const xf86OutputFuncsRec nv_tmds_output_funcs = {
	.dpms = nv_tmds_output_dpms,
	.save = nv_output_save,
	.restore = nv_output_restore,
	.mode_valid = nv_output_mode_valid,
	.mode_fixup = nv_output_mode_fixup,
	.mode_set = nv_output_mode_set,
	.detect = nv_tmds_output_detect,
	.get_modes = nv_output_get_ddc_modes,
	.destroy = nv_output_destroy,
	.prepare = nv_output_prepare,
	.commit = nv_output_commit,
	.create_resources = nv_digital_output_create_resources,
	.set_property = nv_digital_output_set_property,
};

static xf86OutputStatus
nv_lvds_output_detect(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVOutputPrivatePtr nv_output = output->driver_private;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_lvds_output_detect is called.\n");

	if (nv_ddc_detect(output))
		return XF86OutputStatusConnected;
	if (nv_output->dcb->lvdsconf.use_straps_for_mode && pNv->VBIOS.fp.native_mode)
		return XF86OutputStatusConnected;
	if (pNv->VBIOS.fp.edid)
		return XF86OutputStatusConnected;

	return XF86OutputStatusDisconnected;
}

static DisplayModePtr
nv_lvds_output_get_modes(xf86OutputPtr output)
{
	ScrnInfoPtr pScrn = output->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVOutputPrivatePtr nv_output = output->driver_private;
	DisplayModePtr modes;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_lvds_output_get_modes is called.\n");

	if ((modes = nv_output_get_ddc_modes(output)))
		return modes;

	if (!nv_output->dcb->lvdsconf.use_straps_for_mode || pNv->VBIOS.fp.native_mode == NULL) {
		xf86MonPtr edid_mon;

		if (!pNv->VBIOS.fp.edid)
			return NULL;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using hardcoded BIOS FP EDID\n");
		edid_mon = xf86InterpretEDID(pScrn->scrnIndex, pNv->VBIOS.fp.edid);
		return nv_output_get_modes(output, edid_mon);
	}

	if (nv_output->native_mode)
		xfree(nv_output->native_mode);
	nv_output->native_mode = xf86DuplicateMode(pNv->VBIOS.fp.native_mode);

	return xf86DuplicateMode(pNv->VBIOS.fp.native_mode);
}

static const xf86OutputFuncsRec nv_lvds_output_funcs = {
	.dpms = nv_lvds_output_dpms,
	.save = nv_output_save,
	.restore = nv_output_restore,
	.mode_valid = nv_output_mode_valid,
	.mode_fixup = nv_output_mode_fixup,
	.mode_set = nv_output_mode_set,
	.detect = nv_lvds_output_detect,
	.get_modes = nv_lvds_output_get_modes,
	.destroy = nv_output_destroy,
	.prepare = nv_output_prepare,
	.commit = nv_output_commit,
	.create_resources = nv_digital_output_create_resources,
	.set_property = nv_digital_output_set_property,
};

static void
nv_add_output(ScrnInfoPtr pScrn, struct dcb_entry *dcbent, const xf86OutputFuncsRec *output_funcs, char *outputname)
{
	NVPtr pNv = NVPTR(pScrn);
	xf86OutputPtr output;
	NVOutputPrivatePtr nv_output;

	if (!(output = xf86OutputCreate(pScrn, output_funcs, outputname)))
		return;
	if (!(nv_output = xnfcalloc(sizeof(NVOutputPrivateRec), 1)))
		return;

	output->driver_private = nv_output;

	if (dcbent->i2c_index < 0xf && pNv->pI2CBus[dcbent->i2c_index] == NULL)
		NV_I2CInit(pScrn, &pNv->pI2CBus[dcbent->i2c_index], pNv->dcb_table.i2c_read[dcbent->i2c_index], xstrdup(outputname));
	nv_output->pDDCBus = pNv->pI2CBus[dcbent->i2c_index];
	nv_output->dcb = dcbent;
	nv_output->last_dpms = NV_DPMS_CLEARED;

	nv_output->dithering = (pNv->FPDither || (nv_output->dcb->type == OUTPUT_LVDS && !pNv->VBIOS.fp.if_is_24bit));
	if (pNv->fpScaler) /* GPU Scaling */
		nv_output->scaling_mode = SCALE_ASPECT;
	else if (nv_output->dcb->type == OUTPUT_LVDS)
		nv_output->scaling_mode = SCALE_NOSCALE;
	else
		nv_output->scaling_mode = SCALE_PANEL;
	if (xf86GetOptValString(pNv->Options, OPTION_SCALING_MODE)) {
		nv_output->scaling_mode = nv_scaling_mode_lookup(xf86GetOptValString(pNv->Options, OPTION_SCALING_MODE), -1);
		if (nv_output->scaling_mode == SCALE_INVALID)
			nv_output->scaling_mode = SCALE_ASPECT; /* default */
	}

	output->possible_crtcs = dcbent->heads;
	if (nv_output->dcb->type == OUTPUT_LVDS || nv_output->dcb->type == OUTPUT_TMDS) {
		output->doubleScanAllowed = false;
		output->interlaceAllowed = false;
	} else {
		output->doubleScanAllowed = true;
		if (pNv->Architecture == NV_ARCH_20 ||
		   (pNv->Architecture == NV_ARCH_10 &&
		    (pNv->Chipset & 0x0ff0) != CHIPSET_NV10 &&
		    (pNv->Chipset & 0x0ff0) != CHIPSET_NV15))
			/* HW is broken */
			output->interlaceAllowed = false;
		else
			output->interlaceAllowed = true;
	}
}

void NvSetupOutputs(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int i, i2c_count[MAX_NUM_DCB_ENTRIES];
	struct dcb_entry *dcbent;
	char outputname[20];
	int vga_count = 0, tv_count = 0, dvia_count = 0, dvid_count = 0, lvds_count = 0;

	memset(pNv->pI2CBus, 0, sizeof(pNv->pI2CBus));
	memset(i2c_count, 0, sizeof(i2c_count));
	for (i = 0 ; i < pNv->dcb_table.entries; i++)
		i2c_count[pNv->dcb_table.entry[i].i2c_index]++;

	for (i = 0; i < pNv->dcb_table.entries; i++) {
		dcbent = &pNv->dcb_table.entry[i];

		xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "DCB entry %d: type: %d, heads: %d, or: %d\n", i, dcbent->type, dcbent->heads, dcbent->or);

		switch (dcbent->type) {
		case OUTPUT_ANALOG:
			if (i2c_count[dcbent->i2c_index] == 1)
				sprintf(outputname, "VGA-%d", vga_count++);
			else
				sprintf(outputname, "DVI-A-%d", dvia_count++);
			nv_add_output(pScrn, dcbent, &nv_analog_output_funcs, outputname);
			break;
		case OUTPUT_TMDS:
			sprintf(outputname, "DVI-D-%d", dvid_count++);
			nv_add_output(pScrn, dcbent, &nv_tmds_output_funcs, outputname);
			break;
		case OUTPUT_TV:
			sprintf(outputname, "TV-%d", tv_count++);
			break;
		case OUTPUT_LVDS:
			sprintf(outputname, "LVDS-%d", lvds_count++);
			nv_add_output(pScrn, dcbent, &nv_lvds_output_funcs, outputname);
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "DCB type %d not known\n", dcbent->type);
			break;
		}
	}
}
