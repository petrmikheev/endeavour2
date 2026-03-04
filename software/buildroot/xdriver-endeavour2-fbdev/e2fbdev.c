/*
Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is fur-
nished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the XFree86 Project shall not
be used in advertising or otherwise to promote the sale, use or other deal-
ings in this Software without prior written authorization from the XFree86
Project.
*/

/*
 * It is modified version of xdriver_xf86-video-fbdev-0.5.0/src/fbdev.c
 * Uses Endeavour2 DMA controller.
 * Modified by ptmikheev.
 *
 * Original authors:
 *       Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *       Michel Dänzer, <michel@tungstengraphics.com>
 */

#include "xorg-server.h"

/* Name of package */
#define PACKAGE "e2fbdev"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "-"

/* Define to the full name of this package. */
#define PACKAGE_NAME "e2fbdev"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "e2fbdev 0.5.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "e2fbdev"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.5.0"

/* Major version of this package */
#define PACKAGE_VERSION_MAJOR 0

/* Minor version of this package */
#define PACKAGE_VERSION_MINOR 5

/* Patch version of this package */
#define PACKAGE_VERSION_PATCHLEVEL 0

/* Version number of package */
#define VERSION "0.5.0"

#include <string.h>

/* all driver need this */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "inputstr.h"
#include "mipointer.h"
#include "mipointrst.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"
#include "dgaproc.h"

/* for visuals */
#include "fb.h"

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif

#include "fbdevhw.h"

#include "xf86xv.h"

#include "compat-api.h"

#ifdef XSERVER_LIBPCIACCESS
#include <pciaccess.h>
#endif

/* for xf86{Depth,FbBpp}. i am a terrible person, and i am sorry. */
#include "xf86Priv.h"

#include <endeavour2/display.h>

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) > 23
#define HAVE_SHADOW_3224
#endif

static Bool debug = 0;

#define TRACE_ENTER(str) \
    do { if (debug) ErrorF("fbdev: " str " %d\n",pScrn->scrnIndex); } while (0)
#define TRACE_EXIT(str) \
    do { if (debug) ErrorF("fbdev: " str " done\n"); } while (0)
#define TRACE(str) \
    do { if (debug) ErrorF("fbdev trace: " str "\n"); } while (0)

/* -------------------------------------------------------------------- */
/* prototypes                                                           */

static const OptionInfoRec * FBDevAvailableOptions(int chipid, int busid);
static void	FBDevIdentify(int flags);
static Bool	FBDevProbe(DriverPtr drv, int flags);
#ifdef XSERVER_LIBPCIACCESS
static Bool	FBDevPciProbe(DriverPtr drv, int entity_num,
     struct pci_device *dev, intptr_t match_data);
#endif
static Bool	FBDevPreInit(ScrnInfoPtr pScrn, int flags);
static Bool	FBDevScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool	FBDevCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static void *	FBDevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
				  CARD32 *size, void *closure);
static void	FBDevPointerMoved(SCRN_ARG_TYPE arg, int x, int y);
static Bool	FBDevDGAInit(ScrnInfoPtr pScrn, ScreenPtr pScreen);
static Bool	FBDevDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
				pointer ptr);


enum { FBDEV_ROTATE_NONE=0, FBDEV_ROTATE_CW=270, FBDEV_ROTATE_UD=180, FBDEV_ROTATE_CCW=90 };


/* -------------------------------------------------------------------- */

#define FBDEV_VERSION		4000
#define FBDEV_NAME		"E2FBDEV"
#define FBDEV_DRIVER_NAME	"e2fbdev"

#ifdef XSERVER_LIBPCIACCESS
static const struct pci_id_match fbdev_device_match[] = {
    {
	PCI_MATCH_ANY, PCI_MATCH_ANY, PCI_MATCH_ANY, PCI_MATCH_ANY,
	0x00030000, 0x00ffffff, 0
    },

    { 0, 0, 0 },
};
#endif

_X_EXPORT DriverRec E2FBDEV = {
	FBDEV_VERSION,
	FBDEV_DRIVER_NAME,
#if 0
	"driver for linux framebuffer devices",
#endif
	FBDevIdentify,
	FBDevProbe,
	FBDevAvailableOptions,
	NULL,
	0,
	FBDevDriverFunc,

#ifdef XSERVER_LIBPCIACCESS
    fbdev_device_match,
    FBDevPciProbe
#endif
};

/* Supported "chipsets" */
static SymTabRec FBDevChipsets[] = {
    { 0, "e2fbdev" },
    {-1, NULL }
};

/* Supported options */
typedef enum {
	OPTION_SHADOW_FB,
	OPTION_ROTATE,
	OPTION_FBDEV,
	OPTION_DEBUG
} FBDevOpts;

static const OptionInfoRec FBDevOptions[] = {
	{ OPTION_SHADOW_FB,	"ShadowFB",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_ROTATE,	"Rotate",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_FBDEV,		"fbdev",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_DEBUG,		"debug",	OPTV_BOOLEAN,	{0},	FALSE },
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

/* -------------------------------------------------------------------- */

#ifdef XFree86LOADER

MODULESETUPPROTO(FBDevSetup);

static XF86ModuleVersionInfo FBDevVersRec =
{
	"e2fbdev",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData e2fbdevModuleData = { &FBDevVersRec, FBDevSetup, NULL };

pointer
FBDevSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&E2FBDEV, module, HaveDriverFuncs);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

#endif /* XFree86LOADER */

struct DmaCmd { unsigned lo, hi; };

typedef volatile struct DmaCmd* DmaCmdPtr;

/* -------------------------------------------------------------------- */
/* our private data, and two functions to allocate/free this            */

typedef struct {
	unsigned char*			fbstart;
	unsigned char*			fbmem;
	int				fboff;
	int				lineLength;
	int				rotate;
	Bool				shadowFB;
        Bool                            shadow24;
	void				*shadow;
	CloseScreenProcPtr		CloseScreen;
    CopyWindowProcPtr CopyWindow;
	CreateScreenResourcesProcPtr	CreateScreenResources;
	void				(*PointerMoved)(SCRN_ARG_TYPE arg, int x, int y);
	EntityInfoPtr			pEnt;
	/* DGA info */
	DGAModePtr			pDGAMode;
	int				nDGAMode;
	OptionInfoPtr			Options;

	int e2_display_fd;
	DmaCmdPtr dma_commands;
} FBDevRec, *FBDevPtr;

#define FRONT_ADDR  GRAPHIC_BUFFER(0)
#define SHADOW_ADDR GRAPHIC_BUFFER(1)
#define DMA_CMD_ADDR (SHADOW_ADDR + 6 * (1<<20))  // 2 MB, max 256K commands

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

static Bool
FBDevGetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;
	
	pScrn->driverPrivate = xnfcalloc(sizeof(FBDevRec), 1);
	return TRUE;
}

static void
FBDevFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/* -------------------------------------------------------------------- */

static const OptionInfoRec *
FBDevAvailableOptions(int chipid, int busid)
{
	return FBDevOptions;
}

static void
FBDevIdentify(int flags)
{
	xf86PrintChipsets(FBDEV_NAME, "driver for endeavour2 framebuffer", FBDevChipsets);
}

static Bool
fbdevSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    return fbdevHWSwitchMode(pScrn, mode);
}

static void
fbdevAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    fbdevHWAdjustFrame(pScrn, x, y);
}

static Bool
fbdevEnterVT(ScrnInfoPtr pScrn)
{
    return fbdevHWEnterVT(pScrn);
}

static void
fbdevLeaveVT(ScrnInfoPtr pScrn)
{
    fbdevHWLeaveVT(pScrn);
}

static ModeStatus
fbdevValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode, Bool verbose, int flags)
{
    return fbdevHWValidMode(pScrn, mode, verbose, flags);
}

#ifdef XSERVER_LIBPCIACCESS
static Bool FBDevPciProbe(DriverPtr drv, int entity_num,
			  struct pci_device *dev, intptr_t match_data)
{
    ScrnInfoPtr pScrn = NULL;

    if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
	return FALSE;
	    
    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, NULL, NULL,
				NULL, NULL, NULL, NULL);
    if (pScrn) {
	char *device;
	GDevPtr devSection = xf86GetDevFromEntity(pScrn->entityList[0],
						  pScrn->entityInstanceList[0]);

	device = xf86FindOptionValue(devSection->options, "fbdev");
	if (fbdevHWProbe(dev, device, NULL)) {
	    pScrn->driverVersion = FBDEV_VERSION;
	    pScrn->driverName    = FBDEV_DRIVER_NAME;
	    pScrn->name          = FBDEV_NAME;
	    pScrn->Probe         = FBDevProbe;
	    pScrn->PreInit       = FBDevPreInit;
	    pScrn->ScreenInit    = FBDevScreenInit;
	    pScrn->SwitchMode    = fbdevSwitchMode;
	    pScrn->AdjustFrame   = fbdevAdjustFrame;
	    pScrn->EnterVT       = fbdevEnterVT;
	    pScrn->LeaveVT       = fbdevLeaveVT;
	    pScrn->ValidMode     = fbdevValidMode;

	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "claimed PCI slot %d@%d:%d:%d\n", 
		       dev->bus, dev->domain, dev->dev, dev->func);
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "using %s\n", device ? device : "default device");
	}
	else {
	    pScrn = NULL;
	}
    }

    return (pScrn != NULL);
}
#endif


static Bool
FBDevProbe(DriverPtr drv, int flags)
{
	int i;
	ScrnInfoPtr pScrn;
       	GDevPtr *devSections;
	int numDevSections;
#ifndef XSERVER_LIBPCIACCESS
	int bus,device,func;
#endif
	char *dev;
	Bool foundScreen = FALSE;

	TRACE("probe start");

	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

	if ((numDevSections = xf86MatchDevice(FBDEV_DRIVER_NAME, &devSections)) <= 0) 
	    return FALSE;
	
	if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
	    return FALSE;
	    
	for (i = 0; i < numDevSections; i++) {
	    Bool isIsa = FALSE;
	    Bool isPci = FALSE;

	    dev = xf86FindOptionValue(devSections[i]->options,"fbdev");
	    if (devSections[i]->busID) {
#ifndef XSERVER_LIBPCIACCESS
	        if (xf86ParsePciBusString(devSections[i]->busID,&bus,&device,
					  &func)) {
		    if (!xf86CheckPciSlot(bus,device,func))
		        continue;
		    isPci = TRUE;
		} else
#endif
#ifdef HAVE_ISA
		if (xf86ParseIsaBusString(devSections[i]->busID))
		    isIsa = TRUE;
		else
#endif
		    0;
		  
	    }
	    if (fbdevHWProbe(NULL,dev,NULL)) {
		pScrn = NULL;
		if (isPci) {
#ifndef XSERVER_LIBPCIACCESS
		    /* XXX what about when there's no busID set? */
		    int entity;
		    
		    entity = xf86ClaimPciSlot(bus,device,func,drv,
					      0,devSections[i],
					      TRUE);
		    pScrn = xf86ConfigPciEntity(pScrn,0,entity,
						      NULL,RES_SHARED_VGA,
						      NULL,NULL,NULL,NULL);
		    /* xf86DrvMsg() can't be called without setting these */
		    pScrn->driverName    = FBDEV_DRIVER_NAME;
		    pScrn->name          = FBDEV_NAME;
		    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
			       "claimed PCI slot %d:%d:%d\n",bus,device,func);

#endif
		} else if (isIsa) {
#ifdef HAVE_ISA
		    int entity;
		    
		    entity = xf86ClaimIsaSlot(drv, 0,
					      devSections[i], TRUE);
		    pScrn = xf86ConfigIsaEntity(pScrn,0,entity,
						      NULL,RES_SHARED_VGA,
						      NULL,NULL,NULL,NULL);
#endif
		} else {
		   int entity;

		    entity = xf86ClaimFbSlot(drv, 0,
					      devSections[i], TRUE);
		    pScrn = xf86ConfigFbEntity(pScrn,0,entity,
					       NULL,NULL,NULL,NULL);
		   
		}
		if (pScrn) {
		    foundScreen = TRUE;
		    
		    pScrn->driverVersion = FBDEV_VERSION;
		    pScrn->driverName    = FBDEV_DRIVER_NAME;
		    pScrn->name          = FBDEV_NAME;
		    pScrn->Probe         = FBDevProbe;
		    pScrn->PreInit       = FBDevPreInit;
		    pScrn->ScreenInit    = FBDevScreenInit;
		    pScrn->SwitchMode    = fbdevSwitchMode;
		    pScrn->AdjustFrame   = fbdevAdjustFrame;
		    pScrn->EnterVT       = fbdevEnterVT;
		    pScrn->LeaveVT       = fbdevLeaveVT;
		    pScrn->ValidMode     = fbdevValidMode;
		    
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			       "using %s\n", dev ? dev : "default device");
		}
	    }
	}
	free(devSections);
	TRACE("probe done");
	return foundScreen;
}

static Bool
FBDevPreInit(ScrnInfoPtr pScrn, int flags)
{
	FBDevPtr fPtr;
	int default_depth, fbbpp;
	const char *s;
	int type;
	void *pci_dev = NULL;

	if (flags & PROBE_DETECT) return FALSE;

	TRACE_ENTER("PreInit");

	/* Check the number of entities, and fail if it isn't one. */
	if (pScrn->numEntities != 1)
		return FALSE;

	pScrn->monitor = pScrn->confScreen->monitor;

	FBDevGetRec(pScrn);
	fPtr = FBDEVPTR(pScrn);

	fPtr->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

#ifndef XSERVER_LIBPCIACCESS
	pScrn->racMemFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;
	/* XXX Is this right?  Can probably remove RAC_FB */
	pScrn->racIoFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;

	if (fPtr->pEnt->location.type == BUS_PCI &&
	    xf86RegisterResources(fPtr->pEnt->index,NULL,ResExclusive)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "xf86RegisterResources() found resource conflicts\n");
		return FALSE;
	}
#else
	if (fPtr->pEnt->location.type == BUS_PCI)
	    pci_dev = fPtr->pEnt->location.id.pci;
#endif
	/* open device */
	if (!fbdevHWInit(pScrn, pci_dev,
			 xf86FindOptionValue(fPtr->pEnt->device->options,
					     "fbdev")))
		return FALSE;
	default_depth = fbdevHWGetDepth(pScrn,&fbbpp);

	if (default_depth == 8) do {
	    /* trust the command line */
	    if (xf86FbBpp > 0 || xf86Depth > 0)
		break;

	    /* trust the config file's Screen stanza */
	    if (pScrn->confScreen->defaultfbbpp > 0 ||
		pScrn->confScreen->defaultdepth > 0)
		break;

	    /* trust our Device stanza in the config file */
	    if (xf86FindOption(fPtr->pEnt->device->options, "DefaultDepth") ||
		xf86FindOption(fPtr->pEnt->device->options, "DefaultFbBpp"))
		break;

	    /* otherwise, lol no */
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "Console is 8bpp, defaulting to 32bpp\n");
	    default_depth = 24;
	    fbbpp = 32;
	} while (0);

        fPtr->shadow24 = FALSE;
#ifdef HAVE_SHADOW_3224
        /* okay but 24bpp is awful */
        if (fbbpp == 24) {
            fPtr->shadow24 = TRUE;
            fbbpp = 32;
        }
#endif

	if (!xf86SetDepthBpp(pScrn, default_depth, default_depth, fbbpp,
			     Support24bppFb | Support32bppFb | SupportConvert32to24 | SupportConvert24to32))
		return FALSE;
	xf86PrintDepthBpp(pScrn);

	/* color weight */
	if (pScrn->depth > 8) {
		rgb zeros = { 0, 0, 0 };
		if (!xf86SetWeight(pScrn, zeros, zeros))
			return FALSE;
	}

	/* visual init */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;

	/* We don't currently support DirectColor at > 8bpp */
	if (pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "requested default visual"
			   " (%s) is not supported at depth %d\n",
			   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
		return FALSE;
	}

	{
		Gamma zeros = {0.0, 0.0, 0.0};

		if (!xf86SetGamma(pScrn,zeros)) {
			return FALSE;
		}
	}

	pScrn->progClock = TRUE;
	pScrn->rgbBits   = 8;
	pScrn->chipset   = "e2fbdev";
	pScrn->videoRam  = fbdevHWGetVidmem(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware: %s (video memory:"
		   " %dkB)\n", fbdevHWGetName(pScrn), pScrn->videoRam/1024);

	/* handle options */
	xf86CollectOptions(pScrn, NULL);
	if (!(fPtr->Options = malloc(sizeof(FBDevOptions))))
		return FALSE;
	memcpy(fPtr->Options, FBDevOptions, sizeof(FBDevOptions));
	xf86ProcessOptions(pScrn->scrnIndex, fPtr->pEnt->device->options, fPtr->Options);

	/* use shadow framebuffer by default */
	fPtr->shadowFB = xf86ReturnOptValBool(fPtr->Options, OPTION_SHADOW_FB, TRUE);
        if (!fPtr->shadowFB && fPtr->shadow24) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "24bpp requires shadow framebuffer, forcing\n");
            fPtr->shadowFB = TRUE;
        }

	debug = xf86ReturnOptValBool(fPtr->Options, OPTION_DEBUG, FALSE);

	/* rotation */
	fPtr->rotate = FBDEV_ROTATE_NONE;
	s = xf86GetOptValString(fPtr->Options, OPTION_ROTATE);
	if (s && !fPtr->shadow24)
	{
	  if(!xf86NameCmp(s, "CW"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_CW;
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "rotating screen clockwise\n");
	  }
	  else if(!xf86NameCmp(s, "CCW"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_CCW;
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "rotating screen counter-clockwise\n");
	  }
	  else if(!xf86NameCmp(s, "UD"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_UD;
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "rotating screen upside-down\n");
	  }
	  else
	  {
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "\"%s\" is not a valid value for Option \"Rotate\"\n", s);
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "valid options are \"CW\", \"CCW\" and \"UD\"\n");
	  }
	}

	/* select video modes */

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checking modes against framebuffer device...\n");
	fbdevHWSetVideoModes(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checking modes against monitor...\n");
	{
		DisplayModePtr mode, first = mode = pScrn->modes;
		
		if (mode != NULL) do {
			mode->status = xf86CheckModeForMonitor(mode, pScrn->monitor);
			mode = mode->next;
		} while (mode != NULL && mode != first);

		xf86PruneDriverModes(pScrn);
	}

	if (NULL == pScrn->modes)
		fbdevHWUseBuildinMode(pScrn);
	pScrn->currentMode = pScrn->modes;

	/* First approximation, may be refined in ScreenInit */
	pScrn->displayWidth = pScrn->virtualX;

	xf86PrintModes(pScrn);

	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);

	/* Load bpp-specific modules */
	switch ((type = fbdevHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel)
		{
		case 8:
		case 16:
		case 24:
		case 32:
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unsupported number of bits per pixel: %d",
			pScrn->bitsPerPixel);
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
               /* Not supported yet, don't know what to do with this */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "interleaved planes are not yet supported by the "
			  "fbdev driver\n");
		return FALSE;
	case FBDEVHW_TEXT:
               /* This should never happen ...
                * we should check for this much much earlier ... */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "text mode is not supported by the fbdev driver\n");
		return FALSE;
       case FBDEVHW_VGA_PLANES:
               /* Not supported yet */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "EGA/VGA planes are not yet supported by the fbdev "
			  "driver\n");
               return FALSE;
       default:
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "unrecognised fbdev hardware type (%d)\n", type);
               return FALSE;
	}
	if (xf86LoadSubModule(pScrn, "fb") == NULL) {
		FBDevFreeRec(pScrn);
		return FALSE;
	}

	/* Load shadow if needed */
	if (fPtr->shadowFB) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "using shadow framebuffer\n");
		if (!xf86LoadSubModule(pScrn, "shadow")) {
			FBDevFreeRec(pScrn);
			return FALSE;
		}
	}

	TRACE_EXIT("PreInit");
	return TRUE;
}

static void
fbdevUpdate32to24(ScreenPtr pScreen, shadowBufPtr pBuf)
{
#ifdef HAVE_SHADOW_3224
    shadowUpdate32to24(pScreen, pBuf);
#endif
}

static void
fbdevUpdateRotatePacked(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    shadowUpdateRotatePacked(pScreen, pBuf);
}

static void
fbdevUpdatePacked(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    //shadowUpdatePacked(pScreen, pBuf);
    RegionPtr damage = DamageRegion(pBuf->pDamage);
    unsigned shadow_line_size = pBuf->pPixmap->devKind;
    int nbox = RegionNumRects(damage);
    BoxPtr pbox = RegionRects(damage);

    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    DmaCmdPtr cmds = fPtr->dma_commands;

    while (nbox--) {
        unsigned from = (pbox->x1 & ~31) << 1;
        unsigned to = ((pbox->x2 + 31) & ~31) << 1;
        unsigned cmd_read  = DMA_CMD_HI(DMA_READ_SYNC, from, to);
        unsigned cmd_write = DMA_CMD_HI(DMA_WRITE_SYNC, from, to);
        unsigned src = SHADOW_ADDR + pbox->y1 * shadow_line_size + from;
        unsigned dst = FRONT_ADDR + pbox->y1 * GRAPHIC_LINE_SIZE + from;
        for (unsigned y = pbox->y1; y < pbox->y2; ++y) {
            cmds[0].lo = src;
            cmds[0].hi = cmd_read;
            cmds[1].lo = dst;
            cmds[1].hi = cmd_write;
            src += shadow_line_size;
            dst += GRAPHIC_LINE_SIZE;
            cmds += 2;
        }
        pbox++;
    }

    display_dma(fPtr->e2_display_fd, DMA_CMD_ADDR, cmds - fPtr->dma_commands, /*sync=*/1);
}

static Bool
FBDevCreateScreenResources(ScreenPtr pScreen)
{
    PixmapPtr pPixmap;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    Bool ret;
    void (*update)(ScreenPtr, shadowBufPtr);

    pScreen->CreateScreenResources = fPtr->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = FBDevCreateScreenResources;

    if (!ret)
	return FALSE;

    pPixmap = pScreen->GetScreenPixmap(pScreen);

    if (fPtr->shadow24)
        update = fbdevUpdate32to24;
    else if (fPtr->rotate)
        update = fbdevUpdateRotatePacked;
    else
        update = fbdevUpdatePacked;

    if (!shadowAdd(pScreen, pPixmap, update, FBDevWindowLinear, fPtr->rotate,
                   NULL)) {
	return FALSE;
    }

    return TRUE;
}

static Bool
FBDevShadowInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    
    if (!shadowSetup(pScreen)) {
	return FALSE;
    }

    fPtr->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = FBDevCreateScreenResources;

    return TRUE;
}

static void
fbdevLoadPalette(ScrnInfoPtr pScrn, int num, int *i, LOCO *col, VisualPtr pVis)
{
    fbdevHWLoadPalette(pScrn, num, i, col, pVis);
}

static void
fbdevDPMSSet(ScrnInfoPtr pScrn, int mode, int flags)
{
    fbdevHWDPMSSet(pScrn, mode, flags);
}

static Bool
fbdevSaveScreen(ScreenPtr pScreen, int mode)
{
    return fbdevHWSaveScreen(pScreen, mode);
}

static void
FBDevCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    PixmapPtr pScreenPixmap = pScreen->GetScreenPixmap(pScreen);

#if 0
	fPtr->CopyWindow(pWin, ptOldOrg, prgnSrc);
#else

    miPointerScreenPtr pPtrPriv = 
        (miPointerScreenPtr)dixLookupPrivate(&pScreen->devPrivates, miPointerScreenKey);

    // Hide mouse cursos
    CursorPtr pCurrentCursor = NULL;
    int cursor_x, cursor_y;
    if (inputInfo.pointer && inputInfo.pointer->spriteInfo && inputInfo.pointer->spriteInfo->sprite && pPtrPriv && pPtrPriv->spriteFuncs) {
        SpritePtr pSprite = inputInfo.pointer->spriteInfo->sprite;
        pCurrentCursor = pSprite->current;
        cursor_x = pSprite->hotPhys.x;
        cursor_y = pSprite->hotPhys.y;
        pPtrPriv->spriteFuncs->SetCursor(inputInfo.pointer, pScreen, NULL, cursor_x, cursor_y);
    }

    unsigned stride = fPtr->shadowFB ? pScrn->displayWidth * 2 : GRAPHIC_LINE_SIZE; //pScreen->GetScreenPixmap(pScreen)->devKind;
    unsigned base_addr = fPtr->shadowFB ? SHADOW_ADDR : FRONT_ADDR;

    int dx = pWin->drawable.x - ptOldOrg.x;
    int dy = pWin->drawable.y - ptOldOrg.y;

    RegionRec rgnDst;

    RegionInit(&rgnDst, NullBox, 0);
    RegionCopy(&rgnDst, prgnSrc);
    RegionTranslate(&rgnDst, dx, dy);
    RegionIntersect(&rgnDst, &rgnDst, &pWin->borderClip);

    BoxPtr pbox = RegionRects(&rgnDst);
    int nbox = RegionNumRects(&rgnDst);

    DmaCmdPtr cmds = fPtr->dma_commands;

    while (nbox--) {
        if (pbox->x1 >= pbox->x2) continue;
        int dst_block_start = (pbox->x1 & ~31) << 1;
        int dst_block_end = ((pbox->x2 + 31) & ~31) << 1;
        int src_block_start = ((pbox->x1-dx) & ~31) << 1;
        int src_block_end = ((pbox->x2-dx + 31) & ~31) << 1;
        unsigned DST_READL = DMA_CMD_HI(DMA_READ, dst_block_start, dst_block_start + 64);
        unsigned DST_READR = DMA_CMD_HI(DMA_READ, dst_block_end - 64, dst_block_end);
        unsigned SRC_READ  = DMA_CMD_HI(DMA_READ_SYNC, src_block_start + 4096, src_block_end + 4096);
        unsigned COPY_LO   = ((pbox->x1 - dx) << 1) + 4096;
        unsigned COPY_HI   = DMA_CMD_HI(DMA_COPY, pbox->x1 << 1, pbox->x2 << 1);
        unsigned DST_WRITE = DMA_CMD_HI(DMA_WRITE_SYNC, dst_block_start, dst_block_end);
        unsigned dst_line, src_line, line_step;
        if (dy <= 0) {
            dst_line = base_addr + pbox->y1 * stride;
            src_line = base_addr + (pbox->y1 - dy) * stride;
            line_step = stride;
        } else {
            dst_line = base_addr + (pbox->y2 - 1) * stride;
            src_line = base_addr + (pbox->y2 - dy - 1) * stride;
            line_step = -stride;
        }
        for (unsigned i = pbox->y1; i < pbox->y2; ++i) {
            cmds->lo = dst_line + dst_block_start;
            cmds->hi = DST_READL;
            cmds++;
            if (DST_READL != DST_READR) {
                cmds->lo = dst_line + dst_block_end - 64;
                cmds->hi = DST_READR;
                cmds++;
            }
            cmds[0].lo = src_line + src_block_start;
            cmds[0].hi = SRC_READ;
            cmds[1].lo = COPY_LO;
            cmds[1].hi = COPY_HI;
            cmds[2].lo = dst_line + dst_block_start;
            cmds[2].hi = DST_WRITE;
            cmds += 3;
            dst_line += line_step;
            src_line += line_step;
        }
        pbox++;
    }

    display_dma(fPtr->e2_display_fd, DMA_CMD_ADDR, cmds - fPtr->dma_commands, /*sync=*/1);

    if (fPtr->shadowFB) {
        RegionUnion(&rgnDst, &rgnDst, prgnSrc);
        DamageRegionAppend(&pScreenPixmap->drawable, &rgnDst);
        DamageRegionProcessPending(&pScreenPixmap->drawable);
    }

    RegionUninit(&rgnDst);

    // Restore mouse cursos
    if (pCurrentCursor)
        pPtrPriv->spriteFuncs->SetCursor(inputInfo.pointer, pScreen, pCurrentCursor, cursor_x, cursor_y);
#endif
}

static Bool
FBDevScreenInit(SCREEN_INIT_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	VisualPtr visual;
	int init_picture = 0;
	int ret, flags;
	int type;

	TRACE_ENTER("FBDevScreenInit");

	//xf86DrvMsg(pScrn->scrnIndex,X_INFO,"E2FBDEV FBDevScreenInit\n");
	fPtr->e2_display_fd = display_open();

#if DEBUG
	ErrorF("\tbitsPerPixel=%d, depth=%d, defaultVisual=%s\n"
	       "\tmask: %x,%x,%x, offset: %d,%d,%d\n",
	       pScrn->bitsPerPixel,
	       pScrn->depth,
	       xf86GetVisualName(pScrn->defaultVisual),
	       pScrn->mask.red,pScrn->mask.green,pScrn->mask.blue,
	       pScrn->offset.red,pScrn->offset.green,pScrn->offset.blue);
#endif

	if (NULL == (fPtr->fbmem = fbdevHWMapVidmem(pScrn))) {
	        xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"mapping of video memory"
			   " failed\n");
		return FALSE;
	}
	fPtr->fboff = fbdevHWLinearOffset(pScrn);

	fbdevHWSave(pScrn);

	if (!fbdevHWModeInit(pScrn, pScrn->currentMode)) {
		xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"mode initialization failed\n");
		return FALSE;
	}
	fbdevHWSaveScreen(pScreen, SCREEN_SAVER_ON);
	fbdevHWAdjustFrame(ADJUST_FRAME_ARGS(pScrn, 0, 0));

	/* mi layer */
	miClearVisualTypes();
	if (pScrn->bitsPerPixel > 8) {
		if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
				   " for %d bits per pixel [1]\n",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	} else {
		if (!miSetVisualTypes(pScrn->depth,
				      miGetDefaultVisualMask(pScrn->depth),
				      pScrn->rgbBits, pScrn->defaultVisual)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
				   " for %d bits per pixel [2]\n",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	}
	if (!miSetPixmapDepths()) {
	  xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"pixmap depth setup failed\n");
	  return FALSE;
	}

	if(fPtr->rotate==FBDEV_ROTATE_CW || fPtr->rotate==FBDEV_ROTATE_CCW)
	{
	  int tmp = pScrn->virtualX;
	  pScrn->virtualX = pScrn->displayWidth = pScrn->virtualY;
	  pScrn->virtualY = tmp;
	} else if (!fPtr->shadowFB) {
		/* FIXME: this doesn't work for all cases, e.g. when each scanline
			has a padding which is independent from the depth (controlfb) */
		pScrn->displayWidth = fbdevHWGetLineLength(pScrn) /
				      (pScrn->bitsPerPixel / 8);

		if (pScrn->displayWidth != pScrn->virtualX) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				   "Pitch updated to %d after ModeInit\n",
				   pScrn->displayWidth);
		}
	}

	if(fPtr->rotate && !fPtr->PointerMoved) {
		fPtr->PointerMoved = pScrn->PointerMoved;
		pScrn->PointerMoved = FBDevPointerMoved;
	}

	fPtr->fbstart = fPtr->fbmem + fPtr->fboff;

	if (fPtr->shadowFB) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "mmaping shadow framebuffer\n");
		fPtr->shadow = display_map_video_memory(fPtr->e2_display_fd, SHADOW_ADDR, GRAPHIC_BUFFER_SIZE);
		fPtr->dma_commands = fPtr->shadow + 6 * (1<<20);
	    //fPtr->shadow = calloc(1, pScrn->displayWidth * pScrn->virtualY *
				//  ((pScrn->bitsPerPixel + 7) / 8));

	    if (!fPtr->shadow) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to allocate shadow framebuffer\n");
		return FALSE;
	    }
	    memset(fPtr->shadow, 0, GRAPHIC_BUFFER_SIZE);
	} else {
        fPtr->dma_commands = display_map_video_memory(fPtr->e2_display_fd, DMA_CMD_ADDR, 2<<20);
    }

	switch ((type = fbdevHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel) {
		case 8:
		case 16:
		case 24:
		case 32:
			ret = fbScreenInit(pScreen, fPtr->shadowFB ? fPtr->shadow
					   : fPtr->fbstart, pScrn->virtualX,
					   pScrn->virtualY, pScrn->xDpi,
					   pScrn->yDpi, pScrn->displayWidth,
					   pScrn->bitsPerPixel);
			init_picture = 1;
			break;
	 	default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "internal error: invalid number of bits per"
				   " pixel (%d) encountered in"
				   " FBDevScreenInit()\n", pScrn->bitsPerPixel);
			ret = FALSE;
			break;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: interleaved planes are not yet "
			   "supported by the fbdev driver\n");
		ret = FALSE;
		break;
	case FBDEVHW_TEXT:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: text mode is not supported by the "
			   "fbdev driver\n");
		ret = FALSE;
		break;
	case FBDEVHW_VGA_PLANES:
		/* Not supported yet */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: EGA/VGA Planes are not yet "
			   "supported by the fbdev driver\n");
		ret = FALSE;
		break;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: unrecognised hardware type (%d) "
			   "encountered in FBDevScreenInit()\n", type);
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
				visual->offsetRed   = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue  = pScrn->offset.blue;
				visual->redMask     = pScrn->mask.red;
				visual->greenMask   = pScrn->mask.green;
				visual->blueMask    = pScrn->mask.blue;
			}
		}
	}

	/* must be after RGB ordering fixed */
	if (init_picture && !fbPictureInit(pScreen, NULL, 0))
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Render extension initialisation failed\n");

	if (fPtr->shadowFB && !FBDevShadowInit(pScreen)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "shadow framebuffer initialization failed\n");
	    return FALSE;
	}

	if (!fPtr->rotate)
	  FBDevDGAInit(pScrn, pScreen);
	else {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "display rotated; disabling DGA\n");
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "using driver rotation; disabling "
			                "XRandR\n");
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 24
	  xf86DisableRandR();
#endif
	  if (pScrn->bitsPerPixel == 24)
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "rotation might be broken at 24 "
                                             "bits per pixel\n");
	}

	xf86SetBlackWhitePixels(pScreen);
	xf86SetBackingStore(pScreen);

	/* software cursor */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/* colormap */
	switch ((type = fbdevHWGetType(pScrn)))
	{
	/* XXX It would be simpler to use miCreateDefColormap() in all cases. */
	case FBDEVHW_PACKED_PIXELS:
		if (!miCreateDefColormap(pScreen)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                   "internal error: miCreateDefColormap failed "
				   "in FBDevScreenInit()\n");
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: interleaved planes are not yet "
			   "supported by the fbdev driver\n");
		return FALSE;
	case FBDEVHW_TEXT:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: text mode is not supported by "
			   "the fbdev driver\n");
		return FALSE;
	case FBDEVHW_VGA_PLANES:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: EGA/VGA planes are not yet "
			   "supported by the fbdev driver\n");
		return FALSE;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: unrecognised fbdev hardware type "
			   "(%d) encountered in FBDevScreenInit()\n", type);
		return FALSE;
	}
	flags = CMAP_PALETTED_TRUECOLOR;
	if(!xf86HandleColormaps(pScreen, 256, 8, fbdevLoadPalette, NULL, flags))
		return FALSE;

	xf86DPMSInit(pScreen, fbdevDPMSSet, 0);

	pScreen->SaveScreen = fbdevSaveScreen;

	/* Wrap the current CloseScreen function */
	fPtr->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = FBDevCloseScreen;

    fPtr->CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = FBDevCopyWindow;

#if XV
	{
	    XF86VideoAdaptorPtr *ptr;

	    int n = xf86XVListGenericAdaptors(pScrn,&ptr);
	    if (n) {
		xf86XVScreenInit(pScreen,ptr,n);
	    }
	}
#endif

	TRACE_EXIT("FBDevScreenInit");

	return TRUE;
}

static Bool
FBDevCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);
	
	close(fPtr->e2_display_fd);
	fbdevHWRestore(pScrn);
	fbdevHWUnmapVidmem(pScrn);
	if (fPtr->shadow) {
	    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));
	    //free(fPtr->shadow);
	    fPtr->shadow = NULL;
	}
	if (fPtr->pDGAMode) {
	  free(fPtr->pDGAMode);
	  fPtr->pDGAMode = NULL;
	  fPtr->nDGAMode = 0;
	}
	pScrn->vtSema = FALSE;

	pScreen->CreateScreenResources = fPtr->CreateScreenResources;
	pScreen->CloseScreen = fPtr->CloseScreen;
    pScreen->CopyWindow = fPtr->CopyWindow;
	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}


/***********************************************************************
 * Shadow stuff
 ***********************************************************************/

static void *
FBDevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
		 CARD32 *size, void *closure)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (!pScrn->vtSema)
      return NULL;

    if (fPtr->lineLength)
      *size = fPtr->lineLength;
    else
      *size = fPtr->lineLength = fbdevHWGetLineLength(pScrn);

    return ((CARD8 *)fPtr->fbstart + row * fPtr->lineLength + offset);
}

static void
FBDevPointerMoved(SCRN_ARG_TYPE arg, int x, int y)
{
    SCRN_INFO_PTR(arg);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    int newX, newY;

    switch (fPtr->rotate)
    {
    case FBDEV_ROTATE_CW:
	/* 90 degrees CW rotation. */
	newX = pScrn->pScreen->height - y - 1;
	newY = x;
	break;

    case FBDEV_ROTATE_CCW:
	/* 90 degrees CCW rotation. */
	newX = y;
	newY = pScrn->pScreen->width - x - 1;
	break;

    case FBDEV_ROTATE_UD:
	/* 180 degrees UD rotation. */
	newX = pScrn->pScreen->width - x - 1;
	newY = pScrn->pScreen->height - y - 1;
	break;

    default:
	/* No rotation. */
	newX = x;
	newY = y;
	break;
    }

    /* Pass adjusted pointer coordinates to wrapped PointerMoved function. */
    (*fPtr->PointerMoved)(arg, newX, newY);
}


/***********************************************************************
 * DGA stuff
 ***********************************************************************/
static Bool FBDevDGAOpenFramebuffer(ScrnInfoPtr pScrn, char **DeviceName,
				   unsigned char **ApertureBase,
				   int *ApertureSize, int *ApertureOffset,
				   int *flags);
static Bool FBDevDGASetMode(ScrnInfoPtr pScrn, DGAModePtr pDGAMode);
static void FBDevDGASetViewport(ScrnInfoPtr pScrn, int x, int y, int flags);

static Bool
FBDevDGAOpenFramebuffer(ScrnInfoPtr pScrn, char **DeviceName,
		       unsigned char **ApertureBase, int *ApertureSize,
		       int *ApertureOffset, int *flags)
{
    *DeviceName = NULL;		/* No special device */
    *ApertureBase = (unsigned char *)(pScrn->memPhysBase);
    *ApertureSize = pScrn->videoRam;
    *ApertureOffset = pScrn->fbOffset;
    *flags = 0;

    return TRUE;
}

static Bool
FBDevDGASetMode(ScrnInfoPtr pScrn, DGAModePtr pDGAMode)
{
    DisplayModePtr pMode;
    int scrnIdx = pScrn->pScreen->myNum;
    int frameX0, frameY0;

    if (pDGAMode) {
	pMode = pDGAMode->mode;
	frameX0 = frameY0 = 0;
    }
    else {
	if (!(pMode = pScrn->currentMode))
	    return TRUE;

	frameX0 = pScrn->frameX0;
	frameY0 = pScrn->frameY0;
    }

    if (!(*pScrn->SwitchMode)(SWITCH_MODE_ARGS(pScrn, pMode)))
	return FALSE;
    (*pScrn->AdjustFrame)(ADJUST_FRAME_ARGS(pScrn, frameX0, frameY0));

    return TRUE;
}

static void
FBDevDGASetViewport(ScrnInfoPtr pScrn, int x, int y, int flags)
{
    (*pScrn->AdjustFrame)(ADJUST_FRAME_ARGS(pScrn, x, y));
}

static int
FBDevDGAGetViewport(ScrnInfoPtr pScrn)
{
    return (0);
}

static DGAFunctionRec FBDevDGAFunctions =
{
    FBDevDGAOpenFramebuffer,
    NULL,       /* CloseFramebuffer */
    FBDevDGASetMode,
    FBDevDGASetViewport,
    FBDevDGAGetViewport,
    NULL,       /* Sync */
    NULL,       /* FillRect */
    NULL,       /* BlitRect */
    NULL,       /* BlitTransRect */
};

static void
FBDevDGAAddModes(ScrnInfoPtr pScrn)
{
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    DisplayModePtr pMode = pScrn->modes;
    DGAModePtr pDGAMode;

    do {
	pDGAMode = realloc(fPtr->pDGAMode,
		           (fPtr->nDGAMode + 1) * sizeof(DGAModeRec));
	if (!pDGAMode)
	    break;

	fPtr->pDGAMode = pDGAMode;
	pDGAMode += fPtr->nDGAMode;
	(void)memset(pDGAMode, 0, sizeof(DGAModeRec));

	++fPtr->nDGAMode;
	pDGAMode->mode = pMode;
	pDGAMode->flags = DGA_CONCURRENT_ACCESS | DGA_PIXMAP_AVAILABLE;
	pDGAMode->byteOrder = pScrn->imageByteOrder;
	pDGAMode->depth = pScrn->depth;
	pDGAMode->bitsPerPixel = pScrn->bitsPerPixel;
	pDGAMode->red_mask = pScrn->mask.red;
	pDGAMode->green_mask = pScrn->mask.green;
	pDGAMode->blue_mask = pScrn->mask.blue;
	pDGAMode->visualClass = pScrn->bitsPerPixel > 8 ?
	    TrueColor : PseudoColor;
	pDGAMode->xViewportStep = 1;
	pDGAMode->yViewportStep = 1;
	pDGAMode->viewportWidth = pMode->HDisplay;
	pDGAMode->viewportHeight = pMode->VDisplay;

	if (fPtr->lineLength)
	  pDGAMode->bytesPerScanline = fPtr->lineLength;
	else
	  pDGAMode->bytesPerScanline = fPtr->lineLength = fbdevHWGetLineLength(pScrn);

	pDGAMode->imageWidth = pMode->HDisplay;
	pDGAMode->imageHeight =  pMode->VDisplay;
	pDGAMode->pixmapWidth = pDGAMode->imageWidth;
	pDGAMode->pixmapHeight = pDGAMode->imageHeight;
	pDGAMode->maxViewportX = pScrn->virtualX -
				    pDGAMode->viewportWidth;
	pDGAMode->maxViewportY = pScrn->virtualY -
				    pDGAMode->viewportHeight;

	pDGAMode->address = fPtr->fbstart;

	pMode = pMode->next;
    } while (pMode != pScrn->modes);
}

static Bool
FBDevDGAInit(ScrnInfoPtr pScrn, ScreenPtr pScreen)
{
#ifdef XFreeXDGA
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (pScrn->depth < 8)
	return FALSE;

    if (!fPtr->nDGAMode)
	FBDevDGAAddModes(pScrn);

    return (DGAInit(pScreen, &FBDevDGAFunctions,
	    fPtr->pDGAMode, fPtr->nDGAMode));
#else
    return TRUE;
#endif
}

static Bool
FBDevDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    xorgHWFlags *flag;

    switch (op) {
	case GET_REQUIRED_HW_INTERFACES:
	    flag = (CARD32*)ptr;
	    (*flag) = 0;
	    return TRUE;
	default:
	    return FALSE;
    }
}
