#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vl_hwmc.h"
#include <os.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/XvMC.h>
#include <xf86.h>
#include <fourcc.h>

#define FOURCC_RGB	0x0000003
#define XVIMAGE_RGB								\
{										\
	FOURCC_RGB,								\
	XvRGB,									\
	LSBFirst,								\
	{									\
		'R', 'G', 'B', 0x00,						\
		0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71	\
	},									\
	32,									\
	XvPacked,								\
	1,									\
	24, 0x00FF0000, 0x0000FF00, 0x000000FF,					\
	0, 0, 0,								\
	0, 0, 0,								\
	0, 0, 0,								\
	{									\
		'B','G','R','X',						\
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0		\
	},									\
	XvTopToBottom								\
}

static int subpicture_index_list[] =
{
	FOURCC_RGB
};

static XF86MCImageIDList subpicture_list =
{
	1,
	subpicture_index_list
};

static XF86MCSurfaceInfoRec yv12_mpeg2_surface =
{
	FOURCC_YV12,
	XVMC_CHROMA_FORMAT_420,
	0,
	2048,
	2048,
	2048,
	2048,
	/*XVMC_IDCT*/ XVMC_MOCOMP | XVMC_MPEG_2,
	XVMC_INTRA_UNSIGNED | XVMC_SUBPICTURE_INDEPENDENT_SCALING | XVMC_BACKEND_SUBPICTURE,
	&subpicture_list
};

static XF86MCSurfaceInfoPtr surfaces[] =
{
	(XF86MCSurfaceInfoPtr)&yv12_mpeg2_surface
};

static XF86ImageRec rgb_subpicture = XVIMAGE_RGB;

static XF86ImagePtr subpictures[] =
{
	(XF86ImagePtr)&rgb_subpicture
};

static XF86MCAdaptorRec adaptor_template =
{
	"",
	1,
	surfaces,
	1,
	subpictures,
	(xf86XvMCCreateContextProcPtr)NULL,
	(xf86XvMCDestroyContextProcPtr)NULL,
	(xf86XvMCCreateSurfaceProcPtr)NULL,
	(xf86XvMCDestroySurfaceProcPtr)NULL,
	(xf86XvMCCreateSubpictureProcPtr)NULL,
	(xf86XvMCDestroySubpictureProcPtr)NULL
};

XF86MCAdaptorPtr vlCreateAdaptorXvMC(ScreenPtr pScreen, char *xv_adaptor_name)
{
	XF86MCAdaptorPtr	adaptor;
	ScrnInfoPtr		pScrn;
	
	assert(pScreen);
	assert(xv_adaptor_name);
	
	pScrn = xf86Screens[pScreen->myNum];
	adaptor = xf86XvMCCreateAdaptorRec();
	
	if (!adaptor)
	{
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "[XvMC] Memory allocation failed.\n");
		return NULL;
	}
	
	*adaptor = adaptor_template;
	adaptor->name = xv_adaptor_name;
	
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[XvMC] Associated with %s.\n", xv_adaptor_name);
	
	return adaptor;
}

void vlDestroyAdaptorXvMC(XF86MCAdaptorPtr adaptor)
{
	assert(adaptor);
	xf86XvMCDestroyAdaptorRec(adaptor);
}

/* TODO: Manage adaptor list and adaptor rec memory internally */

void vlInitXvMC(ScreenPtr pScreen, unsigned int num_adaptors, XF86MCAdaptorPtr *adaptors)
{
	ScrnInfoPtr	pScrn;
	int		i;
	
	assert(pScreen);
	assert(adaptors);
	
	for (i = 0; i < num_adaptors; ++i)
		assert(adaptors[i]);
	
	pScrn = xf86Screens[pScreen->myNum];
	
	if (!xf86XvMCScreenInit(pScreen, num_adaptors, adaptors))
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "[XvMC] Failed to initialize extension.\n");
	else
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[XvMC] Extension initialized.\n");
	
#if (XvMCVersion > 1) || (XvMCRevision > 0)
	/*
	if (xf86XvMCRegisterDRInfo(pScreen, "XvMCg3dvl", "0:0.0", -1, -1, -1) != Success)
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "[XvMC] Failed to register client library, using XvMCConfig.\n");
	else
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[XvMC] Registered client library.\n");
	*/
#endif
}

