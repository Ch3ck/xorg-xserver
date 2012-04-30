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
                                    CARD64 * target_msc,
                                    CARD64 divisor,
                                    CARD64 remainder,
                                    DRI2SwapEventPtr func, void *data)
{
    ScreenPtr pScreen = pDraw->pScreen;
    PixmapPtr pPixmap = GetDrawablePixmap(pDraw);
    //    DRI2ScreenPtr gpu_ds = DRI2GetScreenPrime(pDraw->pScreen, buffer->prime_id);
    BoxRec box;
    RegionRec region;

 blit_fallback:
    box.x1 = 0;
    box.y1 = 0;
    box.x2 = pDraw->width;
    box.y2 = pDraw->height;

    RegionInit(&region, &box, 0);

    imped_dri2_copy_region(pDraw, &region, pDestBuffer, pSrcBuffer);
    DRI2SwapComplete(client, pDraw, 0, 0, 0, DRI2_BLIT_COMPLETE, func, data);
    return TRUE;
}

static int imped_dri2_get_msc(DrawablePtr pDraw, CARD64 *ust, CARD64 *msc)
{
    return 0;
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
