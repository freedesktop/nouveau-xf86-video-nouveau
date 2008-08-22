/*
 * Copyright 2007 NVIDIA, Corporation
 * Copyright 2008 Maarten Maathuis
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nouveau_modeset.h"
#include "nouveau_crtc.h"
#include "nouveau_output.h"
#include "nouveau_connector.h"

static int
NV50SorModeValid(nouveauOutputPtr output, DisplayModePtr mode)
{
	if (mode->Clock > 165000) /* no dual link until we figure it out completely */
		return MODE_CLOCK_HIGH;

	if (mode->Clock < 25000)
		return MODE_CLOCK_LOW;

	if (mode->Flags & V_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (mode->HDisplay > output->native_mode->HDisplay || mode->VDisplay > output->native_mode->VDisplay)
		return MODE_PANEL;

	return MODE_OK;
}

static void
NV50SorModeSet(nouveauOutputPtr output, DisplayModePtr mode)
{
	ScrnInfoPtr pScrn = output->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50SorModeSet is called.\n");

	const int sorOff = 0x40 * NV50OrOffset(output);
	uint32_t mode_ctl = NV50_SOR_MODE_CTRL_OFF;

	if (!mode) {
		/* Disconnect the SOR */
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Disconnecting SOR.\n");
		NV50DisplayCommand(pScrn, NV50_SOR0_MODE_CTRL + sorOff, mode_ctl);
		return;
	}

	/* Anyone know a more appropriate name? */
	DisplayModePtr desired_mode = output->crtc->use_native_mode ? output->crtc->native_mode : mode;

	if (output->type == OUTPUT_LVDS) {
		mode_ctl |= NV50_SOR_MODE_CTRL_LVDS;
	} else {
		mode_ctl |= NV50_SOR_MODE_CTRL_TMDS;
		if (desired_mode->Clock > 165000)
			mode_ctl |= NV50_SOR_MODE_CTRL_TMDS_DUAL_LINK;
	}

	if (output->crtc) {
		if (output->crtc->index == 1)
			mode_ctl |= NV50_SOR_MODE_CTRL_CRTC1;
		else
			mode_ctl |= NV50_SOR_MODE_CTRL_CRTC0;
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Warning, output has no crtc.\n");
		return;
	}

	if (desired_mode->Flags & V_NHSYNC)
		mode_ctl |= NV50_SOR_MODE_CTRL_NHSYNC;

	if (desired_mode->Flags & V_NVSYNC)
		mode_ctl |= NV50_SOR_MODE_CTRL_NVSYNC;

	// This wouldn't be necessary, but the server is stupid and calls
	// nv50_sor_dpms after the output is disconnected, even though the hardware
	// turns it off automatically.
	output->SetPowerMode(output, DPMSModeOn);

	NV50DisplayCommand(pScrn, NV50_SOR0_MODE_CTRL + sorOff, mode_ctl);

	output->crtc->SetScaleMode(output->crtc, output->scale_mode);
}

static void
NV50SorSetClockMode(nouveauOutputPtr output, int clock)
{
	ScrnInfoPtr pScrn = output->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50SorSetClockMode is called.\n");

	NVPtr pNv = NVPTR(pScrn);
	const int limit = 165000;

	/* 0x70000 was a late addition to nv, mentioned as fixing tmds initialisation on certain gpu's. */
	/* I presume it's some kind of clock setting, but what precisely i do not know. */
	NVWrite(pNv, NV50_SOR0_CLK_CTRL2 + NV50OrOffset(output) * 0x800, 0x70000 | ((clock > limit) ? 0x101 : 0));
}

static void
NV50SorSetClockModeLVDS(nouveauOutputPtr output, int clock)
{
}

static int
NV50SorSense(nouveauOutputPtr output)
{
	switch (output->type) {
		case OUTPUT_TMDS:
		case OUTPUT_LVDS:
			return output->type;
		default:
			return OUTPUT_NONE;
	}
}

static void
NV50SorSetPowerMode(nouveauOutputPtr output, int mode)
{
	ScrnInfoPtr pScrn = output->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50SorSetPowerMode is called with mode %d.\n", mode);

	NVPtr pNv = NVPTR(pScrn);
	uint32_t tmp;

	while ((NVRead(pNv, NV50_SOR0_DPMS_CTRL + NV50OrOffset(output) * 0x800) & NV50_SOR_DPMS_CTRL_PENDING));

	tmp = NVRead(pNv, NV50_SOR0_DPMS_CTRL + NV50OrOffset(output) * 0x800);
	tmp |= NV50_SOR_DPMS_CTRL_PENDING;

	if (mode == DPMSModeOn)
		tmp |= NV50_SOR_DPMS_CTRL_MODE_ON;
	else
		tmp &= ~NV50_SOR_DPMS_CTRL_MODE_ON;

	NVWrite(pNv, NV50_SOR0_DPMS_CTRL + NV50OrOffset(output) * 0x800, tmp);
	while ((NVRead(pNv, NV50_SOR0_DPMS_STATE + NV50OrOffset(output) * 0x800) & NV50_SOR_DPMS_STATE_WAIT));
}

static Bool
NV50SorDetect(nouveauOutputPtr output)
{
	if (output->type == OUTPUT_LVDS) /* assume connected */
		return TRUE;

	return FALSE;
}

void
NV50SorSetFunctionPointers(nouveauOutputPtr output)
{
	output->ModeValid = NV50SorModeValid;
	output->ModeSet = NV50SorModeSet;
	if (output->type == OUTPUT_TMDS)
		output->SetClockMode = NV50SorSetClockMode;
	else
		output->SetClockMode = NV50SorSetClockModeLVDS;
	output->Sense = NV50SorSense;
	output->Detect = NV50SorDetect;
	output->SetPowerMode = NV50SorSetPowerMode;
}

/*
 * Some misc functions.
 */

static DisplayModePtr
ReadLVDSNativeMode(ScrnInfoPtr pScrn, const int off)
{
	NVPtr pNv = NVPTR(pScrn);
	DisplayModePtr mode = xnfcalloc(1, sizeof(DisplayModeRec));
	const CARD32 size = NVRead(pNv, 0x00610b4c + off);
	const int width = size & 0x3fff;
	const int height = (size >> 16) & 0x3fff;

	mode->HDisplay = width;
	mode->VDisplay = height;
	mode->Clock = NVRead(pNv, 0x00610ad4 + off) & 0x3fffff;

	/* We should investigate what else is found in these register ranges. */
	uint32_t unk1 = NVRead(pNv, NV50_CRTC0_DISPLAY_TOTAL_VAL + off);
	uint32_t unk2 = NVRead(pNv, NV50_CRTC0_SYNC_DURATION_VAL + off);
	uint32_t unk3 = NVRead(pNv, NV50_CRTC0_SYNC_START_TO_BLANK_END_VAL + off);
	/*uint32_t unk4 = NVRead(pNv, NV50_CRTC0_MODE_UNK1_VAL + off);*/

	/* Recontruct our mode, so it can be handled normally. */
	mode->HTotal = (unk1 & 0xFFFF);
	mode->VTotal = (unk1 >> 16);

	/* Assuming no interlacing. */
	mode->HSyncStart = mode->HTotal - (unk3 & 0xFFFF) - 1;
	mode->VSyncStart = mode->VTotal - (unk3 >> 16) - 1;

	mode->HSyncEnd = mode->HSyncStart + (unk2 & 0xFFFF) + 1;
	mode->VSyncEnd = mode->VSyncStart + (unk2 >> 16) + 1;

	mode->next = mode->prev = NULL;
	mode->status = MODE_OK;
	mode->type = M_T_DRIVER | M_T_PREFERRED;

	xf86SetModeDefaultName(mode);

	return mode;
}

DisplayModePtr
GetLVDSNativeMode(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	uint32_t val = NVRead(pNv, NV50_DISPLAY_UNK50_CTRL);

	/* This is rather crude imo, i wonder if it always works. */
	if ((val & NV50_DISPLAY_UNK50_CTRL_CRTC0_MASK) == NV50_DISPLAY_UNK50_CTRL_CRTC0_ACTIVE) {
		return ReadLVDSNativeMode(pScrn, 0);
	} else if ((val & NV50_DISPLAY_UNK50_CTRL_CRTC1_MASK) == NV50_DISPLAY_UNK50_CTRL_CRTC1_ACTIVE) {
		return ReadLVDSNativeMode(pScrn, 0x540);
	}

	return NULL;
}
