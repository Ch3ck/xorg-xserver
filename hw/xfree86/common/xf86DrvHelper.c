#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "os.h"
#include "servermd.h"
#include "pixmapstr.h"
#include "windowstr.h"
#include "gcstruct.h"

#include "xf86.h"
#include "mipointer.h"
#include "micmap.h"
#include "imped.h"
#include "xf86Priv.h"

static void xf86FixupRGBOrdering(ScrnInfoPtr scrn, ScreenPtr screen)
{
    VisualPtr visual;
    visual = screen->visuals + screen->numVisuals;
    while (--visual >= screen->visuals) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed = scrn->offset.red;
            visual->offsetGreen = scrn->offset.green;
            visual->offsetBlue = scrn->offset.blue;
            visual->redMask = scrn->mask.red;
            visual->greenMask = scrn->mask.green;
            visual->blueMask = scrn->mask.blue;
        }
    }
}

static Bool impedSaveScreen(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

static Bool
impedHelperScreenInit(ScreenPtr pScreen,
                    int argc, char **argv)
{
    int width = 0, height = 0;
    int i;
    Bool allow_slave = FALSE;
    ScrnInfoPtr master;

    if (!impedSetupScreen(pScreen))
        return FALSE;

retry:
    for (i = 0; i < xf86NumGPUScreens; i++) {
	if (xf86GPUScreens[i]->confScreen->screennum != pScreen->myNum)
            continue;
	
	if (xf86GPUScreens[i]->numEntities != 1)
            continue;
	//	if (!xf86IsEntityPrimary(xf86GPUScreens[i]->entityList[0]))
	//  continue;

	master = xf86GPUScreens[i];

        if (xf86GPUScreens[i]->virtualX > width)
            width = xf86GPUScreens[i]->virtualX;
                
        if (xf86GPUScreens[i]->virtualY > height)
            height = xf86GPUScreens[i]->virtualY;
	impedAttachScreen(pScreen, xf86GPUScreens[i]->pScreen);
	break;
    }

    if (!impedFinishScreenInit(pScreen, NULL, width, height,
                               75, 75,
                               master->displayWidth, master->bitsPerPixel))
        return FALSE;

   // xf86FixupRGBOrdering(master, pScreen);
    if (!impedPictureInit(pScreen, 0, 0))
	return FALSE;
    
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());
    if (!miCreateDefColormap (pScreen)) {
        return FALSE;
    }

    pScreen->SaveScreen = impedSaveScreen;
    xf86DisableRandR(); /* disable old randr extension */
    //    impedRandR12Init(pScreen);
    return TRUE;
}

static Bool impedPointerMoved(ScrnInfoPtr pScrn, int x, int y)
{
    return TRUE;
}

static void
impedLeaveVT(ScrnInfoPtr pScrn, int flags)
{

}

static void
impedEnterVT(ScrnInfoPtr pScrn, int flags)
{

}

void xf86HelperAddProtoScreens(int screennum)
{
    ScrnInfoPtr pScrn;
    Pix24Flags           screenpix24;
    ScrnInfoPtr first = NULL;
    int i;

    pScrn = xf86AllocateScreen(NULL, 0);
    pScrn->ScreenInit = impedHelperScreenInit;
    pScrn->PointerMoved = impedPointerMoved;
    pScrn->LeaveVT = impedLeaveVT;
    pScrn->EnterVT = impedEnterVT;
    screenpix24 = Pix24DontCare;
    for (i = 0; i < xf86NumGPUScreens; i++) {
        if (xf86GPUScreens[i]->confScreen->screennum != screennum)
            continue;

        if (!first) {
            first = xf86GPUScreens[i];
            if (first->pixmap24 != Pix24DontCare)
                screenpix24 = first->pixmap24;
            continue;
        }
        /* sort out inconsistencies */
        if (first->imageByteOrder != xf86GPUScreens[i]->imageByteOrder)
            FatalError("Inconsistent display imageByteOrder. Exiting\n");

        if (first->bitmapScanlinePad != xf86GPUScreens[i]->bitmapScanlinePad)
            FatalError("Inconsistent display bitmapScanlinePad. Exiting\n");

        if (first->bitmapScanlineUnit != xf86GPUScreens[i]->bitmapScanlineUnit)
            FatalError("Inconsistent display bitmapScanlineUnit. Exiting\n");

        if (first->bitmapBitOrder != xf86GPUScreens[i]->bitmapBitOrder)
            FatalError("Inconsistent display bitmapBitOrder. Exiting\n");

        if (xf86GPUScreens[i]->pixmap24 != Pix24DontCare) {
            if (screenpix24 == Pix24DontCare)
                screenpix24 = xf86GPUScreens[i]->pixmap24;
            else if (screenpix24 != xf86GPUScreens[i]->pixmap24)
                FatalError("Inconsistent depth 24 pixmap format. Exiting\n");
        }
    }
    pScrn->imageByteOrder = first->imageByteOrder;
    pScrn->bitmapScanlinePad = first->bitmapScanlinePad;
    pScrn->bitmapScanlineUnit = first->bitmapScanlineUnit;
    pScrn->bitmapBitOrder = first->bitmapBitOrder;
    pScrn->pixmap24 = first->pixmap24;

}
