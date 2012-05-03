#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif
#include <errno.h>
#ifdef WITH_LIBDRM
#include <xf86drm.h>
#endif
#include <pciaccess.h>
#include <libudev.h>
#include <fcntl.h>
#include <unistd.h>
#include "os.h"
#include "hotplug.h"

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86str.h"
#include "xf86Bus.h"

#include "xf86udev.h"

static int num_udev_devices;

static struct xf86_udev_device *xf86_udev_devices;

static int
xf86_add_udev_device(const char *path, char *busid, const char *syspath)
{
    xf86_udev_devices = xnfrealloc(xf86_udev_devices,
				   (sizeof(struct xf86_udev_device)
				    * (num_udev_devices + 1)));
    xf86_udev_devices[num_udev_devices].path = strdup(path);
    xf86_udev_devices[num_udev_devices].busid = strdup(busid);
    xf86_udev_devices[num_udev_devices].syspath = strdup(syspath);
    num_udev_devices++;
    return 0;
}

static void
get_drm_info(const char *path, const char *syspath)
{
    drmSetVersion sv;
    char *buf;
    int fd;

    fd = open(path, O_RDWR, O_CLOEXEC);
    if (fd == -1)
	return;

    sv.drm_di_major = 1;
    sv.drm_di_minor = 4;
    sv.drm_dd_major = -1;       /* Don't care */
    sv.drm_dd_minor = -1;       /* Don't care */
    if (drmSetInterfaceVersion(fd, &sv)) {
	ErrorF("setversion 1.4 failed\n");
	return;
    }

    buf = drmGetBusid(fd);
    xf86_add_udev_device(path, buf, syspath);
    drmFreeBusid(buf);
    close(fd);

}

static void
kms_device_test(struct udev_device *udev_device)
{
    const char *path, *name = NULL;
    const char *syspath;
    int i;

    path = udev_device_get_devnode(udev_device);
    syspath = udev_device_get_syspath(udev_device);

    if (!path || !syspath)
        return;

    if (!strcmp(udev_device_get_subsystem(udev_device), "drm")) {
	const char *sysname = udev_device_get_sysname(udev_device);

	if (strncmp(sysname, "card", 4))
	    return;

	for (i = 0; i < num_udev_devices; i++)
	    if (!strcmp(path, xf86_udev_devices[i].path))
		break;

	if (i != num_udev_devices)
	    return;

	LogMessage(X_INFO, "config/udev: Adding drm device (%s)\n",
               path);
	
	get_drm_info(path, syspath);
	return;
    }
}

int
config_udev_output_probe(void)
{
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *device;
    struct udev_monitor *udev_monitor = config_udev_monitor();

    udev = udev_monitor_get_udev(udev_monitor);
    enumerate = udev_enumerate_new(udev);
    if (!enumerate)
        return 0;

    udev_enumerate_add_match_subsystem(enumerate, "drm");
    udev_enumerate_add_match_sysname(enumerate, "card[0-9]*");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry_foreach(device, devices) {
        const char *syspath = udev_list_entry_get_name(device);
        struct udev_device *udev_device = udev_device_new_from_syspath(udev, syspath);
        kms_device_test(udev_device);
        udev_device_unref(udev_device);
    }
    udev_enumerate_unref(enumerate);
    return 0;
}

static void
udev_find_pci_info(int index)
{
    struct pci_slot_match devmatch;
    struct pci_device *info;
    struct pci_device_iterator *iter;
    int ret;
    int i;
    ret = sscanf(xf86_udev_devices[index].busid, "pci:%04x:%02x:%02x.%u",
		 &devmatch.domain, &devmatch.bus, &devmatch.dev,
		 &devmatch.func);
    if (ret != 4)
	return;

    iter = pci_slot_match_iterator_create(&devmatch);
    info = pci_device_next(iter);
    if (info) {
	char *drvnames[5] = {0};
	ErrorF("vendor id is %04x:%04x\n", info->vendor_id,
	       info->device_id);

	ret = videoPtrToDriverList(info, drvnames, 5);
	if (ret < 5) {
	    drvnames[ret] = "modesetting";
	    ret++;
	}
	if (ret > 0) {
	    for (i = 0; i < ret; i++)
		fprintf(stderr,"drvname %s\n", drvnames[i]);
	}

	xf86_udev_devices[index].pdev = info;
	pci_device_probe(info);
    }
    pci_iterator_destroy(iter); 

}

int xf86udevMatchDriver(char *matches[], int nmatches)
{
    int i;
    int num = 0;
    if (!num_udev_devices)
	return 0;

    for (i = 0; i < num_udev_devices; i++) {
	if (!xf86_udev_devices[i].pdev)
	    continue;

	if (xf86_udev_devices[i].pdev && num < nmatches)
	    num += videoPtrToDriverList(xf86_udev_devices[i].pdev, &(matches[num]),
					nmatches - num);

    }
    if (num < (nmatches - 1)) {
	matches[num++] = xnfstrdup("modesetting");
    }
    return num;
}

Bool
xf86_check_udev_slot(const struct xf86_udev_device *ud)
{
    int i;

    for (i = 0; i < xf86NumEntities; i++) {
	const EntityPtr u = xf86Entities[i];

	if ((u->bus.type == BUS_UDEV) && (ud == u->bus.id.udev)) {
	    return FALSE;
	}
    }
    return TRUE;
}

int xf86udevProbe(void)
{
    int ret;
    int i;
    Bool pci = TRUE;

    if (!xf86scanpci()) {
	pci = FALSE;
    }

    ret = config_udev_output_probe();
    for (i = 0; i < num_udev_devices; i++) {
	ErrorF("found device %s %s %s\n", xf86_udev_devices[i].path,
	       xf86_udev_devices[i].busid, xf86_udev_devices[i].syspath);
	if (pci && !strncmp(xf86_udev_devices[i].busid, "pci:", 4)) {
	    udev_find_pci_info(i);
	} else if (!strncmp(xf86_udev_devices[i].busid, "usb:", 4)) {

	}
    }
    return 0;

}

int
xf86ClaimUdevSlot(struct xf86_udev_device * d, DriverPtr drvp,
		  int chipset, GDevPtr dev, Bool active)
{
    EntityPtr p = NULL;
    int num;
    
    num = xf86AllocateEntity();
    p = xf86Entities[num];
    p->driver = drvp;
    p->chipset = chipset;
    p->bus.type = BUS_UDEV;
    p->bus.id.udev = d;
    p->active = active;
    p->inUse = FALSE;
    if (dev)
      xf86AddDevToEntity(num, dev);
    
    return num;
}

int xf86UnclaimUdevSlot(struct xf86_udev_device *d)
{
    int i;

    for (i = 0; i < xf86NumEntities; i++) {
	const EntityPtr p = xf86Entities[i];

	if ((p->bus.type == BUS_UDEV) && (p->bus.id.udev == d)) {
	    p->bus.type = BUS_NONE;
	    return 0;
	}
    }
    return 0;
}

#define END_OF_MATCHES(m) \
    (((m).vendor_id == 0) && ((m).device_id == 0) && ((m).subvendor_id == 0))

int xf86udevProbeDev(DriverPtr drvp)
{
    Bool foundScreen = FALSE;
    GDevPtr *devList;
    const unsigned numDevs = xf86MatchDevice(drvp->driverName, &devList);
    int i, j, k;
    int  entity;
    const struct pci_id_match *const devices = drvp->supported_devices;
    struct pci_device *pPci;

    for (i = 0; i < numDevs; i++) {
	for (j = 0; j < num_udev_devices; j++) {
	    /* overload PCI match loading if we can use it */
	    if (xf86_udev_devices[j].pdev && devices) {
		int device_id = xf86_udev_devices[j].pdev->device_id;
		pPci = xf86_udev_devices[j].pdev;
		for (k = 0; !END_OF_MATCHES(devices[k]); k++) {
		    if (PCI_ID_COMPARE(devices[k].vendor_id, pPci->vendor_id)
			&& PCI_ID_COMPARE(devices[k].device_id, device_id)
			&& ((devices[k].device_class_mask & pPci->device_class)
			    ==  devices[k].device_class)) {
			if (xf86_check_udev_slot(&xf86_udev_devices[j])) {
			    entity = xf86ClaimUdevSlot(&xf86_udev_devices[j],
						       drvp, 0, devList[i], devList[i]->active);
			    if (entity != -1) {
				if (drvp->UdevProbe(drvp, entity, &xf86_udev_devices[j], devices[k].match_data))
				    continue;
				foundScreen = TRUE;
				break;
			    }
			    else
				xf86UnclaimUdevSlot(&xf86_udev_devices[j]);
			}
		    }
		}
	    } else if (xf86_udev_devices[j].pdev && !devices)
		  continue;
	    else {
		if (xf86_check_udev_slot(&xf86_udev_devices[j])) {
		    entity = xf86ClaimUdevSlot(&xf86_udev_devices[j],
					       drvp, 0, devList[i], devList[i]->active);
		    if (drvp->UdevProbe(drvp, entity, &xf86_udev_devices[j], 0))
			continue;
		    foundScreen = TRUE;
		}
	    }
	}
    }
    return foundScreen;
}

int AddOutputDevice(struct udev_device *udev_device)
{
    int old_num = num_udev_devices;
    int old_screens;
    int entity;
    screenLayoutPtr layout;

    kms_device_test(udev_device);

    if (old_num != num_udev_devices) {
	DriverPtr drvp;
	int i = old_num;
	ErrorF("found device %s %s %s\n", xf86_udev_devices[i].path,
	       xf86_udev_devices[i].busid, xf86_udev_devices[i].syspath);

	if (!strncmp(xf86_udev_devices[i].busid, "pci:", 4)) {
	    udev_find_pci_info(i);
	}

	for (i = 0; i < xf86NumDrivers; i++) {
	    if (!strcmp(xf86DriverList[i]->driverName, "modesetting")) {
		drvp = xf86DriverList[i];
		break;
	    }
	}
	if (i == xf86NumDrivers)
	    return 0;
	
	old_screens = xf86NumGPUScreens;
	entity = xf86ClaimUdevSlot(&xf86_udev_devices[num_udev_devices-1],
				   drvp, 0, 0, 0);
	drvp->UdevProbe(drvp, entity, &xf86_udev_devices[num_udev_devices-1], 0);
	
	if (old_screens == xf86NumGPUScreens)
	    return 0;
	i = old_screens;

        for (layout = xf86ConfigLayout.screens; layout->screen != NULL;
             layout++) {
	    xf86GPUScreens[i]->confScreen = layout->screen;
	    break;
	}
#if 0

	/* preinit */
	if (xf86GPUScreens[i]->PreInit &&
	    xf86GPUScreens[i]->PreInit(xf86GPUScreens[i], 0))
	    xf86GPUScreens[i]->configured = TRUE;

	xf86GPUScreens[i]->pGPUScreen = GPUScreenAllocate();
	xf86GPUScreens[i]->pGPUScreen->roles = xf86GPUScreens[i]->roles;
	dixSetPrivate(&xf86GPUScreens[i]->pGPUScreen->devPrivates,
		      xf86GPUScreenKey, xf86GPUScreens[i]);
	xf86GPUScreens[i]->ScreenInit(xf86GPUScreens[i]);

	xf86GPUScreens[i]->pGPUScreen->provider = RRProviderCreate(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pGPUScreen);
	xf86GPUScreens[i]->pGPUScreen->provider_id = xf86GPUScreens[i]->pGPUScreen->provider->id;
	xf86GPUScreens[i]->pGPUScreen->provider->allowed_roles = xf86GPUScreens[i]->pGPUScreen->roles;
	impedAttachUnboundScreen(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pGPUScreen);
#endif
    }
    return 0;
}

void RemoveOutputDevice(struct udev_device *udev_device)
{
    int index, i, j, ent_num;
    const char *syspath;
    EntityPtr entity;
    Bool found;

    syspath = udev_device_get_syspath(udev_device);
    if (!syspath)
	return;

    for (index = 0; index < num_udev_devices; index++) {
	if (!strcmp(syspath, xf86_udev_devices[index].syspath))
	    break;
    }

    if (index == num_udev_devices)
	return;

    ErrorF("removing %d : %s\n", index, syspath);
    
    for (ent_num = 0; ent_num < xf86NumEntities; ent_num++) {
	entity = xf86Entities[ent_num];
	if (entity->bus.type == BUS_UDEV &&
	    entity->bus.id.udev == &xf86_udev_devices[index])
	    break;
    }
    if (ent_num == xf86NumEntities)
	return;

    ErrorF("removing entity %d\n", ent_num);

    found = FALSE;
    for (i = 0; i < xf86NumGPUScreens; i++) {
	for (j = 0; j < xf86GPUScreens[i]->numEntities; j++)
	    if (xf86GPUScreens[i]->entityList[j] == ent_num) {
		found = TRUE;
		break;
	    }
	if (found)
	    break;
    }
    if (!found) {
        ErrorF("failed to find screen to remove\n");
	return;
    }
#if 0
    switch (xf86GPUScreens[i]->pGPUScreen->provider->current_role) {
    case ROLE_MASTER:
	impedRemoveGPUScreen(xf86GPUScreens[i]->pGPUScreen->pScreen,
			     xf86GPUScreens[i]->pGPUScreen);
	break;
    case ROLE_SLAVE_OUTPUT:
	impedDetachOutputSlave(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pGPUScreen, 0);
	impedRandRUnbindScreen(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pGPUScreen);
	break;
    case ROLE_SLAVE_OFFLOAD:
	impedDetachOffloadSlave(xf86Screens[0]->pScreen, xf86GPUScreens[i]->pGPUScreen, 0);
	break;
    default:
	break;
    }

    //  RRProviderDestroy(xf86GPUScreens[i]->pGPUScreen->provider);

    xf86GPUScreens[i]->pGPUScreen->CloseScreen(xf86GPUScreens[i]->pGPUScreen);

    xf86DeleteGPUScreen(xf86GPUScreens[i], 0);
#endif

    xf86UnclaimUdevSlot(&xf86_udev_devices[i]);

    for (j = i; j < num_udev_devices - 1; j++)
	memcpy(&xf86_udev_devices[j], &xf86_udev_devices[j+1], sizeof(struct xf86_udev_device));
    num_udev_devices--;

    //    impedRemoveGPUScreen(ScreenPtr pScreen, GPUScreenPtr pGPUScreen)
}
