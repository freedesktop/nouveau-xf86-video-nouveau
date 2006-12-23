/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_type.h,v 1.51 2005/04/16 23:57:26 mvojkovi Exp $ */

#ifndef __NV_STRUCT_H__
#define __NV_STRUCT_H__

#include "colormapst.h"
#include "vgaHW.h"
#include "xaa.h"
#include "xf86Cursor.h"
#include "xf86int10.h"
#include "exa.h"
#ifdef XF86DRI
#define _XF86DRI_SERVER_
#include "xf86drm.h"
#include "dri.h"
#include <stdint.h>
#include "nouveau_drm.h"
#include "nv_xf86Crtc.h"
#else
#error "This driver requires a DRI-enabled X server"
#endif

#define NV_ARCH_03  0x03
#define NV_ARCH_04  0x04
#define NV_ARCH_10  0x10
#define NV_ARCH_20  0x20
#define NV_ARCH_30  0x30
#define NV_ARCH_40  0x40

#define CHIPSET_NV03     0x0010
#define CHIPSET_NV04     0x0020
#define CHIPSET_NV10     0x0100
#define CHIPSET_NV11     0x0110
#define CHIPSET_NV15     0x0150
#define CHIPSET_NV17     0x0170
#define CHIPSET_NV18     0x0180
#define CHIPSET_NFORCE   0x01A0
#define CHIPSET_NFORCE2  0x01F0
#define CHIPSET_NV20     0x0200
#define CHIPSET_NV25     0x0250
#define CHIPSET_NV28     0x0280
#define CHIPSET_NV30     0x0300
#define CHIPSET_NV31     0x0310
#define CHIPSET_NV34     0x0320
#define CHIPSET_NV35     0x0330
#define CHIPSET_NV36     0x0340
#define CHIPSET_NV40     0x0040
#define CHIPSET_NV41     0x00C0
#define CHIPSET_NV43     0x0140
#define CHIPSET_NV44     0x0160
#define CHIPSET_NV44A    0x0220
#define CHIPSET_NV45     0x0210
#define CHIPSET_MISC_BRIDGED  0x00F0
#define CHIPSET_G70      0x0090
#define CHIPSET_G71      0x0290
#define CHIPSET_G72      0x01D0
#define CHIPSET_G73      0x0390
// integrated GeForces (6100, 6150)
#define CHIPSET_C51      0x0240
// variant of C51, seems based on a G70 design
#define CHIPSET_C512     0x03D0
#define CHIPSET_G73_BRIDGED 0x02E0


#define BITMASK(t,b) (((unsigned)(1U << (((t)-(b)+1)))-1)  << (b))
#define MASKEXPAND(mask) BITMASK(1?mask,0?mask)
#define SetBF(mask,value) ((value) << (0?mask))
#define GetBF(var,mask) (((unsigned)((var) & MASKEXPAND(mask))) >> (0?mask) )
#define SetBitField(value,from,to) SetBF(to, GetBF(value,from))
#define SetBit(n) (1<<(n))
#define Set8Bits(value) ((value)&0xff)

typedef enum
{
    OUTPUT_NONE,
    OUTPUT_VGA,
    OUTPUT_DVI,
    OUTPUT_LVDS,
    OUTPUT_STV,
    OUTPUT_CTV,
} NVOutputType;

typedef enum
{
    MT_UNKNOWN = -1,
    MT_NONE    = 0,
    MT_CRT     = 1,
    MT_LCD     = 2,
    MT_DFP     = 3,
    MT_CTV     = 4,
    MT_STV     = 5
} NVMonitorType;

typedef struct {
    int bitsPerPixel;
    int depth;
    int displayWidth;
    rgb weight;
    DisplayModePtr mode;
} NVFBLayout;

typedef struct _nv_crtc_reg 
{
    unsigned char MiscOutReg;     /* */
    CARD8 CRTC[90];
    CARD8 Sequencer[5];
    CARD8 Graphics[9];
    CARD8 Attribute[21];
    unsigned char DAC[768];       /* Internal Colorlookuptable */
} NVCrtcRegRec, *NVCrtcRegPtr;

typedef struct _riva_hw_state
{
    CARD32 bpp;
    CARD32 width;
    CARD32 height;
    CARD32 interlace;
    CARD32 repaint0;
    CARD32 repaint1;
    CARD32 screen;
    CARD32 scale;
    CARD32 dither;
    CARD32 extra;
    CARD32 fifo;
    CARD32 pixel;
    CARD32 horiz;
    CARD32 arbitration0;
    CARD32 arbitration1;
    CARD32 pll;
    CARD32 pllB;
    CARD32 vpll;
    CARD32 vpll2;
    CARD32 vpllB;
    CARD32 vpll2B;
    CARD32 pllsel;
    CARD32 general;
    CARD32 crtcOwner;
    CARD32 head;
    CARD32 head2;
    CARD32 config;
    CARD32 cursorConfig;
    CARD32 cursor0;
    CARD32 cursor1;
    CARD32 cursor2;
    CARD32 timingH;
    CARD32 timingV;
    CARD32 displayV;
    CARD32 crtcSync;
    NVCrtcRegRec crtc_reg[2];
} RIVA_HW_STATE, *NVRegPtr;

typedef struct {
	int type;
	uint64_t size;
	uint64_t offset;
	void *map;
} NVAllocRec;

typedef struct _NVCrtcPrivateRec {
	int crtc;
        volatile CARD8 *pCRTCReg;
} NVCrtcPrivateRec, *NVCrtcPrivatePtr;

#define NVCrtcPrivate(c) ((NVCrtcPrivatePtr)(c)->driver_private)

typedef struct _NVOutputPrivateRec {
        I2CBusPtr		    pDDCBus;
        NVOutputType type;
        NVMonitorType mon_type;
} NVOutputPrivateRec, *NVOutputPrivatePtr;

#define NVOutputPrivate(o) ((NVOutputPrivatePtr (o)->driver_private)

typedef struct _NVRec *NVPtr;
typedef struct _NVRec {
    RIVA_HW_STATE       SavedReg;
    RIVA_HW_STATE       ModeReg;
    RIVA_HW_STATE       *CurrentState;
    CARD32              Architecture;
    EntityInfoPtr       pEnt;
    pciVideoPtr         PciInfo;
    PCITAG              PciTag;
    int                 Chipset;
    int                 ChipRev;
    Bool                Primary;
    CARD32              IOAddress;
	unsigned long       VRAMPhysical;
	unsigned long		VRAMPhysicalSize;
	NVAllocRec *        FB;
	NVAllocRec *        Cursor;
	NVAllocRec *        ScratchBuffer;
	NVAllocRec *        AGPScratch;
    Bool                NoAccel;
    Bool                HWCursor;
    Bool                FpScale;
    Bool                ShadowFB;
    unsigned char *     ShadowPtr;
    int                 ShadowPitch;
    CARD32              MinVClockFreqKHz;
    CARD32              MaxVClockFreqKHz;
    CARD32              CrystalFreqKHz;
    CARD32              RamAmountKBytes;
    int drm_fd;

    volatile CARD32 *REGS;
    volatile CARD32 *PCRTC0;
    volatile CARD32 *PCRTC1;

    volatile CARD32 *PRAMDAC0;
    volatile CARD32 *PRAMDAC1;
    volatile CARD32 *PFB;
    volatile CARD32 *PFIFO;
    volatile CARD32 *PGRAPH;
    volatile CARD32 *PEXTDEV;
    volatile CARD32 *PTIMER;
    volatile CARD32 *PVIDEO;
    volatile CARD32 *PMC;
    volatile CARD32 *PRAMIN;
    volatile CARD32 *FIFO;
    volatile CARD32 *CURSOR;
    volatile CARD8 *PCIO0;
    volatile CARD8 *PCIO1;
    volatile CARD8 *PVIO;
    volatile CARD8 *PDIO0;
    volatile CARD8 *PDIO1;
    volatile CARD8 *PROM;


    volatile CARD32 *RAMHT;
    CARD32 pramin_free;

    uint8_t cur_head;
    XAAInfoRecPtr       AccelInfoRec;
    ExaDriverPtr	EXADriverPtr;
    Bool                useEXA;
    xf86CursorInfoPtr   CursorInfoRec;
    void		(*PointerMoved)(int index, int x, int y);
    ScreenBlockHandlerProcPtr BlockHandler;
    CloseScreenProcPtr  CloseScreen;
    int			Rotate;
    NVFBLayout		CurrentLayout;
    /* Cursor */
    CARD32              curFg, curBg;
    CARD32              curImage[256];
    /* I2C / DDC */
    int ddc2;
    I2CBusPtr           I2C;
    xf86Int10InfoPtr    pInt;
    void		(*VideoTimerCallback)(ScrnInfoPtr, Time);
    void		(*DMAKickoffCallback)(NVPtr pNv);
    XF86VideoAdaptorPtr	overlayAdaptor;
    XF86VideoAdaptorPtr	blitAdaptor;
    int			videoKey;
    int			FlatPanel;
    Bool                FPDither;
    Bool                Television;
    int			CRTCnumber;
	int         vtOWNER;
    OptionInfoPtr	Options;
    Bool                alphaCursor;
    unsigned char       DDCBase;
    Bool                twoHeads;
    Bool                twoStagePLL;
    Bool                fpScaler;
    int                 fpWidth;
    int                 fpHeight;
    CARD32              fpSyncs;
    Bool                usePanelTweak;
    int                 PanelTweak;
    Bool                LVDS;

    int                 IRQ;
    Bool                LockedUp;

    NVAllocRec *        Notifier0;
    drm_nouveau_fifo_alloc_t fifo;
    CARD32              dmaPut;
    CARD32              dmaCurrent;
    CARD32              dmaFree;
    CARD32              dmaMax;
    CARD32              *dmaBase;

    Bool                use3D;
    void                (*Reset3D)(NVPtr pNv);
    void		(*InitEXA3D)(NVPtr pNv);

    CARD32              currentRop;
    int                 M2MFDirection;

    Bool                WaitVSyncPossible;
    Bool                BlendingPossible;
    Bool                RandRRotation;
    DRIInfoPtr          pDRIInfo;
    drmVersionPtr       pLibDRMVersion;
    drmVersionPtr       pKernelDRMVersion;

  CreateScreenResourcesProcPtr    CreateScreenResources;
} NVRec;

#define NVPTR(p) ((NVPtr)((p)->driverPrivate))

#define nvReadRAMDAC0(pNv, reg) nvReadRAMDAC(pNv, 0, reg)
#define nvWriteRAMDAC0(pNv, reg, val) nvWriteRAMDAC(pNv, 0, reg, val)

#define nvReadCurRAMDAC(pNv, reg) nvReadRAMDAC(pNv, pNv->cur_head, reg)
#define nvWriteCurRAMDAC(pNv, reg, val) nvWriteRAMDAC(pNv, pNv->cur_head, reg, val)

#define nvReadCRTC0(pNv, reg) nvReadCRTC(pNv, 0, reg)
#define nvWriteCRTC0(pNv, reg, val) nvWriteCRTC(pNv, 0, reg, val)

#define nvReadCurCRTC(pNv, reg) nvReadCRTC(pNv, pNv->cur_head, reg)
#define nvWriteCurCRTC(pNv, reg, val) nvWriteCRTC(pNv, pNv->cur_head, reg, val)

#define nvReadFB(pNv, fb_reg) MMIO_IN32(pNv->PFB, fb_reg)
#define nvWriteFB(pNv, fb_reg, val) MMIO_OUT32(pNv->PFB, fb_reg, val)

#define nvReadGRAPH(pNv, reg) MMIO_IN32(pNv->PGRAPH, reg)
#define nvWriteGRAPH(pNv, reg, val) MMIO_OUT32(pNv->PGRAPH, reg, val)

#define nvReadMC(pNv, reg) MMIO_IN32(pNv->PMC, reg)
#define nvWriteMC(pNv, reg, val) MMIO_OUT32(pNv->PMC, reg, val)

#define nvReadEXTDEV(pNv, reg) MMIO_IN32(pNv->PEXTDEV, reg)
#define nvWriteEXTDEV(pNv, reg, val) MMIO_OUT32(pNv->PEXTDEV, reg, val)

#define nvReadTIMER(pNv, reg) MMIO_IN32(pNv->PTIMER, reg)
#define nvWriteTIMER(pNv, reg, val) MMIO_OUT32(pNv->PTIMER, reg, val)

#define nvReadVIDEO(pNv, reg) MMIO_IN32(pNv->PVIDEO, reg)
#define nvWriteVIDEO(pNv, reg, val) MMIO_OUT32(pNv->PVIDEO, reg, val)

#endif /* __NV_STRUCT_H__ */
