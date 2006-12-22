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
    return TRUE;
}

static void
nv_output_mode_set(xf86OutputPtr output, DisplayModePtr mode,
		  DisplayModePtr adjusted_mode)
{


}

static xf86OutputStatus
nv_output_detect(xf86OutputPtr output)
{
  
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
  xf86OutputPtr	    output;
  NVOutputPrivatePtr    nv_output;
  char *name[2] =  { "VGA0", "VGA1" };
  int   crtc_mask = (1<<0) | (1<<1);

  for (i = 0; i<NV_MAX_OUTPUT; i++) {

    output = xf86OutputCreate (pScrn, &nv_output_funcs, name[i]);
    if (!output)
	return;
    nv_output = xnfcalloc (sizeof (NVOutputPrivateRec), 1);
    if (!nv_output)
    {
      xf86OutputDestroy (output);
      return;
    }
    
    output->driver_private = nv_output;

    NV_I2CInit(pScrn, &nv_output->pDDCBus, i ? 0x36 : 0x3e, name[i]);
    
    output->possible_crtcs = crtc_mask;
  }
}
