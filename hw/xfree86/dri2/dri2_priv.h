#ifndef DRI2_PRIV_H
#define DRI2_PRIV_H
typedef struct _DRI2Screen {
    ScreenPtr screen;
    int refcnt;
    unsigned int numDrivers;
    const char **driverNames;
    const char *deviceName;
    int fd;
    unsigned int lastSequence;

    DRI2CreateBufferProcPtr CreateBuffer;
    DRI2DestroyBufferProcPtr DestroyBuffer;
    DRI2CopyRegionProcPtr CopyRegion;
    DRI2ScheduleSwapProcPtr ScheduleSwap;
    DRI2GetMSCProcPtr GetMSC;
    DRI2ScheduleWaitMSCProcPtr ScheduleWaitMSC;
    DRI2AuthMagicProcPtr AuthMagic;
    DRI2ReuseBufferNotifyProcPtr ReuseBufferNotify;
    DRI2SwapLimitValidateProcPtr SwapLimitValidate;

    HandleExposuresProcPtr HandleExposures;

    ConfigNotifyProcPtr ConfigNotify;

    DRI2GetDriverInfoProcPtr GetDriverInfo;
    DRI2AuthMagic2ProcPtr AuthMagic2;
    DRI2CreateBuffer2ProcPtr CreateBuffer2;

    DRI2CreateBufferPixmapProcPtr CreateBufferPixmap;
    DRI2DestroyBufferPixmapProcPtr DestroyBufferPixmap;
    DRI2CopyRegionPixmapProcPtr CopyRegionPixmap;

    DRI2ScheduleSwapPixmapProcPtr ScheduleSwapPixmap;
    DRI2PageFlipPixmapProcPtr PageFlipPixmap;
    DRI2ExchangePixmapProcPtr ExchangePixmap;
} DRI2ScreenRec;

typedef struct _DRI2Screen *DRI2ScreenPtr;

extern DevPrivateKeyRec dri2ScreenPrivateKeyRec;
#define dri2ScreenPrivateKey (&dri2ScreenPrivateKeyRec)

static inline DRI2ScreenPtr
DRI2GetScreen(ScreenPtr pScreen)
{
    return dixLookupPrivate(&pScreen->devPrivates, dri2ScreenPrivateKey);
}
#endif
