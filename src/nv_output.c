/*
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
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

#include "nv_xf86Crtc.h"
#include "nv_randr.h"
#include "nv_include.h"

#define NV_MAX_OUTPUT 2

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

void NVWriteRAMDAC0(xf86OutputPtr output, CARD32 ramdac_reg, CARD32 val)
{
  NVOutputPrivatePtr nv_output = output->driver_private;
  ScrnInfoPtr	pScrn = output->scrn;
  NVPtr pNv = NVPTR(pScrn);

  NV_WR32(pNv->PRAMDAC0, ramdac_reg, val);
}

void NVWriteRAMDAC(xf86OutputPtr output, CARD32 ramdac_reg, CARD32 val)
{
  NVOutputPrivatePtr nv_output = output->driver_private;
  NV_WR32(nv_output->pRAMDACReg, ramdac_reg, val);
}

CARD32 NVReadRAMDAC(xf86OutputPtr output, CARD32 ramdac_reg)
{
  NVOutputPrivatePtr nv_output = output->driver_private;
  return NV_RD32(nv_output->pRAMDACReg, ramdac_reg);
}

static void
nv_output_dpms(xf86OutputPtr output, int mode)
{


}

static void
nv_output_save (xf86OutputPtr output)
{

}

static void
nv_output_restore (xf86OutputPtr output)
{


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
    NVOutputPrivatePtr nv_output = output->driver_private;

#if 0
    if(nv_output->mon_type == MT_LCD) {
       vertStart = vertTotal - 3;  
       vertEnd = vertTotal - 2;
       vertBlankStart = vertStart;
       horizStart = horizTotal - 5;
       horizEnd = horizTotal - 2;   
       horizBlankEnd = horizTotal + 4;    
    }
#endif

    return TRUE;
}

static void
nv_output_mode_set(xf86OutputPtr output, DisplayModePtr mode,
		  DisplayModePtr adjusted_mode)
{
    NVOutputPrivatePtr nv_output = output->driver_private;
    ScrnInfoPtr	pScrn = output->scrn;
    NVPtr pNv = NVPTR(pScrn);
    RIVA_HW_STATE *state;

    state = &pNv->ModeReg;
    if(nv_output->mon_type == MT_CRT) {
	NVWriteRAMDAC0(output, NV_RAMDAC_PLL_SELECT, state->pllsel);
	NVWriteRAMDAC0(output, NV_RAMDAC_VPLL, state->vpll);
	if(pNv->twoHeads)
	    NVWriteRAMDAC0(output, NV_RAMDAC_VPLL2, state->vpll2);
	if(pNv->twoStagePLL) {
	    NVWriteRAMDAC0(output, NV_RAMDAC_VPLL_B, state->vpllB);
	    NVWriteRAMDAC0(output, NV_RAMDAC_VPLL2_B, state->vpll2B);
	}
    } else {
	NVWriteRAMDAC(output, NV_RAMDAC_FP_CONTROL, state->scale);
	NVWriteRAMDAC(output, NV_RAMDAC_FP_HCRTC, state->crtcSync);
    }
    NVWriteRAMDAC(output, NV_RAMDAC_GENERAL_CONTROL, state->general);

}

static Bool
nv_ddc_detect(xf86OutputPtr output)
{
  NVOutputPrivatePtr nv_output = output->driver_private;
  
  return xf86I2CProbeAddress(nv_output->pDDCBus, 0x00A0);
}

static Bool
nv_crt_load_detect(xf86OutputPtr output)
{
  NVOutputPrivatePtr nv_output = output->driver_private;
  CARD32 reg52C, reg608, temp;
  int present = FALSE;
  
  reg52C = NVReadRAMDAC(output, NV_RAMDAC_052C);
  reg608 = NVReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL);

  NVWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, (reg608 & ~0x00010000));
  
  NVWriteRAMDAC(output, NV_RAMDAC_052C, (reg52C & 0x0000FEEE));
  usleep(1000);
  
  temp = NVReadRAMDAC(output, NV_RAMDAC_052C);
  NVWriteRAMDAC(output, NV_RAMDAC_052C, temp | 1);

  NVWriteRAMDAC(output, NV_RAMDAC_TEST_DATA, 0x94050140);
  temp = NVReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL);
  NVWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, temp | 0x1000);

  usleep(1000);
  
  present = (NVReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL) & (1 << 28)) ? TRUE : FALSE;
  
  temp = NVReadRAMDAC(output, NV_RAMDAC_TEST_CONTROL);
  NVWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, temp & 0x000EFFF);
  
  NVWriteRAMDAC(output, NV_RAMDAC_052C, reg52C);
  NVWriteRAMDAC(output, NV_RAMDAC_TEST_CONTROL, reg608);
  
  return present;

}

static xf86OutputStatus
nv_output_detect(xf86OutputPtr output)
{
  NVOutputPrivatePtr nv_output = output->driver_private;

  if (nv_output->type == OUTPUT_LVDS)
    return XF86OutputStatusUnknown;

  if (nv_output->type == OUTPUT_DVI) {
    if (nv_ddc_detect(output))
      return XF86OutputStatusConnected;

    if (nv_crt_load_detect(output))
      return XF86OutputStatusConnected;

    return XF86OutputStatusDisconnected;
  }
  return XF86OutputStatusUnknown;
}

static DisplayModePtr
nv_output_get_modes(xf86OutputPtr output)
{
  ScrnInfoPtr	pScrn = output->scrn;
  NVOutputPrivatePtr nv_output = output->driver_private;
  xf86MonPtr ddc_mon;
  DisplayModePtr ddc_modes, mode;
  int i;


  ddc_mon = xf86DoEDID_DDC2(pScrn->scrnIndex, nv_output->pDDCBus);
  if (ddc_mon == NULL) {
#ifdef RANDR_12_INTERFACE
    nv_ddc_set_edid_property(output, NULL, 0);
#endif
    return NULL;
  }
  
  if (output->MonInfo != NULL)
    xfree(output->MonInfo);
  output->MonInfo = ddc_mon;

  /* check if a CRT or DFP */
  if (ddc_mon->features.input_type)
    nv_output->mon_type = MT_LCD;
  else
    nv_output->mon_type = MT_CRT;

#ifdef RANDR_12_INTERFACE
  if (output->MonInfo->ver.version == 1) {
    nv_ddc_set_edid_property(output, ddc_mon->rawData, 128);
    } else if (output->MonInfo->ver.version == 2) {
	nv_ddc_set_edid_property(output, ddc_mon->rawData, 256);
    } else {
	nv_ddc_set_edid_property(output, NULL, 0);
    }
#endif

  /* Debug info for now, at least */
  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EDID for output %s\n", output->name);
  xf86PrintEDID(output->MonInfo);
  
  ddc_modes = xf86DDCGetModes(pScrn->scrnIndex, ddc_mon);
  
  /* Strip out any modes that can't be supported on this output. */
  for (mode = ddc_modes; mode != NULL; mode = mode->next) {
    int status = (*output->funcs->mode_valid)(output, mode);
    
    if (status != MODE_OK)
      mode->status = status;
  }
  i830xf86PruneInvalidModes(pScrn, &ddc_modes, TRUE);
  
  /* Pull out a phyiscal size from a detailed timing if available. */
  for (i = 0; i < 4; i++) {
    if (ddc_mon->det_mon[i].type == DT &&
	ddc_mon->det_mon[i].section.d_timings.h_size != 0 &&
	ddc_mon->det_mon[i].section.d_timings.v_size != 0)
      {
	output->mm_width = ddc_mon->det_mon[i].section.d_timings.h_size;
	output->mm_height = ddc_mon->det_mon[i].section.d_timings.v_size;
	break;
      }
  }
  
  return ddc_modes;

}

static void
nv_output_destroy (xf86OutputPtr output)
{
    if (output->driver_private)
      xfree (output->driver_private);

}

static const xf86OutputFuncsRec nv_output_funcs = {
    .dpms = nv_output_dpms,
    .save = nv_output_save,
    .restore = nv_output_restore,
    .mode_valid = nv_output_mode_valid,
    .mode_fixup = nv_output_mode_fixup,
    .mode_set = nv_output_mode_set,
    .detect = nv_output_detect,
    .get_modes = nv_output_get_modes,
    .destroy = nv_output_destroy
};

/**
 * Set up the outputs according to what type of chip we are.
 *
 * Some outputs may not initialize, due to allocation failure or because a
 * controller chip isn't found.
 */
void NvSetupOutputs(ScrnInfoPtr pScrn)
{
  int i;
  NVPtr pNv = NVPTR(pScrn);
  xf86OutputPtr	    output;
  NVOutputPrivatePtr    nv_output;
  char *ddc_name[2] =  { "DDC0", "DDC1" };
  int   crtc_mask = (1<<0) | (1<<1);
  int output_type = OUTPUT_DVI;
  int num_outputs = pNv->twoHeads ? 2 : 1;

  pNv->Television = FALSE;

  /* work out outputs and type of outputs here */
  for (i = 0; i<num_outputs; i++) {
    output = xf86OutputCreate (pScrn, &nv_output_funcs, OutputType[output_type]);
    if (!output)
	return;
    nv_output = xnfcalloc (sizeof (NVOutputPrivateRec), 1);
    if (!nv_output)
    {
      xf86OutputDestroy (output);
      return;
    }
    
    output->driver_private = nv_output;
    nv_output->type = output_type;
    nv_output->ramdac = i;
    if (i == 0)
      nv_output->pRAMDACReg = pNv->PRAMDAC0;
    else
      nv_output->pRAMDACReg = pNv->PRAMDAC1;

    NV_I2CInit(pScrn, &nv_output->pDDCBus, i ? 0x36 : 0x3e, ddc_name[i]);
    
    output->possible_crtcs = crtc_mask;
  }

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

    output->possible_crtcs = crtc_mask;
  }
}
