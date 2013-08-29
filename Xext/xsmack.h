/*
 * Copyright (c) 2011 Intel Corporation
 * Copyright (c) 2011 Casey Schaufler
 * 
 * Author: Casey Schaufler <casey@schaufler-ca.com>
 * 
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * this permission notice appear in supporting documentation.  This permission
 * notice shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef _XSMACK_H
#define _XSMACK_H

/* Extension info */
#define SMACK_EXTENSION_NAME		"Smack"
#define SMACK_MAJOR_VERSION		1
#define SMACK_MINOR_VERSION		0
#define SmackNumberEvents		0
#define SmackNumberErrors		0

/*
 * Steal the SELinux private property.
 */
#define PRIVATE_XSMACK PRIVATE_XSELINUX

#define COMMAND_LEN 64
#define SMACK_SIZE 256

typedef struct {
    CARD8   label[SMACK_SIZE];
} SmackLabel;

/* subject state (clients and devices only) */
typedef struct {
    SmackLabel smack;
    char command[COMMAND_LEN];
    int privileged;
} SmackSubjectRec;

/* object state */
typedef struct {
    SmackLabel smack;
} SmackObjectRec;

#define SmackReadMask  (DixReadAccess|DixGetAttrAccess|DixListPropAccess| \
                        DixGetPropAccess|DixGetFocusAccess|DixListAccess| \
                        DixShowAccess|DixBlendAccess|DixReceiveAccess| \
                        DixUseAccess|DixDebugAccess)
#define SmackWriteMask (DixWriteAccess|DixDestroyAccess|DixCreateAccess| \
                        DixSetAttrAccess|DixSetPropAccess|DixSetFocusAccess| \
                        DixAddAccess|DixRemoveAccess|DixHideAccess| \
                        DixGrabAccess|DixFreezeAccess|DixForceAccess| \
                        DixInstallAccess|DixUninstallAccess|DixSendAccess| \
                        DixManageAccess|DixBellAccess)

#endif /* _XSMACK_H */
