/* impedance layer screen functions - replaces
 *
 * fb/mi as the bottom layer of wrapping for protocol level screens
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>

#include "windowstr.h"
#include "servermd.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "imped.h"
#include "mi.h"
#include "micmap.h"

#define impedWindowEnabled(pWin) \
    RegionNotEmpty(&(pWin)->drawable.pScreen->root->borderClip)

#define impedDrawableEnabled(pDrawable) \
    ((pDrawable)->type == DRAWABLE_PIXMAP ? \
     TRUE : impedWindowEnabled((WindowPtr) pDrawable))

static DevPrivateKeyRec impedWinPrivateKeyRec;
static DevPrivateKey
impedGetWinPrivateKey (void)
{
    return &impedWinPrivateKeyRec;
}

#define impedGetWindowPixmap(pWin) ((PixmapPtr)                         \
                                    dixLookupPrivate(&((WindowPtr)(pWin))->devPrivates, impedGetWinPrivateKey()))

static Bool
impedAllocatePrivates(ScreenPtr pScreen)
{
    if (!dixRegisterPrivateKey(&impedWinPrivateKeyRec, PRIVATE_WINDOW, 0))
        return FALSE;

    return TRUE;
}

static Bool
impedCreateScreenResources(ScreenPtr pScreen)
{
    ScreenPtr iter;
    Bool ret = TRUE;
    int i;
    PixmapPtr pPixmap;
    xorg_list_init(&pScreen->gc_list);
    xorg_list_init(&pScreen->pixmap_list);
    xorg_list_init(&pScreen->picture_list);

    ret = miCreateScreenResources(pScreen);
    if (!ret)
	return ret;


    /* have to fixup the screen pixmap linkages */
    pPixmap = pScreen->GetScreenPixmap(pScreen);
    i = 0;
    xorg_list_for_each_entry(iter, &pScreen->gpu_screen_list, gpu_screen_head) {
	iter->omghack = pPixmap->gpu[i];
	i++;
    }

    xorg_list_for_each_entry(iter, &pScreen->gpu_screen_list, gpu_screen_head) {
	ret = iter->CreateScreenResources(iter);
    }


    return ret;
}

static Bool
impedCreateWindow(WindowPtr pWin)
{
    dixSetPrivate(&pWin->devPrivates, impedGetWinPrivateKey(),
                  impedGetScreenPixmap(pWin->drawable.pScreen));
    return TRUE;
}

static Bool
impedPositionWindow(WindowPtr pWin, int x, int y)
{
    return TRUE;
}

static Bool
impedDestroyWindow(WindowPtr pWin)
{
    return TRUE;
}

static Bool
impedMapWindow(WindowPtr pWindow)
{
    return TRUE;
}


static Bool
impedUnmapWindow(WindowPtr pWindow)
{
    return TRUE;
}

static void
impedCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    PixmapPtr pPixmap = pScreen->GetWindowPixmap(pWin);
    RegionRec   rgnDst;
    int dx, dy;

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;
    RegionTranslate(prgnSrc, -dx, -dy);
    RegionNull(&rgnDst);
    RegionIntersect(&rgnDst, &pWin->borderClip, prgnSrc);
#ifdef COMPOSITE
    if (pPixmap->screen_x || pPixmap->screen_y) {
        int xoff = 0, yoff = 0;

        xoff = -pPixmap->screen_x;
        yoff = -pPixmap->screen_y;
        RegionTranslate(&rgnDst, xoff, yoff);
    }
#endif

    miCopyRegion(&pWin->drawable, &pWin->drawable, NULL,
                 &rgnDst, dx, dy, impedCopyNtoN, 0, 0);

    RegionUninit(&rgnDst);
}

static Bool
impedChangeWindowAttributes(WindowPtr pWin, unsigned long mask)
{
    return FALSE;
}

static Bool
impedRealizeFont(ScreenPtr pScreen, FontPtr pFont)
{
    return TRUE;
}

static Bool
impedUnrealizeFont(ScreenPtr pScreen, FontPtr pFont)
{
    return TRUE;
}

static void
impedGetImage (DrawablePtr          pDrawable,
               int                  x,
               int                  y,
               int                  w,
               int                  h,
               unsigned int    format,
               unsigned long   planeMask,
               char         *d)
{
    ScreenPtr pScreen = pDrawable->pScreen;
    RegionRec img_region;
    PixmapPtr pPixmap = GetDrawablePixmap(pDrawable);
    int x_off, y_off;

    if (!impedDrawableEnabled(pDrawable))
        return;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    x += x_off;
    y += y_off;

    pScreen->gpu[0]->GetImage(&pPixmap->gpu[0]->drawable, x, y, w, h,
			      format, planeMask, d);
    /* TODO shatter*/
}

static void
impedGetSpans(DrawablePtr       pDrawable, 
              int               wMax, 
              DDXPointPtr       ppt, 
              int               *pwidth, 
              int               nspans, 
              char              *pchardstStart)
{
}

static PixmapPtr
impedCreatePixmap (ScreenPtr pScreen, int width, int height, int depth,
                   unsigned usage_hint)
{
    PixmapPtr pPixmap;
    pPixmap = AllocatePixmap(pScreen, 0);
    if (!pPixmap)
	return NULL;

    pPixmap->drawable.type = DRAWABLE_PIXMAP;
    pPixmap->drawable.class = 0;
    pPixmap->drawable.pScreen = pScreen;
    pPixmap->drawable.depth = depth;
    pPixmap->drawable.bitsPerPixel = BitsPerPixel (depth);
    pPixmap->drawable.id = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x = 0;
    pPixmap->drawable.y = 0;
    pPixmap->drawable.width = width;
    pPixmap->drawable.height = height;
    pPixmap->devKind = (width * (BitsPerPixel(depth)/8));
    pPixmap->refcnt = 1;

#ifdef COMPOSITE
    pPixmap->screen_x = 0;
    pPixmap->screen_y = 0;
#endif
    pPixmap->usage_hint = usage_hint;

    xorg_list_add(&pPixmap->member, &pScreen->pixmap_list);

    {
	ScreenPtr iter;
        int i = 0;
        xorg_list_for_each_entry(iter, &pScreen->gpu_screen_list, gpu_screen_head) {
            pPixmap->gpu[i] = iter->CreatePixmap(iter, width, height, depth, usage_hint);
            i++;
        }
    }
    return pPixmap;
}

static Bool
impedModifyPixmapHeader(PixmapPtr pPixmap, int w, int h, int d,
                        int bpp, int devKind, pointer pPixData)
{
    ScreenPtr iter;
    int i;
    if (!pPixmap)
	return FALSE;

    miModifyPixmapHeader(pPixmap, w, h, d, bpp, devKind, pPixData);

    i = 0;
    xorg_list_for_each_entry(iter, &pPixmap->drawable.pScreen->gpu_screen_list, gpu_screen_head) {
	iter->ModifyPixmapHeader(pPixmap->gpu[i], w, h, d, bpp, devKind, pPixData);
    }
    return TRUE;
}

static void
impedQueryBestSize (int class, 
                    unsigned short *width, unsigned short *height,
                    ScreenPtr pScreen)
{
    pScreen->gpu[0]->QueryBestSize(class, width, height, pScreen->gpu[0]);
}

static RegionPtr
impedBitmapToRegion(PixmapPtr pPix)
{
    return pPix->drawable.pScreen->gpu[0]->BitmapToRegion(pPix->gpu[0]);
}

static Bool
impedDestroyPixmap(PixmapPtr pPixmap)
{
    int i;
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    if (--pPixmap->refcnt)
	return TRUE;

    xorg_list_del(&pPixmap->member);
    for (i = 0; i < pScreen->num_gpu; i++) {
	pScreen->gpu[i]->DestroyPixmap(pPixmap->gpu[i]);
    }
    FreePixmap(pPixmap);
    return TRUE;
}

Bool
impedCloseScreen (ScreenPtr pScreen)
{
    return FALSE;
}

static void 
impedBlockHandler(ScreenPtr pScreen, pointer blockData, pointer pTimeout,
                  pointer pReadmask)
{
}

PixmapPtr
_impedGetWindowPixmap (WindowPtr pWindow)
{
    return impedGetWindowPixmap (pWindow);
}

void
_impedSetWindowPixmap (WindowPtr pWindow, PixmapPtr pPixmap)
{
    dixSetPrivate(&pWindow->devPrivates, impedGetWinPrivateKey(), pPixmap);
}


Bool
impedSetupScreen(ScreenPtr pScreen)
{

    if (!impedAllocatePrivates(pScreen))
        return FALSE;
    pScreen->defColormap = FakeClientID(0);

    pScreen->ChangeWindowAttributes = impedChangeWindowAttributes;
    pScreen->CreateWindow = impedCreateWindow;
    pScreen->CopyWindow = impedCopyWindow;
    pScreen->PositionWindow = impedPositionWindow;
    pScreen->RealizeWindow = impedMapWindow;
    pScreen->UnrealizeWindow = impedUnmapWindow;
    pScreen->DestroyWindow = impedDestroyWindow;
    pScreen->GetImage = impedGetImage;
    pScreen->GetSpans = impedGetSpans;
    pScreen->GetWindowPixmap = _impedGetWindowPixmap;
    pScreen->SetWindowPixmap = _impedSetWindowPixmap;

    pScreen->RealizeFont = impedRealizeFont;
    pScreen->UnrealizeFont = impedUnrealizeFont;

    pScreen->CreateColormap = miInitializeColormap;
    pScreen->DestroyColormap = (void (*)(ColormapPtr))NoopDDA;
    pScreen->InstallColormap = miInstallColormap;
    pScreen->UninstallColormap = miUninstallColormap;
    pScreen->ListInstalledColormaps = miListInstalledColormaps;
    pScreen->StoreColors = (void (*)(ColormapPtr, int, xColorItem *))NoopDDA;
    pScreen->ResolveColor = miResolveColor;

    pScreen->CreatePixmap = impedCreatePixmap;
    pScreen->DestroyPixmap = impedDestroyPixmap;

    pScreen->CreateGC = impedCreateGC;

    pScreen->QueryBestSize = impedQueryBestSize;

    pScreen->BitmapToRegion = impedBitmapToRegion;

    /* replace miCloseScreen */

    //    drvmiSetZeroLineBias(pScreen, DEFAULTZEROLINEBIAS);

    xorg_list_init(&pScreen->gpu_screen_list);
    /* protocol screen should have no offload/output slaves attached directly
       to it they are attached to the respective masters - don't
       init the lists so we spot failure */
    xorg_list_init(&pScreen->unattached_list);
    return TRUE;
}

Bool impedFinishScreenInit(ScreenPtr pScreen,
                           pointer pbits,
                           int          xsize,
                           int          ysize,
                           int          dpix,
                           int          dpiy,
                           int          width,
                           int          bpp)
{
    VisualPtr   visuals;
    DepthPtr    depths;
    int         nvisuals;
    int         ndepths;
    int         rootdepth;
    VisualID    defaultVisual;
    int         imagebpp = bpp;
    //    impedScreenPrivPtr imped_screen;
    rootdepth = 0;

    if (!miInitVisuals(&visuals, &depths, &nvisuals, &ndepths, &rootdepth,
                       &defaultVisual, 8, ((unsigned long)1<<(imagebpp - 1)), -1))
        return FALSE;
    if (!miScreenInit(pScreen, NULL,  xsize, ysize, dpix, dpiy, width,
                      rootdepth, ndepths, depths, defaultVisual,
                      nvisuals, visuals))
        return FALSE;

    //    imped_screen = impedGetScreen(pScreen); 
    pScreen->ModifyPixmapHeader = impedModifyPixmapHeader;
    pScreen->CreateScreenResources = impedCreateScreenResources;
    pScreen->BlockHandler = impedBlockHandler;
    //    imped_screen->SavedCloseScreen = pScreen->CloseScreen;
    //    pScreen->CloseScreen = impedCloseScreen;

    return TRUE;
}

/* attach a gpu screen to a list on the protocol screen of unbound screens */
void
impedAttachUnboundScreen(ScreenPtr pScreen, ScreenPtr new)
{
    assert(!pScreen->isGPU);
    assert(new->isGPU);
    xorg_list_add(&new->unattached_head, &pScreen->unattached_list);
}

/* attach a gpu screen to a protocol screen */
void
impedAttachScreen(ScreenPtr pScreen, ScreenPtr slave)
{
    assert(!pScreen->isGPU);
    assert(slave->isGPU);
    xorg_list_add(&slave->gpu_screen_head, &pScreen->gpu_screen_list);
    slave->protocol_master = pScreen;
    pScreen->gpu[pScreen->num_gpu] = slave;
    pScreen->num_gpu++;
}

/* attach a gpu screen as an output slave to another gpu screen */
void
impedAttachOutputSlave(ScreenPtr master, ScreenPtr slave, int index)
{
    assert(master->isGPU);
    assert(slave->isGPU);
    xorg_list_add(&slave->output_head, &master->output_slave_list);
    slave->protocol_master = master->protocol_master;
    slave->output_master = master;
}

/* attach a gpu screen as an offload slave to another gpu screen */
void
impedAttachOffloadSlave(ScreenPtr master, ScreenPtr slave, int index)
{
    assert(master->isGPU);
    assert(slave->isGPU);
    xorg_list_add(&slave->offload_head, &master->offload_slave_list);
    slave->protocol_master = master->protocol_master;
    slave->offload_master = master;
}

void
impedDetachOutputSlave(ScreenPtr master, ScreenPtr slave)
{
    assert(master->isGPU);
    assert(slave->isGPU);
    xorg_list_del(&slave->output_head);
    slave->output_master = NULL;
    slave->protocol_master = NULL;
}

void
impedDetachOffloadSlave(ScreenPtr master, ScreenPtr slave)
{
    assert(master->isGPU);
    assert(slave->isGPU);
    xorg_list_del(&slave->offload_head);
    slave->offload_master = NULL;
    slave->protocol_master = NULL;
}

void
impedDetachAllSlaves(ScreenPtr pScreen)
{
    ScreenPtr iter, safe;

    xorg_list_for_each_entry_safe(iter, safe, &pScreen->offload_slave_list, offload_head) {
        impedDetachOffloadSlave(iter->protocol_master, iter);
    }


    xorg_list_for_each_entry_safe(iter, safe, &pScreen->output_slave_list, output_head) {
        impedDetachOutputSlave(iter->protocol_master, iter);
    }

}

void
impedMigrateOutputSlaves(ScreenPtr pOldMaster, ScreenPtr pNewMaster)
{

}
