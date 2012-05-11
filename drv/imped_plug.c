/* impedance layer screen functions - replaces
 *
 * fb/mi as the bottom layer of wrapping for protocol level screens
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>

#include "windowstr.h"
#include "gcstruct.h"
#include "pixmapstr.h"
#include "servermd.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "imped.h"
#include "randrstr.h"
#include "mi.h"
#include "micmap.h"

static Bool
dup_pixmap_contents(PixmapPtr pDst, PixmapPtr pSrc)
{
    char *image_ptr;
    int size;
    GCPtr pGC;

    size = PixmapBytePad(pSrc->drawable.width, pSrc->drawable.depth) * pSrc->drawable.height;
    image_ptr = malloc(size);

    pSrc->drawable.pScreen->GetImage(&pSrc->drawable, 0, 0, pSrc->drawable.width,
				     pSrc->drawable.height, ZPixmap, ~0,
				     image_ptr);

    pGC = GetScratchGC(pSrc->drawable.depth, pDst->drawable.pScreen);
    pGC->stateChanges &= ~GCTile;

    ValidateGC(&pDst->drawable, pGC);

    pGC->ops->PutImage(&pDst->drawable, pGC, pDst->drawable.depth, 0, 0,
		       pSrc->drawable.width, pSrc->drawable.height, 0,
		       ZPixmap, image_ptr);

    FreeScratchGC(pGC);
    free(image_ptr);
    return TRUE;
}

static void dup_pixmap(PixmapPtr pPixmap, ScreenPtr new, int new_gpu_idx)
{
    pPixmap->gpu[new_gpu_idx] = new->CreatePixmap(new, pPixmap->drawable.width,
						  pPixmap->drawable.height,
						  pPixmap->drawable.depth,
						  pPixmap->usage_hint);

    dup_pixmap_contents(pPixmap->gpu[new_gpu_idx], pPixmap->gpu[0]);
}

int
impedAddScreen(ScreenPtr protocol_master, ScreenPtr new)
{
    int new_gpu_index;
    int ret;

    impedAttachScreen(protocol_master, new);

    new_gpu_index = protocol_master->num_gpu - 1;
    ErrorF("hot adding GPU %d\n", new_gpu_index);

    {
	GCPtr pGC;
	xorg_list_for_each_entry(pGC, &protocol_master->gc_list, member) {
	    pGC->gpu[new_gpu_index] = NewGCObject(new, pGC->depth);
	    pGC->gpu[new_gpu_index]->parent = pGC;
	    ret = new->CreateGC(pGC->gpu[new_gpu_index]);
	    if (ret == FALSE)
		ErrorF("failed to create GC\n");
   	}
    }

    {
	PixmapPtr pPixmap;
	xorg_list_for_each_entry(pPixmap, &protocol_master->pixmap_list, member) {
	    dup_pixmap(pPixmap, new, new_gpu_index);
   	}
    }

    {
	PicturePtr pPicture;
	xorg_list_for_each_entry(pPicture, &protocol_master->picture_list, member) {
	    impedPictureDuplicate(pPicture, new_gpu_index);
   	}
    }

    /* set the screen pixmap up correctly */
    {
        PixmapPtr pPixmap;

        pPixmap = protocol_master->GetScreenPixmap(protocol_master);

        protocol_master->gpu[new_gpu_index]->SetScreenPixmap(pPixmap->gpu[new_gpu_index]);
    }

    return 0;
}

Bool
impedRemoveScreen(ScreenPtr protocol_master, ScreenPtr slave)
{
    int remove_index = -1;
    int i;

    for (i = 0; i < protocol_master->num_gpu; i++) {
	if (protocol_master->gpu[i] == slave){
	    remove_index = i;
	    break;
	}
    }
    
    if (remove_index == -1)
	return FALSE;

    ErrorF("ot removing GPU %d\n", remove_index);

    {
	PicturePtr pPicture;
	xorg_list_for_each_entry(pPicture, &protocol_master->picture_list, member) {
	    PicturePtr tofree = pPicture->gpu[remove_index];
	    pPicture->gpu[remove_index] = NULL;
	    for (i = remove_index ; i < protocol_master->num_gpu - 1; i++)
		pPicture->gpu[i] = pPicture->gpu[i + 1];
	    FreePicture(tofree, (XID)0);
	}
    }
    {
	GCPtr pGC;
	xorg_list_for_each_entry(pGC, &protocol_master->gc_list, member) {
	    GCPtr tofree = pGC->gpu[remove_index];
	    pGC->serialNumber = NEXT_SERIAL_NUMBER;
	    pGC->gpu[remove_index] = NULL;
	    for (i = remove_index ; i < protocol_master->num_gpu - 1; i++)
		pGC->gpu[i] = pGC->gpu[i + 1];
	    FreeGC(tofree, 0);
	}
    }

    {
	PixmapPtr pPixmap;
	xorg_list_for_each_entry(pPixmap, &protocol_master->pixmap_list, member) {
	    PixmapPtr tofree = pPixmap->gpu[remove_index];
	    pPixmap->gpu[remove_index] = NULL;
	    for (i = remove_index ; i < protocol_master->num_gpu - 1; i++)
		pPixmap->gpu[i] = pPixmap->gpu[i + 1];
	    (*slave->DestroyPixmap)(tofree);
	}
    }

    xorg_list_del(&slave->gpu_screen_head);
    protocol_master->gpu[remove_index] = NULL;
    for (i = remove_index; i < protocol_master->num_gpu - 1; i++)
	protocol_master->gpu[i] = protocol_master->gpu[i + 1];

    protocol_master->num_gpu--;

    return TRUE;
}

