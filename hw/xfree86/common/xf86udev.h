#ifndef XF86_UDEV_H
#define XF86_UDEV_H


struct xf86_udev_device {
    char *path;
    char *busid;
    char *syspath;
    int dev_type;
    /* for PCI devices */
    struct pci_device *pdev;
};


#if defined(CONFIG_UDEV)

#endif

#endif
