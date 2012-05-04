/*
 * Copyright © 2006 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "randrstr.h"

RESTYPE	RRProviderType;

int
ProcRRGetProviders (ClientPtr client)
{
    REQUEST(xRRGetProvidersReq);
    xRRGetProvidersReply rep;
    WindowPtr pWin;
    ScreenPtr pScreen;
    rrScrPrivPtr		pScrPriv;
    int rc;
    CARD8 *extra;
    unsigned int extraLen;
    RRProvider *providers;
    int i;

    REQUEST_SIZE_MATCH(xRRGetProvidersReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
	return rc;

    pScreen = pWin->drawable.pScreen;
    pScrPriv = rrGetScrPriv(pScreen);

    rep.pad = 0;
    
    if (!pScrPriv)
    {
	rep.type = X_Reply;
	rep.sequenceNumber = client->sequence;
	rep.length = 0;
	rep.timestamp = currentTime.milliseconds;
	rep.nProviders = 0;
	extra = NULL;
	extraLen = 0;
    } else {
	rep.type = X_Reply;
	rep.sequenceNumber = client->sequence;
	rep.timestamp = pScrPriv->lastSetTime.milliseconds;
	rep.length = 0;
	rep.nProviders = pScrPriv->numProviders;

	rep.length = pScrPriv->numProviders;
	extraLen = rep.length << 2;
	if (extraLen) {
	    extra = malloc(extraLen);
	    if (!extra)
		return BadAlloc;
	} else
	    extra = NULL;
		
	providers = (RRProvider *)extra;
	for (i = 0; i < pScrPriv->numProviders; i++) {
	    providers[i] = pScrPriv->providers[i]->id;
	    if (client->swapped)
		swapl(&providers[i]);
	}
    }

    if (client->swapped) {
	swaps(&rep.sequenceNumber);
	swapl(&rep.length);
	swapl(&rep.timestamp);
	swaps(&rep.nProviders);
    }
    WriteToClient(client, sizeof(xRRGetProvidersReply), (char *)&rep);
    if (extraLen)
    {
	WriteToClient (client, extraLen, (char *) extra);
	free(extra);
    }
    return Success;
}

int
ProcRRGetProviderInfo (ClientPtr client)
{
    REQUEST(xRRGetProviderInfoReq);
    xRRGetProviderInfoReply rep;
    rrScrPrivPtr		pScrPriv;
    RRProviderPtr provider;
    ScreenPtr pScreen;
    CARD8 *extra;
    unsigned int extraLen = 0;

    REQUEST_SIZE_MATCH(xRRGetProviderInfoReq);
    VERIFY_RR_PROVIDER(stuff->provider, provider, DixReadAccess);

    pScreen = provider->pScreen;
    pScrPriv = rrGetScrPriv(pScreen);

    rep.type = X_Reply;
    rep.status = RRSetConfigSuccess;
    rep.sequenceNumber = client->sequence;
    rep.length = 0;
    rep.current_role = provider->current_role;
    rep.allowed_roles = provider->allowed_roles;
    if (rep.current_role > 0) {
      //        rep.nCrtcs = provider->numCrtcs;
      //	rep.nOutputs = provider->numOutputs;
    } else {
        rep.nCrtcs = 0;
	rep.nOutputs = 0;
    }
    if (client->swapped) {
      	swaps(&rep.sequenceNumber);
	swapl(&rep.length);
	swapl(&rep.current_role);
	swapl(&rep.allowed_roles);
	swaps(&rep.nCrtcs);
	swaps(&rep.nOutputs);
    }
    WriteToClient(client, sizeof(xRRGetProviderInfoReply), (char *)&rep);
    if (extraLen)
    {
	WriteToClient (client, extraLen, (char *) extra);
	free(extra);
    }
    return Success;
}

int
ProcRRSetProviderRole (ClientPtr client)
{
    REQUEST(xRRSetProviderRoleReq);
    rrScrPrivPtr		pScrPriv;
    RRProviderPtr provider;
    ScreenPtr pScreen;
    Bool ret;
    REQUEST_SIZE_MATCH(xRRSetProviderRoleReq);
    VERIFY_RR_PROVIDER(stuff->provider, provider, DixReadAccess);

    pScreen = provider->pScreen;
    pScrPriv = rrGetScrPriv(pScreen);

    if (stuff->new_role) {
	if (!(stuff->new_role & provider->allowed_roles))
	    return BadValue;

	if (stuff->new_role == provider->current_role)
	    return Success;
    }

    ret = pScrPriv->rrProviderSetRole(pScreen, provider, stuff->new_role);

    RRTellChanged (pScreen);
    return Success;
}

RRProviderPtr
RRProviderCreate(ScreenPtr pScreen, void *devPrivate)
{
    RRProviderPtr provider;
    RRProviderPtr *providers;
    rrScrPrivPtr    pScrPriv;
    
    pScrPriv = rrGetScrPriv(pScreen);

    /* make space for the crtc pointer */
    if (pScrPriv->numProviders)
	providers = realloc(pScrPriv->providers, 
			  (pScrPriv->numProviders + 1) * sizeof (RRProviderPtr));
    else
      providers = malloc(sizeof (RRProviderPtr));
    if (!providers)
	return NULL;
    pScrPriv->providers = providers;

    provider = calloc(1, sizeof(RRProviderRec));
    if (!provider)
	return NULL;

    provider->id = FakeClientID(0);
    provider->pScreen = pScreen;
    provider->devPrivate = devPrivate;
    if (!AddResource (provider->id, RRProviderType, (pointer) provider))
	return NULL;
    pScrPriv->providers[pScrPriv->numProviders++] = provider;
    return provider;
}

/*
 * Destroy a provider at shutdown
 */
void
RRProviderDestroy (RRProviderPtr provider)
{
    FreeResource (provider->id, 0);
}

static int
RRProviderDestroyResource (pointer value, XID pid)
{
    RRProviderPtr provider = (RRProviderPtr)value;
    ScreenPtr pScreen = provider->pScreen;

    if (pScreen)
    {
	rrScrPriv(pScreen);
	int		i;
    
	for (i = 0; i < pScrPriv->numProviders; i++)
	{
	    if (pScrPriv->providers[i] == provider)
	    {
		memmove (pScrPriv->providers + i, pScrPriv->providers + i + 1,
			 (pScrPriv->numProviders - (i + 1)) * sizeof (RRProviderPtr));
		--pScrPriv->numProviders;
		break;
	    }
	}
    }
    free(provider);
    return 1;
}

Bool
RRProviderInit(void)
{
    RRProviderType = CreateNewResourceType(RRProviderDestroyResource, "Provider");
    if (!RRProviderType)
	return FALSE;

    return TRUE;
}

extern _X_EXPORT Bool
RRProviderLookup(XID id, RRProviderPtr *provider_p)
{
    RRProviderPtr provider;
    int rc = dixLookupResourceByType((void **)provider_p, id,
				   RRProviderType, NullClient, DixReadAccess);
    if (rc == Success)
        return TRUE;
    return FALSE;
}
