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

/* Don't call the directly, only load state should do this on the long run*/
static void 
NV50CheckWriteVClk(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int t_start = GetTimeInMillis();

	while (NVRead(pNv, NV50_DISPLAY_CTRL_STATE) & NV50_DISPLAY_CTRL_STATE_PENDING) {
		/* An educated guess. */
		const uint32_t supervisor = NVRead(pNv, NV50_DISPLAY_SUPERVISOR);

		/* Just in case something goes bad, at least you can blindly restart your machine. */
		if ((GetTimeInMillis() - t_start) > 5000) {
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "NV50CheckWriteVClk() timed out.\n");
			xf86DrvMsg(pScrn->scrnIndex, X_INFO, "A reboot is probably required now.\n");
			break;
		}

		/* Simply acknowledge it, maybe we should do more? */
		if (supervisor & NV50_DISPLAY_SUPERVISOR_CRTCn) {
			NVWrite(pNv, NV50_DISPLAY_SUPERVISOR, supervisor & NV50_DISPLAY_SUPERVISOR_CRTCn);
		}

		if (supervisor & NV50_DISPLAY_SUPERVISOR_CLK_MASK) {
			if (supervisor & NV50_DISPLAY_SUPERVISOR_CLK_UPDATE) {
				const uint32_t clockvar = NVRead(pNv, NV50_DISPLAY_UNK30_CTRL);
				int i;

				for(i = 0; i < 2; i++) {
					nouveauCrtcPtr crtc = pNv->crtc[i];
					uint32_t mask = 0;

					if (crtc->index == 1)
						mask = NV50_DISPLAY_UNK30_CTRL_UPDATE_VCLK1;
					else
						mask = NV50_DISPLAY_UNK30_CTRL_UPDATE_VCLK0;

					if (clockvar & mask)
						crtc->SetPixelClock(crtc, crtc->pixel_clock);
					/* Always do something if the supervisor wants a clock change. */
					/* This is needed because you get a deadlock if you don't kick the NV50_CRTC0_CLK_CTRL2 register. */
					if (crtc->modeset_lock) {
						crtc->SetClockMode(crtc, crtc->pixel_clock);

						nouveauOutputPtr output;
						for (output = pNv->output; output != NULL; output = output->next) {
							if (output->crtc == crtc)
								output->SetClockMode(output, crtc->pixel_clock);
						}
					}
				}
			}

			NVWrite(pNv, NV50_DISPLAY_SUPERVISOR, 1 << (ffs(supervisor & NV50_DISPLAY_SUPERVISOR_CLK_MASK) - 1));
			NVWrite(pNv, NV50_DISPLAY_UNK30_CTRL, NV50_DISPLAY_UNK30_CTRL_PENDING);
		}
	}
}

void NV50DisplayCommand(ScrnInfoPtr pScrn, uint32_t addr, uint32_t value)
{
	DDXMMIOH("NV50DisplayCommand: head %d addr 0x%X value 0x%X\n", 0, addr, value);
	NVPtr pNv = NVPTR(pScrn);
	NVWrite(pNv, NV50_DISPLAY_CTRL_VAL, value);
	NVWrite(pNv, NV50_DISPLAY_CTRL_STATE, addr | 0x10000 | NV50_DISPLAY_CTRL_STATE_ENABLE | NV50_DISPLAY_CTRL_STATE_PENDING);
	NV50CheckWriteVClk(pScrn);
}

void NV50CrtcCommand(nouveauCrtcPtr crtc, uint32_t addr, uint32_t value)
{
	ScrnInfoPtr pScrn = crtc->scrn;

	/* This head dependent offset is only for crtc commands. */
	NV50DisplayCommand(pScrn, addr + 0x400 * crtc->index, value);
}

static Bool
NV50CrtcModeValid(nouveauCrtcPtr crtc, DisplayModePtr mode)
{
	return TRUE;
}

static void
NV50CrtcModeSet(nouveauCrtcPtr crtc, DisplayModePtr mode)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcModeSet is called for %s.\n", crtc->index ? "CRTC1" : "CRTC0");

	/* Anyone know a more appropriate name? */
	DisplayModePtr desired_mode = crtc->use_native_mode ? crtc->native_mode : mode;

	/* Save the pixel clock for posterity. */
	crtc->pixel_clock = desired_mode->Clock;
	crtc->cur_mode = mode;

	uint32_t hsync_dur = desired_mode->CrtcHSyncEnd - desired_mode->CrtcHSyncStart;
	uint32_t vsync_dur = desired_mode->CrtcVSyncEnd - desired_mode->CrtcVSyncStart;
	uint32_t hsync_start_to_end = desired_mode->CrtcHBlankEnd - desired_mode->CrtcHSyncStart;
	uint32_t vsync_start_to_end = desired_mode->CrtcVBlankEnd - desired_mode->CrtcVSyncStart;
	/* I can't give this a proper name, anyone else can? */
	uint32_t hunk1 = desired_mode->CrtcHTotal - desired_mode->CrtcHSyncStart + desired_mode->CrtcHBlankStart;
	uint32_t vunk1 = desired_mode->CrtcVTotal - desired_mode->CrtcVSyncStart + desired_mode->CrtcVBlankStart;
	/* Another strange value, this time only for interlaced modes. */
	uint32_t vunk2a = 2*desired_mode->CrtcVTotal - desired_mode->CrtcVSyncStart + desired_mode->CrtcVBlankStart;
	uint32_t vunk2b = desired_mode->CrtcVTotal - desired_mode->CrtcVSyncStart + desired_mode->CrtcVBlankEnd;

	if (desired_mode->Flags & V_INTERLACE) {
		vsync_dur /= 2;
		vsync_start_to_end  /= 2;
		vunk1 /= 2;
		vunk2a /= 2;
		vunk2b /= 2;
		/* magic */
		if (desired_mode->Flags & V_DBLSCAN) {
			vsync_start_to_end -= 1;
			vunk1 -= 1;
			vunk2a -= 1;
			vunk2b -= 1;
		}
	}

	/* NV50CrtcCommand includes head offset */
	/* This is the native mode when DFP && !SCALE_PANEL */
	NV50CrtcCommand(crtc, NV50_CRTC0_CLOCK, desired_mode->Clock | 0x800000);
	NV50CrtcCommand(crtc, NV50_CRTC0_INTERLACE, (desired_mode->Flags & V_INTERLACE) ? 2 : 0);
	NV50CrtcCommand(crtc, NV50_CRTC0_DISPLAY_START, 0);
	NV50CrtcCommand(crtc, NV50_CRTC0_UNK82C, 0);
	NV50CrtcCommand(crtc, NV50_CRTC0_DISPLAY_TOTAL, desired_mode->CrtcVTotal << 16 | desired_mode->CrtcHTotal);
	NV50CrtcCommand(crtc, NV50_CRTC0_SYNC_DURATION, (vsync_dur - 1) << 16 | (hsync_dur - 1));
	NV50CrtcCommand(crtc, NV50_CRTC0_SYNC_START_TO_BLANK_END, (vsync_start_to_end - 1) << 16 | (hsync_start_to_end - 1));
	NV50CrtcCommand(crtc, NV50_CRTC0_MODE_UNK1, (vunk1 - 1) << 16 | (hunk1 - 1));
	if (desired_mode->Flags & V_INTERLACE) {
		NV50CrtcCommand(crtc, NV50_CRTC0_MODE_UNK2, (vunk2b - 1) << 16 | (vunk2a - 1));
	}
	NV50CrtcCommand(crtc, NV50_CRTC0_FB_SIZE, pScrn->virtualY << 16 | pScrn->virtualX);

	/* Maybe move this calculation elsewhere? */
	crtc->fb_pitch = pScrn->displayWidth * (pScrn->bitsPerPixel / 8);
	NV50CrtcCommand(crtc, NV50_CRTC0_FB_PITCH, crtc->fb_pitch | 0x100000);

	switch (pScrn->depth) {
		case 8:
			NV50CrtcCommand(crtc, NV50_CRTC0_DEPTH, NV50_CRTC0_DEPTH_8BPP); 
			break;
		case 15:
			NV50CrtcCommand(crtc, NV50_CRTC0_DEPTH, NV50_CRTC0_DEPTH_15BPP);
			break;
		case 16:
			NV50CrtcCommand(crtc, NV50_CRTC0_DEPTH, NV50_CRTC0_DEPTH_16BPP);
			break;
		case 24:
			NV50CrtcCommand(crtc, NV50_CRTC0_DEPTH, NV50_CRTC0_DEPTH_24BPP); 
			break;
	}
	crtc->SetDither(crtc);
	NV50CrtcCommand(crtc, NV50_CRTC0_COLOR_CTRL, NV50_CRTC_COLOR_CTRL_MODE_COLOR);
	NV50CrtcCommand(crtc, NV50_CRTC0_FB_POS, (crtc->y << 16) | (crtc->x));
	/* This is the actual resolution of the mode. */
	NV50CrtcCommand(crtc, NV50_CRTC0_REAL_RES, (mode->VDisplay << 16) | mode->HDisplay);
	NV50CrtcCommand(crtc, NV50_CRTC0_SCALE_CENTER_OFFSET, NV50_CRTC_SCALE_CENTER_OFFSET_VAL(0,0));

	/* Maybe move this as well? */
	crtc->Blank(crtc, FALSE);
}

static void
NV50CrtcSetPixelClock(nouveauCrtcPtr crtc, int clock)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcSetPixelClock is called for %s.\n", crtc->index ? "CRTC1" : "CRTC0");

	NVPtr pNv = NVPTR(pScrn);

	/* I don't know why exactly, but these were in my table. */
	uint32_t pll_reg = crtc->index ? NV50_CRTC1_CLK_CTRL1 : NV50_CRTC0_CLK_CTRL1;

	int NM1 = 0xbeef, NM2 = 0xdead, log2P;
	struct pll_lims pll_lim;
	get_pll_limits(pScrn, pll_reg, &pll_lim);
	/* NV5x hardware doesn't seem to support a single vco mode, otherwise the blob is hiding it well. */
	getMNP_double(pScrn, &pll_lim, clock, &NM1, &NM2, &log2P);

	uint32_t reg1 = NVRead(pNv, pll_reg + 4);
	uint32_t reg2 = NVRead(pNv, pll_reg + 8);

	/* bit0: The blob (and bios) seem to have this on (almost) always.
	 *          I'm hoping this (experiment) will fix my image stability issues.
	 */
	NVWrite(pNv, NV50_CRTC0_CLK_CTRL1 + crtc->index * 0x800, NV50_CRTC_CLK_CTRL1_CONNECTED | 0x10000011);

	/* Eventually we should learn ourselves what all the bits should be. */
	reg1 &= 0xff00ff00;
	reg2 &= 0x8000ff00;

	uint8_t N1 = (NM1 >> 8) & 0xFF;
	uint8_t M1 = NM1 & 0xFF;
	uint8_t N2 = (NM2 >> 8) & 0xFF;
	uint8_t M2 = NM2 & 0xFF;

	reg1 |= (M1 << 16) | N1;
	reg2 |= (log2P << 28) | (M2 << 16) | N2;

	NVWrite(pNv, pll_reg + 4, reg1);
	NVWrite(pNv, pll_reg + 8, reg2);
}

static void
NV50CrtcSetClockMode(nouveauCrtcPtr crtc, int clock)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcSetClockMode is called for %s.\n", crtc->index ? "CRTC1" : "CRTC0");

	NVPtr pNv = NVPTR(pScrn);

	/* There seem to be a few indicator bits, which are similar to the SOR_CTRL bits. */
	NVWrite(pNv, NV50_CRTC0_CLK_CTRL2 + crtc->index * 0x800, 0);
}

static void
NV50CrtcSetFB(nouveauCrtcPtr crtc, struct nouveau_bo * buffer)
{
	/* For the moment the actual hardware settings stays in ModeSet(). */
	crtc->front_buffer = buffer;
}

static void 
NV50CrtcSetFBOffset(nouveauCrtcPtr crtc, uint32_t x, uint32_t y)
{
	/* For the moment the actual hardware settings stays in ModeSet(). */
	crtc->x = x;
	crtc->y = y;
}

static void 
NV50CrtcBlank(nouveauCrtcPtr crtc, Bool blanked)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcBlank is called (%s) for %s.\n", blanked ? "blanked" : "unblanked", crtc->index ? "CRTC1" : "CRTC0");

	NVPtr pNv = NVPTR(pScrn);

	if (blanked) {
		crtc->HideCursor(crtc, TRUE);

		NV50CrtcCommand(crtc, NV50_CRTC0_CLUT_MODE, NV50_CRTC0_CLUT_MODE_BLANK);
		NV50CrtcCommand(crtc, NV50_CRTC0_CLUT_OFFSET, 0);
		if (pNv->NVArch != 0x50)
			NV50CrtcCommand(crtc, NV84_CRTC0_BLANK_UNK1, NV84_CRTC0_BLANK_UNK1_BLANK);
		NV50CrtcCommand(crtc, NV50_CRTC0_BLANK_CTRL, NV50_CRTC0_BLANK_CTRL_BLANK);
		if (pNv->NVArch != 0x50)
			NV50CrtcCommand(crtc, NV84_CRTC0_BLANK_UNK2, NV84_CRTC0_BLANK_UNK2_BLANK);
	} else {
		NV50CrtcCommand(crtc, NV50_CRTC0_FB_OFFSET, crtc->front_buffer->offset >> 8);
		NV50CrtcCommand(crtc, 0x864, 0);
		NVWrite(pNv, NV50_DISPLAY_UNK_380, 0);
		/* RAM is clamped to 256 MiB. */
		NVWrite(pNv, NV50_DISPLAY_RAM_AMOUNT, pNv->RamAmountKBytes * 1024 - 1);
		NVWrite(pNv, NV50_DISPLAY_UNK_388, 0x150000);
		NVWrite(pNv, NV50_DISPLAY_UNK_38C, 0);
		if (crtc->index == 1)
			NV50CrtcCommand(crtc, NV50_CRTC0_CURSOR_OFFSET, pNv->Cursor2->offset >> 8);
		else
			NV50CrtcCommand(crtc, NV50_CRTC0_CURSOR_OFFSET, pNv->Cursor->offset >> 8);
		if(pNv->NVArch != 0x50)
			NV50CrtcCommand(crtc, NV84_CRTC0_BLANK_UNK2, NV84_CRTC0_BLANK_UNK2_UNBLANK);

		if (crtc->cursor_visible)
			crtc->ShowCursor(crtc, TRUE);

		NV50CrtcCommand(crtc, NV50_CRTC0_CLUT_MODE, 
			pScrn->depth == 8 ? NV50_CRTC0_CLUT_MODE_OFF : NV50_CRTC0_CLUT_MODE_ON);
		/* Each CRTC has it's own CLUT. */
		if (crtc->index == 1)
			NV50CrtcCommand(crtc, NV50_CRTC0_CLUT_OFFSET, pNv->CLUT1->offset >> 8);
		else
			NV50CrtcCommand(crtc, NV50_CRTC0_CLUT_OFFSET, pNv->CLUT0->offset >> 8);
		if (pNv->NVArch != 0x50)
			NV50CrtcCommand(crtc, NV84_CRTC0_BLANK_UNK1, NV84_CRTC0_BLANK_UNK1_UNBLANK);
		NV50CrtcCommand(crtc, NV50_CRTC0_BLANK_CTRL, NV50_CRTC0_BLANK_CTRL_UNBLANK);
	}
}

static void
NV50CrtcSetDither(nouveauCrtcPtr crtc)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcSetDither is called (%s).\n", !crtc->modeset_lock ? "update" : "no update");

	NV50CrtcCommand(crtc, NV50_CRTC0_DITHERING_CTRL, crtc->dithering ? 
			NV50_CRTC0_DITHERING_CTRL_ON : NV50_CRTC0_DITHERING_CTRL_OFF);

	if (!crtc->modeset_lock)
		NV50DisplayCommand(pScrn, NV50_UPDATE_DISPLAY, 0);
}

static void
ComputeAspectScale(DisplayModePtr mode, DisplayModePtr adjusted_mode, int *outX, int *outY)
{
	float scaleX, scaleY, scale;

	scaleX = adjusted_mode->HDisplay / (float)mode->HDisplay;
	scaleY = adjusted_mode->VDisplay / (float)mode->VDisplay;

	if (scaleX > scaleY)
		scale = scaleY;
	else
		scale = scaleX;

	*outX = mode->HDisplay * scale;
	*outY = mode->VDisplay * scale;
}

static void
NV50CrtcSetScaleMode(nouveauCrtcPtr crtc, int scale)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcSetScale is called with mode %d for %s.\n", scale, crtc->index ? "CRTC1" : "CRTC0");

	uint32_t scale_val = 0;
	int outX = 0, outY = 0;

	switch(scale) {
		case SCALE_ASPECT:
			ComputeAspectScale(crtc->cur_mode, crtc->native_mode, &outX, &outY);
			break;
		case SCALE_FULLSCREEN:
			outX = crtc->native_mode->HDisplay;
			outY = crtc->native_mode->VDisplay;
			break;
		case SCALE_NOSCALE:
		case SCALE_PANEL:
		default:
			outX = crtc->cur_mode->HDisplay;
			outY = crtc->cur_mode->VDisplay;
			break;
	}

	/* Got a better name for SCALER_ACTIVE? */
	if ((crtc->cur_mode->Flags & V_DBLSCAN) || (crtc->cur_mode->Flags & V_INTERLACE) ||
		crtc->cur_mode->HDisplay != outX || crtc->cur_mode->VDisplay != outY) {
		scale_val = NV50_CRTC0_SCALE_CTRL_SCALER_ACTIVE;
	} else {
		scale_val = NV50_CRTC0_SCALE_CTRL_SCALER_INACTIVE;
	}

	NV50CrtcCommand(crtc, NV50_CRTC0_SCALE_CTRL, scale_val);
	NV50CrtcCommand(crtc, NV50_CRTC0_SCALE_RES1, outY << 16 | outX);
	NV50CrtcCommand(crtc, NV50_CRTC0_SCALE_RES2, outY << 16 | outX);
}

/*
 * Cursor stuff.
 */

static void
NV50CrtcShowCursor(nouveauCrtcPtr crtc, Bool forced_lock)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	//xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcShowCursor is called for %s.\n", crtc->index ? "CRTC1" : "CRTC0");

	if (!crtc->modeset_lock)
		crtc->cursor_visible = TRUE;

	NV50CrtcCommand(crtc, NV50_CRTC0_CURSOR_CTRL, NV50_CRTC0_CURSOR_CTRL_SHOW);

	/* Calling this during modeset will lock things up. */
	if (!crtc->modeset_lock && !forced_lock)
		NV50DisplayCommand(pScrn, NV50_UPDATE_DISPLAY, 0);
}

static void
NV50CrtcHideCursor(nouveauCrtcPtr crtc, Bool forced_lock)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	//xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcHideCursor is called for %s.\n", crtc->index ? "CRTC1" : "CRTC0");

	if (!crtc->modeset_lock)
		crtc->cursor_visible = FALSE;

	NV50CrtcCommand(crtc, NV50_CRTC0_CURSOR_CTRL, NV50_CRTC0_CURSOR_CTRL_HIDE);

	/* Calling this during modeset will lock things up. */
	if (!crtc->modeset_lock && !forced_lock)
		NV50DisplayCommand(pScrn, NV50_UPDATE_DISPLAY, 0);
}

static void
NV50CrtcSetCursorPosition(nouveauCrtcPtr crtc, int x, int y)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	NVPtr pNv = NVPTR(pScrn);
	NVWrite(pNv, NV50_CRTC0_CURSOR_POS + crtc->index * 0x1000, (y & 0xFFFF) << 16 | (x & 0xFFFF));

	/* This is needed to allow the cursor to move. */
	NVWrite(pNv, NV50_CRTC0_CURSOR_POS_CTRL + crtc->index * 0x1000, 0);
}

static void
NV50CrtcLoadCursor(nouveauCrtcPtr crtc, Bool argb, uint32_t *src)
{
	if (!argb) /* FIXME */
		return;

	NVPtr pNv = NVPTR(crtc->scrn);
	uint32_t *dst = NULL;

	if (crtc->index == 1)
		dst = (uint32_t *) pNv->Cursor2->map;
	else
		dst = (uint32_t *) pNv->Cursor->map;

	/* Assume cursor is 64x64 */
	memcpy(dst, src, 64 * 64 * 4);
}

/*
 * Gamma stuff.
 */

/*
 * The indices are a bit strange, but i'll assume it's correct (taken from nv).
 * The LUT resolution seems to be 14 bits on NV50 as opposed to the 8 bits of previous hardware.
 */
#define NV50_LUT_INDEX(val, w) ((val << (8 - w)) | (val >> ((w << 1) - 8)))
static void
NV50CrtcGammaSet(nouveauCrtcPtr crtc, uint16_t *red, uint16_t *green, uint16_t *blue, int size)
{
	ScrnInfoPtr pScrn = crtc->scrn;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NV50CrtcGammaSet is called for %s.\n", crtc->index ? "CRTC1" : "CRTC0");

	NVPtr pNv = NVPTR(pScrn);
	uint32_t index, i;
	void * CLUT = NULL;

	/* Each CRTC has it's own CLUT. */
	if (crtc->index == 1)
		CLUT = pNv->CLUT1->map;
	else
		CLUT = pNv->CLUT0->map;

	volatile struct {
		unsigned short red, green, blue, unused;
	} *lut = (void *) CLUT;

	switch (pScrn->depth) {
	case 15:
		/* R5G5B5 */
		for (i = 0; i < 32; i++) {
			index = NV50_LUT_INDEX(i, 5);
			lut[index].red = red[i] >> 2;
			lut[index].green = green[i] >> 2;
			lut[index].blue = blue[i] >> 2;
		}
		break;
	case 16:
		/* R5G6B5 */
		for (i = 0; i < 32; i++) {
			index = NV50_LUT_INDEX(i, 5);
			lut[index].red = red[i] >> 2;
			lut[index].blue = blue[i] >> 2;
		}

		/* Green has an extra bit. */
		for (i = 0; i < 64; i++) {
			index = NV50_LUT_INDEX(i, 6);
			lut[index].green = green[i] >> 2;
		}
		break;
	default:
		/* R8G8B8 */
		for (i = 0; i < 256; i++) {
			lut[i].red = red[i] >> 2;
			lut[i].green = green[i] >> 2;
			lut[i].blue = blue[i] >> 2;
		}
		break;
	}
}

void
NV50CrtcInit(ScrnInfoPtr pScrn)
{
	int i;
	NVPtr pNv = NVPTR(pScrn);

	for (i=0; i < 2; i++) {
		nouveauCrtcPtr crtc = xnfcalloc(sizeof(nouveauCrtcRec), 1);
		crtc->scrn = pScrn;
		crtc->index = i;

		/* Function pointers. */
		crtc->ModeValid = NV50CrtcModeValid;
		crtc->ModeSet = NV50CrtcModeSet;
		crtc->SetPixelClock = NV50CrtcSetPixelClock;
		crtc->SetClockMode = NV50CrtcSetClockMode;

		crtc->SetFB = NV50CrtcSetFB;
		crtc->SetFBOffset = NV50CrtcSetFBOffset;

		crtc->Blank = NV50CrtcBlank;
		crtc->SetDither = NV50CrtcSetDither;

		crtc->SetScaleMode = NV50CrtcSetScaleMode;

		crtc->ShowCursor = NV50CrtcShowCursor;
		crtc->HideCursor = NV50CrtcHideCursor;
		crtc->SetCursorPosition = NV50CrtcSetCursorPosition;
		crtc->LoadCursor = NV50CrtcLoadCursor;

		crtc->GammaSet = NV50CrtcGammaSet;

		pNv->crtc[i] = crtc;
	}
}

void
NV50CrtcDestroy(ScrnInfoPtr pScrn)
{
	int i;
	NVPtr pNv = NVPTR(pScrn);

	for (i=0; i < 2; i++) {
		nouveauCrtcPtr crtc = pNv->crtc[i];

		xfree(crtc->name);
		xfree(crtc);
		pNv->crtc[i] = NULL;
	}
}
