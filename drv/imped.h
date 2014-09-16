#ifndef IMPED_H
#define IMPED_H

#include "randrstr.h"
#include "picturestr.h"

extern _X_EXPORT Bool impedSetupScreen(ScreenPtr pScreen);

extern _X_EXPORT Bool impedFinishScreenInit(ScreenPtr pScreen,
                                            void *      pbits,
                                            int         xsize,
                                            int         ysize,
                                            int         dpix,
                                            int         dpiy,
                                            int         width,
                                            int         bpp);

extern _X_EXPORT Bool impedCloseScreen (ScreenPtr pScreen);

#define impedGetScreenPixmap(s)	((PixmapPtr) (s)->devPrivate)

extern _X_EXPORT PixmapPtr
 _impedGetWindowPixmap(WindowPtr pWindow);

extern _X_EXPORT void
 _impedSetWindowPixmap(WindowPtr pWindow, PixmapPtr pPixmap);

extern _X_EXPORT Bool
impedCreateGC(GCPtr pGC);

extern _X_EXPORT void
impedAttachUnboundScreen(ScreenPtr pScreen, ScreenPtr new);
extern _X_EXPORT void
impedDetachUnboundScreen(ScreenPtr pScreen, ScreenPtr slave);

extern _X_EXPORT void
impedAttachScreen(ScreenPtr pScreen, ScreenPtr slave);

extern _X_EXPORT void
impedAttachOutputSlave(ScreenPtr pScreen, ScreenPtr slave, int index);

extern _X_EXPORT void
impedAttachOffloadSlave(ScreenPtr pScreen, ScreenPtr slave, int index);

extern _X_EXPORT void
impedDetachOutputSlave(ScreenPtr pScreen, ScreenPtr slave);

extern _X_EXPORT void
impedDetachOffloadSlave(ScreenPtr pScreen, ScreenPtr slave);

extern _X_EXPORT void
impedMigrateOutputSlaves(ScreenPtr pOldMaster, ScreenPtr pNewMaster);

extern _X_EXPORT void
impedCopyNtoN (DrawablePtr	pSrcDrawable,
	       DrawablePtr	pDstDrawable,
	       GCPtr	pGC,
	       BoxPtr	pbox,
	       int		nbox,
	       int		dx,
	       int		dy,
	       Bool	reverse,
	       Bool	upsidedown,
	       Pixel	bitplane,
	       void	*closure);

static inline void impedGetDrawableDeltas(DrawablePtr pDrawable, PixmapPtr pPixmap,
                                          int *x_off, int *y_off)
{
    *x_off = pDrawable->x;
    *y_off = pDrawable->y;

#ifdef COMPOSITE
    if (pDrawable->type == DRAWABLE_WINDOW) {
        *x_off -= pPixmap->screen_x;
        *y_off -= pPixmap->screen_y;
    }
#endif
}

/* only used for CopyNtoN */
static inline void impedGetCompositeDeltas(DrawablePtr pDrawable, PixmapPtr pPixmap,
                                          int *x_off, int *y_off)
{
    *x_off = 0;
    *y_off = 0;

#ifdef COMPOSITE
    if (pDrawable->type == DRAWABLE_WINDOW) {
        *x_off -= pPixmap->screen_x;
        *y_off -= pPixmap->screen_y;
    }
#endif
}

static inline PixmapPtr impedGetDrawablePixmap(DrawablePtr drawable)
{
    if (drawable->type == DRAWABLE_PIXMAP)
        return (PixmapPtr)drawable;
    else {
        struct _Window *pWin = (struct _Window *)drawable;
        return drawable->pScreen->GetWindowPixmap(pWin);
    }
}

extern _X_EXPORT Bool
impedPictureInit (ScreenPtr pScreen, PictFormatPtr formats, int nformats);
extern _X_EXPORT void
impedPictureDuplicate(PicturePtr pPicture, int new_gpu_index);

extern _X_EXPORT int
impedAddScreen(ScreenPtr protocol_master, ScreenPtr new);

extern _X_EXPORT Bool
impedRemoveScreen(ScreenPtr protocol_master, ScreenPtr slave);

extern _X_EXPORT Bool
impedRandR12Init(ScreenPtr pScreen);

Bool impedCheckPixmapBounding(ScreenPtr pScreen,
			      RRCrtcPtr rr_crtc, int x, int y, int w, int h);
#endif
