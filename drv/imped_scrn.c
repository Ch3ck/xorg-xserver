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
	iter->SetScreenPixmap(pPixmap->gpu[i]);
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
    pPixmap->devKind = width;
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
    return TRUE;
}
static void
impedQueryBestSize (int class, 
                    unsigned short *width, unsigned short *height,
                    ScreenPtr pScreen)
{

}

static RegionPtr
impedBitmapToRegion(PixmapPtr pPix)
{
    return NULL;
}

static Bool
impedDestroyPixmap(PixmapPtr pPixmap)
{
    return FALSE;
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

    //    xorg_list_init(&pScreen->unattached_list);
    xorg_list_init(&pScreen->gpu_screen_list);
    xorg_list_init(&pScreen->offload_slave_list);
    xorg_list_init(&pScreen->output_slave_list);
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

void
impedAttachUnboundScreen(ScreenPtr pScreen, ScreenPtr new)
{
    xorg_list_add(&new->unattached_head, &pScreen->unattached_list);
}

void
impedAttachScreen(ScreenPtr pScreen, ScreenPtr slave)
{
    xorg_list_add(&slave->gpu_screen_head, &pScreen->gpu_screen_list);
    slave->protocol_master = pScreen;
    pScreen->gpu[pScreen->num_gpu] = slave;
    pScreen->num_gpu++;
}

void
impedAttachOutputSlave(ScreenPtr pScreen, ScreenPtr slave, int index)
{
    xorg_list_add(&slave->output_head, &pScreen->output_slave_list);
    slave->protocol_master = pScreen;
    slave->output_master = pScreen;
}

void
impedAttachOffloadSlave(ScreenPtr pScreen, ScreenPtr slave, int index)
{
    xorg_list_add(&slave->offload_head, &pScreen->offload_slave_list);    
    slave->protocol_master = pScreen;
    slave->offload_master = pScreen;
}

void
impedDetachOutputSlave(ScreenPtr pScreen, ScreenPtr slave)
{
    xorg_list_del(&slave->output_head);
    slave->output_master = NULL;
    slave->protocol_master = NULL;
}

void
impedDetachOffloadSlave(ScreenPtr pScreen, ScreenPtr slave)
{
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
