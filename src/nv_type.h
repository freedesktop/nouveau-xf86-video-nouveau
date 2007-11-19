/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_type.h,v 1.51 2005/04/16 23:57:26 mvojkovi Exp $ */

#ifndef __NV_STRUCT_H__
#define __NV_STRUCT_H__

#include "colormapst.h"
#include "vgaHW.h"
#include "xf86Cursor.h"
#include "xf86int10.h"
#include "exa.h"
#ifdef XF86DRI
#define _XF86DRI_SERVER_
#include "xf86drm.h"
#include "dri.h"
#include <stdint.h>
#include "nouveau_drm.h"
#include "xf86Crtc.h"
#else
#error "This driver requires a DRI-enabled X server"
#endif

#include "nv50_type.h"
#include "nv_pcicompat.h"

#define NV_ARCH_03  0x03
#define NV_ARCH_04  0x04
#define NV_ARCH_10  0x10
#define NV_ARCH_20  0x20
#define NV_ARCH_30  0x30
#define NV_ARCH_40  0x40
#define NV_ARCH_50  0x50

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
#define CHIPSET_NV50     0x0190
#define CHIPSET_NV84     0x0400
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


#define MAX_NUM_DCB_ENTRIES 16

typedef enum /* matches DCB types */
{
    OUTPUT_NONE = 4,
    OUTPUT_ANALOG = 0,
    OUTPUT_TMDS = 2,
    OUTPUT_LVDS = 3,
    OUTPUT_TV = 1,
} NVOutputType;

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
	CARD32 cursorConfig;
	CARD32 crtcOwner;
	CARD32 gpio;
	CARD32 unk830;
	CARD32 unk834;
	CARD32 unk850;
	CARD32 unk81c;
	CARD32 head;
} NVCrtcRegRec, *NVCrtcRegPtr;

typedef struct _nv_output_reg
{
	CARD32 fp_control;
	CARD32 crtcSync;
	CARD32 dither;
	CARD32 general;
	CARD32 test_control;
	CARD32 unk_670;
	CARD32 unk_900;
	CARD32 bpp;
	CARD32 nv10_cursync;
	CARD32 output;
	CARD32 debug_0;
	CARD32 debug_1;
	CARD32 debug_2;
	CARD32 sel_clk;
	CARD32 unk_a20;
	CARD32 unk_a24;
	CARD32 unk_a34;
	CARD32 fp_horiz_regs[7];
	CARD32 fp_vert_regs[7];
	CARD32 fp_hvalid_start;
	CARD32 fp_hvalid_end;
	CARD32 fp_vvalid_start;
	CARD32 fp_vvalid_end;
	CARD8 TMDS[128];
} NVOutputRegRec, *NVOutputRegPtr;

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
	CARD32 sel_clk;
	Bool crosswired;
	Bool db1_ratio[2];
	/* These vpll values are only for nv4x hardware */
	uint32_t vpll1_a;
	uint32_t vpll1_b;
	uint32_t vpll2_a;
	uint32_t vpll2_b;
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
    NVOutputRegRec dac_reg[2];
} RIVA_HW_STATE, *NVRegPtr;

typedef struct _nv50_crtc_reg
{
	
} NV50CrtcRegRec, *NV50CrtcRegPtr;

typedef struct _nv50_hw_state
{
	NV50CrtcRegRec crtc_reg[2];
} NV50_HW_STATE, *NV50RegPtr;

typedef enum {
	RAMDAC_0 = (1 << 0),
	RAMDAC_1 = (1 << 1)
} ValidRamdac;

typedef struct _NVOutputPrivateRec {
	int ramdac;
	Bool ramdac_assigned;
	uint8_t valid_ramdac;
        I2CBusPtr		    pDDCBus;
        NVOutputType type;
        CARD32 fpSyncs;
        CARD32 fpWidth;
        CARD32 fpHeight;
	DisplayModePtr native_mode;
        Bool fpdither;
} NVOutputPrivateRec, *NVOutputPrivatePtr;

typedef struct _MiscStartupInfo {
	CARD8 crtc_0_reg_52;
	CARD32 ramdac_0_reg_580;
	CARD32 ramdac_0_pllsel;
	CARD32 reg_c040;
	CARD32 sel_clk;
	Bool prefer_db1;
} MiscStartupInfo;

typedef enum {
	OUTPUT_0_SLAVED = (1 << 0),
	OUTPUT_1_SLAVED = (1 << 1),
	OUTPUT_0_LVDS = (1 << 2),
	OUTPUT_1_LVDS = (1 << 3),
	OUTPUT_0_CROSSWIRED_TMDS = (1 << 4),
	OUTPUT_1_CROSSWIRED_TMDS = (1 << 5)
} OutputInfo;

struct dcb_entry {
	uint8_t type;
	uint8_t i2c_index;
	uint8_t head;
	uint8_t bus;
	uint8_t or;
};

#define NVOutputPrivate(o) ((NVOutputPrivatePtr (o)->driver_private)

typedef struct _NVRec *NVPtr;
typedef struct _NVRec {
    RIVA_HW_STATE       SavedReg;
    RIVA_HW_STATE       ModeReg;
    RIVA_HW_STATE       *CurrentState;
	NV50_HW_STATE	NV50SavedReg;
	NV50_HW_STATE	NV50ModeReg;
    CARD32              Architecture;
    EntityInfoPtr       pEnt;
#ifndef XSERVER_LIBPCIACCESS
	pciVideoPtr	PciInfo;
	PCITAG		PciTag;
#else
	struct pci_device *PciInfo;
#endif /* XSERVER_LIBPCIACCESS */
    int                 Chipset;
    int                 NVArch;
    Bool                Primary;
    CARD32              IOAddress;
    Bool cursorOn;

    /* VRAM physical address */
    unsigned long	VRAMPhysical;
    /* Size of VRAM BAR */
    unsigned long	VRAMPhysicalSize;
    /* Accesible VRAM size (by the GPU) */
    unsigned long	VRAMSize;
    /* Accessible AGP size */
    unsigned long	AGPSize;

    /* Various pinned memory regions */
    struct nouveau_bo * FB;
    struct nouveau_bo * Cursor;
    struct nouveau_bo * CLUT;	/* NV50 only */
    struct nouveau_bo * GART;

    uint32_t *		VBIOS;
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

    volatile CARD32 *REGS;
    volatile CARD32 *PCRTC0;
    volatile CARD32 *PCRTC1;

	volatile CARD32 *NV50_PCRTC;

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
    volatile CARD32 *CURSOR;
    volatile CARD8 *PCIO0;
    volatile CARD8 *PCIO1;
    volatile CARD8 *PVIO0;
    volatile CARD8 *PVIO1;
    volatile CARD8 *PDIO0;
    volatile CARD8 *PDIO1;
    volatile CARD8 *PROM;


    volatile CARD32 *RAMHT;
    CARD32 pramin_free;

    unsigned int SaveGeneration;
    uint8_t cur_head;
    ExaDriverPtr	EXADriverPtr;
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
    xf86Int10InfoPtr    pInt10;
    I2CBusPtr           I2C;
  void		(*VideoTimerCallback)(ScrnInfoPtr, Time);
    XF86VideoAdaptorPtr	overlayAdaptor;
    XF86VideoAdaptorPtr	blitAdaptor;
    int			videoKey;
    int			FlatPanel;
    Bool                FPDither;
    int                 Mobile;
    Bool                Television;
	int         vtOWNER;
	Bool		crtc_active[2];
	Bool		ramdac_active[2];
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

    Bool                LockedUp;

    CARD32              currentRop;

    Bool                WaitVSyncPossible;
    Bool                BlendingPossible;
    Bool                RandRRotation;
    DRIInfoPtr          pDRIInfo;
    drmVersionPtr       pLibDRMVersion;
    drmVersionPtr       pKernelDRMVersion;

    Bool randr12_enable;
    CreateScreenResourcesProcPtr    CreateScreenResources;

    I2CBusPtr           pI2CBus[MAX_NUM_DCB_ENTRIES];

	int vga_count;
	int dvi_d_count;
	int dvi_a_count;
	int lvds_count;

	struct {
		int entries;
		struct dcb_entry entry[MAX_NUM_DCB_ENTRIES];
		unsigned char i2c_read[MAX_NUM_DCB_ENTRIES];
		unsigned char i2c_write[MAX_NUM_DCB_ENTRIES];
	} dcb_table;

	uint32_t output_info;
	MiscStartupInfo misc_info;

	DisplayModePtr fp_native_mode;

	Bool sel_clk_override;

	struct {
		ORNum dac;
		ORNum sor;
	} i2cMap[4];
	struct {
		Bool  present;
		ORNum or;
	} lvds;

	/* DRM interface */
	struct nouveau_device *dev;

	/* GPU context */
	struct nouveau_channel *chan;
	struct nouveau_notifier *notify0;
	struct nouveau_grobj *NvNull;
	struct nouveau_grobj *NvContextSurfaces;
	struct nouveau_grobj *NvContextBeta1;
	struct nouveau_grobj *NvContextBeta4;
	struct nouveau_grobj *NvImagePattern;
	struct nouveau_grobj *NvRop;
	struct nouveau_grobj *NvRectangle;
	struct nouveau_grobj *NvImageBlit;
	struct nouveau_grobj *NvScaledImage;
	struct nouveau_grobj *NvClipRectangle;
	struct nouveau_grobj *NvMemFormat;
	struct nouveau_grobj *NvImageFromCpu;
	struct nouveau_grobj *Nv2D;
	struct nouveau_grobj *Nv3D;

} NVRec;

typedef struct _NVCrtcPrivateRec {
	int crtc;
	int head;
	Bool paletteEnabled;
} NVCrtcPrivateRec, *NVCrtcPrivatePtr;

typedef struct _NV50CrtcPrivRec {
	int head;
	int pclk; /* Target pixel clock in kHz */
	Bool cursorVisible;
	Bool skipModeFixup;
	Bool dither;
} NV50CrtcPrivRec, *NV50CrtcPrivPtr;

#define NVCrtcPrivate(c) ((NVCrtcPrivatePtr)(c)->driver_private)

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
