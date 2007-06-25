/* $XdotOrg: driver/xf86-video-nv/src/nv_driver.c,v 1.21 2006/01/24 16:45:29 aplattner Exp $ */
/* $XConsortium: nv_driver.c /main/3 1996/10/28 05:13:37 kaleb $ */
/*
 * Copyright 1996-1997  David J. McKay
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
 * DAVID J. MCKAY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Hacked together from mga driver and 3.3.4 NVIDIA driver by Jarno Paananen
   <jpaana@s2.org> */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_driver.c,v 1.144 2006/06/16 00:19:32 mvojkovi Exp $ */

#include "nv_include.h"

#include "xf86int10.h"

#include "xf86drm.h"

extern DisplayModePtr xf86ModesAdd(DisplayModePtr Modes, DisplayModePtr Additions);

/*const   OptionInfoRec * RivaAvailableOptions(int chipid, int busid);
Bool    RivaGetScrnInfoRec(PciChipsets *chips, int chip);*/

/*
 * Forward definitions for the functions that make up the driver.
 */
/* Mandatory functions */
static const OptionInfoRec * NVAvailableOptions(int chipid, int busid);
static void    NVIdentify(int flags);
static Bool    NVProbe(DriverPtr drv, int flags);
static Bool    NVPreInit(ScrnInfoPtr pScrn, int flags);
static Bool    NVScreenInit(int Index, ScreenPtr pScreen, int argc,
                            char **argv);
static Bool    NVEnterVT(int scrnIndex, int flags);
static void    NVLeaveVT(int scrnIndex, int flags);
static Bool    NVCloseScreen(int scrnIndex, ScreenPtr pScreen);
static Bool    NVSaveScreen(ScreenPtr pScreen, int mode);

/* Optional functions */
static void    NVFreeScreen(int scrnIndex, int flags);
static ModeStatus NVValidMode(int scrnIndex, DisplayModePtr mode,
			      Bool verbose, int flags);
#ifdef RANDR
static Bool    NVDriverFunc(ScrnInfoPtr pScrnInfo, xorgDriverFuncOp op,
			      pointer data);
#endif

/* Internally used functions */

static Bool	NVMapMem(ScrnInfoPtr pScrn);
static Bool	NVUnmapMem(ScrnInfoPtr pScrn);
static void	NVSave(ScrnInfoPtr pScrn);
static void	NVRestore(ScrnInfoPtr pScrn);
static Bool	NVModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode);


/*
 * This contains the functions needed by the server after loading the
 * driver module.  It must be supplied, and gets added the driver list by
 * the Module Setup funtion in the dynamic case.  In the static case a
 * reference to this is compiled in, and this requires that the name of
 * this DriverRec be an upper-case version of the driver name.
 */

_X_EXPORT DriverRec NV = {
        NV_VERSION,
	NV_DRIVER_NAME,
        NVIdentify,
        NVProbe,
	NVAvailableOptions,
        NULL,
        0
};

struct NvFamily
{
  char *name;
  char *chipset;
};

static struct NvFamily NVKnownFamilies[] =
{
  { "RIVA 128",    "NV03" },
  { "RIVA TNT",    "NV04" },
  { "RIVA TNT2",   "NV05" },
  { "GeForce 256", "NV10" },
  { "GeForce 2",   "NV11, NV15" },
  { "GeForce 4MX", "NV17, NV18" },
  { "GeForce 3",   "NV20" },
  { "GeForce 4Ti", "NV25, NV28" },
  { "GeForce FX",  "NV3x" },
  { "GeForce 6",   "NV4x" },
  { "GeForce 7",   "G7x" },
  { NULL, NULL}
};

/* Known cards as of 2006/06/16 */

static SymTabRec NVKnownChipsets[] =
{
  { 0x12D20018, "RIVA 128" },
  { 0x12D20019, "RIVA 128ZX" },

  { 0x10DE0020, "RIVA TNT" },

  { 0x10DE0028, "RIVA TNT2" },
  { 0x10DE002A, "Unknown TNT2" },
  { 0x10DE002C, "Vanta" },
  { 0x10DE0029, "RIVA TNT2 Ultra" },
  { 0x10DE002D, "RIVA TNT2 Model 64" },

  { 0x10DE00A0, "Aladdin TNT2" },

  { 0x10DE0100, "GeForce 256" },
  { 0x10DE0101, "GeForce DDR" },
  { 0x10DE0103, "Quadro" },

  { 0x10DE0110, "GeForce2 MX/MX 400" },
  { 0x10DE0111, "GeForce2 MX 100/200" },
  { 0x10DE0112, "GeForce2 Go" },
  { 0x10DE0113, "Quadro2 MXR/EX/Go" },

  { 0x10DE01A0, "GeForce2 Integrated GPU" },

  { 0x10DE0150, "GeForce2 GTS" },
  { 0x10DE0151, "GeForce2 Ti" },
  { 0x10DE0152, "GeForce2 Ultra" },
  { 0x10DE0153, "Quadro2 Pro" },

  { 0x10DE0170, "GeForce4 MX 460" },
  { 0x10DE0171, "GeForce4 MX 440" },
  { 0x10DE0172, "GeForce4 MX 420" },
  { 0x10DE0173, "GeForce4 MX 440-SE" },
  { 0x10DE0174, "GeForce4 440 Go" },
  { 0x10DE0175, "GeForce4 420 Go" },
  { 0x10DE0176, "GeForce4 420 Go 32M" },
  { 0x10DE0177, "GeForce4 460 Go" },
  { 0x10DE0178, "Quadro4 550 XGL" },
#if defined(__powerpc__)
  { 0x10DE0179, "GeForce4 MX (Mac)" },
#else
  { 0x10DE0179, "GeForce4 440 Go 64M" },
#endif
  { 0x10DE017A, "Quadro NVS" },
  { 0x10DE017C, "Quadro4 500 GoGL" },
  { 0x10DE017D, "GeForce4 410 Go 16M" },

  { 0x10DE0181, "GeForce4 MX 440 with AGP8X" },
  { 0x10DE0182, "GeForce4 MX 440SE with AGP8X" },
  { 0x10DE0183, "GeForce4 MX 420 with AGP8X" },
  { 0x10DE0185, "GeForce4 MX 4000" },
  { 0x10DE0186, "GeForce4 448 Go" },
  { 0x10DE0187, "GeForce4 488 Go" },
  { 0x10DE0188, "Quadro4 580 XGL" },
#if defined(__powerpc__)
  { 0x10DE0189, "GeForce4 MX with AGP8X (Mac)" },
#endif
  { 0x10DE018A, "Quadro4 NVS 280 SD" },
  { 0x10DE018B, "Quadro4 380 XGL" },
  { 0x10DE018C, "Quadro NVS 50 PCI" },
  { 0x10DE018D, "GeForce4 448 Go" },

  { 0x10DE01F0, "GeForce4 MX Integrated GPU" },

  { 0x10DE0200, "GeForce3" },
  { 0x10DE0201, "GeForce3 Ti 200" },
  { 0x10DE0202, "GeForce3 Ti 500" },
  { 0x10DE0203, "Quadro DCC" },

  { 0x10DE0250, "GeForce4 Ti 4600" },
  { 0x10DE0251, "GeForce4 Ti 4400" },
  { 0x10DE0253, "GeForce4 Ti 4200" },
  { 0x10DE0258, "Quadro4 900 XGL" },
  { 0x10DE0259, "Quadro4 750 XGL" },
  { 0x10DE025B, "Quadro4 700 XGL" },

  { 0x10DE0280, "GeForce4 Ti 4800" },
  { 0x10DE0281, "GeForce4 Ti 4200 with AGP8X" },
  { 0x10DE0282, "GeForce4 Ti 4800 SE" },
  { 0x10DE0286, "GeForce4 4200 Go" },
  { 0x10DE028C, "Quadro4 700 GoGL" },
  { 0x10DE0288, "Quadro4 980 XGL" },
  { 0x10DE0289, "Quadro4 780 XGL" },

  { 0x10DE0301, "GeForce FX 5800 Ultra" },
  { 0x10DE0302, "GeForce FX 5800" },
  { 0x10DE0308, "Quadro FX 2000" },
  { 0x10DE0309, "Quadro FX 1000" },

  { 0x10DE0311, "GeForce FX 5600 Ultra" },
  { 0x10DE0312, "GeForce FX 5600" },
  { 0x10DE0314, "GeForce FX 5600XT" },
  { 0x10DE031A, "GeForce FX Go5600" },
  { 0x10DE031B, "GeForce FX Go5650" },
  { 0x10DE031C, "Quadro FX Go700" },

  { 0x10DE0320, "GeForce FX 5200" },
  { 0x10DE0321, "GeForce FX 5200 Ultra" },
  { 0x10DE0322, "GeForce FX 5200" },
  { 0x10DE0323, "GeForce FX 5200LE" },
  { 0x10DE0324, "GeForce FX Go5200" },
  { 0x10DE0325, "GeForce FX Go5250" },
  { 0x10DE0326, "GeForce FX 5500" },
  { 0x10DE0327, "GeForce FX 5100" },
  { 0x10DE0328, "GeForce FX Go5200 32M/64M" },
#if defined(__powerpc__)
  { 0x10DE0329, "GeForce FX 5200 (Mac)" },
#endif
  { 0x10DE032A, "Quadro NVS 55/280 PCI" },
  { 0x10DE032B, "Quadro FX 500/600 PCI" },
  { 0x10DE032C, "GeForce FX Go53xx Series" },
  { 0x10DE032D, "GeForce FX Go5100" },

  { 0x10DE0330, "GeForce FX 5900 Ultra" },
  { 0x10DE0331, "GeForce FX 5900" },
  { 0x10DE0332, "GeForce FX 5900XT" },
  { 0x10DE0333, "GeForce FX 5950 Ultra" },
  { 0x10DE0334, "GeForce FX 5900ZT" },
  { 0x10DE0338, "Quadro FX 3000" },
  { 0x10DE033F, "Quadro FX 700" },

  { 0x10DE0341, "GeForce FX 5700 Ultra" },
  { 0x10DE0342, "GeForce FX 5700" },
  { 0x10DE0343, "GeForce FX 5700LE" },
  { 0x10DE0344, "GeForce FX 5700VE" },
  { 0x10DE0347, "GeForce FX Go5700" },
  { 0x10DE0348, "GeForce FX Go5700" },
  { 0x10DE034C, "Quadro FX Go1000" },
  { 0x10DE034E, "Quadro FX 1100" },

  { 0x10DE0040, "GeForce 6800 Ultra" },
  { 0x10DE0041, "GeForce 6800" },
  { 0x10DE0042, "GeForce 6800 LE" },
  { 0x10DE0043, "GeForce 6800 XE" },
  { 0x10DE0044, "GeForce 6800 XT" },
  { 0x10DE0045, "GeForce 6800 GT" },
  { 0x10DE0046, "GeForce 6800 GT" },
  { 0x10DE0047, "GeForce 6800 GS" },
  { 0x10DE0048, "GeForce 6800 XT" },
  { 0x10DE004E, "Quadro FX 4000" },

  { 0x10DE00C0, "GeForce 6800 GS" },
  { 0x10DE00C1, "GeForce 6800" },
  { 0x10DE00C2, "GeForce 6800 LE" },
  { 0x10DE00C3, "GeForce 6800 XT" },
  { 0x10DE00C8, "GeForce Go 6800" },
  { 0x10DE00C9, "GeForce Go 6800 Ultra" },
  { 0x10DE00CC, "Quadro FX Go1400" },
  { 0x10DE00CD, "Quadro FX 3450/4000 SDI" },
  { 0x10DE00CE, "Quadro FX 1400" },

  { 0x10DE0140, "GeForce 6600 GT" },
  { 0x10DE0141, "GeForce 6600" },
  { 0x10DE0142, "GeForce 6600 LE" },
  { 0x10DE0143, "GeForce 6600 VE" },
  { 0x10DE0144, "GeForce Go 6600" },
  { 0x10DE0145, "GeForce 6610 XL" },
  { 0x10DE0146, "GeForce Go 6600 TE/6200 TE" },
  { 0x10DE0147, "GeForce 6700 XL" },
  { 0x10DE0148, "GeForce Go 6600" },
  { 0x10DE0149, "GeForce Go 6600 GT" },
  { 0x10DE014C, "Quadro FX 550" },
  { 0x10DE014D, "Quadro FX 550" },
  { 0x10DE014E, "Quadro FX 540" },
  { 0x10DE014F, "GeForce 6200" },

  { 0x10DE0160, "GeForce 6500" },
  { 0x10DE0161, "GeForce 6200 TurboCache(TM)" },
  { 0x10DE0162, "GeForce 6200SE TurboCache(TM)" },
  { 0x10DE0163, "GeForce 6200 LE" },
  { 0x10DE0164, "GeForce Go 6200" },
  { 0x10DE0165, "Quadro NVS 285" },
  { 0x10DE0166, "GeForce Go 6400" },
  { 0x10DE0167, "GeForce Go 6200" },
  { 0x10DE0168, "GeForce Go 6400" },
  { 0x10DE0169, "GeForce 6250" },

  { 0x10DE0211, "GeForce 6800" },
  { 0x10DE0212, "GeForce 6800 LE" },
  { 0x10DE0215, "GeForce 6800 GT" },
  { 0x10DE0218, "GeForce 6800 XT" },

  { 0x10DE0221, "GeForce 6200" },
  { 0x10DE0222, "GeForce 6200 A-LE" },

  { 0x10DE0090, "GeForce 7800 GTX" },
  { 0x10DE0091, "GeForce 7800 GTX" },
  { 0x10DE0092, "GeForce 7800 GT" },
  { 0x10DE0093, "GeForce 7800 GS" },
  { 0x10DE0095, "GeForce 7800 SLI" },
  { 0x10DE0098, "GeForce Go 7800" },
  { 0x10DE0099, "GeForce Go 7800 GTX" },
  { 0x10DE009D, "Quadro FX 4500" },

  { 0x10DE01D1, "GeForce 7300 LE" },
  { 0x10DE01D3, "GeForce 7300 SE" },
  { 0x10DE01D6, "GeForce Go 7200" },
  { 0x10DE01D7, "GeForce Go 7300" },
  { 0x10DE01D8, "GeForce Go 7400" },
  { 0x10DE01D9, "GeForce Go 7400 GS" },
  { 0x10DE01DA, "Quadro NVS 110M" },
  { 0x10DE01DB, "Quadro NVS 120M" },
  { 0x10DE01DC, "Quadro FX 350M" },
  { 0x10DE01DD, "GeForce 7500 LE" },
  { 0x10DE01DE, "Quadro FX 350" },
  { 0x10DE01DF, "GeForce 7300 GS" },

  { 0x10DE0391, "GeForce 7600 GT" },
  { 0x10DE0392, "GeForce 7600 GS" },
  { 0x10DE0393, "GeForce 7300 GT" },
  { 0x10DE0394, "GeForce 7600 LE" },
  { 0x10DE0395, "GeForce 7300 GT" },
  { 0x10DE0397, "GeForce Go 7700" },
  { 0x10DE0398, "GeForce Go 7600" },
  { 0x10DE0399, "GeForce Go 7600 GT"},
  { 0x10DE039A, "Quadro NVS 300M" },
  { 0x10DE039B, "GeForce Go 7900 SE" },
  { 0x10DE039C, "Quadro FX 550M" },
  { 0x10DE039E, "Quadro FX 560" },

  { 0x10DE0290, "GeForce 7900 GTX" },
  { 0x10DE0291, "GeForce 7900 GT" },
  { 0x10DE0292, "GeForce 7900 GS" },
  { 0x10DE0298, "GeForce Go 7900 GS" },
  { 0x10DE0299, "GeForce Go 7900 GTX" },
  { 0x10DE029A, "Quadro FX 2500M" },
  { 0x10DE029B, "Quadro FX 1500M" },
  { 0x10DE029C, "Quadro FX 5500" },
  { 0x10DE029D, "Quadro FX 3500" },
  { 0x10DE029E, "Quadro FX 1500" },
  { 0x10DE029F, "Quadro FX 4500 X2" },

  { 0x10DE0240, "GeForce 6150" },
  { 0x10DE0241, "GeForce 6150 LE" },
  { 0x10DE0242, "GeForce 6100" },
  { 0x10DE0244, "GeForce Go 6150" },
  { 0x10DE0247, "GeForce Go 6100" },

  {-1, NULL}
};


/*
 * List of symbols from other modules that this module references.  This
 * list is used to tell the loader that it is OK for symbols here to be
 * unresolved providing that it hasn't been told that they haven't been
 * told that they are essential via a call to xf86LoaderReqSymbols() or
 * xf86LoaderReqSymLists().  The purpose is this is to avoid warnings about
 * unresolved symbols that are not required.
 */

static const char *vgahwSymbols[] = {
    "vgaHWUnmapMem",
    "vgaHWDPMSSet",
    "vgaHWFreeHWRec",
    "vgaHWGetHWRec",
    "vgaHWGetIndex",
    "vgaHWInit",
    "vgaHWMapMem",
    "vgaHWProtect",
    "vgaHWRestore",
    "vgaHWSave",
    "vgaHWSaveScreen",
    NULL
};

static const char *fbSymbols[] = {
    "fbPictureInit",
    "fbScreenInit",
    NULL
};

static const char *xaaSymbols[] = {
    "XAACopyROP",
    "XAACreateInfoRec",
    "XAADestroyInfoRec",
    "XAAFallbackOps",
    "XAAInit",
    "XAAPatternROP",
    NULL
};

static const char *exaSymbols[] = {
    "exaDriverInit",
    "exaOffscreenInit",
    NULL
};

static const char *ramdacSymbols[] = {
    "xf86CreateCursorInfoRec",
    "xf86DestroyCursorInfoRec",
    "xf86InitCursor",
    NULL
};

static const char *ddcSymbols[] = {
    "xf86PrintEDID",
    "xf86DoEDID_DDC2",
    "xf86SetDDCproperties",
    NULL
};

static const char *vbeSymbols[] = {
    "VBEInit",
    "vbeFree",
    "vbeDoEDID",
    NULL
};

static const char *i2cSymbols[] = {
    "xf86CreateI2CBusRec",
    "xf86I2CBusInit",
    NULL
};

static const char *shadowSymbols[] = {
    "ShadowFBInit",
    NULL
};

static const char *int10Symbols[] = {
    "xf86FreeInt10",
    "xf86InitInt10",
    NULL
};

static const char *rivaSymbols[] = {
   "RivaGetScrnInfoRec",
   "RivaAvailableOptions",
    NULL
};

const char *drmSymbols[] = {
    "drmOpen", 
    "drmAddBufs",
    "drmAddMap",
    "drmAgpAcquire",
    "drmAgpVersionMajor",
    "drmAgpVersionMinor",
    "drmAgpAlloc",
    "drmAgpBind",
    "drmAgpEnable",
    "drmAgpFree",
    "drmAgpRelease",
    "drmAgpUnbind",
    "drmAuthMagic",
    "drmCommandNone",
    "drmCommandWrite",
    "drmCommandWriteRead",
    "drmCreateContext",
    "drmCtlInstHandler",
    "drmCtlUninstHandler",
    "drmDestroyContext",
    "drmFreeVersion",
    "drmGetInterruptFromBusID",
    "drmGetLibVersion",
    "drmGetVersion",
    NULL
};

const char *driSymbols[] = {
    "DRICloseScreen",
    "DRICreateInfoRec",
    "DRIDestroyInfoRec",
    "DRIFinishScreenInit",
    "DRIGetSAREAPrivate",
    "DRILock",
    "DRIQueryVersion",
    "DRIScreenInit",
    "DRIUnlock",
    "GlxSetVisualConfigs",
    "DRICreatePCIBusID",
    NULL
};


static MODULESETUPPROTO(nouveauSetup);

static XF86ModuleVersionInfo nouveauVersRec =
{
    "nouveau",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    NV_MAJOR_VERSION, NV_MINOR_VERSION, NV_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,                     /* This is a video driver */
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0,0,0,0}
};

_X_EXPORT XF86ModuleData nouveauModuleData = { &nouveauVersRec, nouveauSetup, NULL };


/*
 * This is intentionally screen-independent.  It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

static Bool
NVGetRec(ScrnInfoPtr pScrn)
{
    /*
     * Allocate an NVRec, and hook it into pScrn->driverPrivate.
     * pScrn->driverPrivate is initialised to NULL, so we can check if
     * the allocation has already been done.
     */
    if (pScrn->driverPrivate != NULL)
        return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(NVRec), 1);
    /* Initialise it */

    return TRUE;
}

static void
NVFreeRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate == NULL)
        return;
    xfree(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static pointer
nouveauSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    /* This module should be loaded only once, but check to be sure. */

    if (!setupDone) {
        setupDone = TRUE;
        xf86AddDriver(&NV, module, 0);

        /*
         * Modules that this driver always requires may be loaded here
         * by calling LoadSubModule().
         */
        /*
         * Tell the loader about symbols from other modules that this module
         * might refer to.
         */
        LoaderRefSymLists(vgahwSymbols, xaaSymbols, exaSymbols, fbSymbols,
#ifdef XF86DRI
                          drmSymbols, 
#endif
                          ramdacSymbols, shadowSymbols, rivaSymbols,
                          i2cSymbols, ddcSymbols, vbeSymbols,
                          int10Symbols, NULL);

        /*
         * The return value must be non-NULL on success even though there
         * is no TearDownProc.
         */
        return (pointer)1;
    } else {
        if (errmaj) *errmaj = LDR_ONCEONLY;
        return NULL;
    }
}

static const OptionInfoRec *
NVAvailableOptions(int chipid, int busid)
{
/*    if(chipid == 0x12D20018) {
	if (!xf86LoadOneModule("riva128", NULL)) {
	    return NULL;
	} else
	    return RivaAvailableOptions(chipid, busid);
    }*/
    
    return NVOptions;
}

/* Mandatory */
static void
NVIdentify(int flags)
{
    struct NvFamily *family;
    size_t maxLen=0;

    xf86DrvMsg(0, X_INFO, NV_NAME " driver " NV_DRIVER_DATE "\n");
    xf86DrvMsg(0, X_INFO, NV_NAME " driver for NVIDIA chipset families :\n");

    /* maximum length for alignment */
    family = NVKnownFamilies;
    while(family->name && family->chipset)
    {
        maxLen = max(maxLen, strlen(family->name));
        family++;
    }

    /* display */
    family = NVKnownFamilies;
    while(family->name && family->chipset)
    {
        size_t len = strlen(family->name);
        xf86ErrorF("\t%s", family->name);
        while(len<maxLen+1)
        {
            xf86ErrorF(" ");
            len++;
        }
        xf86ErrorF("(%s)\n", family->chipset);
        family++;
    }
}


static Bool
NVGetScrnInfoRec(PciChipsets *chips, int chip)
{
    ScrnInfoPtr pScrn;

    pScrn = xf86ConfigPciEntity(NULL, 0, chip,
                                chips, NULL, NULL, NULL,
                                NULL, NULL);

    if(!pScrn) return FALSE;

    pScrn->driverVersion    = NV_VERSION;
    pScrn->driverName       = NV_DRIVER_NAME;
    pScrn->name             = NV_NAME;

    pScrn->Probe            = NVProbe;
    pScrn->PreInit          = NVPreInit;
    pScrn->ScreenInit       = NVScreenInit;
    pScrn->SwitchMode       = NVSwitchMode;
    pScrn->AdjustFrame      = NVAdjustFrame;
    pScrn->EnterVT          = NVEnterVT;
    pScrn->LeaveVT          = NVLeaveVT;
    pScrn->FreeScreen       = NVFreeScreen;
    pScrn->ValidMode        = NVValidMode;

    return TRUE;
}

#define MAX_CHIPS MAXSCREENS


static CARD32 
NVGetPCIXpressChip (pciVideoPtr pVideo)
{
    volatile CARD32 *regs;
    CARD32 pciid, pcicmd;
    PCITAG Tag = ((pciConfigPtr)(pVideo->thisCard))->tag;

    pcicmd = pciReadLong(Tag, PCI_CMD_STAT_REG);
    pciWriteLong(Tag, PCI_CMD_STAT_REG, pcicmd | PCI_CMD_MEM_ENABLE);
    
    regs = xf86MapPciMem(-1, VIDMEM_MMIO, Tag, pVideo->memBase[0], 0x2000);

    pciid = regs[0x1800/4];

    xf86UnMapVidMem(-1, (pointer)regs, 0x2000);

    pciWriteLong(Tag, PCI_CMD_STAT_REG, pcicmd);

    if((pciid & 0x0000ffff) == 0x000010DE) 
       pciid = 0x10DE0000 | (pciid >> 16);
    else 
    if((pciid & 0xffff0000) == 0xDE100000) /* wrong endian */
       pciid = 0x10DE0000 | ((pciid << 8) & 0x0000ff00) |
                            ((pciid >> 8) & 0x000000ff);

    return pciid;
}


/* Mandatory */
static Bool
NVProbe(DriverPtr drv, int flags)
{
    int i;
    GDevPtr *devSections;
    int *usedChips;
    SymTabRec NVChipsets[MAX_CHIPS + 1];
    PciChipsets NVPciChipsets[MAX_CHIPS + 1];
    pciVideoPtr *ppPci;
    int numDevSections;
    int numUsed;
    Bool foundScreen = FALSE;


    if ((numDevSections = xf86MatchDevice(NV_DRIVER_NAME, &devSections)) <= 0) 
        return FALSE;  /* no matching device section */

    if (!(ppPci = xf86GetPciVideoInfo())) 
        return FALSE;  /* no PCI cards found */

    numUsed = 0;

    /* Create the NVChipsets and NVPciChipsets from found devices */
    while (*ppPci && (numUsed < MAX_CHIPS)) {
        if(((*ppPci)->vendor == PCI_VENDOR_NVIDIA_SGS) || 
           ((*ppPci)->vendor == PCI_VENDOR_NVIDIA)) 
        {
            SymTabRec *nvchips = NVKnownChipsets;
            int pciid = ((*ppPci)->vendor << 16) | (*ppPci)->chipType;
            int token = pciid;

            if(((token & 0xfff0) == CHIPSET_MISC_BRIDGED) ||
               ((token & 0xfff0) == CHIPSET_G73_BRIDGED))
            {
                token = NVGetPCIXpressChip(*ppPci);
            }

            while(nvchips->name) {
               if(token == nvchips->token)
                  break;
               nvchips++;
            }

            if(nvchips->name) { /* found one */
               NVChipsets[numUsed].token = pciid;
               NVChipsets[numUsed].name = nvchips->name;
               NVPciChipsets[numUsed].numChipset = pciid; 
               NVPciChipsets[numUsed].PCIid = pciid;
               NVPciChipsets[numUsed].resList = RES_SHARED_VGA;
               numUsed++;
            } else if ((*ppPci)->vendor == PCI_VENDOR_NVIDIA) {
               /* look for a compatible devices which may be newer than 
                  the NVKnownChipsets list above.  */
               switch(token & 0xfff0) {
               case CHIPSET_NV17:
               case CHIPSET_NV18:
               case CHIPSET_NV25:
               case CHIPSET_NV28:
               case CHIPSET_NV30:
               case CHIPSET_NV31:
               case CHIPSET_NV34:
               case CHIPSET_NV35:
               case CHIPSET_NV36:
               case CHIPSET_NV40:
               case CHIPSET_NV41:
               case 0x0120:
               case CHIPSET_NV43:
               case CHIPSET_NV44:
               case 0x0130:
               case CHIPSET_G72:
               case CHIPSET_G70:
               case CHIPSET_NV45:
               case CHIPSET_NV44A:
               case 0x0230:
               case CHIPSET_G71:
               case CHIPSET_G73:
               case CHIPSET_C512:
                   NVChipsets[numUsed].token = pciid;
                   NVChipsets[numUsed].name = "Unknown NVIDIA chip";
                   NVPciChipsets[numUsed].numChipset = pciid;
                   NVPciChipsets[numUsed].PCIid = pciid;
                   NVPciChipsets[numUsed].resList = RES_SHARED_VGA;
                   numUsed++;
                   break;
               default:  break;  /* we don't recognize it */
               }
            }
        }
        ppPci++;
    }

    /* terminate the list */
    NVChipsets[numUsed].token = -1;
    NVChipsets[numUsed].name = NULL; 
    NVPciChipsets[numUsed].numChipset = -1;
    NVPciChipsets[numUsed].PCIid = -1;
    NVPciChipsets[numUsed].resList = RES_UNDEFINED;

    numUsed = xf86MatchPciInstances(NV_NAME, 0, NVChipsets, NVPciChipsets,
                                    devSections, numDevSections, drv,
                                    &usedChips);
                        
    if (numUsed <= 0) 
        return FALSE;

    if (flags & PROBE_DETECT)
	foundScreen = TRUE;
    else for (i = 0; i < numUsed; i++) {
        pciVideoPtr pPci;

        pPci = xf86GetPciInfoForEntity(usedChips[i]);
        if(NVGetScrnInfoRec(NVPciChipsets, usedChips[i])) 
	    foundScreen = TRUE;
    }

    xfree(devSections);
    xfree(usedChips);

    return foundScreen;
}

/* Usually mandatory */
Bool
NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
   ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
   NVPtr pNv = NVPTR(pScrn);
   Bool ret = TRUE;

   NVFBLayout *pLayout = &pNv->CurrentLayout;

   if (pLayout->mode != mode) {
     if (!NVSetMode(pScrn, mode, RR_Rotate_0))
         ret = FALSE;
   }

   pLayout->mode = mode;
    
    return ret;
}

/*
 * This function is used to initialize the Start Address - the first
 * displayed location in the video memory.
 */
/* Usually mandatory */
void 
NVAdjustFrame(int scrnIndex, int x, int y, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(pScrn);
    int startAddr;
    NVPtr pNv = NVPTR(pScrn);
    NVFBLayout *pLayout = &pNv->CurrentLayout;
    xf86CrtcPtr	crtc = config->output[config->compat_output]->crtc;
    
    if (crtc && crtc->enabled) {
	NVCrtcSetBase(crtc, x, y);
    }
}

void
NVResetCrtcConfig(ScrnInfoPtr pScrn, int set)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(pScrn);  
    NVPtr pNv = NVPTR(pScrn);
    int i;
    CARD32 val = 0;

    for (i = 0; i < config->num_crtc; i++) {
      xf86CrtcPtr crtc = config->crtc[i];
      NVCrtcPrivatePtr nv_crtc = crtc->driver_private;      

      if (set) {
	NVCrtcRegPtr regp;

	regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];
	val = regp->head;
      }

      nvWriteCRTC(pNv, nv_crtc->crtc, NV_CRTC_FSEL, val);
    }
}


/*
 * This is called when VT switching back to the X server.  Its job is
 * to reinitialise the video mode.
 *
 * We may wish to unmap video/MMIO memory too.
 */

/* Mandatory */
static Bool
NVEnterVT(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    NVPtr pNv = NVPTR(pScrn);
    int i;

    /* Save the current state */
    if (pNv->SaveGeneration != serverGeneration) {
	pNv->SaveGeneration = serverGeneration;
	NVSave(pScrn);
    }
    
    pScrn->vtSema = TRUE;
    
    NVResetCrtcConfig(pScrn, 0);
    if (!xf86SetDesiredModes(pScrn))
	return FALSE;
    NVResetCrtcConfig(pScrn, 1);
    pScrn->AdjustFrame(scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
    
    if(pNv->overlayAdaptor)
        NVResetVideo(pScrn);
    return TRUE;
}

/*
 * This is called when VT switching away from the X server.  Its job is
 * to restore the previous (text) mode.
 *
 * We may wish to remap video/MMIO memory too.
 */

/* Mandatory */
static void
NVLeaveVT(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    NVPtr pNv = NVPTR(pScrn);

    NVSync(pScrn);
    NVRestore(pScrn);
}



static void 
NVBlockHandler (
    int i, 
    pointer blockData, 
    pointer pTimeout,
    pointer pReadmask
)
{
    ScreenPtr     pScreen = screenInfo.screens[i];
    ScrnInfoPtr   pScrnInfo = xf86Screens[i];
    NVPtr         pNv = NVPTR(pScrnInfo);

    if (pNv->DMAKickoffCallback)
        (*pNv->DMAKickoffCallback)(pNv);
    
    pScreen->BlockHandler = pNv->BlockHandler;
    (*pScreen->BlockHandler) (i, blockData, pTimeout, pReadmask);
    pScreen->BlockHandler = NVBlockHandler;

    if (pNv->VideoTimerCallback) 
        (*pNv->VideoTimerCallback)(pScrnInfo, currentTime.milliseconds);

}


/*
 * This is called at the end of each server generation.  It restores the
 * original (text) mode.  It should also unmap the video memory, and free
 * any per-generation data allocated by the driver.  It should finish
 * by unwrapping and calling the saved CloseScreen function.
 */

/* Mandatory */
static Bool
NVCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    NVPtr pNv = NVPTR(pScrn);

    if (pScrn->vtSema) {
        pScrn->vtSema = FALSE;
        NVSync(pScrn);
        NVRestore(pScrn);
    }

    NVUnmapMem(pScrn);
    vgaHWUnmapMem(pScrn);
    if (pNv->AccelInfoRec)
        XAADestroyInfoRec(pNv->AccelInfoRec);
    if (pNv->CursorInfoRec)
        xf86DestroyCursorInfoRec(pNv->CursorInfoRec);
    if (pNv->ShadowPtr)
        xfree(pNv->ShadowPtr);
    if (pNv->overlayAdaptor)
	xfree(pNv->overlayAdaptor);
    if (pNv->blitAdaptor)
        xfree(pNv->blitAdaptor);

    pScreen->CloseScreen = pNv->CloseScreen;
    pScreen->BlockHandler = pNv->BlockHandler;
    return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

/* Free up any persistent data structures */

/* Optional */
static void
NVFreeScreen(int scrnIndex, int flags)
{
    /*
     * This only gets called when a screen is being deleted.  It does not
     * get called routinely at the end of a server generation.
     */
    if (xf86LoaderCheckSymbol("vgaHWFreeHWRec"))
	vgaHWFreeHWRec(xf86Screens[scrnIndex]);
    NVFreeRec(xf86Screens[scrnIndex]);
}


/* Checks if a mode is suitable for the selected chipset. */

/* Optional */
static ModeStatus
NVValidMode(int scrnIndex, DisplayModePtr mode, Bool verbose, int flags)
{
    NVPtr pNv = NVPTR(xf86Screens[scrnIndex]);

    if(pNv->fpWidth && pNv->fpHeight)
      if((pNv->fpWidth < mode->HDisplay) || (pNv->fpHeight < mode->VDisplay))
        return (MODE_PANEL);

    return (MODE_OK);
}

static void
nvProbeDDC(ScrnInfoPtr pScrn, int index)
{
    vbeInfoPtr pVbe;

    if (xf86LoadSubModule(pScrn, "vbe")) {
        pVbe = VBEInit(NULL,index);
        ConfiguredMonitor = vbeDoEDID(pVbe, NULL);
	vbeFree(pVbe);
    }
}


Bool NVI2CInit(ScrnInfoPtr pScrn)
{
    char *mod = "i2c";

    if (xf86LoadSubModule(pScrn, mod)) {
        xf86LoaderReqSymLists(i2cSymbols,NULL);

        mod = "ddc";
        if(xf86LoadSubModule(pScrn, mod)) {
            xf86LoaderReqSymLists(ddcSymbols, NULL);
            return TRUE;
        } 
    }

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
              "Couldn't load %s module.  DDC probing can't be done\n", mod);

    return FALSE;
}

static Bool NVPreInitDRI(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

    	if (!NVDRIGetVersion(pScrn))
		return FALSE;

 	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"[dri] Found DRI library version %d.%d.%d and kernel"
		" module version %d.%d.%d\n",
		pNv->pLibDRMVersion->version_major,
		pNv->pLibDRMVersion->version_minor,
		pNv->pLibDRMVersion->version_patchlevel,
		pNv->pKernelDRMVersion->version_major,
		pNv->pKernelDRMVersion->version_minor,
		pNv->pKernelDRMVersion->version_patchlevel);

	return TRUE;
}

static Bool
nv_xf86crtc_resize (ScrnInfoPtr scrn, int width, int height)
{
    scrn->virtualX = width;
    scrn->virtualY = height;
    return TRUE;
}

static const xf86CrtcConfigFuncsRec nv_xf86crtc_config_funcs = {
    nv_xf86crtc_resize
};

/* Mandatory */
Bool
NVPreInit(ScrnInfoPtr pScrn, int flags)
{
    xf86CrtcConfigPtr   xf86_config; 
    NVPtr pNv;
    MessageType from;
    int i, max_width, max_height;
    ClockRangePtr clockRanges;
    const char *s;
    int num_crtc;

    if (flags & PROBE_DETECT) {
        EntityInfoPtr pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

        if (!pEnt)
            return FALSE;

        i = pEnt->index;
        xfree(pEnt);

        nvProbeDDC(pScrn, i);
        return TRUE;
    }

    /*
     * Note: This function is only called once at server startup, and
     * not at the start of each server generation.  This means that
     * only things that are persistent across server generations can
     * be initialised here.  xf86Screens[] is (pScrn is a pointer to one
     * of these).  Privates allocated using xf86AllocateScrnInfoPrivateIndex()  
     * are too, and should be used for data that must persist across
     * server generations.
     *
     * Per-generation data should be allocated with
     * AllocateScreenPrivateIndex() from the ScreenInit() function.
     */

    /* Check the number of entities, and fail if it isn't one. */
    if (pScrn->numEntities != 1)
	return FALSE;

    /* Allocate the NVRec driverPrivate */
    if (!NVGetRec(pScrn)) {
	return FALSE;
    }
    pNv = NVPTR(pScrn);

    /* Get the entity, and make sure it is PCI. */
    pNv->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
    if (pNv->pEnt->location.type != BUS_PCI)
	return FALSE;
 
    /* Find the PCI info for this screen */
    pNv->PciInfo = xf86GetPciInfoForEntity(pNv->pEnt->index);
    pNv->PciTag = pciTag(pNv->PciInfo->bus, pNv->PciInfo->device,
			  pNv->PciInfo->func);

    pNv->Primary = xf86IsPrimaryPci(pNv->PciInfo);

    /* Initialize the card through int10 interface if needed */
    if (xf86LoadSubModule(pScrn, "int10")) {
 	xf86LoaderReqSymLists(int10Symbols, NULL);
#if !defined(__alpha__) && !defined(__powerpc__)
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing int10\n");
        pNv->pInt = xf86InitInt10(pNv->pEnt->index);
#endif
    }
   
    xf86SetOperatingState(resVgaIo, pNv->pEnt->index, ResUnusedOpr);
    xf86SetOperatingState(resVgaMem, pNv->pEnt->index, ResDisableOpr);

    /* Set pScrn->monitor */
    pScrn->monitor = pScrn->confScreen->monitor;

    /*
     * Set the Chipset and ChipRev, allowing config file entries to
     * override.
     */
    if (pNv->pEnt->device->chipset && *pNv->pEnt->device->chipset) {
	pScrn->chipset = pNv->pEnt->device->chipset;
        pNv->Chipset = xf86StringToToken(NVKnownChipsets, pScrn->chipset);
        from = X_CONFIG;
    } else if (pNv->pEnt->device->chipID >= 0) {
	pNv->Chipset = pNv->pEnt->device->chipID;
	pScrn->chipset = (char *)xf86TokenToString(NVKnownChipsets, 
                                                   pNv->Chipset);
	from = X_CONFIG;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "ChipID override: 0x%04X\n",
		   pNv->Chipset);
    } else {
	from = X_PROBED;
	pNv->Chipset = (pNv->PciInfo->vendor << 16) | pNv->PciInfo->chipType;

        if(((pNv->Chipset & 0xfff0) == CHIPSET_MISC_BRIDGED) ||
           ((pNv->Chipset & 0xfff0) == CHIPSET_G73_BRIDGED))
        {
            pNv->Chipset = NVGetPCIXpressChip(pNv->PciInfo);
        }

	pScrn->chipset = (char *)xf86TokenToString(NVKnownChipsets, 
                                                   pNv->Chipset);
        if(!pScrn->chipset)
          pScrn->chipset = "Unknown NVIDIA chipset";
    }

    if (pNv->pEnt->device->chipRev >= 0) {
	pNv->ChipRev = pNv->pEnt->device->chipRev;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "ChipRev override: %d\n",
		   pNv->ChipRev);
    } else {
	pNv->ChipRev = pNv->PciInfo->chipRev;
    }

    /*
     * This shouldn't happen because such problems should be caught in
     * NVProbe(), but check it just in case.
     */
    if (pScrn->chipset == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "ChipID 0x%04X is not recognised\n", pNv->Chipset);
	xf86FreeInt10(pNv->pInt);
	return FALSE;
    }
    if (pNv->Chipset < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Chipset \"%s\" is not recognised\n", pScrn->chipset);
	xf86FreeInt10(pNv->pInt);
	return FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, from, "Chipset: \"%s\"\n", pScrn->chipset);


    /*
     * The first thing we should figure out is the depth, bpp, etc.
     */

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support32bppFb)) {
	xf86FreeInt10(pNv->pInt);
	return FALSE;
    } else {
	/* Check that the returned depth is one we support */
	switch (pScrn->depth) {
            case 8:
            case 15:
            case 16:
            case 24:
                /* OK */
                break;
            default:
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Given depth (%d) is not supported by this driver\n",
                    pScrn->depth);
		xf86FreeInt10(pNv->pInt);
                return FALSE;
	}
    }
    xf86PrintDepthBpp(pScrn);

    /* Get the depth24 pixmap format */
    if (pScrn->depth == 24 && pix24bpp == 0)
	pix24bpp = xf86GetBppFromDepth(pScrn, 24);

    /*
     * This must happen after pScrn->display has been set because
     * xf86SetWeight references it.
     */
    if (pScrn->depth > 8) {
	/* The defaults are OK for us */
	rgb zeros = {0, 0, 0};

	if (!xf86SetWeight(pScrn, zeros, zeros)) {
	    xf86FreeInt10(pNv->pInt);
	    return FALSE;
	}
    }

    if (!xf86SetDefaultVisual(pScrn, -1)) {
	xf86FreeInt10(pNv->pInt);
	return FALSE;
    } else {
	/* We don't currently support DirectColor at > 8bpp */
	if (pScrn->depth > 8 && (pScrn->defaultVisual != TrueColor)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Given default visual"
		       " (%s) is not supported at depth %d\n",
		       xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
	    xf86FreeInt10(pNv->pInt);
	    return FALSE;
	}
    }

    /* The vgahw module should be loaded here when needed */
    if (!xf86LoadSubModule(pScrn, "vgahw")) {
	xf86FreeInt10(pNv->pInt);
	return FALSE;
    }
    
    xf86LoaderReqSymLists(vgahwSymbols, NULL);

    /*
     * Allocate a vgaHWRec
     */
    if (!vgaHWGetHWRec(pScrn)) {
	xf86FreeInt10(pNv->pInt);
	return FALSE;
    }
    
    /* We use a programmable clock */
    pScrn->progClock = TRUE;

    /* Collect all of the relevant option flags (fill in pScrn->options) */
    xf86CollectOptions(pScrn, NULL);

    /* Process the options */
    if (!(pNv->Options = xalloc(sizeof(NVOptions))))
	return FALSE;
    memcpy(pNv->Options, NVOptions, sizeof(NVOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, pNv->Options);

    /* Set the bits per RGB for 8bpp mode */
    if (pScrn->depth == 8)
	pScrn->rgbBits = 8;

    from = X_DEFAULT;
    pNv->HWCursor = TRUE;
    /*
     * The preferred method is to use the "hw cursor" option as a tri-state
     * option, with the default set above.
     */
    if (xf86GetOptValBool(pNv->Options, OPTION_HW_CURSOR, &pNv->HWCursor)) {
	from = X_CONFIG;
    }
    /* For compatibility, accept this too (as an override) */
    if (xf86ReturnOptValBool(pNv->Options, OPTION_SW_CURSOR, FALSE)) {
	from = X_CONFIG;
	pNv->HWCursor = FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, from, "Using %s cursor\n",
		pNv->HWCursor ? "HW" : "SW");

    pNv->FpScale = TRUE;
    if (xf86GetOptValBool(pNv->Options, OPTION_FP_SCALE, &pNv->FpScale)) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Flat panel scaling %s\n",
                   pNv->FpScale ? "on" : "off");
    }
    if (xf86ReturnOptValBool(pNv->Options, OPTION_NOACCEL, FALSE)) {
	pNv->NoAccel = TRUE;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Acceleration disabled\n");
    }
    if (xf86ReturnOptValBool(pNv->Options, OPTION_SHADOW_FB, FALSE)) {
	pNv->ShadowFB = TRUE;
	pNv->NoAccel = TRUE;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, 
		"Using \"Shadow Framebuffer\" - acceleration disabled\n");
    }
    if (!pNv->NoAccel) {
        from = X_DEFAULT;
        pNv->useEXA = TRUE;
        if((s = (char *)xf86GetOptValString(pNv->Options, OPTION_ACCELMETHOD))) {
            if(!xf86NameCmp(s,"XAA")) {
                from = X_CONFIG;
                pNv->useEXA = FALSE;
            } else if(!xf86NameCmp(s,"EXA")) {
                from = X_CONFIG;
                pNv->useEXA = TRUE;
            }
        }
	xf86DrvMsg(pScrn->scrnIndex, from, "Using %s acceleration method\n", pNv->useEXA ? "EXA" : "XAA");
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Acceleration disabled\n");
    }
    
    pNv->Rotate = 0;
    pNv->RandRRotation = FALSE;
    if ((s = xf86GetOptValString(pNv->Options, OPTION_ROTATE))) {
      if(!xf86NameCmp(s, "CW")) {
	pNv->ShadowFB = TRUE;
	pNv->NoAccel = TRUE;
	pNv->HWCursor = FALSE;
	pNv->Rotate = 1;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, 
		"Rotating screen clockwise - acceleration disabled\n");
      } else
      if(!xf86NameCmp(s, "CCW")) {
	pNv->ShadowFB = TRUE;
	pNv->NoAccel = TRUE;
	pNv->HWCursor = FALSE;
	pNv->Rotate = -1;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, 
		"Rotating screen counter clockwise - acceleration disabled\n");
      } else
      if(!xf86NameCmp(s, "RandR")) {
#ifdef RANDR
	pNv->ShadowFB = TRUE;
	pNv->NoAccel = TRUE;
	pNv->HWCursor = FALSE;
	pNv->RandRRotation = TRUE;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		"Using RandR rotation - acceleration disabled\n");
#else
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"This driver was not compiled with support for the Resize and "
		"Rotate extension.  Cannot honor 'Option \"Rotate\" "
		"\"RandR\"'.\n");
#endif
      } else {
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, 
		"\"%s\" is not a valid value for Option \"Rotate\"\n", s);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, 
		"Valid options are \"CW\", \"CCW\", and \"RandR\"\n");
      }
    }

    if(xf86GetOptValInteger(pNv->Options, OPTION_VIDEO_KEY, &(pNv->videoKey))) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "video key set to 0x%x\n",
                                pNv->videoKey);
    } else {
        pNv->videoKey =  (1 << pScrn->offset.red) | 
                          (1 << pScrn->offset.green) |
        (((pScrn->mask.blue >> pScrn->offset.blue) - 1) << pScrn->offset.blue); 
    }

    if (xf86GetOptValBool(pNv->Options, OPTION_FLAT_PANEL, &(pNv->FlatPanel))) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "forcing %s usage\n",
                   pNv->FlatPanel ? "DFP" : "CRTC");
    } else {
      pNv->FlatPanel = -1;   /* autodetect later */
    }

    pNv->FPDither = FALSE;
    if (xf86GetOptValBool(pNv->Options, OPTION_FP_DITHER, &(pNv->FPDither))) 
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "enabling flat panel dither\n");

    if (xf86GetOptValInteger(pNv->Options, OPTION_CRTC_NUMBER,
                             &pNv->CRTCnumber)) 
    {
	if((pNv->CRTCnumber < 0) || (pNv->CRTCnumber > 1)) {
           pNv->CRTCnumber = -1;
           xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, 
                      "Invalid CRTC number.  Must be 0 or 1\n");
        }
    } else {
        pNv->CRTCnumber = -1; /* autodetect later */
    }


    if (xf86GetOptValInteger(pNv->Options, OPTION_FP_TWEAK, 
                             &pNv->PanelTweak))
    {
        pNv->usePanelTweak = TRUE;
    } else {
        pNv->usePanelTweak = FALSE;
    }
    
    if (pNv->pEnt->device->MemBase != 0) {
	/* Require that the config file value matches one of the PCI values. */
	if (!xf86CheckPciMemBase(pNv->PciInfo, pNv->pEnt->device->MemBase)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"MemBase 0x%08lX doesn't match any PCI base register.\n",
		pNv->pEnt->device->MemBase);
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
	pNv->VRAMPhysical = pNv->pEnt->device->MemBase;
	from = X_CONFIG;
    } else {
	if (pNv->PciInfo->memBase[1] != 0) {
	    pNv->VRAMPhysical = pNv->PciInfo->memBase[1] & 0xff800000;
	    from = X_PROBED;
	} else {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "No valid FB address in PCI config space\n");
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
    }
    xf86DrvMsg(pScrn->scrnIndex, from, "Linear framebuffer at 0x%lX\n",
	       (unsigned long)pNv->VRAMPhysical);

    if (pNv->pEnt->device->IOBase != 0) {
	/* Require that the config file value matches one of the PCI values. */
	if (!xf86CheckPciMemBase(pNv->PciInfo, pNv->pEnt->device->IOBase)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"IOBase 0x%08lX doesn't match any PCI base register.\n",
		pNv->pEnt->device->IOBase);
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
	pNv->IOAddress = pNv->pEnt->device->IOBase;
	from = X_CONFIG;
    } else {
	if (pNv->PciInfo->memBase[0] != 0) {
	    pNv->IOAddress = pNv->PciInfo->memBase[0] & 0xffffc000;
	    from = X_PROBED;
	} else {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"No valid MMIO address in PCI config space\n");
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
    }
    xf86DrvMsg(pScrn->scrnIndex, from, "MMIO registers at 0x%lX\n",
	       (unsigned long)pNv->IOAddress);
     
    if (xf86RegisterResources(pNv->pEnt->index, NULL, ResExclusive)) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"xf86RegisterResources() found resource conflicts\n");
	xf86FreeInt10(pNv->pInt);
	NVFreeRec(pScrn);
	return FALSE;
    }

    switch (pNv->Chipset & 0x0ff0) {
    case CHIPSET_NV03:   /* Riva128 */
         pNv->Architecture =  NV_ARCH_03;
         break;
    case CHIPSET_NV04:   /* TNT/TNT2 */
         pNv->Architecture =  NV_ARCH_04;
         break;
    case CHIPSET_NV10:   /* GeForce 256 */
    case CHIPSET_NV11:   /* GeForce2 MX */
    case CHIPSET_NV15:   /* GeForce2 */
    case CHIPSET_NV17:   /* GeForce4 MX */
    case CHIPSET_NV18:   /* GeForce4 MX (8x AGP) */
    case CHIPSET_NFORCE: /* nForce */
    case CHIPSET_NFORCE2:/* nForce2 */
         pNv->Architecture =  NV_ARCH_10;
         break;
    case CHIPSET_NV20:   /* GeForce3 */
    case CHIPSET_NV25:   /* GeForce4 Ti */
    case CHIPSET_NV28:   /* GeForce4 Ti (8x AGP) */
         pNv->Architecture =  NV_ARCH_20;
         break;
    case CHIPSET_NV30:   /* GeForceFX 5800 */
    case CHIPSET_NV31:   /* GeForceFX 5600 */
    case CHIPSET_NV34:   /* GeForceFX 5200 */
    case CHIPSET_NV35:   /* GeForceFX 5900 */
    case CHIPSET_NV36:   /* GeForceFX 5700 */
         pNv->Architecture =  NV_ARCH_30;
         break;
    case CHIPSET_NV40:   /* GeForce 6800 */
    case CHIPSET_NV41:   /* GeForce 6800 */
    case 0x0120:   /* GeForce 6800 */
    case CHIPSET_NV43:   /* GeForce 6600 */
    case CHIPSET_NV44:   /* GeForce 6200 */
    case CHIPSET_G72:   /* GeForce 7200, 7300, 7400 */
    case CHIPSET_G70:   /* GeForce 7800 */
    case CHIPSET_NV45:   /* GeForce 6800 */
    case CHIPSET_NV44A:   /* GeForce 6200 */
    case CHIPSET_G71:   /* GeForce 7900 */
    case CHIPSET_G73:   /* GeForce 7600 */
    case CHIPSET_C51:   /* GeForce 6100 */
    case CHIPSET_C512: /* Geforce 6100 (nForce 4xx) */
         pNv->Architecture =  NV_ARCH_40;
         break;
    default:           /* Unknown, probably >=NV40 */
         pNv->Architecture =  NV_ARCH_40;
         break;
    }

    pNv->alphaCursor = (pNv->Architecture >= NV_ARCH_10) &&
                       ((pNv->Chipset & 0x0ff0) != CHIPSET_NV10);


    /* Allocate an xf86CrtcConfig */
    xf86CrtcConfigInit (pScrn, &nv_xf86crtc_config_funcs);
    xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    
    max_width = 16384;
    xf86CrtcSetSizeRange (pScrn, 320, 200, max_width, 2048);
    
    if (NVPreInitDRI(pScrn) == FALSE) {
	    xf86FreeInt10(pNv->pInt);
	    return FALSE;
    }

    NVCommonSetup(pScrn);
    NVI2CInit(pScrn);

    num_crtc = pNv->twoHeads ? 2 : 1;
    for (i = 0; i < num_crtc; i++) {
        nv_crtc_init(pScrn, i);
    }

    NvSetupOutputs(pScrn);

#if 0
    /* Do an initial detection of the outputs while none are configured on yet.
     * This will give us some likely legitimate response for later if both
     * pipes are already allocated and we're asked to do a detect.
     */
    for (i = 0; i < xf86_config->num_output; i++) {
      xf86OutputPtr	      output = xf86_config->output[i];
      
      output->status = (*output->funcs->detect) (output);
    }
#endif
    
    if (!xf86InitialConfiguration (pScrn, FALSE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes.\n");
	return FALSE;
    }    


    pScrn->videoRam = pNv->RamAmountKBytes;
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "VideoRAM: %d kBytes\n",
               pScrn->videoRam);
	
	pNv->VRAMPhysicalSize = pScrn->videoRam * 1024;

    /*
     * If the driver can do gamma correction, it should call xf86SetGamma()
     * here.
     */

    {
	Gamma zeros = {0.0, 0.0, 0.0};

	if (!xf86SetGamma(pScrn, zeros)) {
	    xf86FreeInt10(pNv->pInt);
	    return FALSE;
	}
    }

    /*
     * Setup the ClockRanges, which describe what clock ranges are available,
     * and what sort of modes they can be used for.
     */

    clockRanges = xnfcalloc(sizeof(ClockRange), 1);
    clockRanges->next = NULL;
    clockRanges->minClock = pNv->MinVClockFreqKHz;
    clockRanges->maxClock = pNv->MaxVClockFreqKHz;
    clockRanges->clockIndex = -1;		/* programmable */
    clockRanges->doubleScanAllowed = TRUE;
    if((pNv->Architecture == NV_ARCH_20) ||
         ((pNv->Architecture == NV_ARCH_10) && 
           ((pNv->Chipset & 0x0ff0) != CHIPSET_NV10) &&
           ((pNv->Chipset & 0x0ff0) != CHIPSET_NV15)))
    {
       /* HW is broken */
       clockRanges->interlaceAllowed = FALSE;
    } else {
       clockRanges->interlaceAllowed = TRUE;
    }

    if(pNv->FlatPanel == 1) {
       clockRanges->interlaceAllowed = FALSE;
       clockRanges->doubleScanAllowed = FALSE;
    }

    if(pNv->Architecture < NV_ARCH_10) {
       max_width = (pScrn->bitsPerPixel > 16) ? 2032 : 2048;
       max_height = 2048;
    } else {
       max_width = (pScrn->bitsPerPixel > 16) ? 4080 : 4096;
       max_height = 4096;
    }

    /*
     * xf86ValidateModes will check that the mode HTotal and VTotal values
     * don't exceed the chipset's limit if pScrn->maxHValue and
     * pScrn->maxVValue are set.  Since our NVValidMode() already takes
     * care of this, we don't worry about setting them here.
     */
    i = xf86ValidateModes(pScrn, pScrn->monitor->Modes,
                          pScrn->display->modes, clockRanges,
                          NULL, 256, max_width,
                          512, 128, max_height,
                          pScrn->display->virtualX,
                          pScrn->display->virtualY,
                          pNv->VRAMPhysicalSize / 2,
                          LOOKUP_BEST_REFRESH);

    if (i == -1) {
	xf86FreeInt10(pNv->pInt);
	NVFreeRec(pScrn);
	return FALSE;
    }

    /* Prune the modes marked as invalid */
    xf86PruneDriverModes(pScrn);

    if (i == 0 || pScrn->modes == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
	xf86FreeInt10(pNv->pInt);
	NVFreeRec(pScrn);
	return FALSE;
    }

    /*
     * Set the CRTC parameters for all of the modes based on the type
     * of mode, and the chipset's interlace requirements.
     *
     * Calling this is required if the mode->Crtc* values are used by the
     * driver and if the driver doesn't provide code to set them.  They
     * are not pre-initialised at all.
     */
    xf86SetCrtcForModes(pScrn, 0);

    /* Set the current mode to the first in the list */
    pScrn->currentMode = pScrn->modes;

    /* Print the list of modes being used */
    xf86PrintModes(pScrn);

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);


    /*
     * XXX This should be taken into account in some way in the mode valdation
     * section.
     */

    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
	xf86FreeInt10(pNv->pInt);
	NVFreeRec(pScrn);
	return FALSE;
    }

    xf86LoaderReqSymLists(fbSymbols, NULL);
    
    /* Load XAA if needed */
    if (!pNv->NoAccel) {
	if (!xf86LoadSubModule(pScrn, pNv->useEXA ? "exa" : "xaa")) {
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
	xf86LoaderReqSymLists(xaaSymbols, NULL);
    }

    /* Load ramdac if needed */
    if (pNv->HWCursor) {
	if (!xf86LoadSubModule(pScrn, "ramdac")) {
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
	xf86LoaderReqSymLists(ramdacSymbols, NULL);
    }

    /* Load shadowfb if needed */
    if (pNv->ShadowFB) {
	if (!xf86LoadSubModule(pScrn, "shadowfb")) {
	    xf86FreeInt10(pNv->pInt);
	    NVFreeRec(pScrn);
	    return FALSE;
	}
	xf86LoaderReqSymLists(shadowSymbols, NULL);
    }

    pNv->CurrentLayout.bitsPerPixel = pScrn->bitsPerPixel;
    pNv->CurrentLayout.depth = pScrn->depth;
    pNv->CurrentLayout.displayWidth = pScrn->displayWidth;
    pNv->CurrentLayout.weight.red = pScrn->weight.red;
    pNv->CurrentLayout.weight.green = pScrn->weight.green;
    pNv->CurrentLayout.weight.blue = pScrn->weight.blue;
    pNv->CurrentLayout.mode = pScrn->currentMode;

    if (pScrn->modes == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
	PreInitCleanup(pScrn);
	return FALSE;
    }

    pScrn->currentMode = pScrn->modes;

    xf86FreeInt10(pNv->pInt);

    pNv->pInt = NULL;
    return TRUE;
}


/*
 * Map the framebuffer and MMIO memory.
 */

static Bool
NVMapMem(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	pNv->FB = NVAllocateMemory(pNv, NOUVEAU_MEM_FB, pNv->VRAMPhysicalSize/2);
	if (!pNv->FB) {
		ErrorF("Failed to allocate memory for framebuffer!\n");
		return FALSE;
	}
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Allocated %dMiB VRAM for framebuffer + offscreen pixmaps\n",
		   (unsigned int)(pNv->FB->size >> 20));

	/*XXX: have to get these after we've allocated something, otherwise
	 *     they're uninitialised in the DRM!
	 */
	pNv->VRAMSize     = NVDRMGetParam(pNv, NOUVEAU_GETPARAM_FB_SIZE);
	pNv->VRAMPhysical = NVDRMGetParam(pNv, NOUVEAU_GETPARAM_FB_PHYSICAL);
	pNv->AGPSize      = NVDRMGetParam(pNv, NOUVEAU_GETPARAM_AGP_SIZE);
	pNv->AGPPhysical  = NVDRMGetParam(pNv, NOUVEAU_GETPARAM_AGP_PHYSICAL);

	if (pNv->AGPSize) {
		int gart_scratch_size;

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "GART: %dMiB available\n",
			   (unsigned int)(pNv->AGPSize >> 20));

		if (pNv->AGPSize > (16*1024*1024))
			gart_scratch_size = 16*1024*1024;
		else
			gart_scratch_size = pNv->AGPSize;

		pNv->AGPScratch = NVAllocateMemory(pNv, NOUVEAU_MEM_AGP,
							gart_scratch_size);
		if (!pNv->AGPScratch) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Unable to allocate GART memory\n");
		} else {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				   "GART: mapped %dMiB at %p\n",
				   (unsigned int)(pNv->AGPScratch->size >> 20),
				   pNv->AGPScratch->map);
		}
	}

	pNv->Cursor = NVAllocateMemory(pNv, NOUVEAU_MEM_FB, 64*1024);
	if (!pNv->Cursor) {
		ErrorF("Failed to allocate memory for hardware cursor\n");
		return FALSE;
	}
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
 		   "Allocated %dKiB VRAM for cursor\n",
		   pNv->Cursor->size >> 10
		   );

	pNv->ScratchBuffer = NVAllocateMemory(pNv, NOUVEAU_MEM_FB,
			pNv->Architecture <NV_ARCH_10 ? 8192 : 16384);
	if (!pNv->ScratchBuffer) {
		ErrorF("Failed to allocate memory for scratch buffer\n");
		return FALSE;
	}

    return TRUE;
}

/*
 * Unmap the framebuffer and MMIO memory.
 */

static Bool
NVUnmapMem(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	NVFreeMemory(pNv, pNv->FB);
	NVFreeMemory(pNv, pNv->ScratchBuffer);
	NVFreeMemory(pNv, pNv->Cursor);

    return TRUE;
}


/*
 * Initialise a new mode. 
 */

/*
 * Restore the initial (text) mode.
 */
static void 
NVRestore(ScrnInfoPtr pScrn)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    vgaRegPtr vgaReg = &hwp->SavedReg;
    NVPtr pNv = NVPTR(pScrn);
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    NVRegPtr nvReg = &pNv->SavedReg;
    int i;
    int vgaflags = VGA_SR_CMAP | VGA_SR_MODE;

    NVCrtcLockUnlock(xf86_config->crtc[0], 0);
    NVCrtcLockUnlock(xf86_config->crtc[1], 0);

    for (i = 0; i < xf86_config->num_crtc; i++) {
	xf86_config->crtc[i]->funcs->restore(xf86_config->crtc[i]);
    }

    for (i = 0; i< xf86_config->num_output; i++) {
	xf86_config->output[i]->funcs->restore(xf86_config->output[i]);
    }


#ifndef __powerpc__
   vgaflags |= VGA_SR_FONTS;
#endif
    vgaHWRestore(pScrn, vgaReg, vgaflags);

    vgaHWLock(hwp);
    NVCrtcLockUnlock(xf86_config->crtc[0], 1);
    NVCrtcLockUnlock(xf86_config->crtc[1], 1);


}

#define DEPTH_SHIFT(val, w) ((val << (8 - w)) | (val >> ((w << 1) - 8)))
#define MAKE_INDEX(in, w) (DEPTH_SHIFT(in, w) * 3)

static void
NVLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
	      LOCO * colors, VisualPtr pVisual)
{
  xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
  int c;
  NVPtr pNv = NVPTR(pScrn);
  int i, index;

  for (c = 0; c < xf86_config->num_crtc; c++){
    xf86CrtcPtr crtc = xf86_config->crtc[c];
    NVCrtcPrivatePtr nv_crtc = crtc->driver_private;
    NVCrtcRegPtr regp;

    regp = &pNv->ModeReg.crtc_reg[nv_crtc->crtc];
    
    if (crtc->enabled == 0)
      continue;

    switch(pNv->CurrentLayout.depth) {
    case 15:
      for(i = 0; i < numColors; i++) {
	index = indices[i];
	regp->DAC[MAKE_INDEX(index, 5) + 0] = colors[index].red;
	regp->DAC[MAKE_INDEX(index, 5) + 1] = colors[index].green;
	regp->DAC[MAKE_INDEX(index, 5) + 2] = colors[index].blue;
      }
      break;
    case 16:
      for(i = 0; i < numColors; i++) {
	index = indices[i];
	regp->DAC[MAKE_INDEX(index, 6) + 1] = colors[index].green;
	if(index < 32) {
	  regp->DAC[MAKE_INDEX(index, 5) + 0] = colors[index].red;
	  regp->DAC[MAKE_INDEX(index, 5) + 2] = colors[index].blue;
	}
      }
      break;
    default:
      for(i = 0; i < numColors; i++) {
	index = indices[i];
	regp->DAC[index*3]     = colors[index].red;
	regp->DAC[(index*3)+1] = colors[index].green;
	regp->DAC[(index*3)+2] = colors[index].blue;
      }
      break;
    }

    NVCrtcLoadPalette(crtc);
  }
}

/* Mandatory */

/* This gets called at the start of each server generation */

static Bool
NVScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn;
    vgaHWPtr hwp;
    NVPtr pNv;
    int ret;
    VisualPtr visual;
    unsigned char *FBStart;
    int width, height, displayWidth, offscreenHeight, shadowHeight;
    BoxRec AvailFBArea;

    /* 
     * First get the ScrnInfoRec
     */
    pScrn = xf86Screens[pScreen->myNum];

    hwp = VGAHWPTR(pScrn);
    pNv = NVPTR(pScrn);

    /* Map the VGA memory when the primary video */
    if (pNv->Primary) {
	hwp->MapSize = 0x10000;
	if (!vgaHWMapMem(pScrn))
	    return FALSE;
    }

	/* First init DRI/DRM */
	if (!NVDRIScreenInit(pScrn))
		return FALSE;

	/* Allocate and map memory areas we need */
	if (!NVMapMem(pScrn))
		return FALSE;

	/* Init DRM - Alloc FIFO */
	if (!NVInitDma(pScrn))
		return FALSE;

	/* setup graphics objects */
	if (!NVAccelCommonInit(pScrn))
		return FALSE;

	pScrn->memPhysBase = pNv->VRAMPhysical;
	pScrn->fbOffset = 0;
	
	if (!NVEnterVT(scrnIndex, 0))
	  return FALSE;

    /* Darken the screen for aesthetic reasons and set the viewport */
	//    NVSaveScreen(pScreen, SCREEN_SAVER_ON);
    //    pScrn->AdjustFrame(scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);

    /*
     * The next step is to setup the screen's visuals, and initialise the
     * framebuffer code.  In cases where the framebuffer's default
     * choices for things like visual layouts and bits per RGB are OK,
     * this may be as simple as calling the framebuffer's ScreenInit()
     * function.  If not, the visuals will need to be setup before calling
     * a fb ScreenInit() function and fixed up after.
     *
     * For most PC hardware at depths >= 8, the defaults that fb uses
     * are not appropriate.  In this driver, we fixup the visuals after.
     */

    /*
     * Reset the visual list.
     */
    miClearVisualTypes();

    /* Setup the visuals we support. */

    if (!miSetVisualTypes(pScrn->depth, 
                          miGetDefaultVisualMask(pScrn->depth), 8,
                          pScrn->defaultVisual))
	  return FALSE;
    if (!miSetPixmapDepths ()) return FALSE;

    /*
     * Call the framebuffer layer's ScreenInit function, and fill in other
     * pScreen fields.
     */

    width = pScrn->virtualX;
    height = pScrn->virtualY;
    displayWidth = pScrn->displayWidth;


    if(pNv->Rotate) {
	height = pScrn->virtualX;
	width = pScrn->virtualY;
    }

    /* If RandR rotation is enabled, leave enough space in the
     * framebuffer for us to rotate the screen dimensions without
     * changing the pitch.
     */
    if(pNv->RandRRotation)
        shadowHeight = max(width, height);
    else
        shadowHeight = height;

    if(pNv->ShadowFB) {
 	pNv->ShadowPitch = BitmapBytePad(pScrn->bitsPerPixel * width);
        pNv->ShadowPtr = xalloc(pNv->ShadowPitch * shadowHeight);
	displayWidth = pNv->ShadowPitch / (pScrn->bitsPerPixel >> 3);
        FBStart = pNv->ShadowPtr;
    } else {
	pNv->ShadowPtr = NULL;
	FBStart = pNv->FB->map;
    }

    switch (pScrn->bitsPerPixel) {
        case 8:
        case 16:
        case 32:
            ret = fbScreenInit(pScreen, FBStart, width, height,
                               pScrn->xDpi, pScrn->yDpi,
                               displayWidth, pScrn->bitsPerPixel);
            break;
        default:
            xf86DrvMsg(scrnIndex, X_ERROR,
                       "Internal error: invalid bpp (%d) in NVScreenInit\n",
                       pScrn->bitsPerPixel);
            ret = FALSE;
            break;
    }
    if (!ret)
	return FALSE;

    if (pScrn->bitsPerPixel > 8) {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
	    if ((visual->class | DynamicClass) == DirectColor) {
		visual->offsetRed = pScrn->offset.red;
		visual->offsetGreen = pScrn->offset.green;
		visual->offsetBlue = pScrn->offset.blue;
		visual->redMask = pScrn->mask.red;
		visual->greenMask = pScrn->mask.green;
		visual->blueMask = pScrn->mask.blue;
	    }
	}
    }

    fbPictureInit (pScreen, 0, 0);
    
    xf86SetBlackWhitePixels(pScreen);

    offscreenHeight = pNv->FB->size /
                     (pScrn->displayWidth * pScrn->bitsPerPixel >> 3);
    if(offscreenHeight > 32767)
        offscreenHeight = 32767;

    if (!pNv->useEXA) {
    AvailFBArea.x1 = 0;
    AvailFBArea.y1 = 0;
    AvailFBArea.x2 = pScrn->displayWidth;
    AvailFBArea.y2 = offscreenHeight;
    xf86InitFBManager(pScreen, &AvailFBArea);
    }
    
    if (!pNv->NoAccel) {
        if (pNv->useEXA)
            NVExaInit(pScreen);
        else /* XAA */
            NVXaaInit(pScreen);
    }
    NVResetGraphics(pScrn);
    
    miInitializeBackingStore(pScreen);
    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

    /* Finish DRI init */
    NVDRIFinishScreenInit(pScrn);

    /* Initialize software cursor.  
	Must precede creation of the default colormap */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* Initialize HW cursor layer. 
	Must follow software cursor initialization*/
    if (pNv->HWCursor) { 
	if(!NVCursorInit(pScreen))
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, 
		"Hardware cursor initialization failed\n");
    }

    /* Initialise default colourmap */
    if (!miCreateDefColormap(pScreen))
	return FALSE;

    /* Initialize colormap layer.  
	Must follow initialization of the default colormap */
    if(!xf86HandleColormaps(pScreen, 256, 8, NVLoadPalette,
	NULL, CMAP_RELOAD_ON_MODE_SWITCH | CMAP_PALETTED_TRUECOLOR))
	return FALSE;


    xf86DPMSInit(pScreen, xf86DPMSSet, 0);

   if (!xf86CrtcScreenInit (pScreen))
       return FALSE;

    pNv->PointerMoved = pScrn->PointerMoved;
    pScrn->PointerMoved = NVPointerMoved;

    if(pNv->ShadowFB) {
	RefreshAreaFuncPtr refreshArea = NVRefreshArea;

	if(pNv->Rotate || pNv->RandRRotation) {
	   pNv->PointerMoved = pScrn->PointerMoved;
	   if(pNv->Rotate)
	       pScrn->PointerMoved = NVPointerMoved;

	   switch(pScrn->bitsPerPixel) {
               case 8:	refreshArea = NVRefreshArea8;	break;
               case 16:	refreshArea = NVRefreshArea16;	break;
               case 32:	refreshArea = NVRefreshArea32;	break;
	   }
           if(!pNv->RandRRotation) {
               xf86DisableRandR();
               xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                          "Driver rotation enabled, RandR disabled\n");
           }
	}

	ShadowFBInit(pScreen, refreshArea);
    }

    pScrn->memPhysBase = pNv->VRAMPhysical;
    pScrn->fbOffset = 0;

    if(pNv->Rotate == 0 && !pNv->RandRRotation)
       NVInitVideo(pScreen);

    pScreen->SaveScreen = NVSaveScreen;

    /* Wrap the current CloseScreen function */
    pNv->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = NVCloseScreen;

    pNv->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = NVBlockHandler;

#if 0 //def RANDR
    /* Install our DriverFunc.  We have to do it this way instead of using the
     * HaveDriverFuncs argument to xf86AddDriver, because InitOutput clobbers
     * pScrn->DriverFunc */
    pScrn->DriverFunc = NVDriverFunc;
#endif

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1) {
	xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }
    return TRUE;
}

static Bool
NVSaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    NVPtr pNv = NVPTR(pScrn);
    int i;
    Bool on = xf86IsUnblank(mode);

    if (pScrn->vtSema) {
	for (i = 0; i < xf86_config->num_crtc; i++) {
	    
	    if (xf86_config->crtc[i]->enabled) {
		NVCrtcBlankScreen(xf86_config->crtc[i], on);
	    }
	}

    }
    return TRUE;
    
}

static void
NVSave(ScrnInfoPtr pScrn)
{
    NVPtr pNv = NVPTR(pScrn);
    NVRegPtr nvReg = &pNv->SavedReg;
    vgaHWPtr pVga = VGAHWPTR(pScrn);
    vgaRegPtr vgaReg = &pVga->SavedReg;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;
    int vgaflags = VGA_SR_CMAP | VGA_SR_MODE;
    
    for (i = 0; i < xf86_config->num_crtc; i++) {
	xf86_config->crtc[i]->funcs->save(xf86_config->crtc[i]);
    }


    for (i = 0; i< xf86_config->num_output; i++) {
	xf86_config->output[i]->funcs->save(xf86_config->output[i]);
    }

   vgaHWUnlock(pVga);
#ifndef __powerpc__
   vgaflags |= VGA_SR_FONTS;
#endif
   vgaHWSave(pScrn, vgaReg, vgaflags);
}

#ifdef RANDR
static Bool
NVRandRGetInfo(ScrnInfoPtr pScrn, Rotation *rotations)
{
    NVPtr pNv = NVPTR(pScrn);

    if(pNv->RandRRotation)
       *rotations = RR_Rotate_0 | RR_Rotate_90 | RR_Rotate_270;
    else
       *rotations = RR_Rotate_0;

    return TRUE;
}

static Bool
NVRandRSetConfig(ScrnInfoPtr pScrn, xorgRRConfig *config)
{
    NVPtr pNv = NVPTR(pScrn);

    switch(config->rotation) {
        case RR_Rotate_0:
            pNv->Rotate = 0;
            pScrn->PointerMoved = pNv->PointerMoved;
            break;

        case RR_Rotate_90:
            pNv->Rotate = -1;
            pScrn->PointerMoved = NVPointerMoved;
            break;

        case RR_Rotate_270:
            pNv->Rotate = 1;
            pScrn->PointerMoved = NVPointerMoved;
            break;

        default:
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Unexpected rotation in NVRandRSetConfig!\n");
            pNv->Rotate = 0;
            pScrn->PointerMoved = pNv->PointerMoved;
            return FALSE;
    }

    return TRUE;
}

static Bool
NVDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer data)
{
    switch(op) {
       case RR_GET_INFO:
          return NVRandRGetInfo(pScrn, (Rotation*)data);
       case RR_SET_CONFIG:
          return NVRandRSetConfig(pScrn, (xorgRRConfig*)data);
       default:
          return FALSE;
    }

    return FALSE;
}
#endif

