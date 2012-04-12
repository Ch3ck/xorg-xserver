#ifndef IMPED_H
#define IMPED_H

extern _X_EXPORT Bool impedSetupScreen(ScreenPtr pScreen);

extern _X_EXPORT Bool impedFinishScreenInit(ScreenPtr pScreen,
                                            pointer pbits,
                                            int         xsize,
                                            int         ysize,
                                            int         dpix,
                                            int         dpiy,
                                            int         width,
                                            int         bpp);

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
impedDetachAllSlaves(ScreenPtr pScreen);

extern _X_EXPORT void
impedMigrateOutputSlaves(ScreenPtr pOldMaster, ScreenPtr pNewMaster);

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

#endif
