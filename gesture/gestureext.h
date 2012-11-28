/**************************************************************************

gesture/gestureext.h

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

#ifndef _GESTUREEXT_H_
#define _GESTUREEXT_H_

#include "window.h"
#include <X11/extensions/gestureproto.h>

typedef struct _GestureWindowPrivate {
	unsigned int mask;
} GestureWindowPrivateRec, *GestureWindowPrivatePtr;

typedef struct _GrabWinInfo {
    WindowPtr pWin;
    Window window;
} GestureGrabWinInfoRec, *GestureGrabWinInfoPtr;

typedef struct _GrabEvent {
    GestureGrabWinInfoPtr pGestureGrabWinInfo;
} GestureGrabEventRec, *GestureGrabEventPtr;

/*
 * Gesture extension implementation function list
 */
typedef struct _GestureProcs {
//fuctions for each request type
} GestureProcsRec, *GestureProcsPtr;

extern void GestureExtInit(
	void
);

extern _X_EXPORT Bool GestureExtensionInit(
    void/* GestureProcsPtr procsPtr */
);

extern _X_EXPORT void GestureSendEvent (
	WindowPtr pWin,
	int type,
	unsigned int mask,
	xGestureCommonEvent *gce
);

extern _X_EXPORT Bool GestureHasGrabbedEvents(
	Mask *pGrabMask,
	GestureGrabEventPtr *pGrabEvent
);

extern _X_EXPORT Bool GestureHasSelectedEvents(
	WindowPtr pWin,
	Mask *pEventMask
);

extern _X_EXPORT Bool GestureSetMaxNumberOfFingers(
	int num_finger
);

extern _X_EXPORT Bool GestureUnsetMaxNumberOfFingers(
	void
);

extern _X_EXPORT Bool GestureInstallResourceStateHooks(
	void
);

extern _X_EXPORT Bool GestureUninstallResourceStateHooks(
	void
);

#endif//_GESTUREEXT_H_

