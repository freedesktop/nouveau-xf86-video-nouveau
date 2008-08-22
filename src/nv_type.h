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
#include <stdbool.h>
#include <stdint.h>
#include "nouveau_drm.h"
#include "xf86Crtc.h"
#else
#error "This driver requires a DRI-enabled X server"
#endif

#include "nv_pcicompat.h"

#include "nouveau_local.h"

#include "nouveau_crtc.h"
#include "nouveau_connector.h"
#include "nouveau_output.h"

#include "drmmode_display.h"

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

#define LOC_ON_CHIP 0

struct dcb_entry {
	int index;
	uint8_t type;
	uint8_t i2c_index;
	uint8_t heads;
	uint8_t bus;
	uint8_t location;
	uint8_t or;
	bool duallink_possible;
	union {
		struct {
			bool use_straps_for_mode;
			bool use_power_scripts;
		} lvdsconf;
	};
};

typedef enum
{/* matches DCB types */
	OUTPUT_NONE = 4,
	OUTPUT_ANALOG = 0,
	OUTPUT_TMDS = 2,
	OUTPUT_LVDS = 3,
	OUTPUT_TV = 1,
} NVOutputType;

/* NV50 */
typedef enum Head {
	HEAD0 = 0,
	HEAD1
} Head;

/* NV50 */
typedef enum ORNum {
	DAC0 = 0,
	DAC1 = 1,
	DAC2 = 2,
	SOR0 = 0,
	SOR1 = 1
} ORNum;

enum scaling_modes {
	SCALE_PANEL,
	SCALE_FULLSCREEN,
	SCALE_ASPECT,
	SCALE_NOSCALE,
	SCALE_INVALID
};

typedef struct _nv_crtc_reg 
{
	unsigned char MiscOutReg;     /* */
	uint8_t CRTC[0xff];
	uint8_t CR58[0x10];
	uint8_t Sequencer[5];
	uint8_t Graphics[9];
	uint8_t Attribute[21];
	unsigned char DAC[768];       /* Internal Colorlookuptable */
	uint32_t cursorConfig;
	uint32_t crtcOwner;
	uint32_t gpio;
	uint32_t gpio_ext;
	uint32_t unk830;
	uint32_t unk834;
	uint32_t unk850;
	uint32_t head;
	uint32_t config;
	uint32_t fb_start;

	/* These are former output regs, but are believed to be crtc related */
	uint32_t general;
	uint32_t debug_0;
	uint32_t debug_1;
	uint32_t debug_2;
	uint32_t unk_a20;
	uint32_t unk_a24;
	uint32_t unk_a34;
	uint32_t dither_regs[6];
	uint32_t fp_horiz_regs[7];
	uint32_t fp_vert_regs[7];
	uint32_t nv10_cursync;
	uint32_t fp_control;
	uint32_t dither;
	uint32_t vpll_a;
	uint32_t vpll_b;
} NVCrtcRegRec, *NVCrtcRegPtr;

typedef struct _nv_output_reg
{
	uint32_t output;
	int head;
} NVOutputRegRec, *NVOutputRegPtr;

typedef struct _riva_hw_state
{
	uint32_t bpp;
	uint32_t width;
	uint32_t height;
	uint32_t interlace;
	uint32_t repaint0;
	uint32_t repaint1;
	uint32_t screen;
	uint32_t scale;
	uint32_t dither;
	uint32_t extra;
	uint32_t fifo;
	uint32_t pixel;
	uint32_t horiz;
	uint32_t arbitration0;
	uint32_t arbitration1;
	uint32_t pll;
	uint32_t pllB;
	uint32_t vpll;
	uint32_t vpll2;
	uint32_t vpllB;
	uint32_t vpll2B;
	uint32_t pllsel;
	uint32_t sel_clk;
	uint32_t reg580;
	uint32_t general;
	uint32_t crtcOwner;
	uint32_t head;
	uint32_t head2;
	uint32_t cursorConfig;
	uint32_t cursor0;
	uint32_t cursor1;
	uint32_t cursor2;
	uint32_t timingH;
	uint32_t timingV;
	uint32_t displayV;
	uint32_t crtcSync;

	NVCrtcRegRec crtc_reg[2];
} RIVA_HW_STATE, *NVRegPtr;

typedef struct _NVCrtcPrivateRec {
	int head;
	uint8_t last_dpms;
	struct nouveau_bo *shadow;
	int fp_users;
} NVCrtcPrivateRec, *NVCrtcPrivatePtr;

typedef enum {
	OUTPUT_A = (1 << 0),
	OUTPUT_B = (1 << 1),
	OUTPUT_C = (1 << 2)
} ValidOutputResource;

typedef struct _NVOutputPrivateRec {
	uint8_t last_dpms;
	I2CBusPtr pDDCBus;
	struct dcb_entry *dcb;
	DisplayModePtr native_mode;
	uint8_t scaling_mode;
	bool dithering;
	NVOutputRegRec restore;
} NVOutputPrivateRec, *NVOutputPrivatePtr;

/* changing these requires matching changes to reg tables in nv_get_clock */
#define MAX_PLL_TYPES	4
enum pll_types {
	NVPLL,
	MPLL,
	VPLL1,
	VPLL2
};

struct pll_lims {
	struct {
		int minfreq;
		int maxfreq;
		int min_inputfreq;
		int max_inputfreq;

		uint8_t min_m;
		uint8_t max_m;
		uint8_t min_n;
		uint8_t max_n;
	} vco1, vco2;

	uint8_t unk1c;
	uint8_t max_log2p_bias;
	uint8_t log2p_bias;
	int refclk;
};

typedef struct {
	uint8_t *data;
	unsigned int length;
	bool execute;

	uint8_t major_version, chip_version;
	uint8_t feature_byte;

	uint32_t fmaxvco, fminvco;

	uint32_t dactestval;

	uint16_t init_script_tbls_ptr;
	uint16_t extra_init_script_tbl_ptr;
	uint16_t macro_index_tbl_ptr;
	uint16_t macro_tbl_ptr;
	uint16_t condition_tbl_ptr;
	uint16_t io_condition_tbl_ptr;
	uint16_t io_flag_condition_tbl_ptr;
	uint16_t init_function_tbl_ptr;

	uint16_t pll_limit_tbl_ptr;
	uint16_t ram_restrict_tbl_ptr;

	uint8_t digital_min_front_porch;

	struct {
		DisplayModePtr native_mode;
		uint8_t *edid;
		uint16_t lvdsmanufacturerpointer;
		uint16_t fpxlatemanufacturertableptr;
		uint16_t xlated_entry;
		bool power_off_for_reset;
		bool reset_after_pclk_change;
		bool dual_link;
		bool link_c_increment;
		bool if_is_24bit;
		bool BITbit1;
		int duallink_transition_clk;
		/* lower nibble stores PEXTDEV_BOOT_0 strap
		 * upper nibble stores xlated display strap */
		uint8_t strapping;
	} fp;

	struct {
		uint16_t output0_script_ptr;
		uint16_t output1_script_ptr;
	} tmds;

	struct {
		uint16_t mem_init_tbl_ptr;
		uint16_t sdr_seq_tbl_ptr;
		uint16_t ddr_seq_tbl_ptr;

		struct {
			uint8_t crt, tv, panel;
		} i2c_indices;
	} legacy;
} bios_t;

enum LVDS_script {
	/* Order *does* matter here */
	LVDS_INIT = 1,
	LVDS_RESET,
	LVDS_BACKLIGHT_ON,
	LVDS_BACKLIGHT_OFF,
	LVDS_PANEL_ON,
	LVDS_PANEL_OFF
};

typedef struct _NVRec *NVPtr;
typedef struct _NVRec {
    RIVA_HW_STATE       SavedReg;
    RIVA_HW_STATE       ModeReg;
    uint32_t saved_vga_font[4][16384];
    uint32_t              Architecture;
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
    void *              FBMap;
    //struct nouveau_bo * FB_old; /* for KMS */
    struct nouveau_bo * shadow[2]; /* for easy acces by exa */
    struct nouveau_bo * Cursor;
    struct nouveau_bo * Cursor2;
    struct nouveau_bo * CLUT0;	/* NV50 only */
    struct nouveau_bo * CLUT1;	/* NV50 only */
    struct nouveau_bo * GART;

    bios_t		VBIOS;
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
    volatile CARD32 *FB_BAR;
    volatile CARD32 *PGRAPH;
    volatile CARD32 *PRAMIN;
    volatile CARD32 *CURSOR;
    volatile CARD8 *PCIO0;
    volatile CARD8 *PCIO1;
    volatile CARD8 *PVIO0;
    volatile CARD8 *PVIO1;
    volatile CARD8 *PDIO0;
    volatile CARD8 *PDIO1;

    unsigned int SaveGeneration;
    uint8_t cur_head;
    ExaDriverPtr	EXADriverPtr;
    xf86CursorInfoPtr   CursorInfoRec;
    ScreenBlockHandlerProcPtr BlockHandler;
    CloseScreenProcPtr  CloseScreen;
    /* Cursor */
    CARD32              curFg, curBg;
    CARD32              curImage[256];
    /* I2C / DDC */
    xf86Int10InfoPtr    pInt10;
    unsigned            Int10Mode;
    I2CBusPtr           I2C;
  void		(*VideoTimerCallback)(ScrnInfoPtr, Time);
    XF86VideoAdaptorPtr	overlayAdaptor;
    XF86VideoAdaptorPtr	blitAdaptor;
    XF86VideoAdaptorPtr	textureAdaptor[2];
    int			videoKey;
    int			FlatPanel;
    Bool                FPDither;
    int                 Mobile;
    Bool                Television;
	int         vtOWNER;
	Bool		crtc_active[2];
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
    DRIInfoPtr          pDRIInfo;
    drmVersionPtr       pLibDRMVersion;
    drmVersionPtr       pKernelDRMVersion;

	Bool randr12_enable;
	Bool kms_enable;

	I2CBusPtr           pI2CBus[MAX_NUM_DCB_ENTRIES];

#ifdef XF86DRM_MODE
	void *drmmode; /* for KMS */
#endif

	struct {
		int entries;
		struct dcb_entry entry[MAX_NUM_DCB_ENTRIES];
		unsigned char i2c_read[MAX_NUM_DCB_ENTRIES];
		unsigned char i2c_write[MAX_NUM_DCB_ENTRIES];
	} dcb_table;

	nouveauCrtcPtr crtc[2];
	nouveauOutputPtr output; /* this a linked list. */
	/* Assume a connector can exist for each i2c bus. */
	nouveauConnectorPtr connector[MAX_NUM_DCB_ENTRIES];

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
	struct nouveau_bo *tesla_scratch;

	struct nouveau_fence *exa_sync;
} NVRec;

#define NVPTR(p) ((NVPtr)((p)->driverPrivate))

#define NVShowHideCursor(pScrn, show) do {							\
	NVPtr pNv = NVPTR(pScrn);										\
	nv_crtc_show_hide_cursor(pScrn, pNv->cur_head, show);				\
} while(0)

#define NVLockUnlock(pScrn, lock) NVLockVgaCrtc(NVPTR(pScrn), NVPTR(pScrn)->cur_head, lock)

#define nvReadCurVGA(pNv, reg) NVReadVgaCrtc(pNv, pNv->cur_head, reg)
#define nvWriteCurVGA(pNv, reg, val) NVWriteVgaCrtc(pNv, pNv->cur_head, reg, val)

#define nvReadCurRAMDAC(pNv, reg) NVReadRAMDAC(pNv, pNv->cur_head, reg)
#define nvWriteCurRAMDAC(pNv, reg, val) NVWriteRAMDAC(pNv, pNv->cur_head, reg, val)

#define nvReadCurCRTC(pNv, reg) NVReadCRTC(pNv, pNv->cur_head, reg)
#define nvWriteCurCRTC(pNv, reg, val) NVWriteCRTC(pNv, pNv->cur_head, reg, val)

#define nvReadFB(pNv, reg) DDXMMIOW("nvReadFB: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteFB(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteFB: reg %08x val %08x\n", reg, val))

#define nvReadGRAPH(pNv, reg) DDXMMIOW("nvReadGRAPH: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteGRAPH(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteGRAPH: reg %08x val %08x\n", reg, val))

#define nvReadMC(pNv, reg) DDXMMIOW("nvReadMC: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteMC(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteMC: reg %08x val %08x\n", reg, val))

#define nvReadME(pNv, reg) DDXMMIOW("nvReadME: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteME(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteME: reg %08x val %08x\n", reg, val))

#define nvReadEXTDEV(pNv, reg) DDXMMIOW("nvReadEXTDEV: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteEXTDEV(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteEXTDEV: reg %08x val %08x\n", reg, val))

#define nvReadTIMER(pNv, reg) DDXMMIOW("nvReadTIMER: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteTIMER(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteTIMER: reg %08x val %08x\n", reg, val))

#define nvReadVIDEO(pNv, reg) DDXMMIOW("nvReadVIDEO: reg %08x val %08x\n", reg, (uint32_t)MMIO_IN32(pNv->REGS, reg))
#define nvWriteVIDEO(pNv, reg, val) MMIO_OUT32(pNv->REGS, reg, DDXMMIOW("nvWriteVIDEO: reg %08x val %08x\n", reg, val))

typedef struct _NVPortPrivRec {
	short		brightness;
	short		contrast;
	short		saturation;
	short		hue;
	RegionRec	clip;
	CARD32		colorKey;
	Bool		autopaintColorKey;
	Bool		doubleBuffer;
	CARD32		videoStatus;
	int		currentBuffer;
	Time		videoTime;
	int		overlayCRTC;
	Bool		grabbedByV4L;
	Bool		iturbt_709;
	Bool		blitter;
	Bool		texture;
	Bool		bicubic; /* only for texture adapter */
	Bool		SyncToVBlank;
	struct nouveau_bo *video_mem;
	int		pitch;
	int		offset;
	struct nouveau_bo *TT_mem_chunk[2];
	int		currentHostBuffer;
} NVPortPrivRec, *NVPortPrivPtr;

#define GET_OVERLAY_PRIVATE(pNv) \
            (NVPortPrivPtr)((pNv)->overlayAdaptor->pPortPrivates[0].ptr)

#define GET_BLIT_PRIVATE(pNv) \
            (NVPortPrivPtr)((pNv)->blitAdaptor->pPortPrivates[0].ptr)

#define OFF_TIMER       0x01
#define FREE_TIMER      0x02
#define CLIENT_VIDEO_ON 0x04
#define OFF_DELAY       500  /* milliseconds */
#define FREE_DELAY      5000

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#endif /* __NV_STRUCT_H__ */
