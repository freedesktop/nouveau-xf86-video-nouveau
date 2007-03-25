/*
 * Copyright 2006 Dave Airlie
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
 *
 * Authors:
 *  Dave Airlie
 */
/*
 * this code uses ideas taken from the NVIDIA nv driver - the nvidia license
 * decleration is at the bottom of this file as it is rather ugly 
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

void NVOutputWriteRAMDAC(xf86OutputPtr output, CARD32 ramdac_reg, CARD32 val)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);

    nvWriteRAMDAC(pNv, nv_output->ramdac, ramdac_reg, val);
}

CARD32 NVOutputReadRAMDAC(xf86OutputPtr output, CARD32 ramdac_reg)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);

    return nvReadRAMDAC(pNv, nv_output->ramdac, ramdac_reg);
}

static void nv_output_backlight_enable(xf86OutputPtr output,  Bool on)
{
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);   

    /* This is done differently on each laptop.  Here we
       define the ones we know for sure. */
    
#if defined(__powerpc__)
    if((pNv->Chipset == 0x10DE0179) || 
       (pNv->Chipset == 0x10DE0189) || 
       (pNv->Chipset == 0x10DE0329))
    {
	/* NV17,18,34 Apple iMac, iBook, PowerBook */
	CARD32 tmp_pmc, tmp_pcrt;
	tmp_pmc = nvReadMC(pNv, 0x10F0) & 0x7FFFFFFF;
	tmp_pcrt = nvReadCRTC0(pNv, NV_CRTC_081C) & 0xFFFFFFFC;
	if(on) {
	    tmp_pmc |= (1 << 31);
	    tmp_pcrt |= 0x1;
	}
	nvWriteMC(pNv, 0x10F0, tmp_pmc);
	nvWriteCRTC0(pNv, NV_CRTC_081C, tmp_pcrt);
    }
#endif
    
    if(pNv->twoHeads && ((pNv->Chipset & 0x0ff0) != CHIPSET_NV11))
	nvWriteMC(pNv, 0x130C, on ? 3 : 7);
}

static void
nv_panel_output_dpms(xf86OutputPtr output, int mode)
{

    switch(mode) {
    case DPMSModeStandby:
    case DPMSModeSuspend:
    case DPMSModeOff:
	nv_output_backlight_enable(output, 0);
	break;
    case DPMSModeOn:
	nv_output_backlight_enable(output, 1);
    default:
	break;
    }
}

static void
nv_analog_output_dpms(xf86OutputPtr output, int mode)
{

}

static void
nv_digital_output_dpms(xf86OutputPtr output, int mode)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    xf86CrtcPtr crtc = output->crtc;
    ScrnInfoPtr pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    NVCrtcPrivatePtr nv_crtc;

    CARD32 fpcontrol;
    
    if (crtc)  {
	nv_crtc = crtc->driver_private;
	
	fpcontrol = nvReadRAMDAC(pNv, nv_crtc->crtc, NV_RAMDAC_FP_CONTROL) & 0xCfffffCC;	
	switch(mode) {
	case DPMSModeStandby:
	case DPMSModeSuspend:
	case DPMSModeOff:
	    /* cut the TMDS output */	    
	    fpcontrol |= 0x20000022;
	    break;
	case DPMSModeOn:
	    fpcontrol |= nv_output->fpSyncs;
	}
	
	nvWriteRAMDAC(pNv, nv_crtc->crtc, NV_RAMDAC_FP_CONTROL, fpcontrol);
    }
}

void nv_output_save_state_ext(xf86OutputPtr output, RIVA_HW_STATE *state)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    NVOutputRegPtr regp;

    regp = &state->dac_reg[nv_output->ramdac];
    regp->general       = NVOutputReadRAMDAC(output, NV_RAMDAC_GENERAL_CONTROL);
    regp->fp_control    = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_CONTROL);
    regp->debug_0	= NVOutputReadRAMDAC(output, NV_RAMDAC_FP_DEBUG_0);
    state->config       = nvReadFB(pNv, NV_PFB_CFG0);
    
    regp->output = NVOutputReadRAMDAC(output, NV_RAMDAC_OUTPUT);
    
    if((pNv->Chipset & 0x0ff0) == CHIPSET_NV11) {
	regp->dither = NVOutputReadRAMDAC(output, NV_RAMDAC_DITHER_NV11);
    } else if(pNv->twoHeads) {
	regp->dither = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_DITHER);
    }
    //    regp->crtcSync = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_HCRTC);
    regp->nv10_cursync = NVOutputReadRAMDAC(output, NV_RAMDAC_NV10_CURSYNC);

    if (nv_output->type == OUTPUT_DIGITAL) {
	int i;

	for (i = 0; i < 7; i++) {
	    uint32_t ramdac_reg = NV_RAMDAC_FP_HDISP_END + (i * 4);
	    
	    regp->fp_horiz_regs[i] = NVOutputReadRAMDAC(output, ramdac_reg);
	}
	
	for (i = 0; i < 7; i++) {
	    uint32_t ramdac_reg = NV_RAMDAC_FP_VDISP_END + (i * 4);
	    
	    regp->fp_vert_regs[i] = NVOutputReadRAMDAC(output, ramdac_reg);
	}
    }

}

void nv_output_load_state_ext(xf86OutputPtr output, RIVA_HW_STATE *state)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    NVOutputRegPtr regp;
  
    regp = &state->dac_reg[nv_output->ramdac];
  
    NVOutputWriteRAMDAC(output, NV_RAMDAC_FP_DEBUG_0, regp->debug_0);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_OUTPUT, regp->output);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_FP_CONTROL, regp->fp_control);
    //    NVOutputWriteRAMDAC(output, NV_RAMDAC_FP_HCRTC, regp->crtcSync);
  

    if((pNv->Chipset & 0x0ff0) == CHIPSET_NV11) {
	NVOutputWriteRAMDAC(output, NV_RAMDAC_DITHER_NV11, regp->dither);
    } else if(pNv->twoHeads) {
	NVOutputWriteRAMDAC(output, NV_RAMDAC_FP_DITHER, regp->dither);
    }
  
    NVOutputWriteRAMDAC(output, NV_RAMDAC_GENERAL_CONTROL, regp->general);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_NV10_CURSYNC, regp->nv10_cursync);

    if (nv_output->type == OUTPUT_DIGITAL) {
	int i;

	for (i = 0; i < 7; i++) {
	    uint32_t ramdac_reg = NV_RAMDAC_FP_HDISP_END + (i * 4);
	    NVOutputWriteRAMDAC(output, ramdac_reg, regp->fp_horiz_regs[i]);
	}
	
	for (i = 0; i < 7; i++) {
	    uint32_t ramdac_reg = NV_RAMDAC_FP_VDISP_END + (i * 4);
	    
	    NVOutputWriteRAMDAC(output, ramdac_reg, regp->fp_vert_regs[i]);
	}
    }

}


static void
nv_output_save (xf86OutputPtr output)
{
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    RIVA_HW_STATE *state;
  
    state = &pNv->SavedReg;
  
    nv_output_save_state_ext(output, state);    
  
}

static void
nv_output_restore (xf86OutputPtr output)
{
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    RIVA_HW_STATE *state;
  
    state = &pNv->SavedReg;
  
    nv_output_load_state_ext(output, state);
}

static int
nv_output_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    if (pMode->Flags & V_DBLSCAN)
	return MODE_NO_DBLESCAN;
  
    if (pMode->Clock > 400000 || pMode->Clock < 25000)
	return MODE_CLOCK_RANGE;
  
    return MODE_OK;
}


static Bool
nv_output_mode_fixup(xf86OutputPtr output, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode)
{
    return TRUE;
}

static int
nv_output_tweak_panel(xf86OutputPtr output, NVRegPtr state)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    NVOutputRegPtr regp;
    int tweak = 0;
  
    regp = &state->dac_reg[nv_output->ramdac];
    if (pNv->usePanelTweak) {
	tweak = pNv->PanelTweak;
    } else {
	/* begin flat panel hacks */
	/* This is unfortunate, but some chips need this register
	   tweaked or else you get artifacts where adjacent pixels are
	   swapped.  There are no hard rules for what to set here so all
	   we can do is experiment and apply hacks. */
    
	if(((pNv->Chipset & 0xffff) == 0x0328) && (regp->bpp == 32)) {
	    /* At least one NV34 laptop needs this workaround. */
	    tweak = -1;
	}
		
	if((pNv->Chipset & 0xfff0) == CHIPSET_NV31) {
	    tweak = 1;
	}
	/* end flat panel hacks */
    }
    return tweak;
}

static void
nv_output_mode_set_regs(xf86OutputPtr output, DisplayModePtr mode)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr	pScrn = output->scrn;
    int bpp;
    NVPtr pNv = NVPTR(pScrn);
    NVFBLayout *pLayout = &pNv->CurrentLayout;
    RIVA_HW_STATE *state, *sv_state;
    Bool is_fp = FALSE;
    NVOutputRegPtr regp, savep;
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    state = &pNv->ModeReg;
    regp = &state->dac_reg[nv_output->ramdac];

    sv_state = &pNv->SavedReg;
    savep = &sv_state->dac_reg[nv_output->ramdac];
	    
    if ((nv_output->type == OUTPUT_PANEL) || (nv_output->type == OUTPUT_DIGITAL))
    {
	is_fp = TRUE;

	for (i = 0; i < 7; i++) {
	    regp->fp_horiz_regs[i] = savep->fp_horiz_regs[i];
	    regp->fp_vert_regs[i] = savep->fp_vert_regs[i];
	}

	regp->fp_horiz_regs[REG_DISP_END] = mode->CrtcHDisplay - 1;
	regp->fp_horiz_regs[REG_DISP_TOTAL] = mode->CrtcHTotal - 1;
	regp->fp_horiz_regs[REG_DISP_CRTC] = mode->CrtcHDisplay;
	regp->fp_horiz_regs[REG_DISP_SYNC_START] = mode->CrtcHSyncStart - 1;
	regp->fp_horiz_regs[REG_DISP_SYNC_END] = mode->CrtcHSyncEnd - 1;
	regp->fp_horiz_regs[REG_DISP_VALID_START] = mode->CrtcHSkew;
	regp->fp_horiz_regs[REG_DISP_VALID_END] = mode->CrtcHDisplay - 1;
	
	regp->fp_vert_regs[REG_DISP_END] = mode->CrtcVDisplay - 1;
	regp->fp_vert_regs[REG_DISP_TOTAL] = mode->CrtcVTotal - 1;
	regp->fp_vert_regs[REG_DISP_CRTC] = mode->CrtcVDisplay;
	regp->fp_vert_regs[REG_DISP_SYNC_START] = mode->CrtcVSyncStart - 1;
	regp->fp_vert_regs[REG_DISP_SYNC_END] = mode->CrtcVSyncEnd - 1;
	regp->fp_vert_regs[REG_DISP_VALID_START] = 0;
	regp->fp_vert_regs[REG_DISP_VALID_END] = mode->CrtcVDisplay - 1;
    
    }

    if (pNv->Architecture >= NV_ARCH_10) 
	regp->nv10_cursync = savep->nv10_cursync | (1<<25);

    regp->bpp    = bpp;    /* this is not bitsPerPixel, it's 8,15,16,32 */

    regp->debug_0 = savep->debug_0;
    regp->fp_control = savep->fp_control & 0xfff000ff;
    if(is_fp == 1) {
	if(!pNv->fpScaler || (nv_output->fpWidth <= mode->HDisplay)
	   || (nv_output->fpHeight <= mode->VDisplay))
	{
	    regp->fp_control |= (1 << 8) ;
	}
	regp->crtcSync = savep->crtcSync;
	regp->crtcSync += nv_output_tweak_panel(output, state);

	regp->debug_0 &= ~NV_RAMDAC_FP_DEBUG_0_PWRDOWN_BOTH;
    }
    else
	regp->debug_0 |= NV_RAMDAC_FP_DEBUG_0_PWRDOWN_BOTH;

    ErrorF("output %d debug_0 %08X\n", nv_output->ramdac, regp->debug_0);

    if(pNv->twoHeads) {
	if((pNv->Chipset & 0x0ff0) == CHIPSET_NV11) {
	    regp->dither = savep->dither & ~0x00010000;
	    if(pNv->FPDither)
		regp->dither |= 0x00010000;
	} else {
	    ErrorF("savep->dither %08X\n", savep->dither);
	    regp->dither = savep->dither & ~1;
	    if(pNv->FPDither)
		regp->dither |= 1;
	} 
    }

    if(pLayout->depth < 24) 
	bpp = pLayout->depth;
    else bpp = 32;    

    regp->general  = bpp == 16 ? 0x00101100 : 0x00100100;

    if (pNv->alphaCursor)
	regp->general |= (1<<29);

    if(bpp != 8) /* DirectColor */
	regp->general |= 0x00000030;

    if (output->crtc) {
	NVCrtcPrivatePtr nv_crtc = output->crtc->driver_private;
	int two_crt = FALSE;
	int two_mon = FALSE;

	for (i = 0; i < config->num_output; i++) {
	    NVOutputPrivatePtr nv_output2 = config->output[i]->driver_private;

	    /* is it this output ?? */
	    if (config->output[i] == output)
		continue;

	    /* it the output connected */
	    if (config->output[i]->crtc == NULL)
		continue;

	    two_mon = TRUE;
	    if ((nv_output2->type == OUTPUT_ANALOG) && (nv_output->type == OUTPUT_ANALOG))
		two_crt = TRUE;
	}

	if (is_fp == TRUE)
	    regp->output = 0x0;
	else 
  	    regp->output = NV_RAMDAC_OUTPUT_DAC_ENABLE;

	if (nv_crtc->crtc == 1 && two_mon)
	  regp->output |= NV_RAMDAC_OUTPUT_SELECT_CRTC2;

	ErrorF("%d: crtc %d output%d: %04X: twocrt %d twomon %d\n", is_fp, nv_crtc->crtc, nv_output->ramdac, regp->output, two_crt, two_mon);
    }
}

static void
nv_output_mode_set(xf86OutputPtr output, DisplayModePtr mode,
		   DisplayModePtr adjusted_mode)
{
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    RIVA_HW_STATE *state;

    state = &pNv->ModeReg;

    nv_output_mode_set_regs(output, mode);
    nv_output_load_state_ext(output, state);
}

static Bool
nv_ddc_detect(xf86OutputPtr output)
{
    /* no use for shared DDC output */
    NVOutputPrivatePtr nv_output = output->driver_private;
    xf86MonPtr ddc_mon;

    ddc_mon = xf86OutputGetEDID(output, nv_output->pDDCBus);
    if (!ddc_mon)
	return 0;

    if (ddc_mon->features.input_type && (nv_output->type == OUTPUT_ANALOG))
	return 0;

    if ((!ddc_mon->features.input_type) && (nv_output->type == OUTPUT_DIGITAL))
	return 0;

    return 1;
}

static Bool
nv_crt_load_detect(xf86OutputPtr output)
{
    ScrnInfoPtr	pScrn = output->scrn;
    CARD32 reg_output, reg_test_ctrl, temp;
    int present = FALSE;
	  
    reg_output = NVOutputReadRAMDAC(output, NV_RAMDAC_OUTPUT);
    reg_test_ctrl = NVOutputReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL);

    NVOutputWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, (reg_test_ctrl & ~0x00010000));
    
    NVOutputWriteRAMDAC(output, NV_RAMDAC_OUTPUT, (reg_output & 0x0000FEEE));
    usleep(1000);
	  
    temp = NVOutputReadRAMDAC(output, NV_RAMDAC_OUTPUT);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_OUTPUT, temp | 1);

    NVOutputWriteRAMDAC(output, NV_RAMDAC_TEST_DATA, 0x94050140);
    temp = NVOutputReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, temp | 0x1000);

    usleep(1000);
	  
    present = (NVOutputReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL) & (1 << 28)) ? TRUE : FALSE;
	  
    temp = NVOutputReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, temp & 0x000EFFF);
	  
    NVOutputWriteRAMDAC(output, NV_RAMDAC_OUTPUT, reg_output);
    NVOutputWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, reg_test_ctrl);
    
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "CRT detect returned %d\n",
	       present);

    return present;

}

static xf86OutputStatus
nv_digital_output_detect(xf86OutputPtr output)
{
    NVOutputPrivatePtr nv_output = output->driver_private;

    if (nv_ddc_detect(output))
	return XF86OutputStatusConnected;

    return XF86OutputStatusDisconnected;
}


static xf86OutputStatus
nv_analog_output_detect(xf86OutputPtr output)
{
    NVOutputPrivatePtr nv_output = output->driver_private;

    if (nv_ddc_detect(output))
	return XF86OutputStatusConnected;

    /* seems a bit flaky on ramdac 1 */
    if (nv_crt_load_detect(output) && (nv_output->ramdac == 0))
	return XF86OutputStatusConnected;
    
    return XF86OutputStatusDisconnected;
}

static DisplayModePtr
nv_output_get_modes(xf86OutputPtr output)
{
    ScrnInfoPtr	pScrn = output->scrn;
    NVOutputPrivatePtr nv_output = output->driver_private;
    xf86MonPtr ddc_mon;
    DisplayModePtr ddc_modes, mode;
    int i;


    ddc_mon = xf86OutputGetEDID(output, nv_output->pDDCBus);

    if (ddc_mon == NULL) {
	xf86OutputSetEDID(output, ddc_mon);
	return NULL;
    }

    if (ddc_mon->features.input_type && (nv_output->type == OUTPUT_ANALOG)) {
	xf86OutputSetEDID(output, NULL);
	return NULL;
    }

    if ((!ddc_mon->features.input_type) && (nv_output->type == OUTPUT_DIGITAL)) {
	xf86OutputSetEDID(output, NULL);
	return NULL;
    }

    xf86OutputSetEDID(output, ddc_mon);

    ddc_modes = xf86OutputGetEDIDModes (output);	  
    if (nv_output->type == OUTPUT_DIGITAL) {
	nv_output->fpWidth = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_HDISP_END) + 1;
	nv_output->fpHeight = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_VDISP_END) + 1;
	nv_output->fpSyncs = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_CONTROL) & 0x30000033;
	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Panel size is %i x %i\n",
		   nv_output->fpWidth, nv_output->fpHeight);

    }
    return ddc_modes;

}

static void
nv_output_destroy (xf86OutputPtr output)
{
    if (output->driver_private)
	xfree (output->driver_private);

}

static void
nv_output_prepare(xf86OutputPtr output)
{

}

static void
nv_output_commit(xf86OutputPtr output)
{


}

static const xf86OutputFuncsRec nv_analog_output_funcs = {
    .dpms = nv_analog_output_dpms,
    .save = nv_output_save,
    .restore = nv_output_restore,
    .mode_valid = nv_output_mode_valid,
    .mode_fixup = nv_output_mode_fixup,
    .mode_set = nv_output_mode_set,
    .detect = nv_analog_output_detect,
    .get_modes = nv_output_get_modes,
    .destroy = nv_output_destroy,
    .prepare = nv_output_prepare,
    .commit = nv_output_commit,
};

static const xf86OutputFuncsRec nv_digital_output_funcs = {
    .dpms = nv_digital_output_dpms,
    .save = nv_output_save,
    .restore = nv_output_restore,
    .mode_valid = nv_output_mode_valid,
    .mode_fixup = nv_output_mode_fixup,
    .mode_set = nv_output_mode_set,
    .detect = nv_digital_output_detect,
    .get_modes = nv_output_get_modes,
    .destroy = nv_output_destroy,
    .prepare = nv_output_prepare,
    .commit = nv_output_commit,
};

static xf86OutputStatus
nv_output_lvds_detect(xf86OutputPtr output)
{
    return XF86OutputStatusUnknown;    
}

static DisplayModePtr
nv_output_lvds_get_modes(xf86OutputPtr output)
{
    ScrnInfoPtr	pScrn = output->scrn;
    NVOutputPrivatePtr nv_output = output->driver_private;

    nv_output->fpWidth = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_HDISP_END) + 1;
    nv_output->fpHeight = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_VDISP_END) + 1;
    nv_output->fpSyncs = NVOutputReadRAMDAC(output, NV_RAMDAC_FP_CONTROL) & 0x30000033;
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Panel size is %i x %i\n",
	       nv_output->fpWidth, nv_output->fpHeight);

    return NULL;

}

static const xf86OutputFuncsRec nv_lvds_output_funcs = {
    .dpms = nv_panel_output_dpms,
    .save = nv_output_save,
    .restore = nv_output_restore,
    .mode_valid = nv_output_mode_valid,
    .mode_fixup = nv_output_mode_fixup,
    .mode_set = nv_output_mode_set,
    .detect = nv_output_lvds_detect,
    .get_modes = nv_output_lvds_get_modes,
    .destroy = nv_output_destroy,
    .prepare = nv_output_prepare,
    .commit = nv_output_commit,
};


static void nv_add_analog_output(ScrnInfoPtr pScrn, int i2c_index)
{
  NVPtr pNv = NVPTR(pScrn);
  xf86OutputPtr	    output;
  NVOutputPrivatePtr    nv_output;
  char outputname[20];
  int   crtc_mask = (1<<0) | (1<<1);

  sprintf(outputname, "Analog-%d", pNv->analog_count);
  output = xf86OutputCreate (pScrn, &nv_analog_output_funcs, outputname);
  if (!output)
    return;
  nv_output = xnfcalloc (sizeof (NVOutputPrivateRec), 1);
  if (!nv_output)
  {
    xf86OutputDestroy (output);
    return;
  }
  
  output->driver_private = nv_output;
  nv_output->type = OUTPUT_ANALOG;

  nv_output->ramdac = pNv->analog_count;

  nv_output->pDDCBus = pNv->pI2CBus[i2c_index];

  output->possible_crtcs = crtc_mask;
  xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Adding output %s\n", outputname);
  
  pNv->analog_count++;
}


static void nv_add_digital_output(ScrnInfoPtr pScrn, int i2c_index)
{
  NVPtr pNv = NVPTR(pScrn);
  xf86OutputPtr	    output;
  NVOutputPrivatePtr    nv_output;
  char outputname[20];
  int   crtc_mask = (1<<0) | (1<<1);

  sprintf(outputname, "Digital-%d", pNv->digital_count);
  output = xf86OutputCreate (pScrn, &nv_digital_output_funcs, outputname);
  if (!output)
    return;
  nv_output = xnfcalloc (sizeof (NVOutputPrivateRec), 1);
  if (!nv_output)
  {
    xf86OutputDestroy (output);
    return;
  }
  
  output->driver_private = nv_output;
  nv_output->type = OUTPUT_DIGITAL;
  
  nv_output->ramdac = pNv->digital_count;
  
  nv_output->pDDCBus = pNv->pI2CBus[i2c_index];
  
  output->possible_crtcs = crtc_mask;
  xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Adding output %s\n", outputname);

  pNv->digital_count++;
}
/**
 * Set up the outputs according to what type of chip we are.
 *
 * Some outputs may not initialize, due to allocation failure or because a
 * controller chip isn't found.
 */

void Nv20SetupOutputs(ScrnInfoPtr pScrn)
{
    NVPtr pNv = NVPTR(pScrn);
    xf86OutputPtr	    output;
    NVOutputPrivatePtr    nv_output;
    int i;
    int num_analog_outputs = pNv->twoHeads ? 2 : 1;
    int num_digital_outputs = 1;

    for (i = 0 ; i < num_analog_outputs; i++) {
      nv_add_analog_output(pScrn, i);
    }

    for (i = 0 ; i < num_digital_outputs; i++) {
      nv_add_digital_output(pScrn, i);
    }
}

void Nv40SetupOutputs(ScrnInfoPtr pScrn)
{
  unsigned char type, port, or;
  NVPtr pNv = NVPTR(pScrn);
  int i;

  /* we setup the outputs up from the BIOS table */
  if (pNv->dcb_entries) {
    for (i = 0 ; i < pNv->dcb_entries; i++) {
      type = pNv->dcb_table[i] & 0xf;
      port = (pNv->dcb_table[i] >> 4) & 0xf;
      or = ffs((pNv->dcb_table[i] >> 24) & 0xf) - 1;
     
      if (type < 4)
	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "DCB entry: %d: %08X type: %d, port %d:, or %d\n", i, pNv->dcb_table[i], type, port, or);
      if (type < 4 && port != 0xf) {
	switch(type) {
	case 0: /* analog */
	  nv_add_analog_output(pScrn, port);
	  break;
	case 2:
	  nv_add_digital_output(pScrn, port);
	default:
	  break;
	}
      }
    }
  } else
    Nv20SetupOutputs(pScrn);

}

struct nv_i2c_struct {
    int reg;
    char *name;
} nv_i2c_buses[] = { 
    { 0x3e, "DDC1" },
    { 0x36, "DDC2" },
    { 0x50, "TV" },
};


void NvSetupOutputs(ScrnInfoPtr pScrn)
{
    int i;
    NVPtr pNv = NVPTR(pScrn);
    xf86OutputPtr	    output;
    NVOutputPrivatePtr    nv_output;

    int num_outputs = pNv->twoHeads ? 2 : 1;
    char outputname[20];
    pNv->Television = FALSE;

    /* add the 3 I2C buses */
    for (i = 0; i < NV_I2C_BUSES; i++) {
        NV_I2CInit(pScrn, &pNv->pI2CBus[i], nv_i2c_buses[i].reg, nv_i2c_buses[i].name);
    }
	
    switch(pNv->Chipset & 0x0ff0) {
    case CHIPSET_NV04:
    case CHIPSET_NV10:
    case CHIPSET_NV11:
    case CHIPSET_NV15:
    case CHIPSET_NV17:
    case CHIPSET_NV18:
    case CHIPSET_NFORCE:
    case CHIPSET_NFORCE2:
    case CHIPSET_NV20:
    case CHIPSET_NV25:
    case CHIPSET_NV28:
    case CHIPSET_NV30:
    case CHIPSET_NV31:
    case CHIPSET_NV34:
    case CHIPSET_NV35:
    case CHIPSET_NV36:
	Nv20SetupOutputs(pScrn);
	break;
    default:
	Nv40SetupOutputs(pScrn);
	break;
    }

#if 0
    if (pNv->Mobile) {
	output = xf86OutputCreate(pScrn, &nv_output_funcs, OutputType[OUTPUT_LVDS]);
	if (!output)
	    return;

	nv_output = xnfcalloc(sizeof(NVOutputPrivateRec), 1);
	if (!nv_output) {
	    xf86OutputDestroy(output);
	    return;
	}

	output->driver_private = nv_output;
	nv_output->type = output_type;

	output->possible_crtcs = i ? 1 : crtc_mask;
    }
#endif
}


/*************************************************************************** \
|*                                                                           *|
|*       Copyright 1993-2003 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-1999 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/
