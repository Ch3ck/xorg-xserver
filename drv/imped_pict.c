
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <string.h>

#include "fb.h"

#include "picturestr.h"
#include "mipict.h"
#include "imped.h"

static void setup_shatter_clip(RegionPtr orig_region, PicturePtr pDrvPicture)
{
    RegionRec pixclip;

    /* adjust the composite clip */
    PixmapRegionInit(&pixclip, pDrvPicture->pDrawable);

    RegionNull(orig_region);
    RegionCopy(orig_region, pDrvPicture->pCompositeClip);
    RegionIntersect(pDrvPicture->pCompositeClip, orig_region, &pixclip);
}

static void finish_shatter_clip(RegionPtr orig_region, PicturePtr pDrvPicture)
{
    RegionCopy(pDrvPicture->pCompositeClip, orig_region);
}

static Bool CreateSourcePict(PicturePtr pPicture, PictureScreenPtr ps, int num_gpu)
{
    int i;
    int error;

    for (i = 0; i < num_gpu; i++) {
	if (!pPicture->gpu[i]) {
	    pPicture->gpu[i] = CreatePicture(0, NULL, pPicture->pFormat, 0, NULL, serverClient, &error);
	    if (!pPicture->gpu[i])
		return FALSE;
	}
    }
    return TRUE;
}


static void
impedComposite (CARD8      op,
		PicturePtr pSrc,
		PicturePtr pMask,
		PicturePtr pDst,
		INT16      xSrc,
		INT16      ySrc,
		INT16      xMask,
		INT16      yMask,
		INT16      xDst,
		INT16      yDst,
		CARD16     width,
		CARD16     height)
{
    int x_off, y_off;
    PixmapPtr pSrcPixmap = NULL, pDstPixmap, pMaskPixmap = NULL;
    PicturePtr pDrvSrc, pDrvMask = NULL, pDrvDst;
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    int i;

    if (pSrc->pDrawable) {
	pSrcPixmap = GetDrawablePixmap(pSrc->pDrawable);
    } 
    else {
	Bool ret;
	ret = CreateSourcePict(pSrc, ps, pScreen->num_gpu);
	if (!ret)
	    return;
    }
	
    if (pMask) {
	pMaskPixmap = GetDrawablePixmap(pMask->pDrawable);
    }
    pDstPixmap = GetDrawablePixmap(pDst->pDrawable);

    miCompositeSourceValidate (pSrc);
    if (pMask)
	miCompositeSourceValidate (pMask);

    if (pSrc->pDrawable) {
	impedGetDrawableDeltas(pSrc->pDrawable, pSrcPixmap, &x_off, &y_off);
	xSrc += x_off;
	ySrc += y_off;
    }

    if (pMask) {
	impedGetDrawableDeltas(pMask->pDrawable, pMaskPixmap, &x_off, &y_off);
	xMask += x_off;
	yMask += y_off;
    }

    impedGetDrawableDeltas(pDst->pDrawable, pDstPixmap, &x_off, &y_off);
    xDst += x_off;
    yDst += y_off;

    for (i = 0; i < pScreen->num_gpu; i++) {
	PictureScreenPtr drv_ps;
	RegionRec orig_src_region, orig_mask_region, orig_dst_region;

	pDrvSrc = pSrc->gpu[i];
	if (pMask) {
	    pDrvMask = pMask->gpu[i];
	}
	pDrvDst = pDst->gpu[i];
	if (pSrcPixmap)
	    pDrvSrc->pDrawable = &pSrcPixmap->gpu[i]->drawable;
	if (pDrvMask)
	    pDrvMask->pDrawable = &pMaskPixmap->gpu[i]->drawable;
	pDrvDst->pDrawable = &pDstPixmap->gpu[i]->drawable;

#if 0
	if (pSrcPixmap && imped_src_pixmap->shattered)
	    setup_shatter_clip(&orig_src_region, pDrvSrc);
	if (pDrvMask && imped_mask_pixmap->shattered)
	    setup_shatter_clip(&orig_mask_region, pDrvMask);
	if (imped_dst_pixmap->shattered)
	    setup_shatter_clip(&orig_dst_region, pDrvDst);
#endif
	drv_ps = GetPictureScreen(pScreen->gpu[i]);
	drv_ps->Composite(op, pDrvSrc, pDrvMask, pDrvDst,
			  xSrc, ySrc, xMask, yMask,
			  xDst, yDst, width, height);

#if 0
	if (imped_dst_pixmap->shattered)
	    finish_shatter_clip(&orig_dst_region, pDrvDst);
	if (pDrvMask && imped_mask_pixmap->shattered)
	    finish_shatter_clip(&orig_mask_region, pDrvMask);
	if (pSrcPixmap && imped_src_pixmap->shattered)
	    finish_shatter_clip(&orig_src_region, pDrvSrc);
#endif
    }
}

static void
impedRasterizeTrapezoid (PicturePtr    pPicture,
			 xTrapezoid  *trap,
			 int	    x_off,
			 int	    y_off)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    PixmapPtr pPixmap = GetDrawablePixmap(pPicture->pDrawable);
    PicturePtr pDrvPicture;
    PictureScreenPtr drv_ps;
    int i;

    for (i = 0; i < pScreen->num_gpu; i++) {
	pDrvPicture = pPicture->gpu[i];
	pDrvPicture->pDrawable = &pPixmap->gpu[i]->drawable;

//	if (imped_pixmap->shattered) ErrorF("%s: shattered picture\n", __func__);
	drv_ps = GetPictureScreen(pScreen->gpu[i]);
	drv_ps->RasterizeTrapezoid(pDrvPicture, trap, x_off, y_off);
    }
}

static void
impedAddTraps (PicturePtr	pPicture,
	       INT16	x_off,
	       INT16	y_off,
	       int		ntrap,
	       xTrap	*traps)
{
    PictureScreenPtr drv_ps;
    PixmapPtr pPixmap = GetDrawablePixmap(pPicture->pDrawable);
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    PicturePtr pDrvPicture;
    int i;
    for (i = 0; i < pScreen->num_gpu; i++) {
	pDrvPicture = pPicture->gpu[i];
	pDrvPicture->pDrawable = &pPixmap->gpu[i]->drawable;

//	if (imped_pixmap->shattered) ErrorF("%s: shattered picture\n", __func__);
	drv_ps = GetPictureScreen(pScreen->gpu[i]);
	drv_ps->AddTraps(pDrvPicture, x_off, y_off, ntrap, traps);
    }
}

static void
impedTrapezoids (CARD8	    op,
		 PicturePtr    pSrc,
		 PicturePtr    pDst,
		 PictFormatPtr maskFormat,
		 INT16	    xSrc,
		 INT16	    ySrc,
		 int	    ntrap,
		 xTrapezoid    *traps)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    PictureScreenPtr ps = GetPictureScreen(pDst->pDrawable->pScreen);
    PixmapPtr pSrcPixmap = NULL, pDstPixmap;
    int i;
    int x_off, y_off;
    Bool ret;
    xTrapezoid *orig_traps;

    miCompositeSourceValidate (pSrc);
    if (pSrc) {
	if (pSrc->pDrawable) {
	    pSrcPixmap = GetDrawablePixmap(pSrc->pDrawable);
	    //if (imped_src_pixmap->shattered) ErrorF("%s: shattered src picture\n", __func__);
	    impedGetDrawableDeltas(pSrc->pDrawable, pSrcPixmap, &x_off, &y_off);
	    xSrc += x_off;
	    ySrc += y_off;
	} else {
	    ret = CreateSourcePict(pSrc, ps, pScreen->num_gpu);
	    if (!ret)
		return;
	}
    }
    
    pDstPixmap = GetDrawablePixmap(pDst->pDrawable);
    //if (imped_dst_pixmap->shattered) ErrorF("%s: shattered dst picture\n", __func__);
    impedGetDrawableDeltas(pDst->pDrawable, pDstPixmap, &x_off, &y_off);
    if (x_off || y_off) {
    	for (i = 0; i < ntrap; i++) {
	    traps[i].top += y_off << 16;
	    traps[i].bottom += y_off << 16;
	    traps[i].left.p1.x += x_off << 16;
	    traps[i].left.p1.y += y_off << 16;
	    traps[i].left.p2.x += x_off << 16;
	    traps[i].left.p2.y += y_off << 16;
	    traps[i].right.p1.x += x_off << 16;
	    traps[i].right.p1.y += y_off << 16;
	    traps[i].right.p2.x += x_off << 16;
	    traps[i].right.p2.y += y_off << 16;
	}
    }
    orig_traps = malloc(ntrap * sizeof(xTrapezoid));
    if (!orig_traps)
	return;

    memcpy(orig_traps, traps, ntrap * sizeof(xTrapezoid));

    for (i = 0; i < pScreen->num_gpu; i++) {
	PictureScreenPtr drv_ps = GetPictureScreen(pScreen->gpu[i]);
	PicturePtr pDrvSrc = NULL, pDrvDst;

	if (pSrc) {
	    pDrvSrc = pSrc->gpu[i];
	    if (pSrcPixmap)
		pDrvSrc->pDrawable = &pSrcPixmap->gpu[i]->drawable;
	    if (pDrvSrc) {
	    }
	}
	pDrvDst = pDst->gpu[i];
	pDrvDst->pDrawable = &pDstPixmap->gpu[i]->drawable;
	memcpy(traps, orig_traps, ntrap * sizeof(xTrapezoid));
	drv_ps->Trapezoids(op, pDrvSrc, pDrvDst, maskFormat, xSrc, ySrc, ntrap, traps);
    }
    free(orig_traps);
}

static void
impedAddTriangles (PicturePtr  pPicture,
		   INT16	    x_off_orig,
		   INT16	    y_off_orig,
		   int	    ntri,
		   xTriangle *tris)
{
    int x_off, y_off;
    PixmapPtr pPixmap = GetDrawablePixmap(pPicture->pDrawable);
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    int i;

    impedGetDrawableDeltas(pPicture->pDrawable, pPixmap, &x_off, &y_off);
    x_off_orig += x_off;
    y_off_orig += y_off;

    for (i = 0; i < pScreen->num_gpu; i++) {
	PictureScreenPtr drv_ps;
	PicturePtr pDrvPicture;
		
	pDrvPicture = pPicture->gpu[i];
	pDrvPicture->pDrawable = &pPixmap->gpu[i]->drawable;
	//if (imped_pixmap->shattered) ErrorF("%s: shattered picture\n", __func__);
	drv_ps = GetPictureScreen(pScreen->gpu[i]);
	drv_ps->AddTriangles(pDrvPicture, x_off_orig, y_off_orig, ntri, tris);
    }
}

static void
impedTriangles (CARD8	    op,
		PicturePtr    pSrc,
		PicturePtr    pDst,
		PictFormatPtr maskFormat,
		INT16	    xSrc,
		INT16	    ySrc,
		int	    ntris,
		xTriangle    *tris)
{
    int x_off, y_off, i;
    PixmapPtr pSrcPixmap, pDstPixmap;
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    xTriangle *orig_tris;

    miCompositeSourceValidate (pSrc);

    pSrcPixmap = GetDrawablePixmap(pSrc->pDrawable);
    impedGetDrawableDeltas(pSrc->pDrawable, pSrcPixmap, &x_off, &y_off);
    xSrc += x_off;
    ySrc += y_off;

    pDstPixmap = GetDrawablePixmap(pDst->pDrawable);
    impedGetDrawableDeltas(pDst->pDrawable, pDstPixmap, &x_off, &y_off);
    if (x_off || y_off) {
	for (i = 0; i < ntris; i++) {
	    tris[i].p1.x += x_off << 16;
	    tris[i].p1.y += y_off << 16;
	    tris[i].p2.x += x_off << 16;
	    tris[i].p2.y += y_off << 16;
	    tris[i].p3.x += x_off << 16;
	    tris[i].p3.y += y_off << 16;
	}
    }
    //if (imped_src_pixmap->shattered) ErrorF("%s: shattered src picture\n", __func__);
    //if (imped_dst_pixmap->shattered) ErrorF("%s: shattered dst picture\n", __func__);

    orig_tris = malloc(ntris * sizeof(xTriangle));
    if (!orig_tris)
	return;

    memcpy(orig_tris, tris, ntris * sizeof(xTriangle));
    for (i = 0; i < pScreen->num_gpu; i++) {
	PictureScreenPtr drv_ps = GetPictureScreen(pScreen->gpu[i]);
	PicturePtr pDrvSrc = NULL, pDrvDst;

	pDrvSrc = pSrc->gpu[i];
	pDrvSrc->pDrawable = &pSrcPixmap->gpu[i]->drawable;
	pDrvDst = pDst->gpu[i];
	pDrvDst->pDrawable = &pDstPixmap->gpu[i]->drawable;

	memcpy(tris, orig_tris, ntris * sizeof(xTriangle));
	drv_ps->Triangles(op, pDrvSrc, pDrvDst, maskFormat, xSrc, ySrc, ntris, tris);
    }
    free(orig_tris);
}

static void
impedGlyphs(CARD8      op,
	    PicturePtr pSrc,
	    PicturePtr pDst,
	    PictFormatPtr  maskFormat,
	    INT16      xSrc,
	    INT16      ySrc,
	    int	nlists,
	    GlyphListPtr   lists,
	    GlyphPtr	*glyphs)
{
    PixmapPtr pSrcPixmap, pDstPixmap;
    int x_off, y_off;
    int i;
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    pSrcPixmap = GetDrawablePixmap(pSrc->pDrawable);
    impedGetDrawableDeltas(pSrc->pDrawable, pSrcPixmap, &x_off, &y_off);
    xSrc += x_off;
    ySrc += y_off;

    pDstPixmap = GetDrawablePixmap(pDst->pDrawable);
    impedGetDrawableDeltas(pDst->pDrawable, pDstPixmap, &x_off, &y_off);

    //if (imped_src_pixmap->shattered) ErrorF("%s: shattered src picture\n", __func__);
    //if (imped_dst_pixmap->shattered) ErrorF("%s: shattered dst picture\n", __func__);

    for (i = 0; i < pScreen->num_gpu; i++) {
	PictureScreenPtr drv_ps = GetPictureScreen(pScreen->gpu[i]);
	PicturePtr pDrvSrc = NULL, pDrvDst;
	pDrvSrc = pSrc->gpu[i];
	pDrvSrc->pDrawable = &pSrcPixmap->gpu[i]->drawable;
	pDrvDst = pDst->gpu[i];
	pDrvDst->pDrawable = &pDstPixmap->gpu[i]->drawable;

	drv_ps->Glyphs(op, pDrvSrc, pDrvDst, maskFormat, xSrc, ySrc, nlists, lists, glyphs);
    }
}

static void
impedChangeOnePicture(PicturePtr pPicture, PicturePtr pChild, int index, Mask mask)
{
    Mask maskQ;
    BITS32 index2;

    maskQ = mask;
    while (mask) {
        index2 = (BITS32) lowbit(mask);
        mask &= ~index2;
        pChild->stateChanges |= index2;
        switch (index2) {
        case CPRepeat:
            pChild->repeat = pPicture->repeat;
            pChild->repeatType = pPicture->repeatType;
            break;
        case CPAlphaMap:
            if (pPicture->alphaMap)
                pChild->alphaMap = pPicture->alphaMap->gpu[index];
            else
                pChild->alphaMap = NULL;
            break;
        case CPAlphaXOrigin:
            pChild->alphaOrigin.x = pPicture->alphaOrigin.x;
            break;
        case CPAlphaYOrigin:
            pChild->alphaOrigin.y = pPicture->alphaOrigin.y;
            break;
        case CPClipXOrigin:
            pChild->clipOrigin.x = pPicture->clipOrigin.x;
            break;
        case CPClipYOrigin:
            pChild->clipOrigin.y = pPicture->clipOrigin.y;
            break;
        case CPClipMask:
            /* TODO */
            break;
        case CPGraphicsExposure:
            pChild->graphicsExposures = pPicture->graphicsExposures;
            break;
        case CPSubwindowMode:
            pChild->subWindowMode = pPicture->subWindowMode;
            break;
        case CPPolyEdge:
            pChild->polyEdge = pPicture->polyEdge;
            break;
        case CPPolyMode:
            pChild->polyMode = pPicture->polyMode;
            break;
        case CPDither:
            break;
        case CPComponentAlpha:
            pChild->componentAlpha = pPicture->componentAlpha;
            break;
        default:
            break;
        }
    }
}

static void
impedChangePicture(PicturePtr pPicture, Mask mask)
{
    ScreenPtr pScreen;
    int i;

    if (!pPicture->pDrawable)
        return;
    if (!pPicture->gpu[i])
        return;
    pScreen = pPicture->pDrawable->pScreen;
    for (i = 0; i < pScreen->num_gpu; i++) {
        impedChangeOnePicture(pPicture, pPicture->gpu[i], i, mask);
    }
}

static int
impedCreatePicture (PicturePtr pPicture)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    PictureScreenPtr ps;
    int i;
    PixmapPtr pPixmap;
    int x_off = 0, y_off = 0;
    int error;

    ps = GetPictureScreen(pPicture->pDrawable->pScreen);

    pPixmap = GetDrawablePixmap(pPicture->pDrawable);

    xorg_list_add(&pPicture->member, &pScreen->picture_list);

    /* have to translate the composite clip before syncing it */
#ifdef COMPOSITE
    if (pPicture->pCompositeClip && pPicture->pDrawable->type == DRAWABLE_WINDOW) {
      x_off = -pPixmap->screen_x;
      y_off = -pPixmap->screen_y;
      RegionTranslate(pPicture->pCompositeClip, x_off, y_off);
    }
#endif
    for (i = 0; i < pScreen->num_gpu; i++) {
	pPicture->gpu[i] = CreatePicture(0, &pPixmap->gpu[i]->drawable, pPicture->pFormat,
                                         0, 0, serverClient, &error);
	if (!pPicture->gpu[i])
	    ErrorF("no gpu %d picture\n", i);
        impedChangeOnePicture(pPicture, pPicture->gpu[i], i, 0xffffffff);
    }
#ifdef COMPOSITE
    if (x_off || y_off) {
      RegionTranslate(pPicture->pCompositeClip, -x_off, -y_off);
    }
#endif
    return 0;
}
             
static void
impedDestroyPicture(PicturePtr pPicture)
{
    int i;
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    xorg_list_del(&pPicture->member);

    for (i = 0; i < pScreen->num_gpu; i++) {
	FreePicture(pPicture->gpu[i], 0);
	pPicture->gpu[i] = NULL;
    }
    miDestroyPicture(pPicture);
}

static void
impedValidatePicture(PicturePtr pPicture, Mask mask)
{
    int x_off, y_off;
    int i;
    ScreenPtr pScreen;
    miValidatePicture(pPicture, mask);
    /**/
    if (!pPicture->pDrawable)
        return;

    pScreen = pPicture->pDrawable->pScreen;
#ifdef COMPOSITE
    if (pPicture->pCompositeClip && pPicture->pDrawable->type == DRAWABLE_WINDOW) {
        PixmapPtr pPixmap = GetDrawablePixmap(pPicture->pDrawable);
        x_off = -pPixmap->screen_x;
        y_off = -pPixmap->screen_y;
        RegionTranslate(pPicture->pCompositeClip, x_off, y_off);
    }
#endif
    for (i = 0; i < pScreen->num_gpu; i++) {
        PicturePtr pDrvPicture = pPicture->gpu[i];
        pDrvPicture->freeCompClip = TRUE;
        if (!pDrvPicture->pCompositeClip)
            pDrvPicture->pCompositeClip = RegionCreate(NullBox, 0);

        if (pPicture->pCompositeClip)
            RegionCopy(pDrvPicture->pCompositeClip, pPicture->pCompositeClip);
        else
            RegionNull(pDrvPicture->pCompositeClip);
    }
        

#ifdef COMPOSITE
    if (x_off || y_off) {
      RegionTranslate(pPicture->pCompositeClip, -x_off, -y_off);
    }
#endif
}

Bool
impedPictureInit (ScreenPtr pScreen, PictFormatPtr formats, int nformats)
{
    PictureScreenPtr ps;
    int i;

    if (!miPictureInit (pScreen, formats, nformats))
	return FALSE;

    /* must get after pictureinit as privates could get reallocated. */
    ps = GetPictureScreen(pScreen);

    ps->CreatePicture = impedCreatePicture;
    ps->ChangePicture = impedChangePicture;
    ps->ValidatePicture = impedValidatePicture;
    ps->Composite = impedComposite;
    ps->RasterizeTrapezoid = impedRasterizeTrapezoid;
    ps->AddTraps = impedAddTraps;
    ps->Trapezoids = impedTrapezoids;
    ps->AddTriangles = impedAddTriangles;
    ps->Triangles = impedTriangles;
    ps->Glyphs = impedGlyphs;
    ps->DestroyPicture = impedDestroyPicture;

    for (i = 0; i < pScreen->num_gpu; i++) {
      PictureScreenPtr drvps = GetPictureScreenIfSet(pScreen->gpu[i]);
      //      if (drvps)
	//	drvps->parent = ps;
    }
    
    return TRUE;
}

#if 0
void
impedPictureDuplicate(PicturePtr pPicture, int new_gpu_index)
{
    impedPictPrivPtr imped_pict = impedGetPict(pPicture);
    PixmapPtr pPixmap;
    impedPixmapPrivPtr imped_pixmap;

    pPixmap = GetDrawablePixmap(pPicture->pDrawable);
    imped_pixmap = impedGetPixmap(pPixmap);
    imped_pict->gpu[new_gpu_index] = DrvCreatePicture(imped_pixmap->gpu[new_gpu_index], pPicture->pFormat, 0, 0);
}
#endif
