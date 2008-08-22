/*
 * Copyright 1996-1997 David J. McKay
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

#include <stdio.h>

#include "nv_include.h"

#include "xf86int10.h"

#include "xf86drm.h"

/*
 * Forward definitions for the functions that make up the driver.
 */
/* Mandatory functions */
static const OptionInfoRec * NVAvailableOptions(int chipid, int busid);
static void    NVIdentify(int flags);
#ifndef XSERVER_LIBPCIACCESS
static Bool    NVProbe(DriverPtr drv, int flags);
#endif /* XSERVER_LIBPCIACCESS */
static Bool    NVPreInit(ScrnInfoPtr pScrn, int flags);
static Bool    NVScreenInit(int Index, ScreenPtr pScreen, int argc,
                            char **argv);
static Bool    NVEnterVT(int scrnIndex, int flags);
static void    NVLeaveVT(int scrnIndex, int flags);
static Bool    NVCloseScreen(int scrnIndex, ScreenPtr pScreen);
static Bool    NVSaveScreen(ScreenPtr pScreen, int mode);

/* Optional functions */
static Bool    NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
static void    NVAdjustFrame(int scrnIndex, int x, int y, int flags);
static void    NVFreeScreen(int scrnIndex, int flags);
static ModeStatus NVValidMode(int scrnIndex, DisplayModePtr mode,
			      Bool verbose, int flags);

/* Internally used functions */

static Bool	NVMapMem(ScrnInfoPtr pScrn);
static Bool	NVUnmapMem(ScrnInfoPtr pScrn);
static void	NVSave(ScrnInfoPtr pScrn);
static void	NVRestore(ScrnInfoPtr pScrn);
static Bool	NVModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode);

#ifdef XSERVER_LIBPCIACCESS

#define NOUVEAU_PCI_DEVICE(_vendor_id, _device_id) \
	{ (_vendor_id), (_device_id), PCI_MATCH_ANY, PCI_MATCH_ANY, 0x00030000, 0x00ffffff, 0 }

static const struct pci_id_match nouveau_device_match[] = {
	NOUVEAU_PCI_DEVICE(PCI_VENDOR_NVIDIA, PCI_MATCH_ANY),
	NOUVEAU_PCI_DEVICE(PCI_VENDOR_NVIDIA_SGS, PCI_MATCH_ANY),
	{ 0, 0, 0 },
};

static Bool NVPciProbe (	DriverPtr 		drv,
				int 			entity_num,
				struct pci_device	*dev,
				intptr_t		match_data	);

#endif /* XSERVER_LIBPCIACCESS */

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
#ifdef XSERVER_LIBPCIACCESS
	NULL,
#else
	NVProbe,
#endif /* XSERVER_LIBPCIACCESS */
	NVAvailableOptions,
	NULL,
	0,
	NULL,
#ifdef XSERVER_LIBPCIACCESS
	nouveau_device_match,
	NVPciProbe
#endif /* XSERVER_LIBPCIACCESS */
};

struct NvFamily
{
  char *name;
  char *chipset;
};

static struct NvFamily NVKnownFamilies[] =
{
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
  { "GeForce 8",   "G8x" },
  { NULL, NULL}
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
    "xf86ExecX86int10",
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
	NVPtr pNv = NVPTR(pScrn);
	if (pNv->Architecture == NV_ARCH_50 && !pNv->kms_enable) {
		NV50ConnectorDestroy(pScrn);
		NV50OutputDestroy(pScrn);
		NV50CrtcDestroy(pScrn);
	}
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
		/* The 1 here is needed to turn off a backwards compatibility mode */
		/* Otherwise NVPciProbe() is not called */
		xf86AddDriver(&NV, module, 1);

		/*
		 * Modules that this driver always requires may be loaded here
		 * by calling LoadSubModule().
		 */
		/*
		 * Tell the loader about symbols from other modules that this module
		 * might refer to.
		 */
		LoaderRefSymLists(vgahwSymbols, exaSymbols, fbSymbols,
				ramdacSymbols, shadowSymbols, drmSymbols, 
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


#ifndef XSERVER_LIBPCIACCESS
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

    pScrn->Probe = NVProbe;
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
#endif

/* This returns architecture in hexdecimal, so NV40 is 0x40 */
static int NVGetArchitecture(volatile uint32_t *regs)
{
	int architecture = 0;

	/* We're dealing with >=NV10 */
	if ((regs[0] & 0x0f000000) > 0 )
		/* Bit 27-20 contain the architecture in hex */
		architecture = (regs[0] & 0xff00000) >> 20;
	/* NV04 or NV05 */
	else if ((regs[0] & 0xff00fff0) == 0x20004000)
		architecture = 0x04;

	return architecture;
}

/* Reading the pci_id from the card registers is the most reliable way */
static uint32_t NVGetPCIID(volatile uint32_t *regs)
{
	int architecture = NVGetArchitecture(regs);
	uint32_t pci_id;

	/* Dealing with an unknown or unsupported card */
	if (architecture == 0)
		return 0;

	if (architecture >= 0x40)
		pci_id = regs[0x88000/4];
	else
		pci_id = regs[0x1800/4];

	/* A pci-id can be inverted, we must correct this */
	if ((pci_id & 0xffff) == PCI_VENDOR_NVIDIA)
		pci_id = (PCI_VENDOR_NVIDIA << 16) | (pci_id >> 16);
	else if ((pci_id & 0xffff) == PCI_VENDOR_NVIDIA_SGS)
		pci_id = (PCI_VENDOR_NVIDIA_SGS << 16) | (pci_id >> 16);
	/* Checking endian issues */
	else {
		/* PCI_VENDOR_NVIDIA = 0x10DE */
		if ((pci_id & (0xffff << 16)) == (0xDE10 << 16)) /* wrong endian */
			pci_id = (PCI_VENDOR_NVIDIA << 16) | ((pci_id << 8) & 0x0000ff00) |
				((pci_id >> 8) & 0x000000ff);
		/* PCI_VENDOR_NVIDIA_SGS = 0x12D2 */
		else if ((pci_id & (0xffff << 16)) == (0xD212 << 16)) /* wrong endian */
			pci_id = (PCI_VENDOR_NVIDIA_SGS << 16) | ((pci_id << 8) & 0x0000ff00) |
				((pci_id >> 8) & 0x000000ff);
	}

	return pci_id;
}

#ifdef XSERVER_LIBPCIACCESS

static Bool NVPciProbe (	DriverPtr 		drv,
				int 			entity_num,
				struct pci_device	*dev,
				intptr_t		match_data	)
{
	ScrnInfoPtr pScrn = NULL;

	volatile uint32_t *regs = NULL;

	/* Temporary mapping to discover the architecture */
	pci_device_map_range(dev, PCI_DEV_MEM_BASE(dev, 0), 0x90000, 0,
			     (void *) &regs);

	uint8_t architecture = NVGetArchitecture(regs);

	CARD32 pci_id = NVGetPCIID(regs);

	pci_device_unmap_range(dev, (void *) regs, 0x90000);

	/* Currently NV04 up to NV98 is known. */
	/* Using 0x9F as upper bound for some margin. */
	if (architecture >= 0x04 && architecture <= 0x9F) {

		/* At this stage the pci_id should be ok, so we generate this
		 * to avoid list duplication */
		/* AGP bridge chips need their bridge chip id to be detected */
		PciChipsets NVChipsets[] = {
			{ pci_id, PCI_DEV_PCI_ID(dev), RES_SHARED_VGA },
			{ -1, -1, RES_UNDEFINED }
		};

		pScrn = xf86ConfigPciEntity(pScrn, 0, entity_num, NVChipsets, 
						NULL, NULL, NULL, NULL, NULL);

		if (pScrn != NULL) {
			pScrn->driverVersion    = NV_VERSION;
			pScrn->driverName       = NV_DRIVER_NAME;
			pScrn->name             = NV_NAME;

			pScrn->Probe            = NULL;
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
	}

	return FALSE;
}

#endif /* XSERVER_LIBPCIACCESS */

#define MAX_CHIPS MAXSCREENS

#ifndef XSERVER_LIBPCIACCESS
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
		if (((*ppPci)->vendor == PCI_VENDOR_NVIDIA_SGS) || 
			((*ppPci)->vendor == PCI_VENDOR_NVIDIA)) 
		{
			volatile uint32_t *regs;
			uint32_t pcicmd;

			PCI_DEV_READ_LONG(*ppPci, PCI_CMD_STAT_REG, &pcicmd);
			/* Enable reading memory? */
			PCI_DEV_WRITE_LONG(*ppPci, PCI_CMD_STAT_REG, pcicmd | PCI_CMD_MEM_ENABLE);

			regs = xf86MapPciMem(-1, VIDMEM_MMIO, PCI_DEV_TAG(*ppPci), PCI_DEV_MEM_BASE(*ppPci, 0), 0x90000);
			int pciid = NVGetPCIID(regs);

			int architecture = NVGetArchitecture(regs);
			char name[25];
			sprintf(name, "NVIDIA NV%02X", architecture);
			/* NV04 upto NV98 is known. */
			if (architecture >= 0x04 && architecture <= 0x9F) {
				NVChipsets[numUsed].token = pciid;
				NVChipsets[numUsed].name = name;
				NVPciChipsets[numUsed].numChipset = pciid;
				/* AGP bridge chips need their bridge chip id to be detected */
				NVPciChipsets[numUsed].PCIid = PCI_DEV_PCI_ID(*ppPci);
				NVPciChipsets[numUsed].resList = RES_SHARED_VGA;
				numUsed++;
			}
			xf86UnMapVidMem(-1, (pointer)regs, 0x90000);

			/* Reset previous state */
			PCI_DEV_WRITE_LONG(*ppPci, PCI_CMD_STAT_REG, pcicmd);
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

	if (numUsed <= 0) {
		return FALSE;
	}

	if (flags & PROBE_DETECT) {
		foundScreen = TRUE;
	} else {
		for (i = 0; i < numUsed; i++) {
			pciVideoPtr pPci;

			pPci = xf86GetPciInfoForEntity(usedChips[i]);
			if (NVGetScrnInfoRec(NVPciChipsets, usedChips[i])) {
				foundScreen = TRUE;
			}
		}
	}

	xfree(devSections);
	xfree(usedChips);

	return foundScreen;
}
#endif /* XSERVER_LIBPCIACCESS */

Bool
NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	NVPtr pNv = NVPTR(pScrn);

	if (pNv->randr12_enable)
		return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);

	return NVModeInit(xf86Screens[scrnIndex], mode);
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
	NVPtr pNv = NVPTR(pScrn);

	if (pNv->randr12_enable) {
		xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
		xf86CrtcPtr crtc = config->output[config->compat_output]->crtc;

		if (crtc && crtc->enabled)
			NVCrtcSetBase(crtc, x, y);
	} else {
		int startAddr;
		startAddr = (((y*pScrn->displayWidth)+x)*(pScrn->bitsPerPixel/8));
		startAddr += pNv->FB->offset;
		NVSetStartAddress(pNv, startAddr);
	}
}

static Bool
NV50AcquireDisplay(ScrnInfoPtr pScrn)
{
	if (!NV50DispInit(pScrn))
		return FALSE;
	if (!NV50CursorAcquire(pScrn))
		return FALSE;
	xf86SetDesiredModes(pScrn);

	return TRUE;
}

static Bool
NV50ReleaseDisplay(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	NV50CursorRelease(pScrn);
	NV50DispShutdown(pScrn);

	if (pNv->pInt10 && pNv->Int10Mode) {
		xf86Int10InfoPtr pInt10 = pNv->pInt10;

		pInt10->num = 0x10;
		pInt10->ax  = 0x4f02;
		pInt10->bx  = pNv->Int10Mode | 0x8000;
		pInt10->cx  =
		pInt10->dx  = 0;
		xf86ExecX86int10(pInt10);
	}

	return TRUE;
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
	NVPtr pNv = NVPTR(pScrn);

	if (!pNv->kms_enable) {
		if (pNv->randr12_enable) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NVEnterVT is called.\n");
			xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
			int i;
			pScrn->vtSema = TRUE;

			if (pNv->Architecture == NV_ARCH_50) {
				if (!NV50AcquireDisplay(pScrn))
					return FALSE;
				return TRUE;
			}

			/* Save the current state */
			if (pNv->SaveGeneration != serverGeneration) {
				pNv->SaveGeneration = serverGeneration;
				NVSave(pScrn);
			}

			for (i = 0; i < xf86_config->num_crtc; i++) {
				NVCrtcLockUnlock(xf86_config->crtc[i], 0);
			}

			if (!xf86SetDesiredModes(pScrn))
				return FALSE;
		} else {
			if (!NVModeInit(pScrn, pScrn->currentMode))
				return FALSE;

			NVAdjustFrame(scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
		}
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NVEnterVT is called.\n");
		if (!xf86SetDesiredModes(pScrn)) {
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "xf86SetDesiredModes failed\n");
			return FALSE;
		}
	}

	if (pNv->overlayAdaptor && pNv->Architecture != NV_ARCH_04)
		NV10WriteOverlayParameters(pScrn);

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
	if (pNv->randr12_enable)
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NVLeaveVT is called.\n");

	NVSync(pScrn);

	if (pNv->kms_enable)
		return;

	if (pNv->Architecture == NV_ARCH_50) {
		NV50ReleaseDisplay(pScrn);
		return;
	}

	NVRestore(pScrn);
	if (!pNv->randr12_enable)
		NVLockUnlock(pScrn, 1);
}

static void 
NVBlockHandler (
	int i, 
	pointer blockData, 
	pointer pTimeout,
	pointer pReadmask
)
{
	ScreenPtr pScreen = screenInfo.screens[i];
	ScrnInfoPtr pScrnInfo = xf86Screens[i];
	NVPtr pNv = NVPTR(pScrnInfo);

	if (!pNv->NoAccel)
		FIRE_RING (pNv->chan);

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
#ifdef XF86DRM_MODE
		if (pNv->kms_enable) {
			NVSync(pScrn);
		} else
#endif
		if (pNv->Architecture == NV_ARCH_50) {
			NV50ReleaseDisplay(pScrn);
		} else {
			if (pNv->randr12_enable)
				xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NVCloseScreen is called.\n");
			NVSync(pScrn);
			NVRestore(pScrn);
			if (!pNv->randr12_enable)
				NVLockUnlock(pScrn, 1);
		}
	}

	if (pNv->pInt10)
		xf86FreeInt10(pNv->pInt10);

	NVUnmapMem(pScrn);
	vgaHWUnmapMem(pScrn);
	NVDRICloseScreen(pScrn);
	if (pNv->CursorInfoRec)
		xf86DestroyCursorInfoRec(pNv->CursorInfoRec);
	if (pNv->ShadowPtr) {
		xfree(pNv->ShadowPtr);
		pNv->ShadowPtr = NULL;
	}
	if (pNv->overlayAdaptor) {
		xfree(pNv->overlayAdaptor);
		pNv->overlayAdaptor = NULL;
	}
	if (pNv->blitAdaptor) {
		xfree(pNv->blitAdaptor);
		pNv->blitAdaptor = NULL;
	}
	if (pNv->textureAdaptor[0]) {
		xfree(pNv->textureAdaptor[0]);
		pNv->textureAdaptor[0] = NULL;
	}
	if (pNv->textureAdaptor[1]) {
		xfree(pNv->textureAdaptor[1]);
		pNv->textureAdaptor[1] = NULL;
	}
	if (pNv->EXADriverPtr) {
		exaDriverFini(pScreen);
		xfree(pNv->EXADriverPtr);
		pNv->EXADriverPtr = NULL;
	}

	pScrn->vtSema = FALSE;
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

Bool NVI2CInit(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	if (xf86LoadSubModule(pScrn, "i2c") && xf86LoadSubModule(pScrn, "ddc")) {
		xf86LoaderReqSymLists(i2cSymbols,NULL);
		xf86LoaderReqSymLists(ddcSymbols, NULL);

		/* randr-1.2 clients have their DDCs initialized elsewhere */
		if (!pNv->randr12_enable)
			return NVDACi2cInit(pScrn);
		return true;
	} else
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		"Couldn't load i2c and ddc modules.  DDC probing can't be done\n");
	return false;
}

#ifdef XF86DRM_MODE
static bool nouveau_kernel_modesetting_enabled(ScrnInfoPtr pScrn)
{
#if XSERVER_LIBPCIACCESS
	struct pci_device *PciInfo;
#else
	pciVideoPtr PciInfo;
#endif
	EntityInfoPtr pEnt;
	char *busIdString;
	int ret;

	pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
	PciInfo = xf86GetPciInfoForEntity(pEnt->index);

	busIdString = DRICreatePCIBusID(PciInfo);

	ret = drmCheckModesettingSupported(busIdString);
	xfree(busIdString);
	if (ret)
		return FALSE;

	return TRUE;
}
#else
#define nouveau_kernel_modesetting_enabled(x) FALSE
#endif

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
nv_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "nv_xf86crtc_resize is called with %dx%d resolution.\n", width, height);
	pScrn->virtualX = width;
	pScrn->virtualY = height;
	return TRUE;
}

static const xf86CrtcConfigFuncsRec nv_xf86crtc_config_funcs = {
	nv_xf86crtc_resize
};

#define NVPreInitFail(fmt, args...) do {                                    \
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%d: "fmt, __LINE__, ##args); \
	if (pNv->pInt10)                                                    \
		xf86FreeInt10(pNv->pInt10);                                 \
	NVFreeRec(pScrn);                                                   \
	return FALSE;                                                       \
} while(0)

/* Mandatory */
Bool
NVPreInit(ScrnInfoPtr pScrn, int flags)
{
	xf86CrtcConfigPtr xf86_config;
	NVPtr pNv;
	MessageType from;
	int i, max_width, max_height;
	ClockRangePtr clockRanges;
	int config_mon_rates = FALSE;

	if (flags & PROBE_DETECT) {
		EntityInfoPtr pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

		if (!pEnt)
			return FALSE;

		i = pEnt->index;
		xfree(pEnt);

		if (xf86LoadSubModule(pScrn, "vbe")) {
			vbeInfoPtr pVbe = VBEInit(NULL, i);
			ConfiguredMonitor = vbeDoEDID(pVbe, NULL);
			vbeFree(pVbe);
		}

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
#ifndef XSERVER_LIBPCIACCESS
	pNv->PciTag = pciTag(pNv->PciInfo->bus, pNv->PciInfo->device,
				pNv->PciInfo->func);
#endif /* XSERVER_LIBPCIACCESS */

	pNv->Primary = xf86IsPrimaryPci(pNv->PciInfo);

	volatile uint32_t *regs = NULL;
#ifdef XSERVER_LIBPCIACCESS
	pci_device_map_range(pNv->PciInfo, PCI_DEV_MEM_BASE(pNv->PciInfo, 0),
			     0x90000, 0, (void *)&regs);
	pNv->Chipset = NVGetPCIID(regs) & 0xffff;
	pNv->NVArch = NVGetArchitecture(regs);
	pci_device_unmap_range(pNv->PciInfo, (void *) regs, 0x90000);
#else
	CARD32 pcicmd;
	PCI_DEV_READ_LONG(pNv->PciInfo, PCI_CMD_STAT_REG, &pcicmd);
	/* Enable reading memory? */
	PCI_DEV_WRITE_LONG(pNv->PciInfo, PCI_CMD_STAT_REG, pcicmd | PCI_CMD_MEM_ENABLE);
	regs = xf86MapPciMem(-1, VIDMEM_MMIO, pNv->PciTag, PCI_DEV_MEM_BASE(pNv->PciInfo, 0), 0x90000);
	pNv->Chipset = NVGetPCIID(regs) & 0xffff;
	pNv->NVArch = NVGetArchitecture(regs);
	xf86UnMapVidMem(-1, (pointer)regs, 0x90000);
	/* Reset previous state */
	PCI_DEV_WRITE_LONG(pNv->PciInfo, PCI_CMD_STAT_REG, pcicmd);
#endif /* XSERVER_LIBPCIACCESS */

	pScrn->chipset = malloc(sizeof(char) * 25);
	sprintf(pScrn->chipset, "NVIDIA NV%02X", pNv->NVArch);

	if(!pScrn->chipset) {
		pScrn->chipset = "Unknown NVIDIA";
	}

	/*
	* This shouldn't happen because such problems should be caught in
	* NVProbe(), but check it just in case.
	*/
	if (pScrn->chipset == NULL)
		NVPreInitFail("ChipID 0x%04X is not recognised\n", pNv->Chipset);

	if (pNv->NVArch < 0x04)
		NVPreInitFail("Chipset \"%s\" is not recognised\n", pScrn->chipset);

	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Chipset: \"%s\"\n", pScrn->chipset);

	/* The highest architecture currently supported is NV5x */
	if (pNv->NVArch >= 0x80) {
		pNv->Architecture =  NV_ARCH_50;
	} else if (pNv->NVArch >= 0x60) {
		pNv->Architecture =  NV_ARCH_40;
	} else if (pNv->NVArch >= 0x50) {
		pNv->Architecture =  NV_ARCH_50;
	} else if (pNv->NVArch >= 0x40) {
		pNv->Architecture =  NV_ARCH_40;
	} else if (pNv->NVArch >= 0x30) {
		pNv->Architecture = NV_ARCH_30;
	} else if (pNv->NVArch >= 0x20) {
		pNv->Architecture = NV_ARCH_20;
	} else if (pNv->NVArch >= 0x10) {
		pNv->Architecture = NV_ARCH_10;
	} else if (pNv->NVArch >= 0x04) {
		pNv->Architecture = NV_ARCH_04;
	/*  The lowest architecture currently supported is NV04 */
	} else {
		return FALSE;
	}

	/* Initialize the card through int10 interface if needed */
	if (xf86LoadSubModule(pScrn, "int10")) {
		xf86LoaderReqSymLists(int10Symbols, NULL);
#if !defined(__alpha__) && !defined(__powerpc__)
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing int10\n");
		pNv->pInt10 = xf86InitInt10(pNv->pEnt->index);
#endif
	}

	/* Save current console video mode */
	if (pNv->Architecture >= NV_ARCH_50 && pNv->pInt10 && !pNv->kms_enable) {
		const xf86Int10InfoPtr pInt10 = pNv->pInt10;

		pInt10->num = 0x10;
		pInt10->ax  = 0x4f03;
		pInt10->bx  =
		pInt10->cx  =
		pInt10->dx  = 0;
		xf86ExecX86int10(pInt10);
		pNv->Int10Mode = pInt10->bx & 0x3fff;

		xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
			   "VESA-HACK: Console VGA mode is 0x%x\n",
			   pNv->Int10Mode);
	}

	xf86SetOperatingState(resVgaIo, pNv->pEnt->index, ResUnusedOpr);
	xf86SetOperatingState(resVgaMem, pNv->pEnt->index, ResDisableOpr);

	/* Set pScrn->monitor */
	pScrn->monitor = pScrn->confScreen->monitor;

	/*
	 * The first thing we should figure out is the depth, bpp, etc.
	 */

	if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support32bppFb)) {
		NVPreInitFail("\n");
	} else {
		/* Check that the returned depth is one we support */
		switch (pScrn->depth) {
			case 16:
			case 24:
				/* OK */
				break;
			case 15: /* 15 may get done one day, so leave any code for it in place */
			default:
				NVPreInitFail("Given depth (%d) is not supported by this driver\n",
					pScrn->depth);
		}
	}
	xf86PrintDepthBpp(pScrn);

	/*
	 * This must happen after pScrn->display has been set because
	 * xf86SetWeight references it.
	 */
	/* The defaults are OK for us */
	rgb rgbzeros = {0, 0, 0};

	if (!xf86SetWeight(pScrn, rgbzeros, rgbzeros))
		NVPreInitFail("\n");

	if (!xf86SetDefaultVisual(pScrn, -1))
		NVPreInitFail("\n");
	/* We don't support DirectColor */
	else if (pScrn->defaultVisual != TrueColor)
		NVPreInitFail("Given default visual (%s) is not supported at depth %d\n",
			      xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);

	/* The vgahw module should be loaded here when needed */
	if (!xf86LoadSubModule(pScrn, "vgahw")) {
		NVPreInitFail("\n");
	}

	xf86LoaderReqSymLists(vgahwSymbols, NULL);

	/*
	 * Allocate a vgaHWRec
	 */
	if (!vgaHWGetHWRec(pScrn)) {
		NVPreInitFail("\n");
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

	from = X_DEFAULT;

	pNv->kms_enable = false;
#ifdef XF86DRM_MODE
	if (pNv->Architecture == NV_ARCH_50)
		pNv->kms_enable = nouveau_kernel_modesetting_enabled(pScrn);
#endif /* XF86DRM_MODE */

	if (pNv->kms_enable)
		xf86DrvMsg(pScrn->scrnIndex, from, "NV50 Kernel modesetting enabled\n");

	pNv->randr12_enable = true;
	if (pNv->Architecture != NV_ARCH_50 && !xf86ReturnOptValBool(pNv->Options, OPTION_RANDR12, TRUE))
		pNv->randr12_enable = false;
	xf86DrvMsg(pScrn->scrnIndex, from, "Randr1.2 support %sabled\n", pNv->randr12_enable ? "en" : "dis");

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

	if(xf86GetOptValInteger(pNv->Options, OPTION_VIDEO_KEY, &(pNv->videoKey))) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "video key set to 0x%x\n",
					pNv->videoKey);
	} else {
		pNv->videoKey =  (1 << pScrn->offset.red) | 
					(1 << pScrn->offset.green) |
		(((pScrn->mask.blue >> pScrn->offset.blue) - 1) << pScrn->offset.blue);
	}

	/* Things happen on a per output basis for a randr-1.2 driver. */
	if (xf86GetOptValBool(pNv->Options, OPTION_FLAT_PANEL, &(pNv->FlatPanel)) && !pNv->randr12_enable) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "forcing %s usage\n",
			pNv->FlatPanel ? "DFP" : "CRTC");
	} else {
		pNv->FlatPanel = -1; /* autodetect later */
	}

	pNv->FPDither = FALSE;
	if (xf86GetOptValBool(pNv->Options, OPTION_FP_DITHER, &(pNv->FPDither))) 
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "enabling flat panel dither\n");

	if (xf86GetOptValInteger(pNv->Options, OPTION_FP_TWEAK, 
						&pNv->PanelTweak)) {
		pNv->usePanelTweak = TRUE;
	} else {
		pNv->usePanelTweak = FALSE;
	}

	if (pNv->pEnt->device->MemBase != 0) {
		/* Require that the config file value matches one of the PCI values. */
		if (!xf86CheckPciMemBase(pNv->PciInfo, pNv->pEnt->device->MemBase)) {
			NVPreInitFail(
				"MemBase 0x%08lX doesn't match any PCI base register.\n",
				pNv->pEnt->device->MemBase);
		}
		pNv->VRAMPhysical = pNv->pEnt->device->MemBase;
		from = X_CONFIG;
	} else {
		if (PCI_DEV_MEM_BASE(pNv->PciInfo, 1) != 0) {
			pNv->VRAMPhysical = PCI_DEV_MEM_BASE(pNv->PciInfo, 1) & 0xff800000;
			from = X_PROBED;
		} else {
			NVPreInitFail("No valid FB address in PCI config space\n");
			return FALSE;
		}
	}
	xf86DrvMsg(pScrn->scrnIndex, from, "Linear framebuffer at 0x%lX\n",
		(unsigned long)pNv->VRAMPhysical);

	if (pNv->pEnt->device->IOBase != 0) {
		/* Require that the config file value matches one of the PCI values. */
		if (!xf86CheckPciMemBase(pNv->PciInfo, pNv->pEnt->device->IOBase)) {
			NVPreInitFail("IOBase 0x%08lX doesn't match any PCI base register.\n",
				pNv->pEnt->device->IOBase);
		}
		pNv->IOAddress = pNv->pEnt->device->IOBase;
		from = X_CONFIG;
	} else {
		if (PCI_DEV_MEM_BASE(pNv->PciInfo, 0) != 0) {
			pNv->IOAddress = PCI_DEV_MEM_BASE(pNv->PciInfo, 0) & 0xffffc000;
			from = X_PROBED;
		} else {
			NVPreInitFail("No valid MMIO address in PCI config space\n");
		}
	}
	xf86DrvMsg(pScrn->scrnIndex, from, "MMIO registers at 0x%lX\n",
		(unsigned long)pNv->IOAddress);

	if (xf86RegisterResources(pNv->pEnt->index, NULL, ResExclusive))
		NVPreInitFail("xf86RegisterResources() found resource conflicts\n");

	if (pNv->Architecture < NV_ARCH_10) {
		max_width = (pScrn->bitsPerPixel > 16) ? 2032 : 2048;
		max_height = 2048;
	} else if (pNv->Architecture < NV_ARCH_50) {
		max_width = (pScrn->bitsPerPixel > 16) ? 4080 : 4096;
		max_height = 4096;
	} else {
		max_width = (pScrn->bitsPerPixel > 16) ? 8176 : 8192;
		max_height = 8192;
	}

#ifdef XF86DRM_MODE
	if (pNv->kms_enable){
		int res = 0;
		char *bus_id;
		bus_id = DRICreatePCIBusID(pNv->PciInfo);

		pNv->drmmode = calloc(1, sizeof(drmmode_rec));
		res = drmmode_pre_init(pScrn, bus_id, pNv->drmmode, pScrn->bitsPerPixel >> 3);
		if (!res) {
			xfree(bus_id);
			NVPreInitFail("Kernel modesetting failed to initialize\n");
		}
	} else
#endif
	if (pNv->randr12_enable) {
		/* Allocate an xf86CrtcConfig */
		xf86CrtcConfigInit(pScrn, &nv_xf86crtc_config_funcs);
		xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

		xf86CrtcSetSizeRange(pScrn, 320, 200, max_width, max_height);
	}

	if (NVPreInitDRI(pScrn) == FALSE)
		NVPreInitFail("\n");

	if (!pNv->randr12_enable) {
		if ((pScrn->monitor->nHsync == 0) && 
			(pScrn->monitor->nVrefresh == 0)) {

			config_mon_rates = FALSE;
		} else {
			config_mon_rates = TRUE;
		}
	}

	NVCommonSetup(pScrn);

	if (pNv->randr12_enable && !pNv->kms_enable) {
		if (pNv->Architecture == NV_ARCH_50)
			if (!NV50DispPreInit(pScrn))
				NVPreInitFail("\n");

		NVI2CInit(pScrn);

		/* This is the internal system, not the randr-1.2 ones. */
		if (pNv->Architecture == NV_ARCH_50) {
			NV50CrtcInit(pScrn);
			NV50ConnectorInit(pScrn);
			NV50OutputSetup(pScrn);
		}

		for (i = 0; i <= pNv->twoHeads; i++) {
			if (pNv->Architecture == NV_ARCH_50)
				nv50_crtc_init(pScrn, i);
			else
				nv_crtc_init(pScrn, i);
		}

		if (pNv->Architecture < NV_ARCH_50)
			NvSetupOutputs(pScrn);
		else
			nv50_output_create(pScrn); /* create randr-1.2 "outputs". */

		if (!xf86InitialConfiguration(pScrn, FALSE))
			NVPreInitFail("No valid modes.\n");
	}

	pScrn->videoRam = pNv->RamAmountKBytes;
	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "VideoRAM: %d kBytes\n",
		pScrn->videoRam);

	pNv->VRAMPhysicalSize = pScrn->videoRam * 1024;

	/*
	 * If the driver can do gamma correction, it should call xf86SetGamma()
	 * here.
	 */
	Gamma gammazeros = {0.0, 0.0, 0.0};

	if (!xf86SetGamma(pScrn, gammazeros))
		NVPreInitFail("\n");

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
	if ((pNv->Architecture == NV_ARCH_20) ||
		((pNv->Architecture == NV_ARCH_10) && 
		((pNv->Chipset & 0x0ff0) != CHIPSET_NV10) &&
		((pNv->Chipset & 0x0ff0) != CHIPSET_NV15))) {
		/* HW is broken */
		clockRanges->interlaceAllowed = FALSE;
	} else {
		clockRanges->interlaceAllowed = TRUE;
	}

	if(pNv->FlatPanel == 1) {
		clockRanges->interlaceAllowed = FALSE;
		clockRanges->doubleScanAllowed = FALSE;
	}

#ifdef M_T_DRIVER
	/* If DFP, add a modeline corresponding to its panel size */
	if (pNv->FlatPanel && !pNv->Television && pNv->fpWidth && pNv->fpHeight) {
		DisplayModePtr Mode;

		Mode = xnfcalloc(1, sizeof(DisplayModeRec));
		Mode = xf86CVTMode(pNv->fpWidth, pNv->fpHeight, 60.00, TRUE, FALSE);
		Mode->type = M_T_DRIVER;
		pScrn->monitor->Modes = xf86ModesAdd(pScrn->monitor->Modes, Mode);

		if (!config_mon_rates) {
			if (!Mode->HSync)
				Mode->HSync = ((float) Mode->Clock ) / ((float) Mode->HTotal);
			if (!Mode->VRefresh)
				Mode->VRefresh = (1000.0 * ((float) Mode->Clock)) /
							((float) (Mode->HTotal * Mode->VTotal));

			if (Mode->HSync < pScrn->monitor->hsync[0].lo)
				pScrn->monitor->hsync[0].lo = Mode->HSync;
			if (Mode->HSync > pScrn->monitor->hsync[0].hi)
				pScrn->monitor->hsync[0].hi = Mode->HSync;
			if (Mode->VRefresh < pScrn->monitor->vrefresh[0].lo)
				pScrn->monitor->vrefresh[0].lo = Mode->VRefresh;
			if (Mode->VRefresh > pScrn->monitor->vrefresh[0].hi)
				pScrn->monitor->vrefresh[0].hi = Mode->VRefresh;

			pScrn->monitor->nHsync = 1;
			pScrn->monitor->nVrefresh = 1;
		}
	}
#endif

	if (pNv->randr12_enable) {
		pScrn->displayWidth = nv_pitch_align(pNv, pScrn->virtualX, pScrn->depth);
	} else {
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
			NVPreInitFail("\n");
		}

		/* Prune the modes marked as invalid */
		xf86PruneDriverModes(pScrn);

		/*
		 * Set the CRTC parameters for all of the modes based on the type
		 * of mode, and the chipset's interlace requirements.
		 *
		 * Calling this is required if the mode->Crtc* values are used by the
		 * driver and if the driver doesn't provide code to set them.  They
		 * are not pre-initialised at all.
		 */
		xf86SetCrtcForModes(pScrn, 0);

		if (pScrn->modes == NULL)
			NVPreInitFail("No valid modes found\n");
	}

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

	if (xf86LoadSubModule(pScrn, "fb") == NULL)
		NVPreInitFail("\n");

	xf86LoaderReqSymLists(fbSymbols, NULL);

	/* Load EXA if needed */
	if (!pNv->NoAccel) {
		if (!xf86LoadSubModule(pScrn, "exa")) {
			NVPreInitFail("\n");
		}
		xf86LoaderReqSymLists(exaSymbols, NULL);
	}

	/* Load ramdac if needed */
	if (pNv->HWCursor) {
		if (!xf86LoadSubModule(pScrn, "ramdac")) {
			NVPreInitFail("\n");
		}
		xf86LoaderReqSymLists(ramdacSymbols, NULL);
	}

	/* Load shadowfb if needed */
	if (pNv->ShadowFB) {
		if (!xf86LoadSubModule(pScrn, "shadowfb")) {
			NVPreInitFail("\n");
		}
		xf86LoaderReqSymLists(shadowSymbols, NULL);
	}

	return TRUE;
}


/*
 * Map the framebuffer and MMIO memory.
 */

static Bool
NVMapMem(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int gart_scratch_size;
	uint64_t res;

	nouveau_device_get_param(pNv->dev, NOUVEAU_GETPARAM_FB_SIZE, &res);
	pNv->VRAMSize=res;
	nouveau_device_get_param(pNv->dev, NOUVEAU_GETPARAM_FB_PHYSICAL, &res);
	pNv->VRAMPhysical=res;
	nouveau_device_get_param(pNv->dev, NOUVEAU_GETPARAM_AGP_SIZE, &res);
	pNv->AGPSize=res;

	if (pNv->AGPSize) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "AGPGART: %dMiB available\n",
			   (unsigned int)(pNv->AGPSize >> 20));
		if (pNv->AGPSize > (16*1024*1024))
			gart_scratch_size = 16*1024*1024;
		else
			/* always leave 512kb for other things like the fifos */
			gart_scratch_size = pNv->AGPSize - 512*1024;
	} else {
		gart_scratch_size = (4 << 20) - (1 << 18) ;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "GART: PCI DMA - using %dKiB\n",
			   gart_scratch_size >> 10);
	}

	if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_GART | NOUVEAU_BO_PIN, 0,
			   gart_scratch_size, &pNv->GART)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Unable to allocate GART memory\n");
	}
	if (pNv->GART) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "GART: Allocated %dMiB as a scratch buffer\n",
			   (unsigned int)(pNv->GART->size >> 20));
	}

	if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN, 0,
			   64 * 1024, &pNv->Cursor)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to allocate memory for hardware cursor\n");
		return FALSE;
	}

	if (pNv->randr12_enable) {
		if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN, 0,
			64 * 1024, &pNv->Cursor2)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"Failed to allocate memory for hardware cursor\n");
			return FALSE;
		}
	}

	if (pNv->Architecture >= NV_ARCH_50) {
		/* Both CRTC's have a CLUT. */
		if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN,
				   0, 0x1000, &pNv->CLUT0)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Failed to allocate memory for CLUT0\n");
			return FALSE;
		}

		if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN,
				   0, 0x1000, &pNv->CLUT1)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Failed to allocate memory for CLUT1\n");
			return FALSE;
		}
	}

	if ((pNv->CLUT0 && nouveau_bo_map(pNv->CLUT0, NOUVEAU_BO_RDWR)) ||
	    (pNv->CLUT1 && nouveau_bo_map(pNv->CLUT1, NOUVEAU_BO_RDWR)) ||
	    nouveau_bo_map(pNv->Cursor, NOUVEAU_BO_RDWR) ||
	    (pNv->randr12_enable && nouveau_bo_map(pNv->Cursor2, NOUVEAU_BO_RDWR))) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to map pinned buffers\n");
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

	nouveau_bo_del(&pNv->FB);
	nouveau_bo_del(&pNv->GART);
	nouveau_bo_del(&pNv->Cursor);
	if (pNv->randr12_enable) {
		nouveau_bo_del(&pNv->Cursor2);
	}
	nouveau_bo_del(&pNv->CLUT0);
	nouveau_bo_del(&pNv->CLUT1);

	return TRUE;
}


/*
 * Initialise a new mode. 
 */

static Bool
NVModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    vgaRegPtr vgaReg;
    NVPtr pNv = NVPTR(pScrn);
    NVRegPtr nvReg;

    /* Initialise the ModeReg values */
    if (!vgaHWInit(pScrn, mode))
	return FALSE;
    pScrn->vtSema = TRUE;

    vgaReg = &hwp->ModeReg;
    nvReg = &pNv->ModeReg;

    if(!NVDACInit(pScrn, mode))
        return FALSE;

    NVLockUnlock(pScrn, 0);
    if(pNv->twoHeads) {
        nvWriteCurVGA(pNv, NV_VGA_CRTCX_OWNER, nvReg->crtcOwner);
        NVLockUnlock(pScrn, 0);
    }

    /* Program the registers */
    vgaHWProtect(pScrn, TRUE);

    NVDACRestore(pScrn, vgaReg, nvReg, FALSE);

#if X_BYTE_ORDER == X_BIG_ENDIAN
    /* turn on LFB swapping */
    {
	unsigned char tmp;

	tmp = nvReadCurVGA(pNv, NV_VGA_CRTCX_SWAPPING);
	tmp |= (1 << 7);
	nvWriteCurVGA(pNv, NV_VGA_CRTCX_SWAPPING, tmp);
    }
#endif

    if (!pNv->NoAccel)
		NVAccelCommonInit(pScrn);

    vgaHWProtect(pScrn, FALSE);

    pScrn->currentMode = mode;

    return TRUE;
}

/*
 * Restore the initial (text) mode.
 */
static void 
NVRestore(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	if (pNv->randr12_enable) {
		xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
		int i;

		for (i = 0; i < xf86_config->num_crtc; i++)
			NVCrtcLockUnlock(xf86_config->crtc[i], 0);

		for (i = 0; i < xf86_config->num_output; i++)
			xf86_config->output[i]->funcs->restore(xf86_config->output[i]);

		for (i = 0; i < xf86_config->num_crtc; i++)
			xf86_config->crtc[i]->funcs->restore(xf86_config->crtc[i]);

		nv_save_restore_vga_fonts(pScrn, 0);

		for (i = 0; i < xf86_config->num_crtc; i++)
			NVCrtcLockUnlock(xf86_config->crtc[i], 1);
	} else {
		vgaHWPtr hwp = VGAHWPTR(pScrn);
		vgaRegPtr vgaReg = &hwp->SavedReg;
		NVRegPtr nvReg = &pNv->SavedReg;

		NVLockUnlock(pScrn, 0);

		if(pNv->twoHeads) {
			nvWriteCurVGA(pNv, NV_VGA_CRTCX_OWNER, pNv->crtc_active[1] * 0x3);
			NVLockUnlock(pScrn, 0);
		}

		/* Only restore text mode fonts/text for the primary card */
		vgaHWProtect(pScrn, TRUE);
		NVDACRestore(pScrn, vgaReg, nvReg, pNv->Primary);
		vgaHWProtect(pScrn, FALSE);
	}

	if (pNv->twoHeads) {
		NVSetOwner(pScrn, 0);	/* move to head A to set owner */
		NVLockVgaCrtc(pNv, 0, false);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Restoring CRTC_OWNER to %d.\n", pNv->vtOWNER);
		NVWriteVgaCrtc(pNv, 0, NV_VGA_CRTCX_OWNER, pNv->vtOWNER);
		NVLockVgaCrtc(pNv, 0, true);
	}
}

static void
NVLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
	      LOCO * colors, VisualPtr pVisual)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	int c;
	int i, j, index;
	CARD16 lut_r[256], lut_g[256], lut_b[256];

	for (c = 0; c < xf86_config->num_crtc; c++) {
		xf86CrtcPtr crtc = xf86_config->crtc[c];

		/* code borrowed from intel driver */
		switch (pScrn->depth) {
		case 15:
			for (i = 0; i < numColors; i++) {
				index = indices[i];
				for (j = 0; j < 8; j++) {
					lut_r[index * 8 + j] = colors[index].red << 8;
					lut_g[index * 8 + j] = colors[index].green << 8;
					lut_b[index * 8 + j] = colors[index].blue << 8;
				}
			}
		case 16:
			for (i = 0; i < numColors; i++) {
				index = indices[i];

				if (i <= 31) {
					for (j = 0; j < 8; j++) {
						lut_r[index * 8 + j] = colors[index].red << 8;
						lut_b[index * 8 + j] = colors[index].blue << 8;
					}
				}

				for (j = 0; j < 4; j++) {
					lut_g[index * 4 + j] = colors[index].green << 8;
				}
			}
		default:
			for (i = 0; i < numColors; i++) {
				index = indices[i];
				lut_r[index] = colors[index].red << 8;
				lut_g[index] = colors[index].green << 8;
				lut_b[index] = colors[index].blue << 8;
			}
			break;
		}

		/* Make the change through RandR */
		RRCrtcGammaSet(crtc->randr_crtc, lut_r, lut_g, lut_b);
	}
}

static void NVBacklightEnable(NVPtr pNv,  Bool on)
{
    /* This is done differently on each laptop.  Here we
       define the ones we know for sure. */

#if defined(__powerpc__)
    if((pNv->Chipset & 0xffff == 0x0179) ||
       (pNv->Chipset & 0xffff == 0x0189) ||
       (pNv->Chipset & 0xffff == 0x0329))
    {
       /* NV17,18,34 Apple iMac, iBook, PowerBook */
      CARD32 tmp_pmc, tmp_pcrt;
      tmp_pmc = nvReadMC(pNv, NV_PBUS_DEBUG_DUALHEAD_CTL) & 0x7FFFFFFF;
      tmp_pcrt = NVReadCRTC(pNv, 0, NV_CRTC_GPIO_EXT) & 0xFFFFFFFC;
      if(on) {
          tmp_pmc |= (1 << 31);
          tmp_pcrt |= 0x1;
      }
      nvWriteMC(pNv, NV_PBUS_DEBUG_DUALHEAD_CTL, tmp_pmc);
      NVWriteCRTC(pNv, 0, NV_CRTC_GPIO_EXT, tmp_pcrt);
    }
#endif
    
    if(pNv->LVDS) {
       if(pNv->twoHeads && ((pNv->Chipset & 0x0ff0) != CHIPSET_NV11)) {
           nvWriteMC(pNv, 0x130C, on ? 3 : 7);
       }
    } else {
       CARD32 fpcontrol;

       fpcontrol = nvReadCurRAMDAC(pNv, NV_RAMDAC_FP_CONTROL) & 0xCfffffCC;

       /* cut the TMDS output */
       if(on) fpcontrol |= pNv->fpSyncs;
       else fpcontrol |= 0x20000022;

       nvWriteCurRAMDAC(pNv, NV_RAMDAC_FP_CONTROL, fpcontrol);
    }
}

static void
NVDPMSSetLCD(ScrnInfoPtr pScrn, int PowerManagementMode, int flags)
{
  NVPtr pNv = NVPTR(pScrn);

  if (!pScrn->vtSema) return;

  vgaHWDPMSSet(pScrn, PowerManagementMode, flags);

  switch (PowerManagementMode) {
  case DPMSModeStandby:  /* HSync: Off, VSync: On */
  case DPMSModeSuspend:  /* HSync: On, VSync: Off */
  case DPMSModeOff:      /* HSync: Off, VSync: Off */
    NVBacklightEnable(pNv, 0);
    break;
  case DPMSModeOn:       /* HSync: On, VSync: On */
    NVBacklightEnable(pNv, 1);
  default:
    break;
  }
}


static void
NVDPMSSet(ScrnInfoPtr pScrn, int PowerManagementMode, int flags)
{
  unsigned char crtc1A;
  vgaHWPtr hwp = VGAHWPTR(pScrn);

  if (!pScrn->vtSema) return;

  crtc1A = hwp->readCrtc(hwp, 0x1A) & ~0xC0;

  switch (PowerManagementMode) {
  case DPMSModeStandby:  /* HSync: Off, VSync: On */
    crtc1A |= 0x80;
    break;
  case DPMSModeSuspend:  /* HSync: On, VSync: Off */
    crtc1A |= 0x40;
    break;
  case DPMSModeOff:      /* HSync: Off, VSync: Off */
    crtc1A |= 0xC0;
    break;
  case DPMSModeOn:       /* HSync: On, VSync: On */
  default:
    break;
  }

  /* vgaHWDPMSSet will merely cut the dac output */
  vgaHWDPMSSet(pScrn, PowerManagementMode, flags);

  hwp->writeCrtc(hwp, 0x1A, crtc1A);
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
	int displayWidth;

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

	if (!pNv->NoAccel) {
		/* Init DRM - Alloc FIFO */
		if (!NVInitDma(pScrn))
			return FALSE;

		/* setup graphics objects */
		if (!NVAccelCommonInit(pScrn))
			return FALSE;
	}

	if (nouveau_bo_new(pNv->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_PIN,
			0,
			NOUVEAU_ALIGN(pScrn->virtualX, 64) *
			NOUVEAU_ALIGN(pScrn->virtualY, 64) *
			(pScrn->bitsPerPixel >> 3), &pNv->FB)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to allocate memory for screen pixmap.\n");
		return FALSE;
	}

	nouveau_bo_map(pNv->FB, NOUVEAU_BO_RDWR);
	pNv->FBMap = pNv->FB->map;
	nouveau_bo_unmap(pNv->FB);

#ifdef XF86DRM_MODE
	if (pNv->kms_enable)
		drmmode_set_fb(pScrn, pNv->drmmode, pScrn->virtualX, pScrn->virtualY, pScrn->displayWidth*(pScrn->bitsPerPixel >> 3), pNv->FB);
#endif

	if (!pNv->randr12_enable) {
		/* Save the current state */
		NVSave(pScrn);
		/* Initialise the first mode */
		if (!NVModeInit(pScrn, pScrn->currentMode))
			return FALSE;

		/* Darken the screen for aesthetic reasons and set the viewport */
		NVSaveScreen(pScreen, SCREEN_SAVER_ON);
		pScrn->AdjustFrame(scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
	} else {
		pScrn->memPhysBase = pNv->VRAMPhysical;
		pScrn->fbOffset = 0;

		if (!NVEnterVT(scrnIndex, 0))
			return FALSE;
		NVSaveScreen(pScreen, SCREEN_SAVER_ON);
	}


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
	if (!miSetPixmapDepths ())
		return FALSE;

	/*
	 * Call the framebuffer layer's ScreenInit function, and fill in other
	 * pScreen fields.
	 */

	if (pNv->ShadowFB) {
		pNv->ShadowPitch = BitmapBytePad(pScrn->bitsPerPixel * pScrn->virtualX);
		pNv->ShadowPtr = xalloc(pNv->ShadowPitch * pScrn->virtualY);
		displayWidth = pNv->ShadowPitch / (pScrn->bitsPerPixel >> 3);
		FBStart = pNv->ShadowPtr;
	} else {
		pNv->ShadowPtr = NULL;
		displayWidth = pScrn->displayWidth;
		nouveau_bo_map(pNv->FB, NOUVEAU_BO_RDWR);
		FBStart = pNv->FB->map;
		nouveau_bo_unmap(pNv->FB);
	}

	switch (pScrn->bitsPerPixel) {
		case 16:
		case 32:
			ret = fbScreenInit(pScreen, FBStart, pScrn->virtualX, pScrn->virtualY,
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

	fbPictureInit (pScreen, 0, 0);

	xf86SetBlackWhitePixels(pScreen);

	if (!pNv->NoAccel) {
		if (!NVExaInit(pScreen))
			return FALSE;
		NVAccelCommonInit(pScrn);
	} else if (pNv->VRAMPhysicalSize / 2 < NOUVEAU_ALIGN(pScrn->virtualX, 64) * NOUVEAU_ALIGN(pScrn->virtualY, 64) * (pScrn->bitsPerPixel >> 3)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "The virtual screen size's resolution is too big for the video RAM framebuffer at this colour depth.\n");
		return FALSE;
	}


	miInitializeBackingStore(pScreen);
	xf86SetBackingStore(pScreen);
	xf86SetSilkenMouse(pScreen);

	/* Finish DRI init */
	NVDRIFinishScreenInit(pScrn);

	/* 
	 * Initialize software cursor.
	 * Must precede creation of the default colormap.
	 */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/*
	 * Initialize HW cursor layer. 
	 * Must follow software cursor initialization.
	 */
	if (pNv->HWCursor) { 
		if (pNv->Architecture < NV_ARCH_50 && !pNv->randr12_enable)
			ret = NVCursorInit(pScreen);
		else if (pNv->Architecture < NV_ARCH_50 && pNv->randr12_enable)
			ret = NVCursorInitRandr12(pScreen);
		else
			ret = NV50CursorInit(pScreen);

		if (ret != TRUE) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR, 
				"Hardware cursor initialization failed\n");
			pNv->HWCursor = FALSE;
		}
	}

	if (pNv->randr12_enable) {
		xf86DPMSInit(pScreen, xf86DPMSSet, 0);

		if (!xf86CrtcScreenInit(pScreen))
			return FALSE;
	}

	/* Initialise default colourmap */
	if (!miCreateDefColormap(pScreen))
		return FALSE;

	/*
	 * Initialize colormap layer.
	 * Must follow initialization of the default colormap 
	 */
	if (!pNv->randr12_enable && !pNv->kms_enable) {
		if(!xf86HandleColormaps(pScreen, 256, 8, NVDACLoadPalette,
				NULL, CMAP_RELOAD_ON_MODE_SWITCH | CMAP_PALETTED_TRUECOLOR))
			return FALSE;
	} else {
		if (!xf86HandleColormaps(pScreen, 256, 8, NVLoadPalette,
				NULL, CMAP_PALETTED_TRUECOLOR))
			return FALSE;
	}

	if (pNv->ShadowFB)
		ShadowFBInit(pScreen, NVRefreshArea);

	if (!pNv->randr12_enable) {
		if(pNv->FlatPanel) {
			xf86DPMSInit(pScreen, NVDPMSSetLCD, 0);
		} else {
			xf86DPMSInit(pScreen, NVDPMSSet, 0);
		}
	}

	pScrn->memPhysBase = pNv->VRAMPhysical;
	pScrn->fbOffset = 0;

	NVInitVideo(pScreen);

	pScreen->SaveScreen = NVSaveScreen;

	/* Wrap the current CloseScreen function */
	pNv->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = NVCloseScreen;

	pNv->BlockHandler = pScreen->BlockHandler;
	pScreen->BlockHandler = NVBlockHandler;

	/* Report any unused options (only for the first generation) */
	if (serverGeneration == 1)
		xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

	return TRUE;
}

static Bool
NVSaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    NVPtr pNv = NVPTR(pScrn);
    int i;
    Bool on = xf86IsUnblank(mode);
    
    if (pNv->randr12_enable) {
    	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	if (pScrn->vtSema && pNv->Architecture < NV_ARCH_50) {
	    for (i = 0; i < xf86_config->num_crtc; i++) {
		
		if (xf86_config->crtc[i]->enabled) {
		    NVCrtcPrivatePtr nv_crtc = xf86_config->crtc[i]->driver_private;
		    NVBlankScreen(pScrn, nv_crtc->head, !on);
		}
	    }
	    
	}
	return TRUE;
    }

	return vgaHWSaveScreen(pScreen, mode);
}

static void
NVSave(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	NVRegPtr nvReg = &pNv->SavedReg;

	if (pNv->randr12_enable) {
		xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
		int i;

		nv_save_restore_vga_fonts(pScrn, 1);

		for (i = 0; i < xf86_config->num_crtc; i++) {
			xf86_config->crtc[i]->funcs->save(xf86_config->crtc[i]);
		}

		for (i = 0; i < xf86_config->num_output; i++) {
			xf86_config->output[i]->funcs->save(xf86_config->
							    output[i]);
		}
	} else {
		vgaHWPtr pVga = VGAHWPTR(pScrn);
		vgaRegPtr vgaReg = &pVga->SavedReg;
		NVLockUnlock(pScrn, 0);
		if (pNv->twoHeads) {
			nvWriteCurVGA(pNv, NV_VGA_CRTCX_OWNER, pNv->crtc_active[1] * 0x3);
			NVLockUnlock(pScrn, 0);
		}

		NVDACSave(pScrn, vgaReg, nvReg, pNv->Primary);
	}
}
