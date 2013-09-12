/**************************************************************************

gesture/gesture.c

Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved

Contact: Sung-Jin Park <sj76.park@samsung.com>
         Sangjin LEE <lsj119@samsung.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "misc.h"
#include "dixstruct.h"
#include "globals.h"
#include "extnsionst.h"
#include "windowstr.h"
#include "servermd.h"
#include "swaprep.h"
#include <X11/extensions/gestureproto.h>
#include "gestureext.h"
#include "protocol-versions.h"
#include "inputstr.h"
#include "list.h"

//#define __DEBUG_GESTURE_EXT__

DevPrivateKeyRec gestureWindowPrivateKeyRec;
#define gestureWindowPrivateKey (&gestureWindowPrivateKeyRec)

static Mask grabbed_events_mask = 0;
static int max_num_finger = 0;
static GestureGrabEventRec grabbed_events[GestureNumberEvents];

static Bool selected_win_exist = 0;
static struct xorg_list selectedWinList;
void (*gpGestureEventsGrabbed)(Mask *pGrabMask, GestureGrabEventPtr *pGrabEvent);
void (*gpGestureEventsSelected)(Window win, Mask *pEventMask);

extern CallbackListPtr ResourceStateCallback;

static unsigned char GestureReqCode = 0;
static int GestureEventBase = 0;
static int GestureErrorBase;

static GestureProcsPtr GestureProcs;
static GestureProcsRec GestureExtProcs = {
};

static DISPATCH_PROC(ProcGestureDispatch);
static DISPATCH_PROC(ProcGestureQueryVersion);
static DISPATCH_PROC(ProcGestureSelectEvents);
static DISPATCH_PROC(ProcGestureGetSelectedEvents);
static DISPATCH_PROC(ProcGestureGrabEvent);
static DISPATCH_PROC(ProcGestureUngrabEvent);
static DISPATCH_PROC(SProcGestureDispatch);
static DISPATCH_PROC(SProcGestureQueryVersion);
static DISPATCH_PROC(SProcGestureSelectEvents);
static DISPATCH_PROC(SProcGestureGetSelectedEvents);
static DISPATCH_PROC(SProcGestureGrabEvent);
static DISPATCH_PROC(SProcGestureUngrabEvent);

//event swap functions
static void SGestureNotifyGroupEvent(xGestureNotifyGroupEvent *from, xGestureNotifyGroupEvent *to);
static void SGestureNotifyFlickEvent(xGestureNotifyFlickEvent *from, xGestureNotifyFlickEvent *to);
static void SGestureNotifyPanEvent(xGestureNotifyPanEvent *from, xGestureNotifyPanEvent *to);
static void SGestureNotifyPinchRotationEvent(xGestureNotifyPinchRotationEvent *from, xGestureNotifyPinchRotationEvent *to);
static void SGestureNotifyTapEvent(xGestureNotifyTapEvent *from, xGestureNotifyTapEvent *to);
static void SGestureNotifyTapNHoldEvent(xGestureNotifyTapNHoldEvent *from, xGestureNotifyTapNHoldEvent *to);
static void SGestureNotifyHoldEvent(xGestureNotifyHoldEvent *from, xGestureNotifyHoldEvent *to);
static void SGestureNotifyEvent(xEvent * from, xEvent * to);

static winInfo *GestureAddWindowToList(Window win, struct xorg_list *winlst);
static int GestureRemoveWindowFromList(Window win, struct xorg_list *winlst);

void GestureExtInit(void)
{
    GestureExtensionInit();
}

void
GestureExtensionInit(void)
{
    ExtensionEntry* extEntry;

    if (extEntry = AddExtension(GESTURE_EXT_NAME,
                                 GestureNumberEvents,
                                 GestureNumberErrors,
                                 ProcGestureDispatch,
                                 SProcGestureDispatch,
                                 NULL,
                                 StandardMinorOpcode))
    {
        GestureReqCode = (unsigned char)extEntry->base;
        GestureErrorBase = extEntry->errorBase;
        GestureEventBase = extEntry->eventBase;
        EventSwapVector[GestureEventBase] = (EventSwapPtr) SGestureNotifyEvent;

        if (!dixRegisterPrivateKey(&gestureWindowPrivateKeyRec, PRIVATE_WINDOW, 0))
    	 {
    	 	ErrorF("[X11][GestureExtensionInit] Failed to register private key object !\n");
		return;
    	 }

	memset(grabbed_events, 0, sizeof(GestureGrabEventRec)*GestureNumberEvents);
	gpGestureEventsGrabbed = gpGestureEventsSelected = NULL;
	xorg_list_init(&selectedWinList);

	return;
    }

    return;
}

Bool
GestureRegisterCallbacks(void (*GestureEventsGrabbed)(Mask *pGrabMask, GestureGrabEventPtr *pGrabEvent),
	void (*GestureEventsSelected)(Window win, Mask *pEventMask))
{
	if(GestureEventsGrabbed && gpGestureEventsGrabbed)
	{
		ErrorF("[X11][GestureRegisterCallbacks] GestureEventsGrabbed has been registered already !\n");
		return FALSE;
	}

	if(GestureEventsSelected && gpGestureEventsSelected)
	{
		ErrorF("[X11][GestureRegisterCallbacks] GestureEventsSelected has been registered already !\n");
		return FALSE;
	}

	if(GestureEventsGrabbed)
		gpGestureEventsGrabbed = GestureEventsGrabbed;
	else
		gpGestureEventsGrabbed = NULL;

	if(GestureEventsSelected)
		gpGestureEventsSelected = GestureEventsSelected;
	else
		gpGestureEventsSelected = NULL;

	return TRUE;
}

static winInfo *
GestureAddWindowToList(Window win, struct xorg_list *winlst)
{
	winInfo *wi = NULL;

	wi = malloc(sizeof(winInfo));

	if(!wi)
	{
		ErrorF("[X11][GestureAddWindowToList] Failed to allocate memory !\n");
		return NULL;
	}

	wi->win = win;
	xorg_list_add(&wi->lnk, winlst);

	return wi;
}

static int
GestureRemoveWindowFromList(Window win, struct xorg_list *winlst)
{
	winInfo *wi = NULL, *tmp = NULL;

	if(xorg_list_is_empty(winlst))
		return 0;

	 xorg_list_for_each_entry_safe (wi, tmp, winlst, lnk)
	{
		if(wi->win == win)
		{
			xorg_list_del(&wi->lnk);
			free(wi);
			return 1;
		}
	}

	return 0;
}

/*
 * functions for gesture driver
 */ 
Bool GestureSetMaxNumberOfFingers(int num_finger)
{
	int i;

	if( max_num_finger )
		return TRUE;

	max_num_finger = num_finger + 1;
#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureSetMaxNumberOfFingers] num_finger=%d, max_num_finger=%d\n", num_finger, max_num_finger);
#endif//__DEBUG_GESTURE_EXT__

	for( i = 0 ;  i < GestureNumberEvents ;  i++ )
	{
		grabbed_events[i].pGestureGrabWinInfo = malloc(sizeof(GestureGrabWinInfoRec)*max_num_finger);
		if( !grabbed_events[i].pGestureGrabWinInfo )
		{
			ErrorF("[X11][GestureSetMaxNumberOfFingers] Failed to allocate memory for grabbed_events[%d].pGestureGrabWinInfo !\n", i);
			return FALSE;
		}
		memset(grabbed_events[i].pGestureGrabWinInfo, 0, sizeof(GestureGrabWinInfoRec)*max_num_finger);
	}

	return TRUE;
}

Bool GestureUnsetMaxNumberOfFingers(void)
{
	int i;


	for( i = 0 ;  i < GestureNumberEvents ;  i++ )
	{
		if( grabbed_events[i].pGestureGrabWinInfo )
		{
			free(grabbed_events[i].pGestureGrabWinInfo);
		}
	}
	max_num_finger = 0;

#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureUnsetMaxNumberOfFingers] max_num_finger=%d\n", max_num_finger);
#endif//__DEBUG_GESTURE_EXT__

	return TRUE;
}

Bool GestureHasGrabbedEvents(Mask *pGrabMask, GestureGrabEventPtr *pGrabEvent)
{
	int i, j;

	if( !pGrabEvent || !pGrabMask )
	{
		ErrorF("[X11][GestureHasGrabbedEvents] pGrabEvent or pGrabMask is NULL !\n");
		return FALSE;
	}

	*pGrabMask = grabbed_events_mask;
	*pGrabEvent = grabbed_events;

	if( !grabbed_events_mask )
	{
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][GestureHasGrabbedEvents] grabbed_events_mask is zero !\n");
#endif//__DEBUG_GESTURE_EXT__
		return FALSE;
	}
#ifdef __DEBUG_GESTURE_EXT__
	else
	{
		ErrorF("[X11][GestureHasGrabbedEvents] grabbed_events_mask=0x%x\n", *pGrabMask);
	}
#endif//__DEBUG_GESTURE_EXT__

	return TRUE;
}

Bool GestureHasSelectedEvents(WindowPtr pWin, Mask *pEventMask)
{
	int rc;
	GestureWindowPrivatePtr pPriv;

	if( !pWin || !pEventMask )
	{
		ErrorF("[X11][GestureHasSelectedEvents] pWin or pEventMask is NULL !\n");
		return FALSE;
	}

	pPriv = dixLookupPrivate(&pWin->devPrivates, gestureWindowPrivateKey);

	 if( !pPriv )
	{
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][GestureHasSelectedEvents] no mask exist on window(0x%x)\n", pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__
		*pEventMask = 0;
		return FALSE;
	}

#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureHasSelectedEvents] mask=0x%x on window(0x%x)\n", pPriv->mask, pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__

	*pEventMask = pPriv->mask;

	return TRUE;
}

/*
  * Hooking functions for tracing destroy of grab window(s)
  */
static void GestureResourceStateCallback(CallbackListPtr *pcbl, pointer closure, pointer calldata)
{
	int i, j, none_win_count;
	GestureWindowPrivatePtr pPriv;
	ResourceStateInfoRec *rec = calldata;
	WindowPtr pWin = (WindowPtr)rec->value;

	if (rec->type != RT_WINDOW)
		return;
	if (rec->state != ResourceStateFreeing)
		return;

	if(selected_win_exist && gpGestureEventsSelected)
	{
		GestureRemoveWindowFromList(pWin->drawable.id, &selectedWinList);
		selected_win_exist = !xorg_list_is_empty(&selectedWinList);

		if(!selected_win_exist)
			gpGestureEventsSelected(None, NULL);
	}

	if( !grabbed_events_mask )
		return;

#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureResourceStateCallback] rec->id=0x%x, pWin->drawable.id=0x%x\n", rec->id, pWin->drawable.id);
	ErrorF("[X11][GestureResourceStateCallback][start] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__

	for( i = 0 ; i < GestureNumberEvents ; i++ )
	{
		none_win_count = 0;
		for( j = 0 ; j < max_num_finger ; j++ )
		{
			if( grabbed_events[i].pGestureGrabWinInfo[j].window == pWin->drawable.id )
			{
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][GestureResourceStateCallback][1.5] i=%d, j=%d\n", i, j);
#endif//__DEBUG_GESTURE_EXT__
				grabbed_events[i].pGestureGrabWinInfo[j].window = None;
				grabbed_events[i].pGestureGrabWinInfo[j].pWin = NULL;
			}

			if( None == grabbed_events[i].pGestureGrabWinInfo[j].window )
			{
				none_win_count++;
			}
		}

		if( none_win_count == max_num_finger )
		{
#ifdef __DEBUG_GESTURE_EXT__
			ErrorF("[X11][GestureResourceStateCallback][before] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__
			grabbed_events_mask = grabbed_events_mask & ~(1 << i);
#ifdef __DEBUG_GESTURE_EXT__
			ErrorF("[X11][GestureResourceStateCallback][after] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__
		}		
	}

#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureResourceStateCallback][end] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__

	return;
}

Bool
GestureInstallResourceStateHooks(void)
{
	Bool res = TRUE;

	res = AddCallback(&ResourceStateCallback, GestureResourceStateCallback, NULL);

	if(!res)
	{
		ErrorF("[X11][GestureInstallResourceStateHooks] Failed to register one or more callbacks ! (res=%d)\n", res);
		return FALSE;
	}

#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureInstallResourceStateHooks] AddCallback result = %d\n", res);
#endif//__DEBUG_GESTURE_EXT__
	return TRUE;
}

Bool
GestureUninstallResourceStateHooks(void)
{
	Bool res;

	res = DeleteCallback(&ResourceStateCallback, GestureResourceStateCallback, NULL);

#ifdef __DEBUG_GESTURE_EXT__
	ErrorF("[X11][GestureUninstallResourceStateHooks] DeleteCallback result = %d\n", res);
#endif//__DEBUG_GESTURE_EXT__
	return res;
}

/*
 * deliver the event
 */
void
GestureSendEvent (WindowPtr pWin, int type, unsigned int mask, xGestureCommonEvent *gce)
{
    int rc;
    Mask access_mode = 0;
    GestureWindowPrivatePtr pPriv;
    WindowPtr pTmp = NULL;

    if( NULL == pWin )
    {
	access_mode |= DixReceiveAccess;
	rc = dixLookupWindow(&pWin, gce->any.window, serverClient, access_mode);
	if( rc != Success || !pWin )
       {
    		ErrorF("[X11][GestureSendEvent] Failed to lookup window !\n");
       	return;
       }

       pPriv = dixLookupPrivate(&pWin->devPrivates, gestureWindowPrivateKey);

       if( !pPriv )
       {
#ifdef __DEBUG_GESTURE_EXT__
    		ErrorF("[X11][GestureSendEvent] no mask exist on win(0x%x)\n", pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__
		return;
       }

#ifdef __DEBUG_GESTURE_EXT__
       ErrorF("[X11][GestureSendEvent] type=%d, mask=%d\n", type, mask);
#endif//__DEBUG_GESTURE_EXT__

       if( !(pPriv->mask & mask) )
       {
#ifdef __DEBUG_GESTURE_EXT__
       	ErrorF("[X11][GestureSendEvent] selected window doesn't have valid event mask ! (pPriv->mask=0x%x, mask=0x%x)\n", pPriv->mask, mask);
#endif//__DEBUG_GESTURE_EXT__       
       	return;
       }
    }

    gce->any.type = type + GestureEventBase;
    gce->any.time = currentTime.milliseconds;
    WriteEventsToClient(wClient(pWin), 1, (xEvent *) gce);
#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][GestureSendEvent] Send gesture event(type:%x) to win(0x%x)\n", type, pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__
}

static void
SGestureNotifyGroupEvent(xGestureNotifyGroupEvent *from, xGestureNotifyGroupEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->groupid = from->groupid;
	to->num_group = from->num_group;
	/* pad1 */
	/* pad2 */
	/* pad3 */
	/* pad4 */
	/* pad5 */
}

static void
SGestureNotifyFlickEvent(xGestureNotifyFlickEvent *from, xGestureNotifyFlickEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->num_finger = from->num_finger;
	to->direction = from->direction;
	cpswaps(from->distance, to->distance);
	cpswapl(from->duration, to->duration);
	cpswapl(from->angle, to->angle);
	/* pad1 */
	/* pad2 */
}

static void
SGestureNotifyPanEvent(xGestureNotifyPanEvent *from, xGestureNotifyPanEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->num_finger = from->num_finger;
	to->direction = from->direction;
	cpswaps(from->distance, to->distance);
	cpswapl(from->duration, to->duration);
	cpswaps(from->dx, to->dx);
	cpswaps(from->dy, to->dy);
	/* pad1 */
	/* pad2 */
}

static void
SGestureNotifyPinchRotationEvent(xGestureNotifyPinchRotationEvent *from, xGestureNotifyPinchRotationEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->num_finger = from->num_finger;
	/* pad1 */
	cpswaps(from->distance, to->distance);
	cpswaps(from->cx, to->cx);
	cpswaps(from->cy, to->cy);
	cpswapl(from->zoom, to->zoom);
	cpswapl(from->angle, to->angle);
	/* pad2 */
}

static void
SGestureNotifyTapEvent(xGestureNotifyTapEvent *from, xGestureNotifyTapEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->num_finger = from->num_finger;
	/* pad1 */
	cpswaps(from->cx, to->cx);
	cpswaps(from->cy, to->cy);
	to->tap_repeat = from->tap_repeat;
	/* pad2 */
	cpswapl(from->interval, to->interval);
	/* pad3 */
	/* pad4 */
}

static void
SGestureNotifyTapNHoldEvent(xGestureNotifyTapNHoldEvent *from, xGestureNotifyTapNHoldEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->num_finger = from->num_finger;
	/* pad1 */
	cpswaps(from->cx, to->cx);
	cpswaps(from->cy, to->cy);
	/* pad2 */
	cpswapl(from->interval, to->interval);
	cpswapl(from->holdtime, to->holdtime);
	/* pad3 */
}

static void
SGestureNotifyHoldEvent(xGestureNotifyHoldEvent *from, xGestureNotifyHoldEvent *to)
{
	to->type = from->type;
	to->kind = from->kind;
	cpswaps(from->sequenceNumber, to->sequenceNumber);
	cpswapl(from->window, to->window);
	cpswapl(from->time, to->time);
	to->num_finger = from->num_finger;
	/* pad1 */
	cpswaps(from->cx, to->cx);
	cpswaps(from->cy, to->cy);
	/* pad2 */
	cpswapl(from->holdtime, to->holdtime);
	/* pad3 */
	/* pad4 */
}

static void
SGestureNotifyEvent(xEvent * from, xEvent * to)
{
    switch (from->u.u.detail) {
	case GestureNotifyGroup:
		SGestureNotifyGroupEvent((xGestureNotifyGroupEvent *)from,
			(xGestureNotifyGroupEvent *)to);
		break;
	case GestureNotifyFlick:
		SGestureNotifyFlickEvent((xGestureNotifyFlickEvent *)from,
			(xGestureNotifyFlickEvent *)to);
		break;
	case GestureNotifyPan:
		SGestureNotifyPanEvent((xGestureNotifyPanEvent *)from,
			(xGestureNotifyPanEvent *)to);
		break;
	case GestureNotifyPinchRotation:
		SGestureNotifyPinchRotationEvent((xGestureNotifyPinchRotationEvent *)from,
			(xGestureNotifyPinchRotationEvent *)to);
		break;
	case GestureNotifyTap:
		SGestureNotifyTapEvent((xGestureNotifyTapEvent *)from,
			(xGestureNotifyTapEvent *)to);
		break;
	case GestureNotifyTapNHold:
		SGestureNotifyTapNHoldEvent((xGestureNotifyTapNHoldEvent *)from,
			(xGestureNotifyTapNHoldEvent *)to);
		break;
	case GestureNotifyHold:
		SGestureNotifyHoldEvent((xGestureNotifyHoldEvent *)from,
			(xGestureNotifyHoldEvent *)to);
		break;
    }
#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][SGestureNotifyEvent] ...\n");
#endif//__DEBUG_GESTURE_EXT__
}

static int
ProcGestureQueryVersion(register ClientPtr client)
{
    xGestureQueryVersionReply rep;
    register int n;

    REQUEST_SIZE_MATCH(xGestureQueryVersionReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.majorVersion = SERVER_GESTURE_MAJOR_VERSION;
    rep.minorVersion = SERVER_GESTURE_MINOR_VERSION;
    rep.patchVersion = SERVER_GESTURE_PATCH_VERSION;
    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
	 swaps(&rep.majorVersion);
	 swaps(&rep.minorVersion);
	 swapl(&rep.patchVersion);
    }
    WriteToClient(client, sizeof(xGestureQueryVersionReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureQueryVersion] return Success !\n");
#endif//__DEBUG_GESTURE_EXT__

    return Success;
}

static int
SProcGestureQueryVersion(register ClientPtr  client)
{
    register int n;
    REQUEST(xGestureQueryVersionReq);
    swaps(&stuff->length);
    return ProcGestureQueryVersion(client);
}

static int
SProcGestureSelectEvents(register ClientPtr client)
{
    register int n;
    REQUEST(xGestureSelectEventsReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swapl(&stuff->mask);
    return ProcGestureSelectEvents(client);
}

static int
ProcGestureSelectEvents (register ClientPtr client)
{
    int rc;
    WindowPtr pWin;
    Mask access_mode = 0;
    GestureWindowPrivatePtr pPriv;

    REQUEST(xGestureSelectEventsReq);
    REQUEST_SIZE_MATCH (xGestureSelectEventsReq);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureSelectEvents] after REQUEST_SIZE_MATCH !\n");
#endif//__DEBUG_GESTURE_EXT__

    access_mode |= DixReceiveAccess;
    rc = dixLookupWindow(&pWin, stuff->window, client, access_mode);
    if( rc != Success || !pWin )
    {
    	 ErrorF("[X11][ProcGestureSelectEvents] Failed to lookup window !\n");
        return BadWindow;
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureSelectEvents] pWin->drawable.id=0x%x\n", pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__

    pPriv = dixLookupPrivate(&pWin->devPrivates, gestureWindowPrivateKey);

    if( NULL == pPriv )//no mask was set
    {
    	if( stuff->mask != 0 )
	{
		pPriv = malloc(sizeof *pPriv);
		if( !pPriv )
		{
			ErrorF("[X11][ProcGestureSelectEvents] Failed to allocate memory !\n");
			return BadAlloc;
		}

		pPriv->mask = stuff->mask;
		dixSetPrivate(&pWin->devPrivates, gestureWindowPrivateKey, pPriv);
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][ProcGestureSelectEvents] mask(0x%x) was addded on win(0x%x) !\n", stuff->mask, stuff->window);
#endif//__DEBUG_GESTURE_EXT__
	}
    }
    else//mask was set already
    {
    	if( stuff->mask == 0 )
	{
		free(pPriv);
		dixSetPrivate(&pWin->devPrivates, gestureWindowPrivateKey, NULL);
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][ProcGestureSelectEvents] mask was removed on win(0x%x) !\n", stuff->window);
#endif//__DEBUG_GESTURE_EXT__
	}
	else
	{
		pPriv->mask = stuff->mask;
		dixSetPrivate(&pWin->devPrivates, gestureWindowPrivateKey, pPriv);
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][ProcGestureSelectEvents] mask(0x%x) was updated on win(0x%x) !\n", stuff->mask, stuff->window);
#endif//__DEBUG_GESTURE_EXT__
	}
    }

    if(stuff->mask)
    {
        GestureAddWindowToList(pWin->drawable.id, &selectedWinList);
    }
    else
    {
        GestureRemoveWindowFromList(pWin->drawable.id, &selectedWinList);
    }

    if(gpGestureEventsSelected)
    {
        selected_win_exist = !xorg_list_is_empty(&selectedWinList);

        if(selected_win_exist)
		gpGestureEventsSelected(pWin->drawable.id, &stuff->mask);
	else
		gpGestureEventsSelected(None, &stuff->mask);
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureSelectEvents] Success !\n");
#endif//__DEBUG_GESTURE_EXT__

    return Success;
}

static int
SProcGestureGetSelectedEvents(register ClientPtr client)
{
    register int n;
    REQUEST(xGestureGetSelectedEventsReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return ProcGestureGetSelectedEvents(client);
}

static int
ProcGestureGetSelectedEvents (register ClientPtr client)
{
    register int n;
    int rc, ret = Success;
    WindowPtr pWin;
    Mask access_mode = 0;
    Mask mask_out = 0;
    GestureWindowPrivatePtr pPriv;
    xGestureGetSelectedEventsReply rep;

    REQUEST(xGestureGetSelectedEventsReq);
    REQUEST_SIZE_MATCH (xGestureGetSelectedEventsReq);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGetSelectedEvents] after REQUEST_SIZE_MATCH !\n");
#endif//__DEBUG_GESTURE_EXT__

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;

    access_mode |= DixReceiveAccess;
    rc = dixLookupWindow(&pWin, stuff->window, client, access_mode);
    if( rc != Success || !pWin )
    {
    	 ErrorF("[X11][ProcGestureGetSelectedEvents] Failed to lookup window !\n");
    	 ret = BadWindow;
        goto failed;
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGetSelectedEvents] pWin->drawable.id=0x%x\n", pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__

    pPriv = dixLookupPrivate(&pWin->devPrivates, gestureWindowPrivateKey);

    if( NULL == pPriv )//no mask was set
    {
    	mask_out = 0;
    }
    else//mask was set already
    {
    	mask_out = pPriv->mask;
    }

    rep.mask = mask_out;
    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
	 swapl(&rep.mask);
    }
    WriteToClient(client, sizeof(xGestureGetSelectedEventsReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGetSelectedEvents] Success !\n");
#endif//__DEBUG_GESTURE_EXT__

    return ret;

failed:

    rep.mask = 0;
    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
	 swapl(&rep.mask);
    }
    WriteToClient(client, sizeof(xGestureGetSelectedEventsReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGetSelectedEvents] Failed !\n");
#endif//__DEBUG_GESTURE_EXT__

    return ret;	
}

static int
SProcGestureGrabEvent(register ClientPtr client)
{
    register int n;
    REQUEST(xGestureGrabEventReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swapl(&stuff->eventType);
    //swapl(&stuff->num_finger);
    swapl(&stuff->time);
    return ProcGestureGrabEvent(client);
}

static int
ProcGestureGrabEvent (register ClientPtr client)
{
    register int n;
    int i, rc, ret = Success;
    TimeStamp ctime;
    WindowPtr pWin;
    Mask eventmask;
    Mask access_mode = 0;
    GestureWindowPrivatePtr pPriv;
    xGestureGrabEventReply rep;

    REQUEST(xGestureGrabEventReq);
    REQUEST_SIZE_MATCH (xGestureGrabEventReq);

    eventmask = (1L << stuff->eventType);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGrabEvent] after REQUEST_SIZE_MATCH !\n");
    ErrorF("[X11][ProcGestureGrabEvent] stuff->window=0x%x, stuff->eventType=%d, stuff->num_finger=%d\n", stuff->window, stuff->eventType, stuff->num_finger);
#endif//__DEBUG_GESTURE_EXT__

    //prepare to reply
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;

    if( stuff->eventType >= GestureNumberEvents )
    {
    	ErrorF("[X11][ProcGestureGrabEvent] GestureGrabAbnormal !(eventType(%d) >= GestureNumberEvents(%d))\n", stuff->eventType, (int)GestureNumberEvents);
    	rep.status = GestureGrabAbnormal;
	ret = BadValue;
	goto grab_failed;
    }

    if( stuff->num_finger >= max_num_finger )
    {
    	ErrorF("[X11][ProcGestureGrabEvent] GestureGrabAbnormal !(num_finger(%d) >= max num finger(%d))\n", stuff->num_finger, max_num_finger);
    	rep.status = GestureGrabAbnormal;
	ret = BadValue;
	goto grab_failed;
    }

    ctime = ClientTimeToServerTime((CARD32)stuff->time);
#if 1
    if ( CompareTimeStamps(ctime, currentTime) == LATER )
#else
    if ( (CompareTimeStamps(ctime, currentTime) == LATER ) ||
    (CompareTimeStamps(ctime, grabInfo->grabTime) == EARLIER) )
#endif
    {
    	ErrorF("[X11][ProcGestureGrabEvent] GestureGrabInvalidTime !(ctime=%d, currentTime=%d)\n", ctime, currentTime);
    	rep.status = GestureGrabInvalidTime;
	ret = BadAccess;
	goto grab_failed;
    }

    //check the event was grabbed already
    if( grabbed_events_mask & eventmask )
    {
    	if( grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].window != None &&
		grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].window != stuff->window )
	{
	    	ErrorF("[X11][ProcGestureGrabEvent] GestureGrabbedAlready ! (org_mask=0x%x, eventmask=0x%x)\n",
				grabbed_events_mask, eventmask);
		rep.status = GestureGrabbedAlready;
		ret = BadAccess;
		goto grab_failed;
	}
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGrabEvent] Gesture(s) are available ! (org_mask=0x%x, eventmask=0x%x, eventType=%d, num_finger=%d)\n",
    			grabbed_events_mask, eventmask, stuff->eventType, stuff->num_finger);
#endif//__DEBUG_GESTURE_EXT__

    access_mode |= DixReceiveAccess;
    rc = dixLookupWindow(&pWin, stuff->window, client, access_mode);
    if( rc != Success || !pWin )
    {
    	 ErrorF("[X11][ProcGestureGrabEvent] Failed to lookup window !\n");
    	 rep.status = GestureGrabAbnormal;
	 ret = BadWindow;
        goto grab_failed;
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGrabEvent] pWin->drawable.id=0x%x\n", pWin->drawable.id);
    ErrorF("[X11][ProcGestureGrabEvent][before] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__

    //set to grab
    grabbed_events_mask |= eventmask;
    grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].pWin = pWin;
    grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].window = stuff->window;

    if(gpGestureEventsGrabbed)
    {
        gpGestureEventsGrabbed(&grabbed_events_mask, &grabbed_events);
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGrabEvent][after] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__

    rep.status = GestureGrabSuccess;

    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }
    WriteToClient(client, sizeof(xGestureGrabEventReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGrabEvent] Success !\n");
#endif//__DEBUG_GESTURE_EXT__

    return Success;

grab_failed:

    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }

    WriteToClient(client, sizeof(xGestureGrabEventReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureGrabEvent] Failed !\n");
#endif//__DEBUG_GESTURE_EXT__

    return ret;
}

static int
SProcGestureUngrabEvent(register ClientPtr client)
{
    register int n;
    REQUEST(xGestureUngrabEventReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swapl(&stuff->eventType);
    //swapl(&stuff->num_finger);
    swapl(&stuff->time);
    return ProcGestureUngrabEvent(client);
}

static int
ProcGestureUngrabEvent (register ClientPtr client)
{
    register int n;
    int i, none_win_count, rc, ret = Success;
    TimeStamp ctime;
    WindowPtr pWin;
    Mask eventmask;
    Mask access_mode = 0;
    GestureWindowPrivatePtr pPriv;
    xGestureUngrabEventReply rep;

    REQUEST(xGestureUngrabEventReq);
    REQUEST_SIZE_MATCH (xGestureUngrabEventReq);

    eventmask = (1L << stuff->eventType);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureUngrabEvent] after REQUEST_SIZE_MATCH !\n");
#endif//__DEBUG_GESTURE_EXT__

    //prepare to reply
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.status = GestureUngrabNotGrabbed;

    if( stuff->eventType >= GestureNumberEvents )
    {
    	ErrorF("[X11][ProcGestureUngrabEvent] GestureGrabAbnormal !(eventType(%d) >= GestureNumberEvents(%d))\n", stuff->eventType, (int)GestureNumberEvents);
    	rep.status = GestureUngrabAbnormal;
	ret = BadValue;
	goto ungrab_failed;
    }

    if( stuff->num_finger >= max_num_finger )
    {
    	ErrorF("[X11][ProcGestureUngrabEvent] GestureGrabAbnormal !(num_finger(%d) >= max num finger(%d))\n", stuff->num_finger, max_num_finger);
    	rep.status = GestureUngrabAbnormal;
	ret = BadValue;
	goto ungrab_failed;
    }

    ctime = ClientTimeToServerTime((CARD32)stuff->time);
#if 1
    if ( CompareTimeStamps(ctime, currentTime) == LATER )
#else
    if ( (CompareTimeStamps(ctime, currentTime) == LATER ) ||
    (CompareTimeStamps(ctime, grabInfo->grabTime) == EARLIER) )
#endif
    {
    	ErrorF("[X11][ProcGestureUngrabEvent] GestureGrabInvalidTime !(ctime=%d, currentTime=%d)\n", ctime, currentTime);
    	rep.status = GestureGrabInvalidTime;
	ret = BadAccess;
	goto ungrab_failed;
    }

    if( grabbed_events_mask & eventmask )
    {
    	if( grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].window == stuff->window )
    	{
    		grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].window = None;
		grabbed_events[stuff->eventType].pGestureGrabWinInfo[stuff->num_finger].pWin = NULL;
    	}

	none_win_count = 0;
	for( i = 0 ; i < max_num_finger ; i++ )
	{
		if( None == grabbed_events[stuff->eventType].pGestureGrabWinInfo[i].window )
		{
			none_win_count++;
		}
	}

	if( none_win_count == max_num_finger )
	{
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][ProcGestureUngrabEvent][before] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__
		grabbed_events_mask = grabbed_events_mask & ~(1<< stuff->eventType);
#ifdef __DEBUG_GESTURE_EXT__
		ErrorF("[X11][ProcGestureUngrabEvent][after] grabbed_events_mask=0x%x\n", grabbed_events_mask);
#endif//__DEBUG_GESTURE_EXT__
	}	
	rep.status = GestureUngrabSuccess;
    }

    if( rep.status == GestureUngrabNotGrabbed )
    {
    	ErrorF("[X11][ProcGestureUngrabEvent] No event(s) were grabbed by window(0x%x) !\n", stuff->window);
	ret = BadAccess;
	goto ungrab_failed;
    }

    //rep.status == GestureUngrabSuccess )
#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureUngrabEvent] Event(s) were grabbed by window(0x%x) exist !\n", stuff->window);
#endif//__DEBUG_GESTURE_EXT__

    access_mode |= DixReceiveAccess;
    rc = dixLookupWindow(&pWin, stuff->window, client, access_mode);
    if( rc != Success || !pWin )
    {
    	 ErrorF("[X11][ProcGestureUngrabEvent] Failed to lookup window !\n");
    	 rep.status = GestureUngrabAbnormal;
	 ret = BadWindow;
        goto ungrab_failed;
    }

    if(gpGestureEventsGrabbed)
    {
        gpGestureEventsGrabbed(&grabbed_events_mask, &grabbed_events);
    }

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureUngrabEvent] pWin->drawable.id=0x%x\n", pWin->drawable.id);
#endif//__DEBUG_GESTURE_EXT__

    rep.status = GestureUngrabSuccess;

    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }
    WriteToClient(client, sizeof(xGestureUngrabEventReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureUngrabEvent] Success !\n");
#endif//__DEBUG_GESTURE_EXT__

    return Success;

ungrab_failed:

    if (client->swapped)
    {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }
    WriteToClient(client, sizeof(xGestureUngrabEventReply), (char *)&rep);

#ifdef __DEBUG_GESTURE_EXT__
    ErrorF("[X11][ProcGestureUngrabEvent] Failed !\n");
#endif//__DEBUG_GESTURE_EXT__

    return ret;
}

/* dispatch */
static int
ProcGestureDispatch(register ClientPtr  client)
{
    REQUEST(xReq);

    switch (stuff->data)
    {
	    case X_GestureQueryVersion:
	        return ProcGestureQueryVersion(client);
    }

#ifdef _F_GESTURE_DENY_REMOTE_CLIENT_
    if (!LocalClient(client))
        return GestureErrorBase + GestureClientNotLocal;
#endif//_F_GESTURE_DENY_REMOTE_CLIENT_

    switch (stuff->data)
    {
	    case X_GestureSelectEvents:
	        return ProcGestureSelectEvents(client);
	    case X_GestureGetSelectedEvents:
	        return ProcGestureGetSelectedEvents(client);
	    case X_GestureGrabEvent:
	        return ProcGestureGrabEvent(client);
	    case X_GestureUngrabEvent:
	        return ProcGestureUngrabEvent(client);
	    default:
	        return BadRequest;
    }
}


static int
SProcGestureDispatch (register ClientPtr  client)
{
    REQUEST(xReq);

    switch (stuff->data)
    {
    case X_GestureQueryVersion:
        return SProcGestureQueryVersion(client);
    }

#ifdef _F_GESTURE_DENY_REMOTE_CLIENT_
    /* It is bound to be non-local when there is byte swapping */
    if (!LocalClient(client))
        return GestureErrorBase + GestureClientNotLocal;
#endif//_F_GESTURE_DENY_REMOTE_CLIENT_

    /* only local clients are allowed Gesture access */
    switch (stuff->data)
    {
    case X_GestureSelectEvents:
        return SProcGestureSelectEvents(client);
    case X_GestureGetSelectedEvents:
        return SProcGestureGetSelectedEvents(client);
    case X_GestureGrabEvent:
        return SProcGestureGrabEvent(client);
    case X_GestureUngrabEvent:
        return SProcGestureUngrabEvent(client);
    default:
        return BadRequest;
    }
}

