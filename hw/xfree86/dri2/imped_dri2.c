#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <errno.h>
#ifdef WITH_LIBDRM
#include <xf86drm.h>
#endif
#include "xf86Module.h"
#include "list.h"
#include "windowstr.h"

#include "dri2.h"
#include "dri2_priv.h"

typedef struct {
    int refcnt;
    PixmapPtr pixmap;
    unsigned int attachment;
    void *driverPrivate;
    PixmapPtr prime_pixmap;
    int prime_id;
} impedDRI2BufferPrivateRec, *impedDRI2BufferPrivatePtr;

/* impedance layer for DRI2 */
/* always appear as a version 7 layer (??) */

static ScreenPtr
GetScreenPrime(ScreenPtr master, int prime_id)
{
    ScreenPtr slave;
    int i;

    if (prime_id == 0 || xorg_list_is_empty(&master->offload_slave_list)) {
        return master->gpu[master->primary_gpu_index];
    }
    i = 0;
    xorg_list_for_each_entry(slave, &master->offload_slave_list, offload_head) {
        if (i == (prime_id - 1))
            break;
        i++;
    }
    if (!slave)
        return master->gpu[master->primary_gpu_index];
    /* TODO */
    return master->gpu[master->primary_gpu_index];
}


static DRI2ScreenPtr
DRI2GetScreenPrime(ScreenPtr master, int prime_id)
{
    ScreenPtr slave = GetScreenPrime(master, prime_id);
    return DRI2GetScreen(slave);
}

static void init_front_region(DrawablePtr pDraw, RegionPtr region)
{
    RegionInit(region, NULL, 0);

    if (pDraw->type == DRAWABLE_WINDOW) {
        WindowPtr pWin = (WindowPtr)pDraw;
        if (!RegionNil(&pWin->clipList))
            RegionUnion(region, region, &pWin->clipList);
        else
            RegionUnion(region, region, &pWin->winSize);

        if (!RegionNil(region))
          return;
    }
    {
        BoxRec box;
        RegionRec boxrec;

        box.x1 = pDraw->x;
        box.y1 = pDraw->y;
        box.x2 = pDraw->x + pDraw->width;
        box.y2 = pDraw->y + pDraw->height;
        RegionInit(&boxrec, &box, 1);
        RegionUnion(region, region, &boxrec);
        RegionUninit(&boxrec);
    }
}

static void
imped_dri2_reference_buffer(DRI2BufferPtr buffer)
{
    impedDRI2BufferPrivatePtr private = buffer->driverPrivate;

    private->refcnt++;
}

static void
imped_dri2_unref_buffer(PixmapPtr pPixmap, DRI2BufferPtr buffer)
{
    impedDRI2BufferPrivatePtr private;

    if (!buffer)
	return;

    private = buffer->driverPrivate;
    if (--private->refcnt == 0) {
	if (private->pixmap) {
	    DRI2ScreenPtr gpu_ds = DRI2GetScreen(private->pixmap->drawable.pScreen);
	    gpu_ds->DestroyBufferPixmap(private->pixmap);
	}
	free(private);
	free(buffer);
    }
}

static Bool imped_dri2_get_driver_info(ScreenPtr pScreen,
                                       int driverType,
                                       uint32_t *is_prime,
                                       int *fd,
                                       const char **drivername,
                                       const char **devicename)
{
    int prime_id = DRI2DriverPrimeId(driverType);
    int driver_id = driverType & 0xffff;
    DRI2ScreenPtr gpu_ds = DRI2GetScreenPrime(pScreen, prime_id);
    
    if (driver_id >= gpu_ds->numDrivers ||
        !gpu_ds->driverNames[driver_id])
        return FALSE;

    *is_prime = prime_id;
    *drivername = gpu_ds->driverNames[driver_id];
    *devicename = gpu_ds->deviceName;
    *fd = gpu_ds->fd;
    return TRUE;
}

static DRI2BufferPtr imped_dri2_create_buffer2(DrawablePtr pDraw,
					       unsigned int attachment,
					       unsigned int format,
                                               unsigned int prime_id, int w, int h)
{
    ScreenPtr gpuScreen = GetScreenPrime(pDraw->pScreen, prime_id);
    DRI2ScreenPtr gpu_ds = DRI2GetScreen(gpuScreen);
    PixmapPtr pPixmap = GetDrawablePixmap(pDraw);
    PixmapPtr gpuPixmap = pPixmap->gpu[pDraw->pScreen->primary_gpu_index];
    DRI2BufferPtr buffer;
    PixmapPtr pixmap;
    uint32_t name;
    impedDRI2BufferPrivatePtr privates;

    buffer = calloc(1, sizeof *buffer);
    if (buffer == NULL)
	return NULL;

    privates = calloc(1, sizeof *privates);
    if (privates == NULL) {
	free(buffer);
	return NULL;
    }

    pixmap = gpu_ds->CreateBufferPixmap(gpuScreen, gpuPixmap, attachment, format,
					pDraw->width, pDraw->height,
					&name);
    
    if (pixmap)  {
	buffer->pitch = pixmap->devKind;
	buffer->cpp = pixmap->drawable.bitsPerPixel / 8;
    }
    buffer->attachment = attachment;
    buffer->driverPrivate = privates;
    buffer->format = format;
    buffer->flags = 0;
    buffer->name = name;
    privates->refcnt = 1;
    privates->pixmap = pixmap;
    privates->attachment = attachment;
    privates->prime_id = prime_id;
    return buffer;
}

static int imped_dri2_auth_magic2(ScreenPtr pScreen,
                           int prime_id,
                           uint32_t magic)
{
    DRI2ScreenPtr gpu_ds = DRI2GetScreenPrime(pScreen, prime_id);

    if ((*gpu_ds->AuthMagic)(gpu_ds->fd, magic))
        return -1;
    return 0;
}

static void imped_dri2_destroy_buffer(DrawablePtr pDraw,
				      DRI2BufferPtr buffer)
{
    impedDRI2BufferPrivatePtr private;
    PixmapPtr pPixmap = GetDrawablePixmap(pDraw);
    PixmapPtr gpuPixmap = pPixmap->gpu[pDraw->pScreen->primary_gpu_index];

    if (!buffer)
	return;
    imped_dri2_unref_buffer(NULL, buffer);
}

static void
imped_dri2_copy_region_callback(PixmapPtr src, PixmapPtr dst,
				RegionPtr pRegion, RegionPtr front_clip)
{
    miCopyProc copy;
    int nbox;
    GCPtr gc;
    BoxPtr pbox;

    copy = src->drawable.pScreen->GetCopyAreaFunction(&src->drawable, &dst->drawable);

    gc = GetScratchGC(dst->drawable.depth, src->drawable.pScreen);
    if (!gc)
	return;

    ValidateGC(&dst->drawable, gc);
    nbox = RegionNumRects(pRegion);
    pbox = RegionRects(pRegion);
    copy(&src->drawable, &dst->drawable, gc, pbox, nbox, 0, 0, 0, 0, 0, NULL);
    FreeScratchGC(gc);
}


static void
imped_copy_region(PixmapPtr pixmap, RegionPtr pRegion,
		  DRI2BufferPtr dst_buffer, DRI2BufferPtr src_buffer)
{
    impedDRI2BufferPrivatePtr src_private = src_buffer->driverPrivate;
    impedDRI2BufferPrivatePtr dst_private = dst_buffer->driverPrivate;
    PixmapPtr src, dst;

    ScreenPtr pScreen = pixmap->drawable.pScreen;
    DRI2ScreenPtr gpu_ds = DRI2GetScreen(pScreen);
    
    src = src_private->pixmap;
    dst = dst_private->pixmap;

    gpu_ds->CopyRegionPixmap(src, dst, pRegion, NULL, imped_dri2_copy_region_callback);
}

static void imped_dri2_copy_region(DrawablePtr pDraw,
                                   RegionPtr pRegion,
                                   DRI2BufferPtr pDestBuffer,
                                   DRI2BufferPtr pSrcBuffer)
{
    ScreenPtr gpuSceren = pDraw->pScreen->gpu[pDraw->pScreen->primary_gpu_index];
    PixmapPtr pPixmap = GetDrawablePixmap(pDraw);
    PixmapPtr src;
    PixmapPtr dst;

    ErrorF("draw copy region %d %dx%d vs p %dx%d\n", pDraw->type, pDraw->width, pDraw->height, pPixmap->drawable.width, pPixmap->drawable.height);

    imped_copy_region(pPixmap->gpu[pDraw->pScreen->primary_gpu_index], pRegion, pDestBuffer, pSrcBuffer);
    
    if (pDraw->type == DRAWABLE_WINDOW) {
	/* translate */
    }

}

static int imped_dri2_schedule_swap(ClientPtr client,
                                    DrawablePtr pDraw,
                                    DRI2BufferPtr pDestBuffer,
                                    DRI2BufferPtr pSrcBuffer,
                                    CARD64 *target_msc,
                                    CARD64 divisor,
                                    CARD64 remainder,
                                    DRI2SwapEventPtr func, void *data)
{
    ScreenPtr pScreen = pDraw->pScreen;
    PixmapPtr pPixmap = GetDrawablePixmap(pDraw);
    PixmapPtr gpuPixmap = pPixmap->gpu[pScreen->primary_gpu_index];
    int index = pScreen->primary_gpu_index;
    DRI2ScreenPtr gpu_ds = DRI2GetScreen(gpuPixmap->drawable.pScreen);
    BoxRec box;
    RegionRec region;
    int ret;
    DRI2FrameEventPtr swap_info = NULL;
    enum DRI2FrameEventType swap_type = DRI2_SWAP;
    Bool can_flip = DRI2CanFlip(pDraw); //can exchange

    if (!gpu_ds->ScheduleSwapPixmap)
	goto blit_fallback;

    init_front_region(pDraw, &region);
 
    swap_info = calloc(1, sizeof(DRI2FrameEventRec));
    if (!swap_info)
	goto blit_fallback;
    
    swap_info->drawable_id = pDraw->id;
    swap_info->client = client;
    swap_info->event_complete = func;
    swap_info->event_data = data;
    swap_info->front = pDestBuffer;
    swap_info->back = pSrcBuffer;

    imped_dri2_reference_buffer(pDestBuffer);
    imped_dri2_reference_buffer(pSrcBuffer);

    ret = gpu_ds->ScheduleSwapPixmap(gpuPixmap, &region,
			       swap_info, target_msc,
			       divisor, remainder, can_flip);

    RegionUninit(&region);
    if (ret == TRUE)
	return TRUE;
    
 blit_fallback:
    box.x1 = 0;
    box.y1 = 0;
    box.x2 = pDraw->width;
    box.y2 = pDraw->height;
    RegionInit(&region, &box, 0);

    imped_dri2_copy_region(pDraw, &region, pDestBuffer, pSrcBuffer);

    DRI2SwapComplete(client, pDraw, 0, 0, 0, DRI2_BLIT_COMPLETE, func, data);
    *target_msc = 0; /* offscreen, so zero out target vblank count */
    return TRUE;
}

static int imped_dri2_get_msc(DrawablePtr pDraw, CARD64 *ust, CARD64 *msc)
{
    PixmapPtr pPixmap = GetDrawablePixmap(pDraw);
    ScreenPtr gpuScreen = pDraw->pScreen->gpu[pDraw->pScreen->primary_gpu_index];
    DRI2ScreenPtr gpu_ds = DRI2GetScreen(gpuScreen);
    PixmapPtr gpuPixmap = pPixmap->gpu[pDraw->pScreen->primary_gpu_index];
    int ret;
 
    ret = gpu_ds->GetMSC(&gpuPixmap->drawable, ust, msc);
    return ret;
}

Bool impedDRI2ScreenInit(ScreenPtr screen)
{
    DRI2InfoRec info;
    const char *driverNames[1];

    memset(&info, 0, sizeof(info));
    info.version = 7;
    info.CreateBuffer2 = imped_dri2_create_buffer2;
    info.DestroyBuffer = imped_dri2_destroy_buffer;
    info.AuthMagic2 = imped_dri2_auth_magic2;
    info.GetDriverInfo = imped_dri2_get_driver_info;
    info.CopyRegion = imped_dri2_copy_region;
    info.ScheduleSwap = imped_dri2_schedule_swap;
    info.GetMSC = imped_dri2_get_msc;
    info.numDrivers = 1;
    driverNames[0] = "hotplug";
    info.driverNames = driverNames;
    info.deviceName = NULL;
    info.fd = -1;
    DRI2ScreenInit(screen, &info);

    return TRUE;
}

void DRI2FrameEventHandler(DRI2FrameEventPtr event,
			   unsigned int frame,
			   unsigned int tv_sec,
			   unsigned int tv_usec,
			   Bool can_flip)
{
    DrawablePtr drawable;
    int status;

    status = dixLookupDrawable(&drawable, event->drawable_id, serverClient,
                               M_ANY, DixWriteAccess);

    if (status != Success) {
        imped_dri2_unref_buffer(NULL, event->front);
        imped_dri2_unref_buffer(NULL, event->back);
        free(event);
        return;
    }

    ErrorF("frame event handler %d:\n", event->type);
    switch (event->type) {
    case DRI2_FLIP:
	break;
    case DRI2_SWAP: {
	int swap_type;
	if (DRI2CanExchange(drawable) && 0) {
	    /* TODO */
	} 
	else {
	    BoxRec box;
	    RegionRec region;
	    box.x1 = 0;
	    box.y1 = 0;
	    box.x2 = drawable->width;
	    box.y2 = drawable->height;
	    RegionInit(&region, &box, 0);
	    imped_dri2_copy_region(drawable, &region,
				   event->front, event->back);
	}
	DRI2SwapComplete(event->client, drawable, frame, tv_sec,
			 tv_usec, swap_type, event->event_complete,
			 event->event_data);
    }
    case DRI2_WAITMSC:
	DRI2WaitMSCComplete(event->client, drawable, frame, tv_sec, tv_usec);
	break;
    default:
	break;
    }

    imped_dri2_unref_buffer(NULL, event->front);
    imped_dri2_unref_buffer(NULL, event->back);
    free(event);
}

void DRI2FlipEventHandler(DRI2FrameEventPtr flip,
			  unsigned int frame,
			  unsigned int tv_sec,
			  unsigned int tv_usec)
{
    int status;
    DrawablePtr drawable;

    status = dixLookupDrawable(&drawable, flip->drawable_id, serverClient,
                               M_ANY, DixWriteAccess);    
    if (status != Success) {
        free(flip);
        return;
    }
    switch (flip->type) {
    case DRI2_SWAP:
	break;
    default:
	break;
    }
    free(flip);
}
