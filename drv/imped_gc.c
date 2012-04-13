
/* Impedance layer for GC ops
   This provides an impedance layer between drawables and pixmaps
   it reworks the operations
*/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>

#include "gcstruct.h"
#include "migc.h"
#include "mi.h"
#include "windowstr.h"
#include "imped.h"
#include "scrnintstr.h"
#include "dixfontstr.h"
#include "fb.h"

static void setup_shatter_clip(RegionPtr orig_region, GCPtr pGC, PixmapPtr pPixmap)
{
    RegionRec pixclip;

    /* adjust the composite clip */
    PixmapRegionInit(&pixclip, pPixmap);

    RegionNull(orig_region);
    RegionCopy(orig_region, pGC->pCompositeClip);
    RegionIntersect(pGC->pCompositeClip, orig_region, &pixclip);
}

static void finish_shatter_clip(RegionPtr orig_region, GCPtr pGC)
{
    RegionCopy(pGC->pCompositeClip, orig_region);
    RegionUninit(orig_region);
}

#define FOR_EACH_PIXMAP_MEMCPY(op, opcpy) \
    for (int _i = 0; _i < pDrawable->pScreen->num_gpu; _i++) {  \
        GCPtr _pDrvGC = pGC->gpu[_i];				\
        PixmapPtr _pDrvPixmap = pPixmap->gpu[_i];			\
        RegionRec orig_region;						\
        while (_pDrvPixmap) {						\
            if (pPixmap->shattered) setup_shatter_clip(&orig_region, _pDrvGC, _pDrvPixmap); \
            op;								\
            opcpy;                                                      \
            if (pPixmap->shattered) finish_shatter_clip(&orig_region, _pDrvGC); \
            _pDrvPixmap = NULL;/* _pDrvPixmap->shatter_next;          */ \
        }                                                               \
  }

#define FOR_EACH_PIXMAP(op) FOR_EACH_PIXMAP_MEMCPY(op, do {} while(0))

void
impedValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
    PixmapPtr pPixmap = GetDrawablePixmap(pDrawable);
    GCPtr pDrvGC;
    int i;
    int x_off = 0, y_off = 0;

    if ((changes & (GCClipXOrigin|GCClipYOrigin|GCClipMask|GCSubwindowMode)) ||
	(pDrawable->serialNumber != (pGC->serialNumber & DRAWABLE_SERIAL_BITS))
	)
    {
	miComputeCompositeClip (pGC, pDrawable);
    }

    /* have to translate the composite clip before syncing it */
#ifdef COMPOSITE
    if (pDrawable->type == DRAWABLE_WINDOW) {
      x_off = -pPixmap->screen_x;
      y_off = -pPixmap->screen_y;
      RegionTranslate(pGC->pCompositeClip, x_off, y_off);
    }
#endif
    for (i = 0; i < pGC->pScreen->num_gpu; i++) {
	pDrvGC = pGC->gpu[i];

	/* check tile pixmap */
	if (pGC->fillStyle == FillTiled && !pGC->tileIsPixel) {
	    if (pDrvGC->tile.pixmap != pGC->tile.pixmap->gpu[i]) {
		pDrvGC->tile.pixmap = pGC->tile.pixmap->gpu[i];
		pDrvGC->tile.pixmap->refcnt++;
	    }
	}
	    
	pDrvGC->funcs->ValidateGC(pDrvGC, changes, &pPixmap->gpu[i]->drawable);
    }
#ifdef COMPOSITE
    if (pDrawable->type == DRAWABLE_WINDOW) {
      RegionTranslate(pGC->pCompositeClip, -x_off, -y_off);
    }
#endif
}

static void impedDestroyGC(GCPtr pGC)
{
    int i;
    ScreenPtr iter;
    xorg_list_del(&pGC->member);
    i = 0;
    xorg_list_for_each_entry(iter, &pGC->pScreen->gpu_screen_list, gpu_screen_head) {
        FreeGC(pGC->gpu[i], 0);
        pGC->gpu[i] = NULL;
        i++;
    }

    // TODO destroy driver GC
    miDestroyGC(pGC);
}

static void impedModChildGC(GCPtr pGC, GCPtr pChild, int index, unsigned long mask)
{
    PixmapPtr pPixmap;
    BITS32 index2, maskQ;
    maskQ = mask;
    while (mask) {
        index2 = (BITS32)lowbit(mask);
        mask &= ~index2;
        switch (index2) {
        case GCFunction:
            pChild->alu = pGC->alu;
            break;
        case GCPlaneMask:
            pChild->planemask = pGC->planemask;
            break;
        case GCForeground:
            pChild->fgPixel = pGC->fgPixel;
            break;
        case GCBackground:
            pChild->bgPixel = pGC->bgPixel;
            break;
        case GCLineWidth:
            pChild->lineWidth = pGC->lineWidth;
            break;
        case GCLineStyle:
            pChild->lineStyle = pGC->lineStyle;
            break;
        case GCCapStyle:
            pChild->capStyle = pGC->capStyle;
            break;
        case GCJoinStyle:
            pChild->joinStyle = pGC->joinStyle;
            break;
        case GCFillStyle:
            pChild->fillStyle = pGC->fillStyle;
            break;
        case GCFillRule:
            pChild->fillRule = pGC->fillRule;
            break;
        case GCTile:
            if (!pChild->tileIsPixel)
                pChild->pScreen->DestroyPixmap(pChild->tile.pixmap);
            pChild->tileIsPixel = pGC->tileIsPixel;
            if (pGC->tileIsPixel == FALSE) {
                pPixmap = pGC->tile.pixmap->gpu[index];
                pChild->tile.pixmap = pPixmap;
                pPixmap->refcnt++;
            }
            break;
        case GCStipple:
            pPixmap = pGC->stipple->gpu[index];
            if (pChild->stipple)
                pChild->pScreen->DestroyPixmap(pChild->stipple);
            pChild->stipple = pPixmap;
            break;
        case GCTileStipXOrigin:
            pChild->patOrg.x = pGC->patOrg.x;
            break;
        case GCTileStipYOrigin:
            pChild->patOrg.y = pGC->patOrg.y;
            break;
        case GCFont:
            if (pChild->font)
                CloseFont(pChild->font, (Font)0);
            pGC->font->refcnt++;
            pChild->font = pGC->font;
            break;
        case GCSubwindowMode:
            pChild->subWindowMode = pGC->subWindowMode;
            break;
        case GCGraphicsExposures:
            pChild->graphicsExposures = pGC->graphicsExposures;
            break;
        case GCClipXOrigin:
            pChild->clipOrg.x = pGC->clipOrg.x;
            break;
        case GCClipYOrigin:
            pChild->clipOrg.y = pGC->clipOrg.y;
            break;
        case GCDashOffset:
            pChild->dashOffset = pGC->dashOffset;
            break;
        case GCArcMode:
            pChild->arcMode = pGC->arcMode;
            break;
        case GCClipMask:
        default:
            ErrorF("unhandled GC bit %lx\n", index2);
        }
    }            
}

static void impedChangeGC(GCPtr pGC, unsigned long mask)
{
    unsigned long maskQ;
    int i;
    ErrorF("imped change GC %08x\n", mask);
    maskQ = mask;
    /* have to execute GC change on the lower layers
       however for have to also do pixmap lookups etc */
    
    for (i = 0; i < pGC->pScreen->num_gpu; i++) {
        impedModChildGC(pGC, pGC->gpu[i], i, mask);
    }
    miChangeGC(pGC, maskQ);
}

const GCFuncs impedGCFuncs = {
    impedValidateGC,
    impedChangeGC,
    miCopyGC,
    impedDestroyGC,
    miChangeClip,
    miDestroyClip,
    miCopyClip,
};

static void
impedFillSpans (DrawablePtr    pDrawable,
		GCPtr	    pGC,
		int	    nInit,
		DDXPointPtr    pptInit,
		int	    *pwidthInit,
		int	    fSorted)
{
    int i;
    int x_off, y_off;
    PixmapPtr pPixmap = GetDrawablePixmap(pDrawable);
    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    for (i = 0; i < nInit; i++) {
	pptInit[i].x += x_off;
	pptInit[i].y += y_off;
    }

    FOR_EACH_PIXMAP(_pDrvGC->ops->FillSpans(&_pDrvPixmap->drawable,
					    _pDrvGC,
					    nInit,
					    pptInit,
					    pwidthInit,
					    fSorted));
}

static void
impedSetSpans (DrawablePtr	    pDrawable,
	       GCPtr	    pGC,
	       char	    *src,
	       DDXPointPtr	    ppt,
	       int		    *pwidth,
	       int		    nspans,
	       int		    fSorted)
{
    int i;
    int x_off, y_off;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    for (i = 0; i < nspans; i++) {
	ppt[i].x += x_off;
	ppt[i].y += y_off;
    }

    
    FOR_EACH_PIXMAP(_pDrvGC->ops->SetSpans(&_pDrvPixmap->drawable,
					   _pDrvGC,
					   src,
					   ppt,
					   pwidth,
					   nspans,
					   fSorted));
}

static void
impedPutImage (DrawablePtr	pDrawable,
	       GCPtr	pGC,
	       int		depth,
	       int		x,
	       int		y,
	       int		w,
	       int		h,
	       int		leftPad,
	       int		format,
	       char	*pImage)
{
    int x_off, y_off;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    x += x_off;
    y += y_off;

    FOR_EACH_PIXMAP(_pDrvGC->ops->PutImage(&_pDrvPixmap->drawable,
					   _pDrvGC,
					   depth, x, y, w, h,
					   leftPad, format, pImage));
    
}

static void
impedPolyPoint (DrawablePtr    pDrawable,
		GCPtr	    pGC,
		int	    mode,
		int	    npt,
		xPoint	    *pptInit)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    xPoint *origPts;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    if (mode == CoordModePrevious) {
	pptInit[0].x += x_off;
	pptInit[0].y += y_off;
    } else {
	for (i = 0; i < npt; i++) {
	    pptInit[i].x += x_off;
	    pptInit[i].y += y_off;
	}
    }

    origPts = malloc(npt * sizeof(*pptInit));
    memcpy(origPts, pptInit, npt * sizeof(xPoint));
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->PolyPoint(&_pDrvPixmap->drawable,
						   _pDrvGC, mode, npt, pptInit),
			   memcpy(pptInit, origPts, npt * sizeof(xPoint)));
    free(origPts);
}

static void
impedPolyLines (DrawablePtr	pDrawable,
	       GCPtr	pGC,
	       int		mode,
	       int		npt,
	       DDXPointPtr	ppt)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    DDXPointPtr ppt_orig;
    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    fprintf(stderr,"poly lines %d %d %d\n", mode, x_off, y_off);
    if (mode == CoordModePrevious) {
	ppt[0].x += x_off;
	ppt[0].y += y_off;
    } else {
	for (i = 0; i < npt; i++) {
	    ppt[i].x += x_off;
	    ppt[i].y += y_off;
	}
    }
    ppt_orig = malloc(sizeof(*ppt_orig) * npt);
    if (!ppt_orig)
      return;

    memcpy(ppt_orig, ppt, npt * sizeof(*ppt_orig));
    
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->Polylines(&_pDrvPixmap->drawable,
						   _pDrvGC, mode, npt, ppt),
			   memcpy(ppt, ppt_orig, npt*sizeof(*ppt_orig)));
    free(ppt_orig);
}

static void
impedPolySegment (DrawablePtr	pDrawable,
		  GCPtr	pGC,
		  int		nseg,
		  xSegment *pSegs)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    xSegment *pSegs_orig;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    for (i = 0; i < nseg; i++) {
	pSegs[i].x1 += x_off;
	pSegs[i].x2 += x_off;
	pSegs[i].y1 += y_off;
	pSegs[i].y2 += y_off;
    }

    pSegs_orig = malloc(nseg * sizeof(*pSegs));
    if (!pSegs_orig)
      return;

    memcpy(pSegs_orig, pSegs, nseg * sizeof(*pSegs));
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->PolySegment(&_pDrvPixmap->drawable,
						     _pDrvGC, nseg, pSegs),
			   memcpy(pSegs, pSegs_orig, nseg * sizeof(*pSegs)));
    free(pSegs_orig);
}

static void
impedPolyRectangle(DrawablePtr pDrawable, GCPtr pGC, int nrects, xRectangle *pRects)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    xRectangle *orig_pRects;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    for (i = 0; i < nrects; i++) {
	pRects[i].x += x_off;
	pRects[i].y += y_off;
    }

    orig_pRects = malloc(nrects * sizeof(xRectangle));
    memcpy(orig_pRects, pRects, nrects * sizeof(xRectangle));
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->PolyRectangle(&_pDrvPixmap->drawable, _pDrvGC, nrects, pRects),
			   memcpy(pRects, orig_pRects, nrects * sizeof(xRectangle)));

    free(orig_pRects);
}

static void impedPolyArc (DrawablePtr	pDrawable,
		   GCPtr	pGC,
		   int		narcs,
		   xArc		*parcs)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    xArc *orig_arcs;
    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    /* impedance match with fb layer */
    for (i = 0; i < narcs; i++) {
	parcs[i].x += x_off;
	parcs[i].y += y_off;
    }

    orig_arcs = malloc(narcs * sizeof(xArc));
    if (!orig_arcs)
      return;

    memcpy(orig_arcs, parcs, narcs * sizeof(xArc));
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->PolyArc(&_pDrvPixmap->drawable, _pDrvGC, narcs, parcs),
			   memcpy(parcs, orig_arcs, narcs * sizeof(xArc)));
    free(orig_arcs);
}

static void impedFillPolygon( DrawablePtr pDrawable, GCPtr pGC,
			      int shape, int mode,
			      int count, DDXPointPtr pPts)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    DDXPointPtr orig_pts;
    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    
    for (i = 0; i < count; i++) {
	pPts[i].x += x_off;
	pPts[i].y += y_off;
    }

    orig_pts = malloc(count * sizeof(DDXPointRec));
    if (!orig_pts)
      return;

    memcpy(orig_pts, pPts, count * sizeof(DDXPointRec));
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->FillPolygon(&_pDrvPixmap->drawable, _pDrvGC, shape,
						     mode, count, pPts),
			   memcpy(pPts, orig_pts, count * sizeof(DDXPointRec)));
    free(orig_pts);
}

static void impedPolyFillRect(DrawablePtr pDrawable,
			      GCPtr pGC,
			      int nrectFill,
			      xRectangle *prectInit)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    xRectangle *orig_prect;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    for (i = 0; i < nrectFill; i++) {
	prectInit[i].x += x_off;
	prectInit[i].y += y_off;
    }

    orig_prect = malloc(nrectFill * sizeof(xRectangle));
    if (!orig_prect)
	return;

    memcpy(orig_prect, prectInit, nrectFill * sizeof(xRectangle));

    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->PolyFillRect(&_pDrvPixmap->drawable, _pDrvGC, nrectFill, prectInit),
		    memcpy(prectInit, orig_prect, nrectFill * sizeof(xRectangle)));
    free(orig_prect);
}

static void impedPolyFillArc(DrawablePtr pDrawable, GCPtr pGC, int narcs, xArc *parcs)
{
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    xArc *orig_arcs;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    for (i = 0; i < narcs; i++) {
	parcs[i].x += x_off;
	parcs[i].y += y_off;
    }

    orig_arcs = malloc(narcs * sizeof(xArc));
    if (!orig_arcs)
	return;

    memcpy(orig_arcs, parcs, narcs * sizeof(xArc));
    FOR_EACH_PIXMAP_MEMCPY(_pDrvGC->ops->PolyFillArc(&_pDrvPixmap->drawable, _pDrvGC, narcs, parcs),
			   memcpy(orig_arcs, parcs, sizeof(narcs * sizeof(xArc))));
    free(orig_arcs);
}

static int
impedPolyText8(DrawablePtr pDrawable, GCPtr pGC, int x, int y, int count, char *chars)
{
    int i, ret = 0;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    GCPtr pDrvGC;
    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    for (i = 0; i < pDrawable->pScreen->num_gpu; i++) {
	pDrvGC = pGC->gpu[i];
	ret = pDrvGC->ops->PolyText8(&pPixmap->gpu[i]->drawable, pDrvGC, x, y, count, chars);
    }
    return ret;
}

static int
impedPolyText16(DrawablePtr pDrawable, GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    int ret = 0;
    int i;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;
    GCPtr pDrvGC;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    for (i = 0; i < pDrawable->pScreen->num_gpu; i++) {
	pDrvGC = pGC->gpu[i];
	ret = pDrvGC->ops->PolyText16(&pPixmap->gpu[i]->drawable, pDrvGC, x, y, count, chars);
	if (ret)
	    return ret;
    }
    return ret;
}

static void
impedImageText8(DrawablePtr pDrawable, GCPtr pGC, int x, int y, int count, char *chars)
{
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);

    FOR_EACH_PIXMAP(_pDrvGC->ops->ImageText8(&_pDrvPixmap->drawable, _pDrvGC, x, y, count, chars));
}

static void
impedImageText16(DrawablePtr pDrawable, GCPtr pGC, int x, int y, int count, unsigned short *chars)
{
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);
    int x_off, y_off;

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    FOR_EACH_PIXMAP(_pDrvGC->ops->ImageText16(&_pDrvPixmap->drawable, _pDrvGC, x, y, count, chars));
}

static void
impedPolyGlyphBlt (DrawablePtr	pDrawable,
		   GCPtr		pGC,
		   int		x, 
		   int		y,
		   unsigned int	nglyph,
		   CharInfoPtr	*ppci,
		   pointer		pglyphBase)
{
    int x_off, y_off;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    x += x_off;
    y += y_off;

    FOR_EACH_PIXMAP(_pDrvGC->ops->PolyGlyphBlt(&_pDrvPixmap->drawable, _pDrvGC,
					       x, y, nglyph,
					       ppci, pglyphBase));
}

static void
impedImageGlyphBlt (DrawablePtr	pDrawable,
		    GCPtr		pGC,
		    int		x, 
		    int		y,
		    unsigned int	nglyph,
		    CharInfoPtr	*ppciInit,
		    pointer	pglyphBase)
{
    int x_off, y_off;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    x += x_off;
    y += y_off;
    FOR_EACH_PIXMAP(_pDrvGC->ops->ImageGlyphBlt(&_pDrvPixmap->drawable, _pDrvGC,
						x, y, nglyph,
						ppciInit, pglyphBase));
}

void
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
	       void	*closure)
{
    miCopyProc copy;
    int src_x_off, src_y_off;
    int dst_x_off, dst_y_off;
    PixmapPtr pSrcPixmap = (PixmapPtr)GetDrawablePixmap(pSrcDrawable);
    PixmapPtr pDstPixmap = (PixmapPtr)GetDrawablePixmap(pDstDrawable);
    int i;
    GCPtr pDrvGC = NULL;

    impedGetCompositeDeltas(pSrcDrawable, pSrcPixmap, &src_x_off, &src_y_off);
    impedGetCompositeDeltas(pDstDrawable, pDstPixmap, &dst_x_off, &dst_y_off);
    
    /* copy already takes care of the pixmap clipping */
    dx += -dst_x_off + src_x_off;//pDstPixmap->screen_x - pSrcPixmap->screen_x;
    dy += -dst_y_off + src_y_off;//pDstPixmap->screen_y - pSrcPixmap->screen_y;

    if (dst_x_off || dst_y_off) {
	for (i = 0; i < nbox; i++) {
	    pbox[i].x1 += dst_x_off;
	    pbox[i].x2 += dst_x_off;
	   
	    pbox[i].y1 += dst_y_off;
	    pbox[i].y2 += dst_y_off;
	}
    }

    for (i = 0; i < pSrcDrawable->pScreen->num_gpu; i++) {
	copy = pSrcDrawable->pScreen->gpu[i]->GetCopyAreaFunction(&pSrcPixmap->drawable, &pDstPixmap->drawable);

	if (pGC) {
	    pDrvGC = pGC->gpu[i];
	}
    
	copy(pSrcPixmap->gpu[i], pDstPixmap->gpu[i],
	     pDrvGC, pbox, nbox, dx, dy, reverse,
	     upsidedown, bitplane, closure);
    }

}

static RegionPtr
impedCopyArea (DrawablePtr	pSrcDrawable,
	       DrawablePtr	pDstDrawable,
	       GCPtr	pGC,
	       int		xIn, 
	       int		yIn,
	       int		widthSrc, 
	       int		heightSrc,
	       int		xOut, 
	       int		yOut)
{
    return miDoCopy(pSrcDrawable,
		    pDstDrawable,
		    pGC, xIn, yIn,
		    widthSrc,
		    heightSrc,
		    xOut,
		    yOut,
		    impedCopyNtoN, 0, 0);
}

#if 0
static void
impedCopyPlaneNtoN (DrawablePtr	pSrcDrawable,
		    DrawablePtr	pDstDrawable,
		    GCPtr	pGC,
		    BoxPtr	pbox,
		    int		nbox,
		    int		dx,
		    int		dy,
		    Bool	reverse,
		    Bool	upsidedown,
		    Pixel	bitplane,
		    void	*closure)
{
    drvCopyProc copy;
    PixmapPtr pSrcPixmap, pDstPixmap;
    DrvGCPtr pDrvGC = NULL;
    impedGCPrivPtr imped_gc;

    int i;
    pSrcPixmap = GetDrawablePixmap(pSrcDrawable);
    pDstPixmap = GetDrawablePixmap(pDstDrawable);

    for (i = 0; i < pSrcDrawable->pScreen->num_gpu; i++) {
	copy = imped_src_screen->gpu[i]->GetCopyPlaneFunction(imped_src_pixmap->gpu[i],
							      imped_dst_pixmap->gpu[i], bitplane);
	
	if (pGC) {
	    imped_gc = impedGetGC(pGC);
	    pDrvGC = imped_gc->gpu[i];
	}
	copy(imped_src_pixmap->gpu[i],
	     imped_dst_pixmap->gpu[i],
	     pDrvGC, pbox, nbox, dx, dy, reverse,
	     upsidedown, bitplane, closure);
    }

}
#endif

static RegionPtr
impedCopyPlane (DrawablePtr	pSrcDrawable,
	    DrawablePtr	pDstDrawable,
	    GCPtr	pGC,
	    int		xIn, 
	    int		yIn,
	    int		widthSrc, 
	    int		heightSrc,
	    int		xOut, 
	    int		yOut,
	    unsigned long bitplane)
{
  //    drvCopyProc copy;
    PixmapPtr pSrcPixmap, pDstPixmap;

    pSrcPixmap = GetDrawablePixmap(pSrcDrawable);
    pDstPixmap = GetDrawablePixmap(pDstDrawable);

#if 0
    copy = imped_src_screen->gpu[0]->GetCopyPlaneFunction(imped_src_pixmap->gpu[0],
							 imped_dst_pixmap->gpu[0], bitplane);

    if (copy)
	return miDoCopy(pSrcDrawable,
			pDstDrawable,
			pGC, xIn, yIn,
			widthSrc,
			heightSrc,
			xOut,
			yOut,
			impedCopyPlaneNtoN, (Pixel)bitplane, 0);
    else
#endif
	return miHandleExposures(pSrcDrawable, pDstDrawable, pGC,
                                 xIn, yIn,
                                 widthSrc,
                                 heightSrc,
                                 xOut, yOut, bitplane);
}

static void
impedPushPixels (GCPtr	    pGC,
		 PixmapPtr  pBitmap,
		 DrawablePtr   pDrawable,
		 int	    dx,
		 int	    dy,
		 int	    xOrg,
		 int	    yOrg)
{
    int x_off, y_off;
    PixmapPtr pPixmap = (PixmapPtr)GetDrawablePixmap(pDrawable);

    impedGetDrawableDeltas(pDrawable, pPixmap, &x_off, &y_off);
    xOrg += x_off;
    yOrg += y_off;

    FOR_EACH_PIXMAP(_pDrvGC->ops->PushPixels(_pDrvGC, pBitmap->gpu[_i], &_pDrvPixmap->drawable,
					     dx, dy, xOrg, yOrg));

}

const GCOps impedGCOps = {
    impedFillSpans,
    impedSetSpans,
    impedPutImage,
    impedCopyArea,
    impedCopyPlane,
    impedPolyPoint,
    impedPolyLines,
    impedPolySegment,
    impedPolyRectangle,
    impedPolyArc,
    impedFillPolygon,
    impedPolyFillRect,
    impedPolyFillArc,
    impedPolyText8,
    impedPolyText16,
    impedImageText8,
    impedImageText16,
    impedImageGlyphBlt,
    impedPolyGlyphBlt,
    impedPushPixels,
};

Bool
impedCreateGC(GCPtr pGC)
{
    GCPtr gpugc;
    ScreenPtr iter;
    int i = 0;
    Bool ret;
    pGC->ops = (GCOps *)&impedGCOps;
    pGC->funcs = &impedGCFuncs;
    
    /* imped wants to translate before scan conversion */
    pGC->miTranslate = 1;
    pGC->fExpose = 1;

    xorg_list_add(&pGC->member, &pGC->pScreen->gc_list);

    xorg_list_for_each_entry(iter, &pGC->pScreen->gpu_screen_list, gpu_screen_head) {
        gpugc = NewGCObject(iter, pGC->depth);
        pGC->gpu[i] = gpugc;
        ret = gpugc->pScreen->CreateGC(gpugc);
        if (ret == FALSE)
            return FALSE;
        i++;
    }

    return TRUE;

}
