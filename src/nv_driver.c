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

#include "xorg-server.h"
#include "xf86int10.h"
#include "xf86drm.h"
#include "xf86drmMode.h"
#include "nouveau_drm.h"

/*
 * Forward definitions for the functions that make up the driver.
 */
/* Mandatory functions */
static const OptionInfoRec * NVAvailableOptions(int chipid, int busid);
static void    NVIdentify(int flags);
static Bool    NVPreInit(ScrnInfoPtr pScrn, int flags);
static Bool    NVScreenInit(int Index, ScreenPtr pScreen, int argc,
                            char **argv);
static Bool    NVEnterVT(int scrnIndex, int flags);
static void    NVLeaveVT(int scrnIndex, int flags);
static Bool    NVCloseScreen(int scrnIndex, ScreenPtr pScreen);
static Bool    NVSaveScreen(ScreenPtr pScreen, int mode);
static void    NVCloseDRM(ScrnInfoPtr);

/* Optional functions */
static Bool    NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
static void    NVAdjustFrame(int scrnIndex, int x, int y, int flags);
static void    NVFreeScreen(int scrnIndex, int flags);

/* Internally used functions */

static Bool	NVMapMem(ScrnInfoPtr pScrn);
static Bool	NVUnmapMem(ScrnInfoPtr pScrn);

#define NOUVEAU_PCI_DEVICE(_vendor_id, _device_id)                             \
	{ (_vendor_id), (_device_id), PCI_MATCH_ANY, PCI_MATCH_ANY,            \
	  0x00030000, 0x00ffffff, 0 }

static const struct pci_id_match nouveau_device_match[] = {
	NOUVEAU_PCI_DEVICE(PCI_VENDOR_NVIDIA, PCI_MATCH_ANY),
	NOUVEAU_PCI_DEVICE(PCI_VENDOR_NVIDIA_SGS, PCI_MATCH_ANY),
	{ 0, 0, 0 },
};

static Bool NVPciProbe (	DriverPtr 		drv,
				int 			entity_num,
				struct pci_device	*dev,
				intptr_t		match_data	);

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
	NULL,
	NVAvailableOptions,
	NULL,
	0,
	NULL,
	nouveau_device_match,
	NVPciProbe
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
  { "GeForce GTX 200", "NVA0" },
  { "GeForce GTX 400", "NVC0" },
  { NULL, NULL}
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

static Bool
NVPciProbe(DriverPtr drv, int entity_num, struct pci_device *pci_dev,
	   intptr_t match_data)
{
	PciChipsets NVChipsets[] = {
		{ pci_dev->device_id,
		  (pci_dev->vendor_id << 16) | pci_dev->device_id, NULL },
		{ -1, -1, NULL }
	};
	struct nouveau_device *dev = NULL;
	EntityInfoPtr pEnt = NULL;
	ScrnInfoPtr pScrn = NULL;
	drmVersion *version;
	int chipset, ret;
	char *busid;

	if (!xf86LoaderCheckSymbol("DRICreatePCIBusID")) {
		xf86DrvMsg(-1, X_ERROR, "[drm] No DRICreatePCIBusID symbol\n");
		return FALSE;
	}
	busid = DRICreatePCIBusID(pci_dev);

	ret = nouveau_device_open(&dev, busid);
	if (ret) {
		xf86DrvMsg(-1, X_ERROR, "[drm] failed to open device\n");
		free(busid);
		return FALSE;
	}

	/* Check the version reported by the kernel module.  In theory we
	 * shouldn't have to do this, as libdrm_nouveau will do its own checks.
	 * But, we're currently using the kernel patchlevel to also version
	 * the DRI interface.
	 */
	version = drmGetVersion(nouveau_device(dev)->fd);
	xf86DrvMsg(-1, X_INFO, "[drm] nouveau interface version: %d.%d.%d\n",
		   version->version_major, version->version_minor,
		   version->version_patchlevel);
	drmFree(version);

	chipset = dev->chipset;
	nouveau_device_close(&dev);

	ret = drmCheckModesettingSupported(busid);
	free(busid);
	if (ret) {
		xf86DrvMsg(-1, X_ERROR, "[drm] KMS not enabled\n");
		return FALSE;
	}

	switch (chipset & 0xf0) {
	case 0x00:
	case 0x10:
	case 0x20:
	case 0x30:
	case 0x40:
	case 0x60:
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
	case 0xc0:
		break;
	default:
		xf86DrvMsg(-1, X_ERROR, "Unknown chipset: NV%02x\n", chipset);
		return FALSE;
	}

	pScrn = xf86ConfigPciEntity(pScrn, 0, entity_num, NVChipsets,
				    NULL, NULL, NULL, NULL, NULL);
	if (!pScrn)
		return FALSE;

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

	xf86SetEntitySharable(entity_num);

	pEnt = xf86GetEntityInfo(entity_num);
	xf86SetEntityInstanceForScreen(pScrn, pEnt->index, xf86GetNumEntityInstances(pEnt->index) - 1);
	free(pEnt);

	return TRUE;
}

#define MAX_CHIPS MAXSCREENS

Bool
NVSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];

	return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
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

	drmmode_adjust_frame(pScrn, x, y, flags);
}

/*
 * This is called when VT switching back to the X server.  Its job is
 * to reinitialise the video mode.
 */

/* Mandatory */
static Bool
NVEnterVT(int scrnIndex, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	NVPtr pNv = NVPTR(pScrn);
	int ret;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NVEnterVT is called.\n");

	ret = drmSetMaster(nouveau_device(pNv->dev)->fd);
	if (ret)
		ErrorF("Unable to get master: %d\n", ret);

	if (!xf86SetDesiredModes(pScrn))
		return FALSE;

	if (pNv->overlayAdaptor && pNv->Architecture != NV_ARCH_04)
		NV10WriteOverlayParameters(pScrn);

	return TRUE;
}

/*
 * This is called when VT switching away from the X server.  Its job is
 * to restore the previous (text) mode.
 */

/* Mandatory */
static void
NVLeaveVT(int scrnIndex, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	NVPtr pNv = NVPTR(pScrn);
	int ret;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "NVLeaveVT is called.\n");

	NVSync(pScrn);

	ret = drmDropMaster(nouveau_device(pNv->dev)->fd);
	if (ret)
		ErrorF("Error dropping master: %d\n", ret);
}

static void
NVFlushCallback(CallbackListPtr *list, pointer user_data, pointer call_data)
{
	ScrnInfoPtr pScrn = user_data;
	NVPtr pNv = NVPTR(pScrn);

	if (pScrn->vtSema && !pNv->NoAccel)
		FIRE_RING (pNv->chan);
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

	pScreen->BlockHandler = pNv->BlockHandler;
	(*pScreen->BlockHandler) (i, blockData, pTimeout, pReadmask);
	pScreen->BlockHandler = NVBlockHandler;

	if (pNv->VideoTimerCallback) 
		(*pNv->VideoTimerCallback)(pScrnInfo, currentTime.milliseconds);
}

static Bool
NVCreateScreenResources(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	PixmapPtr ppix;

	pScreen->CreateScreenResources = pNv->CreateScreenResources;
	if (!(*pScreen->CreateScreenResources)(pScreen))
		return FALSE;
	pScreen->CreateScreenResources = NVCreateScreenResources;

	drmmode_fbcon_copy(pScreen);
	if (!NVEnterVT(pScrn->scrnIndex, 0))
		return FALSE;

	if (!pNv->NoAccel) {
		ppix = pScreen->GetScreenPixmap(pScreen);
		nouveau_bo_ref(pNv->scanout, &nouveau_pixmap(ppix)->bo);
	}

	return TRUE;
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

	drmmode_screen_fini(pScreen);

	if (!pNv->NoAccel)
		nouveau_dri2_fini(pScreen);

	if (pScrn->vtSema) {
		NVLeaveVT(scrnIndex, 0);
		pScrn->vtSema = FALSE;
	}

	NVAccelFree(pScrn);
	NVTakedownVideo(pScrn);
	NVTakedownDma(pScrn);
	NVUnmapMem(pScrn);

	xf86_cursors_fini(pScreen);

	DeleteCallback(&FlushCallback, NVFlushCallback, pScrn);

	if (pNv->ShadowPtr) {
		free(pNv->ShadowPtr);
		pNv->ShadowPtr = NULL;
	}
	if (pNv->overlayAdaptor) {
		free(pNv->overlayAdaptor);
		pNv->overlayAdaptor = NULL;
	}
	if (pNv->blitAdaptor) {
		free(pNv->blitAdaptor);
		pNv->blitAdaptor = NULL;
	}
	if (pNv->textureAdaptor[0]) {
		free(pNv->textureAdaptor[0]);
		pNv->textureAdaptor[0] = NULL;
	}
	if (pNv->textureAdaptor[1]) {
		free(pNv->textureAdaptor[1]);
		pNv->textureAdaptor[1] = NULL;
	}
	if (pNv->EXADriverPtr) {
		exaDriverFini(pScreen);
		free(pNv->EXADriverPtr);
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

	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	NVPtr pNv = NVPTR(pScrn);

	if (!pNv)
		return;

	NVCloseDRM(pScrn);

	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

#define NVPreInitFail(fmt, args...) do {                                    \
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%d: "fmt, __LINE__, ##args); \
	NVFreeScreen(pScrn->scrnIndex, 0);                                  \
	return FALSE;                                                       \
} while(0)

static void
NVCloseDRM(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	nouveau_device_close(&pNv->dev);
	drmFree(pNv->drm_device_name);
}

static Bool
NVDRIGetVersion(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	int errmaj, errmin;
	pointer ret;

	ret = LoadSubModule(pScrn->module, "dri", NULL, NULL, NULL,
			    NULL, &errmaj, &errmin);
	if (!ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				"error %d\n", errmaj);
		LoaderErrorMsg(pScrn->name, "dri", errmaj, errmin);
	}

	if (!ret && errmaj != LDR_ONCEONLY)
		return FALSE;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Loaded DRI module\n");

	/* Check the lib version */
	if (xf86LoaderCheckSymbol("drmGetLibVersion"))
		pNv->pLibDRMVersion = drmGetLibVersion(0);
	if (pNv->pLibDRMVersion == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"NVDRIGetVersion failed because libDRM is really "
		"way to old to even get a version number out of it.\n"
		"[dri] Disabling DRI.\n");
		return FALSE;
	}

	return TRUE;
}

static Bool
NVPreInitDRM(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	char *bus_id;
	int ret;

	if (!NVDRIGetVersion(pScrn))
		return FALSE;

	/* Load the kernel module, and open the DRM */
	bus_id = DRICreatePCIBusID(pNv->PciInfo);
	ret = DRIOpenDRMMaster(pScrn, SAREA_MAX, bus_id, "nouveau");
	free(bus_id);
	if (!ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "[drm] error opening the drm\n");
		return FALSE;
	}

	/* Initialise libdrm_nouveau */
	ret = nouveau_device_open_existing(&pNv->dev, 1, DRIMasterFD(pScrn), 0);
	if (ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "[drm] error creating device\n");
		return FALSE;
	}

	pNv->drm_device_name = drmGetDeviceNameFromFd(DRIMasterFD(pScrn));

	return TRUE;
}

/* Mandatory */
Bool
NVPreInit(ScrnInfoPtr pScrn, int flags)
{
	struct nouveau_device *dev;
	NVPtr pNv;
	MessageType from;
	uint64_t v;
	int ret, i;

	if (flags & PROBE_DETECT) {
		EntityInfoPtr pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

		if (!pEnt)
			return FALSE;

		i = pEnt->index;
		free(pEnt);

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
	if (!(pScrn->driverPrivate = xnfcalloc(1, sizeof(NVRec))))
		return FALSE;
	pNv = NVPTR(pScrn);

	/* Get the entity, and make sure it is PCI. */
	pNv->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
	if (pNv->pEnt->location.type != BUS_PCI)
		return FALSE;

	if (xf86IsEntityShared(pScrn->entityList[0])) {
		if(!xf86IsPrimInitDone(pScrn->entityList[0])) {
			pNv->Primary = TRUE;
			xf86SetPrimInitDone(pScrn->entityList[0]);
		} else {
			pNv->Secondary = TRUE;
		}
        }

	/* Find the PCI info for this screen */
	pNv->PciInfo = xf86GetPciInfoForEntity(pNv->pEnt->index);

	/* Initialise the kernel module */
	if (!NVPreInitDRM(pScrn))
		NVPreInitFail("\n");
	dev = pNv->dev;

	pScrn->chipset = malloc(sizeof(char) * 25);
	sprintf(pScrn->chipset, "NVIDIA NV%02x", dev->chipset);
	xf86DrvMsg(pScrn->scrnIndex, X_PROBED, "Chipset: \"%s\"\n", pScrn->chipset);

	switch (dev->chipset & 0xf0) {
	case 0x00:
		pNv->Architecture = NV_ARCH_04;
		break;
	case 0x10:
		pNv->Architecture = NV_ARCH_10;
		break;
	case 0x20:
		pNv->Architecture = NV_ARCH_20;
		break;
	case 0x30:
		pNv->Architecture = NV_ARCH_30;
		break;
	case 0x40:
	case 0x60:
		pNv->Architecture = NV_ARCH_40;
		break;
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
		pNv->Architecture = NV_ARCH_50;
		break;
	case 0xc0:
		pNv->Architecture = NV_ARCH_C0;
		break;
	default:
		return FALSE;
	}

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
		case 30:
			/* OK on NV50 KMS */
			if (pNv->Architecture < NV_ARCH_50)
				NVPreInitFail("Depth 30 supported on G80+ only\n");
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
	rgb rgbzeros = {0, 0, 0};

	if (pScrn->depth == 30) {
		rgb rgbmask;

		rgbmask.red   = 0x000003ff;
		rgbmask.green = 0x000ffc00;
		rgbmask.blue  = 0x3ff00000;
		if (!xf86SetWeight(pScrn, rgbzeros, rgbmask))
			NVPreInitFail("\n");

		/* xf86SetWeight() seems to think ffs(1) == 0... */
		pScrn->offset.red--;
		pScrn->offset.green--;
		pScrn->offset.blue--;
	} else {
		if (!xf86SetWeight(pScrn, rgbzeros, rgbzeros))
			NVPreInitFail("\n");
	}

	if (!xf86SetDefaultVisual(pScrn, -1))
		NVPreInitFail("\n");

	/* We don't support DirectColor */
	if (pScrn->defaultVisual != TrueColor) {
		NVPreInitFail("Given default visual (%s) is not supported at depth %d\n",
			      xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
	}

	/* We use a programmable clock */
	pScrn->progClock = TRUE;

	/* Collect all of the relevant option flags (fill in pScrn->options) */
	xf86CollectOptions(pScrn, NULL);

	/* Process the options */
	if (!(pNv->Options = malloc(sizeof(NVOptions))))
		return FALSE;
	memcpy(pNv->Options, NVOptions, sizeof(NVOptions));
	xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, pNv->Options);

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
		if (pNv->Architecture >= NV_ARCH_50)
			pNv->wfb_enabled = xf86ReturnOptValBool(
				pNv->Options, OPTION_WFB, FALSE);

		pNv->tiled_scanout = TRUE;
	}

	if (!pNv->NoAccel && pNv->dev->chipset >= 0x11) {
		from = X_DEFAULT;
		if (xf86GetOptValBool(pNv->Options, OPTION_GLX_VBLANK,
				      &pNv->glx_vblank))
			from = X_CONFIG;

		xf86DrvMsg(pScrn->scrnIndex, from, "GLX sync to VBlank %s.\n",
			   pNv->glx_vblank ? "enabled" : "disabled");
	}

#ifdef NOUVEAU_GETPARAM_HAS_PAGEFLIP
	ret = nouveau_device_get_param(pNv->dev,
				       NOUVEAU_GETPARAM_HAS_PAGEFLIP, &v);
	if (!ret)
		pNv->has_pageflip = v;
#else
	(void)v;
#endif

	if(xf86GetOptValInteger(pNv->Options, OPTION_VIDEO_KEY, &(pNv->videoKey))) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "video key set to 0x%x\n",
					pNv->videoKey);
	} else {
		pNv->videoKey =  (1 << pScrn->offset.red) | 
					(1 << pScrn->offset.green) |
		(((pScrn->mask.blue >> pScrn->offset.blue) - 1) << pScrn->offset.blue);
	}

	ret = drmmode_pre_init(pScrn, nouveau_device(pNv->dev)->fd,
			       pScrn->bitsPerPixel >> 3);
	if (ret == FALSE)
		NVPreInitFail("Kernel modesetting failed to initialize\n");

	/*
	 * If the driver can do gamma correction, it should call xf86SetGamma()
	 * here.
	 */
	Gamma gammazeros = {0.0, 0.0, 0.0};

	if (!xf86SetGamma(pScrn, gammazeros))
		NVPreInitFail("\n");

	/* No usable mode */
	if (!pScrn->modes)
		return FALSE;

	/* Set the current mode to the first in the list */
	pScrn->currentMode = pScrn->modes;

	/* Print the list of modes being used */
	xf86PrintModes(pScrn);

	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);

	if (pNv->wfb_enabled) {
		if (xf86LoadSubModule(pScrn, "wfb") == NULL)
			NVPreInitFail("\n");
	}

	if (xf86LoadSubModule(pScrn, "fb") == NULL)
		NVPreInitFail("\n");

	/* Load EXA if needed */
	if (!pNv->NoAccel) {
		if (!xf86LoadSubModule(pScrn, "exa")) {
			NVPreInitFail("\n");
		}
	}

	/* Load shadowfb */
	if (!xf86LoadSubModule(pScrn, "shadowfb"))
		NVPreInitFail("\n");

	return TRUE;
}


static Bool
NVMapMem(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);
	struct nouveau_device *dev = pNv->dev;
	int ret, pitch, size;

	ret = nouveau_allocate_surface(pScrn, pScrn->virtualX, pScrn->virtualY,
				       pScrn->bitsPerPixel,
				       NOUVEAU_CREATE_PIXMAP_SCANOUT,
				       &pitch, &pNv->scanout);
	if (!ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Error allocating scanout buffer: %d\n", ret);
		return FALSE;
	}

	pScrn->displayWidth = pitch / (pScrn->bitsPerPixel / 8);

	if (pNv->NoAccel)
		return TRUE;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "GART: %dMiB available\n",
		   (unsigned int)(dev->vm_gart_size >> 20));
	if (dev->vm_gart_size > (16 * 1024 * 1024))
		size = 16 * 1024 * 1024;
	else
		/* always leave 512kb for other things like the fifos */
		size = dev->vm_gart_size - 512*1024;

	if (nouveau_bo_new(dev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			   0, size, &pNv->GART)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Unable to allocate GART memory\n");
	}
	if (pNv->GART) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "GART: Allocated %dMiB as a scratch buffer\n",
			   (unsigned int)(pNv->GART->size >> 20));
	}

	return TRUE;
}

/*
 * Unmap the framebuffer and offscreen memory.
 */

static Bool
NVUnmapMem(ScrnInfoPtr pScrn)
{
	NVPtr pNv = NVPTR(pScrn);

	drmmode_remove_fb(pScrn);

	nouveau_bo_ref(NULL, &pNv->scanout);
	nouveau_bo_ref(NULL, &pNv->offscreen);
	nouveau_bo_ref(NULL, &pNv->GART);
	return TRUE;
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
			break;

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
			break;

		default:
			for (i = 0; i < numColors; i++) {
				index = indices[i];
				lut_r[index] = colors[index].red << 8;
				lut_g[index] = colors[index].green << 8;
				lut_b[index] = colors[index].blue << 8;
			}
			break;
		}

		if (crtc->randr_crtc)
			/* Make the change through RandR */
			RRCrtcGammaSet(crtc->randr_crtc, lut_r, lut_g, lut_b);
	}
}

/* Mandatory */

/* This gets called at the start of each server generation */
static Bool
NVScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	NVPtr pNv = NVPTR(pScrn);
	int ret;
	VisualPtr visual;
	unsigned char *FBStart;
	int displayWidth;

	if (!pNv->NoAccel) {
		if (!NVInitDma(pScrn) || !NVAccelCommonInit(pScrn)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Error initialising acceleration.  "
				   "Falling back to NoAccel\n");
			pNv->NoAccel = TRUE;
			pNv->ShadowFB = TRUE;
			pNv->wfb_enabled = FALSE;
			pNv->tiled_scanout = FALSE;
			pScrn->displayWidth = nv_pitch_align(pNv,
							     pScrn->virtualX,
							     pScrn->depth);
		}
	}

	if (!pNv->NoAccel)
		nouveau_dri2_init(pScreen);

	/* Allocate and map memory areas we need */
	if (!NVMapMem(pScrn))
		return FALSE;

	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
	int i;

	/* need to point to new screen on server regeneration */
	for (i = 0; i < xf86_config->num_crtc; i++)
		xf86_config->crtc[i]->scrn = pScrn;
	for (i = 0; i < xf86_config->num_output; i++)
		xf86_config->output[i]->scrn = pScrn;

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
			      miGetDefaultVisualMask(pScrn->depth),
			      pScrn->rgbBits, pScrn->defaultVisual))
		return FALSE;

	if (!miSetPixmapDepths ())
		return FALSE;

	/*
	 * Call the framebuffer layer's ScreenInit function, and fill in other
	 * pScreen fields.
	 */

	if (pNv->ShadowFB) {
		pNv->ShadowPitch = BitmapBytePad(pScrn->bitsPerPixel * pScrn->virtualX);
		pNv->ShadowPtr = malloc(pNv->ShadowPitch * pScrn->virtualY);
		displayWidth = pNv->ShadowPitch / (pScrn->bitsPerPixel >> 3);
		FBStart = pNv->ShadowPtr;
	} else
	if (pNv->NoAccel) {
		pNv->ShadowPtr = NULL;
		displayWidth = pScrn->displayWidth;
		nouveau_bo_map(pNv->scanout, NOUVEAU_BO_RDWR);
		FBStart = pNv->scanout->map;
		nouveau_bo_unmap(pNv->scanout);
	} else {
		pNv->ShadowPtr = NULL;
		displayWidth = pScrn->displayWidth;
		FBStart = NULL;
	}

	switch (pScrn->bitsPerPixel) {
	case 16:
	case 32:
	if (pNv->wfb_enabled) {
		ret = wfbScreenInit(pScreen, FBStart, pScrn->virtualX,
				    pScrn->virtualY, pScrn->xDpi, pScrn->yDpi,
				    displayWidth, pScrn->bitsPerPixel,
				    nouveau_wfb_setup_wrap,
				    nouveau_wfb_finish_wrap);
	} else {
		ret = fbScreenInit(pScreen, FBStart, pScrn->virtualX,
				   pScrn->virtualY, pScrn->xDpi, pScrn->yDpi,
				   displayWidth, pScrn->bitsPerPixel);
	}
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

	if (pNv->wfb_enabled)
		wfbPictureInit (pScreen, 0, 0);
	else
		fbPictureInit (pScreen, 0, 0);

	xf86SetBlackWhitePixels(pScreen);

	if (!pNv->NoAccel && !nouveau_exa_init(pScreen))
		return FALSE;

	miInitializeBackingStore(pScreen);
	xf86SetBackingStore(pScreen);
	xf86SetSilkenMouse(pScreen);

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
		ret = drmmode_cursor_init(pScreen);
		if (ret != TRUE) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "Hardware cursor initialization failed\n");
			pNv->HWCursor = FALSE;
		}
	}

	if (pNv->ShadowFB)
		ShadowFBInit(pScreen, NVRefreshArea);

	pScrn->fbOffset = 0;

	NVInitVideo(pScreen);

	/* Wrap the block handler here, if we do it after the EnterVT we
	 * can end up in the unfortunate case where we've wrapped the
	 * xf86RotateBlockHandler which sometimes is not expecting to
	 * be in the wrap chain and calls a NULL pointer...
	 */
	pNv->BlockHandler = pScreen->BlockHandler;
	pScreen->BlockHandler = NVBlockHandler;

	if (!AddCallback(&FlushCallback, NVFlushCallback, pScrn))
		return FALSE;

	pScrn->vtSema = TRUE;
	pScrn->pScreen = pScreen;

	xf86DPMSInit(pScreen, xf86DPMSSet, 0);

	/* Wrap the current CloseScreen function */
	pScreen->SaveScreen = NVSaveScreen;
	pNv->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = NVCloseScreen;
	pNv->CreateScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = NVCreateScreenResources;

	if (!xf86CrtcScreenInit(pScreen))
		return FALSE;

	/* Initialise default colourmap */
	if (!miCreateDefColormap(pScreen))
		return FALSE;

	/*
	 * Initialize colormap layer.
	 * Must follow initialization of the default colormap 
	 */
	if (!xf86HandleColormaps(pScreen, 256, 8, NVLoadPalette,
				 NULL, CMAP_PALETTED_TRUECOLOR))
		return FALSE;

	/* Report any unused options (only for the first generation) */
	if (serverGeneration == 1)
		xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

	drmmode_screen_init(pScreen);

	return TRUE;
}

static Bool
NVSaveScreen(ScreenPtr pScreen, int mode)
{
	return TRUE;
}

