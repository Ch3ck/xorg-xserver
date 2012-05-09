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

#define ADD_PROVIDER(iter) do {                                 \
    pScrPriv = rrGetScrPriv((iter));                            \
    if (pScrPriv->provider) {                                   \
        providers[count_providers] = pScrPriv->provider->id;    \
        if (client->swapped)                                    \
            swapl(&providers[count_providers]);                 \
        count_providers++;                                      \
    }                                                           \
    } while(0)

static int
rrGetProviderMultiScreen(ClientPtr client, ScreenPtr pScreen)
{
    xRRGetProvidersReply rep;
    int total_providers, count_providers;
    ScreenPtr gpuscreen, iter;
    int i;
    rrScrPrivPtr		pScrPriv;
    int flags = RR_Provider_Flag_Dynamic;
    CARD8 *extra;
    unsigned int extraLen;
    RRProvider *providers;

    total_providers = 0;

    for (i = 0; i < pScreen->num_gpu; i++) {
        gpuscreen = pScreen->gpu[i];

        pScrPriv = rrGetScrPriv(gpuscreen);
        if (pScrPriv->provider)
            total_providers++;

        xorg_list_for_each_entry(iter, &gpuscreen->output_slave_list, output_head) {
            pScrPriv = rrGetScrPriv(iter);
            total_providers += pScrPriv->provider ? 1 : 0;
        }
        xorg_list_for_each_entry(iter, &gpuscreen->offload_slave_list, offload_head) {
            pScrPriv = rrGetScrPriv(iter);
            total_providers += pScrPriv->provider ? 1 : 0;
        }
    }
    xorg_list_for_each_entry(iter, &pScreen->unattached_list, unattached_head) {
        pScrPriv = rrGetScrPriv(iter);
        total_providers += pScrPriv->provider ? 1 : 0;
    }

    rep.pad = 0;
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.timestamp = pScrPriv->lastSetTime.milliseconds;
    rep.length = 0;
    rep.nProviders = total_providers;
    rep.flags = flags;

    rep.length = total_providers;
    extraLen = rep.length << 2;
    if (extraLen) {
        extra = malloc(extraLen);
        if (!extra)
            return BadAlloc;
    } else
        extra = NULL;

    providers = (RRProvider *)extra;
    count_providers = 0;
    for (i = 0; i < pScreen->num_gpu; i++) {
        gpuscreen = pScreen->gpu[i];

        ADD_PROVIDER(gpuscreen);

        xorg_list_for_each_entry(iter, &gpuscreen->output_slave_list, output_head) {
            ADD_PROVIDER(iter);
        }
        xorg_list_for_each_entry(iter, &gpuscreen->offload_slave_list, offload_head) {
            ADD_PROVIDER(iter);
        }
    }
    xorg_list_for_each_entry(iter, &pScreen->unattached_list, unattached_head) {
        ADD_PROVIDER(iter);
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
    int flags = 0;

    REQUEST_SIZE_MATCH(xRRGetProvidersReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
	return rc;

    pScreen = pWin->drawable.pScreen;

    if (pScreen->num_gpu)
        return rrGetProviderMultiScreen(client, pScreen);

    pScrPriv = rrGetScrPriv(pScreen);

    rep.pad = 0;
    
    if (!pScrPriv)
    {
	rep.type = X_Reply;
	rep.sequenceNumber = client->sequence;
	rep.length = 0;
	rep.timestamp = currentTime.milliseconds;
        rep.flags = 0;
	rep.nProviders = 0;
	extra = NULL;
	extraLen = 0;
    } else {
	rep.type = X_Reply;
	rep.sequenceNumber = client->sequence;
	rep.timestamp = pScrPriv->lastSetTime.milliseconds;
	rep.length = 0;
	rep.nProviders = 1;
        rep.flags = flags;

	rep.length = 1;
	extraLen = rep.length << 2;
	if (extraLen) {
	    extra = malloc(extraLen);
	    if (!extra)
		return BadAlloc;
	} else
	    extra = NULL;
		
	providers = (RRProvider *)extra;
        providers[i] = pScrPriv->provider->id;
	    if (client->swapped)
		swapl(&providers[i]);
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
    rep.allowed_roles = provider->roles;
    rep.abilities = provider->abilities;
    if (rep.current_role > 0) {
        rep.nCrtcs = 0;//provider->numCrtcs;
        rep.nOutputs = 0;//provider->numOutputs;
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
	if (!(stuff->new_role & provider->roles))
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

    provider = calloc(1, sizeof(RRProviderRec));
    if (!provider)
	return NULL;

    provider->id = FakeClientID(0);
    provider->pScreen = pScreen;
    provider->devPrivate = devPrivate;
    if (!AddResource (provider->id, RRProviderType, (pointer) provider))
	return NULL;
    pScrPriv->provider = provider;
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

void
RRProviderSetRolesAbilities(RRProviderPtr provider, uint32_t roles, uint32_t abilities)
{
    provider->roles = roles;
    provider->abilities = abilities;
}

void
RRProviderSetCurrentRole(RRProviderPtr provider, uint32_t current_role)
{
    provider->current_role = current_role;
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

        pScrPriv->provider = NULL;
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
