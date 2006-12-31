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

#include "vgaHW.h"

#define CRTC_INDEX 0x3d4
#define CRTC_DATA 0x3d5
#define CRTC_IN_STAT_1 0x3da

#define WHITE_VALUE 0x3F
#define BLACK_VALUE 0x00
#define OVERSCAN_VALUE 0x01

void nv_crtc_load_vga_state(xf86CrtcPtr crtc);
void nv_crtc_load_state (xf86CrtcPtr crtc);

static void NVWriteVgaCrtc(xf86CrtcPtr crtc, CARD8 index, CARD8 value)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, CRTC_INDEX, index);
  NV_WR08(nv_crtc->pCRTCReg, CRTC_DATA, value);
}

static CARD8 NVReadVgaCrtc(xf86CrtcPtr crtc, CARD8 index)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, CRTC_INDEX, index);
  return NV_RD08(nv_crtc->pCRTCReg, CRTC_DATA);
}

static void NVWriteVgaSeq(xf86CrtcPtr crtc, CARD8 index, CARD8 value)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, VGA_SEQ_INDEX, index);
  NV_WR08(nv_crtc->pCRTCReg, VGA_SEQ_DATA, value);
}

static CARD8 NVReadVgaSeq(xf86CrtcPtr crtc, CARD8 index)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, VGA_SEQ_INDEX, index);
  return NV_RD08(nv_crtc->pCRTCReg, VGA_SEQ_DATA);
}

static void NVWriteVgaGr(xf86CrtcPtr crtc, CARD8 index, CARD8 value)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, VGA_GRAPH_INDEX, index);
  NV_WR08(nv_crtc->pCRTCReg, VGA_GRAPH_DATA, value);
}

static CARD8 NVReadVgaGr(xf86CrtcPtr crtc, CARD8 index)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, VGA_GRAPH_INDEX, index);
  return NV_RD08(nv_crtc->pCRTCReg, VGA_GRAPH_DATA);
} 


static void NVWriteVgaAttr(xf86CrtcPtr crtc, CARD8 index, CARD8 value)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_RD08(nv_crtc->pCRTCReg, CRTC_IN_STAT_1);
  if (nv_crtc->paletteEnabled)
    index &= ~0x20;
  else
    index |= 0x20;
  NV_WR08(nv_crtc->pCRTCReg, VGA_ATTR_INDEX, index);
  NV_WR08(nv_crtc->pCRTCReg, VGA_ATTR_DATA_W, value);
}

static CARD8 NVReadVgaAttr(xf86CrtcPtr crtc, CARD8 index)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_RD08(nv_crtc->pCRTCReg, CRTC_IN_STAT_1);
  if (nv_crtc->paletteEnabled)
    index &= ~0x20;
  else
    index |= 0x20;
  NV_WR08(nv_crtc->pCRTCReg, VGA_ATTR_INDEX, index);
  return NV_RD08(nv_crtc->pCRTCReg, VGA_ATTR_DATA_R);
}

static void
NVEnablePalette(xf86CrtcPtr crtc)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_RD08(nv_crtc->pCRTCReg, CRTC_IN_STAT_1);
  NV_WR08(nv_crtc->pCRTCReg, VGA_ATTR_INDEX, 0);
  nv_crtc->paletteEnabled = TRUE;
}

static void
NVDisablePalette(xf86CrtcPtr crtc)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_RD08(nv_crtc->pCRTCReg, CRTC_IN_STAT_1);
  NV_WR08(nv_crtc->pCRTCReg, VGA_ATTR_INDEX, 0x20);
  nv_crtc->paletteEnabled = FALSE;
}

static void NVWriteVgaReg(xf86CrtcPtr crtc, CARD32 reg, CARD8 value)
{
  NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

  NV_WR08(nv_crtc->pCRTCReg, reg, value);
}

/* perform a sequencer reset */
static void NVVgaSeqReset(xf86CrtcPtr crtc, Bool start)
{
  if (start)
    NVWriteVgaSeq(crtc, 0x00, 0x1);
  else
    NVWriteVgaSeq(crtc, 0x00, 0x3);

}
static void NVVgaProtect(xf86CrtcPtr crtc, Bool on)
{
  CARD8 tmp;

  if (on) {
    tmp = NVReadVgaSeq(crtc, 0x1);
    NVVgaSeqReset(crtc, TRUE);
    NVWriteVgaSeq(crtc, 0x01, tmp | 0x20);

    NVEnablePalette(crtc);
  } else {
    /*
     * Reenable sequencer, then turn on screen.
     */
    tmp = NVReadVgaSeq(crtc, 0x1);
    NVVgaSeqReset(crtc, FALSE);

    NVDisablePalette(crtc);
  }
}

static void NVCrtcLockUnlock(xf86CrtcPtr crtc, Bool Lock)
{
  CARD8 cr11;

  NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_LOCK, Lock ? 0x99 : 0x57);
  cr11 = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_VSYNCE);
  if (Lock) cr11 |= 0x80;
  else cr11 &= ~0x80;
  NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_VSYNCE, cr11);

}
/*
 * Calculate the Video Clock parameters for the PLL.
 */
static void CalcVClock (
    int           clockIn,
    int          *clockOut,
    CARD32         *pllOut,
    NVPtr        pNv
)
{
    unsigned lowM, highM;
    unsigned DeltaNew, DeltaOld;
    unsigned VClk, Freq;
    unsigned M, N, P;
    
    DeltaOld = 0xFFFFFFFF;

    VClk = (unsigned)clockIn;
    
    if (pNv->CrystalFreqKHz == 13500) {
        lowM  = 7;
        highM = 13;
    } else {
        lowM  = 8;
        highM = 14;
    }

    for (P = 0; P <= 4; P++) {
        Freq = VClk << P;
        if ((Freq >= 128000) && (Freq <= 350000)) {
            for (M = lowM; M <= highM; M++) {
                N = ((VClk << P) * M) / pNv->CrystalFreqKHz;
                if(N <= 255) {
                    Freq = ((pNv->CrystalFreqKHz * N) / M) >> P;
                    if (Freq > VClk)
                        DeltaNew = Freq - VClk;
                    else
                        DeltaNew = VClk - Freq;
                    if (DeltaNew < DeltaOld) {
                        *pllOut   = (P << 16) | (N << 8) | M;
                        *clockOut = Freq;
                        DeltaOld  = DeltaNew;
                    }
                }
            }
        }
    }
}

static void CalcVClock2Stage (
    int           clockIn,
    int          *clockOut,
    CARD32         *pllOut,
    CARD32         *pllBOut,
    NVPtr        pNv
)
{
    unsigned DeltaNew, DeltaOld;
    unsigned VClk, Freq;
    unsigned M, N, P;

    DeltaOld = 0xFFFFFFFF;

    *pllBOut = 0x80000401;  /* fixed at x4 for now */

    VClk = (unsigned)clockIn;

    for (P = 0; P <= 6; P++) {
        Freq = VClk << P;
        if ((Freq >= 400000) && (Freq <= 1000000)) {
            for (M = 1; M <= 13; M++) {
                N = ((VClk << P) * M) / (pNv->CrystalFreqKHz << 2);
                if((N >= 5) && (N <= 255)) {
                    Freq = (((pNv->CrystalFreqKHz << 2) * N) / M) >> P;
                    if (Freq > VClk)
                        DeltaNew = Freq - VClk;
                    else
                        DeltaNew = VClk - Freq;
                    if (DeltaNew < DeltaOld) {
                        *pllOut   = (P << 16) | (N << 8) | M;
                        *clockOut = Freq;
                        DeltaOld  = DeltaNew;
                    }
                }
            }
        }
    }
}

/*
 * Calculate extended mode parameters (SVGA) and save in a 
 * mode state structure.
 */
void nv_crtc_calc_state_ext(
    xf86CrtcPtr crtc,
    int            bpp,
    int            width,
    int            hDisplaySize,
    int            height,
    int            dotClock,
    int		   flags 
)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    int pixelDepth, VClk;
    CARD32 CursorStart;
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    NVCrtcRegPtr regp;
    NVPtr pNv = NVPTR(pScrn);    
    RIVA_HW_STATE *state;

    state = &pNv->ModeReg;

    regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];

    /*
     * Save mode parameters.
     */
    state->bpp    = bpp;    /* this is not bitsPerPixel, it's 8,15,16,32 */
    state->width  = width;
    state->height = height;
    /*
     * Extended RIVA registers.
     */
    pixelDepth = (bpp + 1)/8;
    if(pNv->twoStagePLL)
        CalcVClock2Stage(dotClock, &VClk, &state->pll, &state->pllB, pNv);
    else
        CalcVClock(dotClock, &VClk, &state->pll, pNv);

    switch (pNv->Architecture)
    {
        case NV_ARCH_04:
            nv4UpdateArbitrationSettings(VClk, 
                                         pixelDepth * 8, 
                                        &(state->arbitration0),
                                        &(state->arbitration1),
                                         pNv);
            regp->CRTC[NV_VGA_CRTCX_CURCTL0] = 0x00;
            regp->CRTC[NV_VGA_CRTCX_CURCTL1] = 0xbC;
	    if (flags & V_DBLSCAN)
		regp->CRTC[NV_VGA_CRTCX_CURCTL1] |= 2;
            regp->CRTC[NV_VGA_CRTCX_CURCTL2] = 0x00000000;
            state->pllsel   = 0x10000700;
            state->config   = 0x00001114;
            state->general  = bpp == 16 ? 0x00101100 : 0x00100100;
            regp->CRTC[NV_VGA_CRTCX_REPAINT1] = hDisplaySize < 1280 ? 0x04 : 0x00;
            break;
        case NV_ARCH_10:
        case NV_ARCH_20:
        case NV_ARCH_30:
        default:
            if(((pNv->Chipset & 0xfff0) == CHIPSET_C51) ||
               ((pNv->Chipset & 0xfff0) == CHIPSET_C512))
            {
                state->arbitration0 = 128; 
                state->arbitration1 = 0x0480; 
            } else
            if(((pNv->Chipset & 0xffff) == CHIPSET_NFORCE) ||
               ((pNv->Chipset & 0xffff) == CHIPSET_NFORCE2))
            {
                nForceUpdateArbitrationSettings(VClk,
                                          pixelDepth * 8,
                                         &(state->arbitration0),
                                         &(state->arbitration1),
                                          pNv);
            } else if(pNv->Architecture < NV_ARCH_30) {
                nv10UpdateArbitrationSettings(VClk, 
                                          pixelDepth * 8, 
                                         &(state->arbitration0),
                                         &(state->arbitration1),
                                          pNv);
            } else {
                nv30UpdateArbitrationSettings(pNv,
                                         &(state->arbitration0),
                                         &(state->arbitration1));
            }

	    regp->CRTC[NV_VGA_CRTCX_FIFO0] = state->arbitration0;
	    regp->CRTC[NV_VGA_CRTCX_FIFO_LWM] = state->arbitration1 & 0xff;
	    if (pNv->Architecture >= NV_ARCH_30) {
	      regp->CRTC[NV_VGA_CRTCX_FIFO_LWM_NV30] = state->arbitration1 >> 8;
	    }

	    CursorStart = pNv->Cursor->offset - pNv->VRAMPhysical;
            regp->CRTC[NV_VGA_CRTCX_CURCTL0] = 0x80 | (CursorStart >> 17);
            regp->CRTC[NV_VGA_CRTCX_CURCTL1] = (CursorStart >> 11) << 2;
	    regp->CRTC[NV_VGA_CRTCX_CURCTL2] = CursorStart >> 24;

	    if (flags & V_DBLSCAN) 
		regp->CRTC[NV_VGA_CRTCX_CURCTL1]|= 2;

            state->pllsel   = 0x10000700;
            state->config   = nvReadFB(pNv, NV_PFB_CFG0);
            state->general  = bpp == 16 ? 0x00101100 : 0x00100100;
            regp->CRTC[NV_VGA_CRTCX_REPAINT1] = hDisplaySize < 1280 ? 0x04 : 0x00;
            break;
    }

    if(bpp != 8) /* DirectColor */
	state->general |= 0x00000030;

    regp->CRTC[NV_VGA_CRTCX_REPAINT0] = (((width / 8) * pixelDepth) & 0x700) >> 3;
    regp->CRTC[NV_VGA_CRTCX_PIXEL] = (pixelDepth > 2) ? 3 : pixelDepth;
}


static void
nv_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
     ScrnInfoPtr pScrn = crtc->scrn;
     NVPtr pNv = NVPTR(pScrn);
     NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
     unsigned char crtc1A;
     int ret;

     crtc1A = NVReadVgaCrtc(crtc, 0x1A) & ~0xC0;
     switch(mode) {
     case DPMSModeStandby:
       crtc1A |= 0x80;
       break;
     case DPMSModeSuspend:
       crtc1A |= 0x40;
       break;
     case DPMSModeOff:
       crtc1A |= 0xC0;
       break;
     case DPMSModeOn:
     default:
       break;
     }
     
     NVWriteVgaCrtc(crtc, 0x1A, crtc1A);
}

static Bool
nv_crtc_mode_fixup(xf86CrtcPtr crtc, DisplayModePtr mode,
		     DisplayModePtr adjusted_mode)
{
    return TRUE;
}

static void
nv_crtc_mode_set_vga(xf86CrtcPtr crtc, DisplayModePtr mode)
{
    ScrnInfoPtr pScrn = crtc->scrn;
   NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
   NVCrtcRegPtr regp;
   NVPtr pNv = NVPTR(pScrn);
   int depth = pScrn->depth;
   unsigned int i;

   regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];


   /*
    * compute correct Hsync & Vsync polarity 
    */
   if ((mode->Flags & (V_PHSYNC | V_NHSYNC))
       && (mode->Flags & (V_PVSYNC | V_NVSYNC)))
   {
       regp->MiscOutReg = 0x23;
       if (mode->Flags & V_NHSYNC) regp->MiscOutReg |= 0x40;
       if (mode->Flags & V_NVSYNC) regp->MiscOutReg |= 0x80;
   }
   else
   {
       int VDisplay = mode->VDisplay;
       if (mode->Flags & V_DBLSCAN)
	   VDisplay *= 2;
       if (mode->VScan > 1)
	   VDisplay *= mode->VScan;
       if      (VDisplay < 400)
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
    if (depth == 4)
        regp->Sequencer[0] = 0x02;
    else
        regp->Sequencer[0] = 0x00;
    if (mode->Flags & V_CLKDIV2) 
        regp->Sequencer[1] = 0x09;
    else
        regp->Sequencer[1] = 0x01;
    if (depth == 1)
        regp->Sequencer[2] = 1 << BIT_PLANE;
    else
        regp->Sequencer[2] = 0x0F;
    regp->Sequencer[3] = 0x00;                             /* Font select */
    if (depth < 8)
        regp->Sequencer[4] = 0x06;                             /* Misc */
    else
        regp->Sequencer[4] = 0x0E;                             /* Misc */

    /*
     * CRTC Controller
     */
    regp->CRTC[0]  = (mode->CrtcHTotal >> 3) - 5;
    regp->CRTC[1]  = (mode->CrtcHDisplay >> 3) - 1;
    regp->CRTC[2]  = (mode->CrtcHBlankStart >> 3) - 1;
    regp->CRTC[3]  = (((mode->CrtcHBlankEnd >> 3) - 1) & 0x1F) | 0x80;
    i = (((mode->CrtcHSkew << 2) + 0x10) & ~0x1F);
    if (i < 0x80)
	regp->CRTC[3] |= i;
    regp->CRTC[4]  = (mode->CrtcHSyncStart >> 3);
    regp->CRTC[5]  = ((((mode->CrtcHBlankEnd >> 3) - 1) & 0x20) << 2)
	| (((mode->CrtcHSyncEnd >> 3)) & 0x1F);
    regp->CRTC[6]  = (mode->CrtcVTotal - 2) & 0xFF;
    regp->CRTC[7]  = (((mode->CrtcVTotal - 2) & 0x100) >> 8)
	| (((mode->CrtcVDisplay - 1) & 0x100) >> 7)
	| ((mode->CrtcVSyncStart & 0x100) >> 6)
	| (((mode->CrtcVBlankStart - 1) & 0x100) >> 5)
	| 0x10
	| (((mode->CrtcVTotal - 2) & 0x200)   >> 4)
	| (((mode->CrtcVDisplay - 1) & 0x200) >> 3)
	| ((mode->CrtcVSyncStart & 0x200) >> 2);
    regp->CRTC[8]  = 0x00;
    regp->CRTC[9]  = (((mode->CrtcVBlankStart - 1) & 0x200) >> 4) | 0x40;
    if (mode->Flags & V_DBLSCAN)
	regp->CRTC[9] |= 0x80;
    if (mode->VScan >= 32)
	regp->CRTC[9] |= 0x1F;
    else if (mode->VScan > 1)
	regp->CRTC[9] |= mode->VScan - 1;
    regp->CRTC[10] = 0x00;
    regp->CRTC[11] = 0x00;
    regp->CRTC[12] = 0x00;
    regp->CRTC[13] = 0x00;
    regp->CRTC[14] = 0x00;
    regp->CRTC[15] = 0x00;
    regp->CRTC[16] = mode->CrtcVSyncStart & 0xFF;
    regp->CRTC[17] = (mode->CrtcVSyncEnd & 0x0F) | 0x20;
    regp->CRTC[18] = (mode->CrtcVDisplay - 1) & 0xFF;
    regp->CRTC[19] = pScrn->displayWidth >> 4;  /* just a guess */
    regp->CRTC[20] = 0x00;
    regp->CRTC[21] = (mode->CrtcVBlankStart - 1) & 0xFF; 
    regp->CRTC[22] = (mode->CrtcVBlankEnd - 1) & 0xFF;
    if (depth < 8)
	regp->CRTC[23] = 0xE3;
    else
	regp->CRTC[23] = 0xC3;
    regp->CRTC[24] = 0xFF;

    /*
     * Theory resumes here....
     */

    /*
     * Graphics Display Controller
     */
    regp->Graphics[0] = 0x00;
    regp->Graphics[1] = 0x00;
    regp->Graphics[2] = 0x00;
    regp->Graphics[3] = 0x00;
    if (depth == 1) {
        regp->Graphics[4] = BIT_PLANE;
        regp->Graphics[5] = 0x00;
    } else {
        regp->Graphics[4] = 0x00;
        if (depth == 4)
            regp->Graphics[5] = 0x02;
        else
            regp->Graphics[5] = 0x40;
    }
    regp->Graphics[6] = 0x05;   /* only map 64k VGA memory !!!! */
    regp->Graphics[7] = 0x0F;
    regp->Graphics[8] = 0xFF;
  
    if (depth == 1) {
        /* Initialise the Mono map according to which bit-plane gets used */

	Bool flipPixels = xf86GetFlipPixels();

        for (i=0; i<16; i++)
            if (((i & (1 << BIT_PLANE)) != 0) != flipPixels)
                regp->Attribute[i] = WHITE_VALUE;
            else
                regp->Attribute[i] = BLACK_VALUE;

    } else {
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
        if (depth == 4)
            regp->Attribute[16] = 0x81; /* wrong for the ET4000 */
        else
            regp->Attribute[16] = 0x41; /* wrong for the ET4000 */
        /* Attribute[17] (overscan) initialised in vgaHWGetHWRec() */
    }
    regp->Attribute[18] = 0x0F;
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
    NVRegPtr nvReg = &pNv->ModeReg;
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    NVFBLayout *pLayout = &pNv->CurrentLayout;
    NVCrtcRegPtr regp;
    unsigned int i;
    int horizDisplay    = (mode->CrtcHDisplay/8)   - 1;
    int horizStart      = (mode->CrtcHSyncStart/8) - 1;
    int horizEnd        = (mode->CrtcHSyncEnd/8)   - 1;
    int horizTotal      = (mode->CrtcHTotal/8)     - 5;
    int horizBlankStart = (mode->CrtcHDisplay/8)   - 1;
    int horizBlankEnd   = (mode->CrtcHTotal/8)     - 1;
    int vertDisplay     =  mode->CrtcVDisplay      - 1;
    int vertStart       =  mode->CrtcVSyncStart    - 1;
    int vertEnd         =  mode->CrtcVSyncEnd      - 1;
    int vertTotal       =  mode->CrtcVTotal        - 2;
    int vertBlankStart  =  mode->CrtcVDisplay      - 1;
    int vertBlankEnd    =  mode->CrtcVTotal        - 1;
   
    regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];    

    if(mode->Flags & V_INTERLACE) 
        vertTotal |= 1;

    if(pNv->FlatPanel == 1) {
       vertStart = vertTotal - 3;  
       vertEnd = vertTotal - 2;
       vertBlankStart = vertStart;
       horizStart = horizTotal - 5;
       horizEnd = horizTotal - 2;   
       horizBlankEnd = horizTotal + 4;
    }

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
    regp->CRTC[NV_VGA_CRTCX_MAXSCLIN]  = SetBitField(vertBlankStart,9:9,5:5)
                       | SetBit(6)
                       | ((mode->Flags & V_DBLSCAN) ? 0x80 : 0x00);
    regp->CRTC[NV_VGA_CRTCX_VSYNCS] = Set8Bits(vertStart);
    regp->CRTC[NV_VGA_CRTCX_VSYNCE] = SetBitField(vertEnd,3:0,3:0) | SetBit(5);
    regp->CRTC[NV_VGA_CRTCX_VDISPE] = Set8Bits(vertDisplay);
    regp->CRTC[NV_VGA_CRTCX_PITCHL] = ((pLayout->displayWidth/8)*(pLayout->bitsPerPixel/8));
    regp->CRTC[NV_VGA_CRTCX_VBLANKS] = Set8Bits(vertBlankStart);
    regp->CRTC[NV_VGA_CRTCX_VBLANKE] = Set8Bits(vertBlankEnd);

    regp->Attribute[0x10] = 0x01;

    if(pNv->Television)
       regp->Attribute[0x11] = 0x00;

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
    } else {
	regp->CRTC[NV_VGA_CRTCX_INTERLACE] = 0xff;  /* interlace off */
    }


    /*
     * Initialize DAC palette.
     */
    if(pLayout->bitsPerPixel != 8 )
    {
        for (i = 0; i < 256; i++)
        {
            regp->DAC[i*3]     = i;
            regp->DAC[(i*3)+1] = i;
            regp->DAC[(i*3)+2] = i;
        }
    }
    
    /*
     * Calculate the extended registers.
     */

    if(pLayout->depth < 24) 
	i = pLayout->depth;
    else i = 32;

    if(pNv->Architecture >= NV_ARCH_10)
	pNv->CURSOR = (CARD32 *)pNv->Cursor->map;

    nv_crtc_calc_state_ext(crtc,
                    i,
                    pLayout->displayWidth,
                    mode->CrtcHDisplay,
                    pScrn->virtualY,
                    mode->Clock,
                    mode->Flags);

    nvReg->scale = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_CONTROL) & 0xfff000ff;
    if(pNv->FlatPanel == 1) {
       nvReg->pixel |= (1 << 7);
       if(!pNv->fpScaler || (pNv->fpWidth <= mode->HDisplay)
                         || (pNv->fpHeight <= mode->VDisplay))
       {
           nvReg->scale |= (1 << 8) ;
       }
       nvReg->crtcSync = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_HCRTC);
       //        nvReg->crtcSync += NVDACPanelTweaks(pNv, nvReg);XXXX TODO
    }

    nvReg->vpll = nvReg->pll;
    nvReg->vpll2 = nvReg->pll;
    nvReg->vpllB = nvReg->pllB;
    nvReg->vpll2B = nvReg->pllB;

    regp->CRTC[NV_VGA_CRTCX_FIFO1] = nvReadVGA(pNv, NV_VGA_CRTCX_FIFO1) & ~(1<<5);

    if(nv_crtc->crtc) {
       nvReg->head  = nvReadCRTC(pNv, 0, NV_CRTC_HEAD_CONFIG) & ~0x00001000;
       nvReg->head2 = nvReadCRTC(pNv, 1, NV_CRTC_HEAD_CONFIG) | 0x00001000;
       nvReg->crtcOwner = 3;
       nvReg->pllsel |= 0x20000800;
       nvReg->vpll = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL);
       if(pNv->twoStagePLL) 
          nvReg->vpllB = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL_B);
    } else {
      if(pNv->twoHeads) {
	nvReg->head  =  nvReadCRTC(pNv, 0, NV_CRTC_HEAD_CONFIG) | 0x00001000;
	nvReg->head2 =  nvReadCRTC(pNv, 1, NV_CRTC_HEAD_CONFIG) & ~0x00001000;
	nvReg->crtcOwner = 0;
	nvReg->vpll2 = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL2);
	if(pNv->twoStagePLL) 
          nvReg->vpll2B = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL2_B);
      }
    }

    nvReg->cursorConfig = 0x00000100;
    if(mode->Flags & V_DBLSCAN)
       nvReg->cursorConfig |= (1 << 4);
    if(pNv->alphaCursor) {
        if((pNv->Chipset & 0x0ff0) != CHIPSET_NV11) 
           nvReg->cursorConfig |= 0x04011000;
        else
           nvReg->cursorConfig |= 0x14011000;
        nvReg->general |= (1 << 29);
    } else
       nvReg->cursorConfig |= 0x02000000;

    if(pNv->twoHeads) {
        if((pNv->Chipset & 0x0ff0) == CHIPSET_NV11) {
           nvReg->dither = nvReadCurRAMDAC(pNv, NV_RAMDAC_DITHER_NV11) & ~0x00010000;
           if(pNv->FPDither)
              nvReg->dither |= 0x00010000;
        } else {
           nvReg->dither = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_DITHER) & ~1;
           if(pNv->FPDither)
              nvReg->dither |= 1;
        } 
    }

    regp->CRTC[NV_VGA_CRTCX_FP_HTIMING] = 0;
    regp->CRTC[NV_VGA_CRTCX_FP_VTIMING] = 0;
    nvReg->displayV = mode->CrtcVDisplay;
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
		 DisplayModePtr adjusted_mode)
{
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;

    nv_crtc_mode_set_vga(crtc, mode);
    nv_crtc_mode_set_regs(crtc, mode);

    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_OWNER, nv_crtc->crtc * 0x3);
    NVCrtcLockUnlock(crtc, 0);

    NVVgaProtect(crtc, TRUE);
    nv_crtc_load_state(crtc);
    nv_crtc_load_vga_state(crtc);

    NVVgaProtect(crtc, FALSE);
    //    NVCrtcLockUnlock(crtc, 1);
}

static const xf86CrtcFuncsRec nv_crtc_funcs = {
    .dpms = nv_crtc_dpms,
    .save = NULL, /* XXX */
    .restore = NULL, /* XXX */
    .mode_fixup = nv_crtc_mode_fixup,
    .mode_set = nv_crtc_mode_set,
    .destroy = NULL, /* XXX */
};

void
nv_crtc_init(ScrnInfoPtr pScrn, int crtc_num)
{
    NVPtr pNv = NVPTR(pScrn);
    xf86CrtcPtr crtc;
    NVCrtcPrivatePtr nv_crtc;

    crtc = xf86CrtcCreate (pScrn, &nv_crtc_funcs);
    if (crtc == NULL)
	return;

    nv_crtc = xnfcalloc (sizeof (NVCrtcPrivateRec), 1);
    nv_crtc->crtc = crtc_num;

    if (crtc_num == 0)
      nv_crtc->pCRTCReg = pNv->PCIO0;
    else
      nv_crtc->pCRTCReg = pNv->PCIO1;

    
    crtc->driver_private = nv_crtc;
}

void nv_crtc_load_vga_state(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    NVPtr pNv = NVPTR(pScrn);    
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    int i, j;
    CARD32 temp;
    RIVA_HW_STATE *state;
    NVCrtcRegPtr regp;

    state = &pNv->ModeReg;
    regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];
    
    NVWriteVgaReg(crtc, VGA_MISC_OUT_W, regp->MiscOutReg);

    for (i = 1; i < 5; i++)
      NVWriteVgaSeq(crtc, i, regp->Sequencer[i]);
  
    /* Ensure CRTC registers 0-7 are unlocked by clearing bit 7 of CRTC[17] */
    NVWriteVgaCrtc(crtc, 17, regp->CRTC[17] & ~0x80);

    for (i = 0; i < 25; i++)
      NVWriteVgaCrtc(crtc, i, regp->CRTC[i]);

    for (i = 0; i < 9; i++)
      NVWriteVgaGr(crtc, i, regp->Graphics[i]);
    
    NVEnablePalette(crtc);
    for (i = 0; i < 21; i++)
      NVWriteVgaAttr(crtc, i, regp->Attribute[i]);
    NVDisablePalette(crtc);

}

void nv_crtc_load_state(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    NVPtr pNv = NVPTR(pScrn);    
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    int i, j;
    CARD32 temp;
    RIVA_HW_STATE *state;
    NVCrtcRegPtr regp;
    
    state = &pNv->ModeReg;
    regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];

    if (!pNv->IRQ)
        nvWriteMC(pNv, 0x140, 0);

    nvWriteTIMER(pNv, 0x0200, 0x00000008);
    nvWriteTIMER(pNv, 0x0210, 0x00000003);
    /*TODO: DRM handle PTIMER interrupts */
    nvWriteTIMER(pNv, 0x0140, 0x00000000);
    nvWriteTIMER(pNv, 0x0100, 0xFFFFFFFF);

    NVInitSurface(pScrn, state);
    NVInitGraphContext(pScrn,  state);

    if(pNv->Architecture >= NV_ARCH_10) {
        if(pNv->twoHeads) {
           nvWriteCRTC(pNv, 0, NV_CRTC_HEAD_CONFIG, state->head);
           nvWriteCRTC(pNv, 1, NV_CRTC_HEAD_CONFIG, state->head2);
        }
        temp = nvReadCurRAMDAC(pNv, NV_RAMDAC_0404);
        nvWriteCurRAMDAC(pNv, NV_RAMDAC_0404, temp | (1 << 25));
    
        nvWriteVIDEO(pNv, NV_PVIDEO_STOP, 1);
        nvWriteVIDEO(pNv, NV_PVIDEO_INTR_EN, 0);
        nvWriteVIDEO(pNv, NV_PVIDEO_OFFSET_BUFF(0), 0);
        nvWriteVIDEO(pNv, NV_PVIDEO_OFFSET_BUFF(1), 0);
        nvWriteVIDEO(pNv, NV_PVIDEO_LIMIT(0), pNv->VRAMPhysicalSize - 1);
        nvWriteVIDEO(pNv, NV_PVIDEO_LIMIT(1), pNv->VRAMPhysicalSize - 1);
        nvWriteMC(pNv, 0x1588, 0);

        nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_CURSOR_CONFIG, state->cursorConfig);
        nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_0830, state->displayV - 3);
        nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_0834, state->displayV - 1);
    
        if(pNv->FlatPanel) {
           if((pNv->Chipset & 0x0ff0) == CHIPSET_NV11) {
               nvWriteCurRAMDAC(pNv, NV_RAMDAC_DITHER_NV11, state->dither);
           } else 
           if(pNv->twoHeads) {
               nvWriteCurRAMDAC(pNv, NV_RAMDAC_FP_DITHER, state->dither);
           }
    
	   NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_FP_HTIMING, regp->CRTC[NV_VGA_CRTCX_FP_HTIMING]);
	   NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_FP_VTIMING, regp->CRTC[NV_VGA_CRTCX_FP_VTIMING]);
	   NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_BUFFER, 0xfa);
        }

	NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_EXTRA, regp->CRTC[NV_VGA_CRTCX_EXTRA]);
    }

    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_REPAINT0, regp->CRTC[NV_VGA_CRTCX_REPAINT0]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_REPAINT1, regp->CRTC[NV_VGA_CRTCX_REPAINT1]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_LSR, regp->CRTC[NV_VGA_CRTCX_LSR]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_PIXEL, regp->CRTC[NV_VGA_CRTCX_PIXEL]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_HEB, regp->CRTC[NV_VGA_CRTCX_HEB]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_FIFO1, regp->CRTC[NV_VGA_CRTCX_FIFO1]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_FIFO0, regp->CRTC[NV_VGA_CRTCX_FIFO0]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_FIFO_LWM, regp->CRTC[NV_VGA_CRTCX_FIFO_LWM]);
    if(pNv->Architecture >= NV_ARCH_30) {
      NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_FIFO_LWM_NV30, regp->CRTC[NV_VGA_CRTCX_FIFO_LWM] >> 8);
    }

    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_CURCTL0, regp->CRTC[NV_VGA_CRTCX_CURCTL0]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_CURCTL1, regp->CRTC[NV_VGA_CRTCX_CURCTL1]);
    if(pNv->Architecture == NV_ARCH_40) {  /* HW bug */
       volatile CARD32 curpos = nvReadCurRAMDAC(pNv, NV_RAMDAC_CURSOR_POS);
       nvWriteCurRAMDAC(pNv, NV_RAMDAC_CURSOR_POS, curpos);
    }
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_CURCTL2, regp->CRTC[NV_VGA_CRTCX_CURCTL2]);
    NVWriteVgaCrtc(crtc, NV_VGA_CRTCX_INTERLACE, regp->CRTC[NV_VGA_CRTCX_INTERLACE]);

    if(1/*!pNv->FlatPanel*/) {
       nvWriteRAMDAC0(pNv, NV_RAMDAC_PLL_SELECT, state->pllsel);
       nvWriteRAMDAC0(pNv, NV_RAMDAC_VPLL, state->vpll);
       if(pNv->twoHeads)
          nvWriteRAMDAC0(pNv, NV_RAMDAC_VPLL2, state->vpll2);
       if(pNv->twoStagePLL) {
          nvWriteRAMDAC0(pNv, NV_RAMDAC_VPLL_B, state->vpllB);
          nvWriteRAMDAC0(pNv, NV_RAMDAC_VPLL2_B, state->vpll2B);
       }
    } else {
       nvWriteCurRAMDAC(pNv, NV_RAMDAC_FP_CONTROL, state->scale);
       nvWriteCurRAMDAC(pNv, NV_RAMDAC_FP_HCRTC, state->crtcSync);
    }
    nvWriteCurRAMDAC(pNv, NV_RAMDAC_GENERAL_CONTROL, state->general);

    nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_INTR_EN_0, 0);
    nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_INTR_0, NV_CRTC_INTR_VBLANK);

    pNv->CurrentState = state;
}

void nv_unload_state_ext(xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    NVPtr pNv = NVPTR(pScrn);    
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    RIVA_HW_STATE *state;
    NVCrtcRegPtr regp;
    
    state = &pNv->ModeReg;
    regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];

    regp->CRTC[NV_VGA_CRTCX_REPAINT0] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_REPAINT0);
    regp->CRTC[NV_VGA_CRTCX_REPAINT1] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_REPAINT1);
    regp->CRTC[NV_VGA_CRTCX_LSR] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_LSR);
    regp->CRTC[NV_VGA_CRTCX_PIXEL] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_PIXEL);
    regp->CRTC[NV_VGA_CRTCX_HEB] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_HEB);
    regp->CRTC[NV_VGA_CRTCX_FIFO1] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_FIFO1);
    regp->CRTC[NV_VGA_CRTCX_FIFO0] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_FIFO0);
    regp->CRTC[NV_VGA_CRTCX_FIFO_LWM] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_FIFO_LWM);
    if(pNv->Architecture >= NV_ARCH_30) {
        regp->CRTC[NV_VGA_CRTCX_FIFO_LWM_NV30] |= (NVReadVgaCrtc(crtc, NV_VGA_CRTCX_FIFO_LWM_NV30) & 1) << 8;
    }
    regp->CRTC[NV_VGA_CRTCX_CURCTL0] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_CURCTL0);
    regp->CRTC[NV_VGA_CRTCX_CURCTL1] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_CURCTL1);
    regp->CRTC[NV_VGA_CRTCX_CURCTL2] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_CURCTL2);
    regp->CRTC[NV_VGA_CRTCX_INTERLACE] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_INTERLACE);

    state->vpll         = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL);
    if(pNv->twoHeads)
       state->vpll2     = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL2);
    if(pNv->twoStagePLL) {
        state->vpllB    = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL_B);
        state->vpll2B   = nvReadRAMDAC0(pNv, NV_RAMDAC_VPLL2_B);
    }
    state->pllsel       = nvReadRAMDAC0(pNv, NV_RAMDAC_PLL_SELECT);
    state->general      = nvReadCurRAMDAC(pNv, NV_RAMDAC_GENERAL_CONTROL);
    state->scale        = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_CONTROL);
    state->config       = nvReadFB(pNv, NV_PFB_CFG0);

    if(pNv->Architecture >= NV_ARCH_10) {
        if(pNv->twoHeads) {
           state->head     = nvReadCRTC(pNv, 0, NV_CRTC_HEAD_CONFIG);
           state->head2    = nvReadCRTC(pNv, 1, NV_CRTC_HEAD_CONFIG);
           state->crtcOwner = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_OWNER);
        }
        regp->CRTC[NV_VGA_CRTCX_EXTRA] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_EXTRA);

        state->cursorConfig = nvReadCRTC(pNv, nv_crtc->crtc, NV_CRTC_CURSOR_CONFIG);

        if((pNv->Chipset & 0x0ff0) == CHIPSET_NV11) {
           state->dither = nvReadCurRAMDAC(pNv, NV_RAMDAC_DITHER_NV11);
        } else 
        if(pNv->twoHeads) {
            state->dither = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_DITHER);
        }

        if(pNv->FlatPanel) {
           regp->CRTC[NV_VGA_CRTCX_FP_HTIMING] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_FP_HTIMING);
           regp->CRTC[NV_VGA_CRTCX_FP_VTIMING] = NVReadVgaCrtc(crtc, NV_VGA_CRTCX_FP_VTIMING);
        }
    }

    if(pNv->FlatPanel) {
       state->crtcSync = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_HCRTC);
    }
}

void
NVCrtcSetBase (xf86CrtcPtr crtc, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    NVPtr pNv = NVPTR(pScrn);    
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    NVFBLayout *pLayout = &pNv->CurrentLayout;
    CARD32 start = 0;
    
    start += ((y * pScrn->displayWidth + x) * (pLayout->bitsPerPixel/8));
    start += (pNv->FB->offset - pNv->VRAMPhysical);

    nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_START, start);

    crtc->x = x;
    crtc->y = y;
}


/**
 * In the current world order, there are lists of modes per output, which may
 * or may not include the mode that was asked to be set by XFree86's mode
 * selection.  Find the closest one, in the following preference order:
 *
 * - Equality
 * - Closer in size to the requested mode, but no larger
 * - Closer in refresh rate to the requested mode.
 */
DisplayModePtr
NVCrtcFindClosestMode(xf86CrtcPtr crtc, DisplayModePtr pMode)
{
    ScrnInfoPtr	pScrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    DisplayModePtr pBest = NULL, pScan = NULL;
    int i;

    /* Assume that there's only one output connected to the given CRTC. */
    for (i = 0; i < xf86_config->num_output; i++) 
    {
	xf86OutputPtr  output = xf86_config->output[i];
	if (output->crtc == crtc && output->probed_modes != NULL)
	{
	    pScan = output->probed_modes;
	    break;
	}
    }

    /* If the pipe doesn't have any detected modes, just let the system try to
     * spam the desired mode in.
     */
    if (pScan == NULL) {
	NVCrtcPrivatePtr  nv_crtc = crtc->driver_private;
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "No pipe mode list for pipe %d,"
		   "continuing with desired mode\n", nv_crtc->crtc);
	return pMode;
    }

    for (; pScan != NULL; pScan = pScan->next) {
	assert(pScan->VRefresh != 0.0);

	/* If there's an exact match, we're done. */
	if (xf86ModesEqual(pScan, pMode)) {
	    pBest = pMode;
	    break;
	}

	/* Reject if it's larger than the desired mode. */
	if (pScan->HDisplay > pMode->HDisplay ||
	    pScan->VDisplay > pMode->VDisplay)
	{
	    continue;
	}

	if (pBest == NULL) {
	    pBest = pScan;
	    continue;
	}

	/* Find if it's closer to the right size than the current best
	 * option.
	 */
	if ((pScan->HDisplay > pBest->HDisplay &&
	     pScan->VDisplay >= pBest->VDisplay) ||
	    (pScan->HDisplay >= pBest->HDisplay &&
	     pScan->VDisplay > pBest->VDisplay))
	{
	    pBest = pScan;
	    continue;
	}

	/* Find if it's still closer to the right refresh than the current
	 * best resolution.
	 */
	if (pScan->HDisplay == pBest->HDisplay &&
	    pScan->VDisplay == pBest->VDisplay &&
	    (fabs(pScan->VRefresh - pMode->VRefresh) <
	     fabs(pBest->VRefresh - pMode->VRefresh))) {
	    pBest = pScan;
	}
    }

    if (pBest == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "No suitable mode found to program for the pipe.\n"
		   "	continuing with desired mode %dx%d@%.1f\n",
		   pMode->HDisplay, pMode->VDisplay, pMode->VRefresh);
    } else if (!xf86ModesEqual(pBest, pMode)) {
	NVCrtcPrivatePtr  nv_crtc = crtc->driver_private;
	int		    crtc = nv_crtc->crtc;
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Choosing pipe %d's mode %dx%d@%.1f instead of xf86 "
		   "mode %dx%d@%.1f\n", crtc,
		   pBest->HDisplay, pBest->VDisplay, pBest->VRefresh,
		   pMode->HDisplay, pMode->VDisplay, pMode->VRefresh);
	pMode = pBest;
    }
    return pMode;
}

Bool
NVCrtcInUse (xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int	i;
    
    for (i = 0; i < xf86_config->num_output; i++)
	if (xf86_config->output[i]->crtc == crtc)
	    return TRUE;
    return FALSE;
}

Bool
NVCrtcSetMode(xf86CrtcPtr crtc, DisplayModePtr pMode)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;
    Bool ret = FALSE;
    DisplayModePtr adjusted_mode;

    adjusted_mode = xf86DuplicateMode(pMode);    
    crtc->enabled = NVCrtcInUse(crtc);

    if (!crtc->enabled) {
	return TRUE;
    }

    
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];

	if (output->crtc != crtc)
	    continue;

	if (!output->funcs->mode_fixup(output, pMode, adjusted_mode)) {
	    ret = FALSE;
	    goto done;
	}
    }

    if (!crtc->funcs->mode_fixup(crtc, pMode, adjusted_mode)) {
	ret = FALSE;
	goto done;
    }

        /* Disable the outputs and CRTCs before setting the mode. */
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];

	if (output->crtc != crtc)
	    continue;

	/* Disable the output as the first thing we do. */
	output->funcs->dpms(output, DPMSModeOff);
    }

    crtc->funcs->dpms(crtc, DPMSModeOff);

    /* Set up the DPLL and any output state that needs to adjust or depend
     * on the DPLL.
     */
    crtc->funcs->mode_set(crtc, pMode, adjusted_mode);
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];
	if (output->crtc == crtc)
	    output->funcs->mode_set(output, pMode, adjusted_mode);
    }

    /* Now, enable the clocks, plane, pipe, and outputs that we set up. */
    crtc->funcs->dpms(crtc, DPMSModeOn);
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];
	if (output->crtc == crtc)
	    output->funcs->dpms(output, DPMSModeOn);
    }

    crtc->curMode = *pMode;

    /* XXX free adjustedmode */
    ret = TRUE;

 done:
    return ret;
}

/**
 * This function configures the screens in clone mode on
 * all active outputs using a mode similar to the specified mode.
 */
Bool
NVSetMode(ScrnInfoPtr pScrn, DisplayModePtr pMode)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(pScrn);
    Bool ok = TRUE;
    xf86CrtcPtr crtc = config->output[config->compat_output]->crtc;

    if (crtc && crtc->enabled)
    {
	ok = NVCrtcSetMode(crtc,
			   NVCrtcFindClosestMode(crtc, pMode));

	if (!ok)
	    goto done;
	crtc->desiredMode = *pMode;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Mode bandwidth is %d Mpixel/s\n",
	       (int)(pMode->HDisplay * pMode->VDisplay *
		     pMode->VRefresh / 1000000));

    //    i830DisableUnusedFunctions(pScrn);

    //    i8/30DescribeOutputConfiguration(pScrn);

#ifdef XF86DRI
    //   I830DRISetVBlankInterrupt (pScrn, TRUE);
#endif
done:
    return ok;
}


void
NVDumpAllRegisters(ScrnInfoPtr pScrn)
{
  

 

}
