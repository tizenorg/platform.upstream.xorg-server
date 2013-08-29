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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <stdio.h>
#include <stdarg.h>

#include <X11/Xatom.h>
#include "selection.h"
#include "inputstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "propertyst.h"
#include "extnsionst.h"
#include "xacestr.h"
#include "client.h"
#include "../os/osdep.h"
#include "xsmack.h"

/* private state keys */
DevPrivateKeyRec subjectKeyRec;
DevPrivateKeyRec objectKeyRec;
DevPrivateKeyRec dataKeyRec;

#define subjectKey (&subjectKeyRec)
#define objectKey (&objectKeyRec)
#define dataKey (&dataKeyRec)

/* atoms for window label properties */
static Atom atom_smack;
static Atom atom_client_smack;

/* forward declarations */
static void SmackScreen(CallbackListPtr *, pointer, pointer);

/* "true" pointer value for use as callback data */
static pointer truep = (pointer)1;

#define SMACK_SELF	"/proc/self/attr/current"
#define SMACK_STAR	"*"
#define SMACK_FLOOR	"_"
#define SMACK_HAT	"^"
#define SMACK_WEB	"@"
#define SMACK_UNEXPECTED	"UNEXPECTED"
#define SMACK_DEFAULTED		"DEFAULTED"
#define SMACK_IN	"security.SMACK64IPIN"
#define SMACK_OUT	"security.SMACK64IPOUT"

static inline char *
SmackString(SmackLabel *label)
{
    return (char *)&label->label;
}

static inline void
SmackCopyLabel(SmackLabel *to, const SmackLabel *from)
{
    strncpy(SmackString(to), (const char *)&from->label, SMACK_SIZE);
}

static inline void
SmackCopyString(SmackLabel *to, const char *from)
{
    strncpy(SmackString(to), from, SMACK_SIZE);
}

static void
SmackObjectFromSubject(SmackLabel *to, SmackLabel *from)
{
    char *top = SmackString(to);
    const char *frp = SmackString(from);

    if (strcmp(frp, SMACK_WEB) == 0)
        frp = SMACK_STAR;
    strncpy(top, frp, SMACK_SIZE);
}

static void
SmackCreateObject(SmackSubjectRec *subj, SmackObjectRec *obj)
{
    if (subj->privileged)
        SmackCopyString(&obj->smack, SMACK_STAR);
    else
        SmackObjectFromSubject(&obj->smack, &subj->smack);
}

#define ABSIZE (SMACK_SIZE + SMACK_SIZE + 10)
char access_buff[ABSIZE];
int does_not_have_smack;

static int SmackHaveAccess(const char *subject, const char *object,
				const char *access)
{
    int ret;
    int access_fd;

    if (does_not_have_smack)
        return 1;

    ret = snprintf(access_buff, ABSIZE, "%s %s %s", subject, object, access);
    if (ret < 0) {
#ifdef SMACK_DEBUG
        ErrorF("%s:%d(\"%s\", \"%s\", %s) = %d\n",
            __func__, __LINE__, subject, object, access, ret);
#endif /* SMACK_DEBUG */
        return -1;
    }

    access_fd = open("/sys/fs/smackfs/access2", O_RDWR);
    if (access_fd < 0)
        access_fd = open("/smack/access2", O_RDWR);
    if (access_fd < 0) {
#ifdef SMACK_DEBUG
        ErrorF("%s:%d(\"%s\", \"%s\", %s) fd=%d = %d\n",
        __func__, __LINE__, subject, object, access, access_fd, ret);
        return -1;
#else /* SMACK_DEBUG */
        ErrorF("%s: Smack access checking is unavailable.\n", __func__);
        does_not_have_smack = 1;
        return 1;
#endif /* SMACK_DEBUG */
    }
    ret = write(access_fd, access_buff, strlen(access_buff) + 1);
    if (ret < 0) {
#ifdef SMACK_DEBUG
        perror("access write:");
        ErrorF("%s:%d(\"%s\") fd=%d = %d\n",
            __func__, __LINE__, access_buff, access_fd, ret);
#endif /* SMACK_DEBUG */
        close(access_fd);
        return -1;
    }

    ret = read(access_fd, access_buff, ABSIZE);
    if (ret < 0) {
#ifdef SMACK_DEBUG
        ErrorF("%s:%d(\"%s\", \"%s\", %s) '%c' = %d\n",
            __func__, __LINE__, subject, object, access, access_buff[0], ret);
#endif /* SMACK_DEBUG */
        close(access_fd);
        return -1;
    }
    close(access_fd);
    return access_buff[0] == '1';
}

/*
 * Performs a Smack permission check.
 */
static int
SmackDoCheck(const char *caller, int line,
             SmackSubjectRec *subj, SmackObjectRec *obj, Mask mode)
{
    char *subject = SmackString(&subj->smack);
    const char *object = SmackString(&obj->smack);
    char access[6] = "-----";
    int rc;

    access[0] = (mode & SmackReadMask) ? 'r' : '-';
    access[1] = (mode & SmackWriteMask) ? 'w' : '-';

    /* Privileged subjects get access */
    if (subj->privileged) {
#ifdef SMACK_DEBUG
        if (strcmp(subject, object))
            ErrorF("%s:%d     %s(\"%s\", \"%s\", %s, ...) %s \"%s\"\n",
	        caller, line, __func__, subject, object, access,
                "Privileged", subj->command);
#endif /* SMACK_DEBUG */
	return Success;
    }

    /* Objects created by privileged subjects are accessible */
    if (strcmp(object, SMACK_STAR) == 0 || strcmp(object, SMACK_WEB) == 0) {
#ifdef SMACK_DEBUG
        if (strcmp(subject, object))
            ErrorF("%s:%d     %s(\"%s\", \"%s\", %s, ...) %s \"%s\"\n",
	        caller, line, __func__, subject, object, access,
                "Global Object", subj->command);
#endif /* SMACK_DEBUG */
	return Success;
    }

    /* Shortcut equal labels as we know the answer */
    if (strcmp(subject, object) == 0) {
#ifdef SMACK_DEBUG
        ErrorF("%s:%d %s(\"%s\", \"%s\", %s, ...) %s \"%s\"\n",
           caller, line, __func__, subject, object, access,
           "Equal Labels", subj->command);
#endif /* SMACK_DEBUG */
	return Success;
    }

    if (access[0] == access[1]) {
#ifdef SMACK_DEBUG
        ErrorF("%s:%d %s(\"%s\", \"%s\", %s, ...) %s \"%s\"\n",
           caller, line, __func__, subject, object, access,
           "-- access", subj->command);
#endif /* SMACK_DEBUG */
        return Success;
    }

    rc = SmackHaveAccess(subject, object, access);
    if (rc < 0)
        return BadValue;

#ifdef SMACK_DEBUG
    if (strcmp(subject, object))
	    ErrorF("%s:%d %s(\"%s\", \"%s\", %s, ...) %s \"%s\"\n",
		   caller, line, __func__, subject, object, access,
                   (rc == 0) ? "Failure" : "Success", subj->command);
#endif /* SMACK_DEBUG */

    if (rc > 0)
        return Success;

    return BadAccess;
}

static Bool
SmackLabelSet(SmackLabel *label)
{
    if (label->label[0] == '\0')
        return 0;
    return 1;
}

/*
 * Labels a newly connected client.
 */
static void
SmackLabelClient(ClientPtr client)
{
    int fd = XaceGetConnectionNumber(client);
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    const char *cmdname;
    Bool cached;
    pid_t pid;
    char path[SMACK_SIZE];
    struct ucred peercred;
    socklen_t len;
    int rc;

    subj = dixLookupPrivate(&client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&client->devPrivates, objectKey);

    /*
     * What to use where nothing can be discovered
     */
    SmackCopyString(&subj->smack, SMACK_DEFAULTED);
    SmackCopyString(&obj->smack, SMACK_DEFAULTED);

    /*
     * First try is to get the Smack label from
     * the packet label.
     */
    len = SMACK_SIZE;
    rc = getsockopt(fd, SOL_SOCKET, SO_PEERSEC, path, &len);
#ifdef SMACK_DEBUG
    ErrorF("Smack: getsockopt(%d, SOL_SOCKET, SO_PEERSEC, %s, ...) = %d\n",
        fd, path, rc);
#endif /* SMACK_DEBUG */
    if (rc >= 0 && len > 0 && !(len == 1 && path[0] == '\0')) {
        path[len] = '\0';
#ifdef SMACK_DEBUG
        ErrorF("Smack: PEERSEC client label fetched \"%s\" '0x%02x' %d.\n",
               path, path[0], len);
#endif /* SMACK_DEBUG */
        SmackCopyString(&subj->smack, path);
    }

    len = sizeof(peercred);
    rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peercred, &len);
    if (rc >= 0) {
#ifdef SMACK_DEBUG
        ErrorF("Smack: PEERCRED client cred fetched uid = %d.\n", peercred.uid);
#endif /* SMACK_DEBUG */
        if (peercred.uid == 0)
            subj->privileged = 1;
    }

    /* For local clients, try and determine the executable name */
    if (XaceIsLocal(client)) {
	/* Get cached command name if CLIENTIDS is enabled. */
	cmdname = GetClientCmdName(client);
	cached = (cmdname != NULL);
	/* If CLIENTIDS is disabled, figure out the command name from
	 * scratch. */
	if (!cmdname) {
	    pid = DetermineClientPid(client);
	    if (pid != -1)
		DetermineClientCmd(pid, &cmdname, NULL);
	}

	if (!cmdname)
	    goto finish;

#ifdef SMACK_DEBUG
        ErrorF("Smack: command is %s\n", cmdname);
#endif /* SMACK_DEBUG */
	strncpy(subj->command, cmdname, COMMAND_LEN - 1);

	if (!cached)
	    free((void *) cmdname); /* const char * */
    }

finish:

#ifdef SMACK_DEBUG
    ErrorF("Smack: %s results %s\n", __func__, (char *)&subj->smack);
#endif /* SMACK_DEBUG */
    SmackObjectFromSubject(&obj->smack, &subj->smack);
}

static void
SmackFetchProcessLabel(SmackLabel *label)
{
    int fd;
    int i;
    char process[SMACK_SIZE];

    fd = open(SMACK_SELF, O_RDONLY);
    if (fd < 0) {
#ifdef SMACK_DEBUG
        ErrorF("%s failed to open %s, using \"%s\"\n",
            __func__, SMACK_SELF, SMACK_FLOOR);
#endif /* SMACK_DEBUG */
        SmackCopyString(label, SMACK_FLOOR);
	return;
    }

    i = read(fd, process, SMACK_SIZE);
    close(fd);

    if (i < 0) {
#ifdef SMACK_DEBUG
        ErrorF("%s failed to read %s, using \"%s\"\n",
            __func__, SMACK_SELF, SMACK_FLOOR);
#endif /* SMACK_DEBUG */
        SmackCopyString(label, SMACK_FLOOR);
	return;
    }
    if (i < SMACK_SIZE)
        process[i] = '\0';

    SmackCopyString(label, process);
}

/*
 * Labels initial server objects.
 */
static void
SmackLabelInitial(void)
{
    int i;
    int fd;
    struct stat sb;
    XaceScreenAccessRec srec;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    SmackLabel ServerSmack;
    pointer unused;

    /* Do the serverClient */
    subj = dixLookupPrivate(&serverClient->devPrivates, subjectKey);
    obj = dixLookupPrivate(&serverClient->devPrivates, objectKey);
    SmackFetchProcessLabel(&ServerSmack);

    for (fd = 0; fd < 256; fd++) {
        if (fstat(fd, &sb) < 0)
            continue;
        if (!S_ISSOCK(sb.st_mode))
            continue;
#ifdef SMACK_DEBUG
        ErrorF("%s:%d descriptor %d relabeled\n", __func__, __LINE__, fd);
#endif /* SMACK_DEBUG */
        i = fsetxattr(fd, SMACK_IN, SMACK_STAR, strlen(SMACK_STAR), 0);
#ifdef SMACK_DEBUG
        if (i < 0)
            ErrorF("%s:%d input descriptor %d relabel failed\n",
                   __func__, __LINE__, fd);
#endif /* SMACK_DEBUG */
        i = fsetxattr(fd, SMACK_OUT, SMACK_WEB, strlen(SMACK_WEB), 0);
#ifdef SMACK_DEBUG
        if (i < 0)
            ErrorF("%s:%d output descriptor %d relabel failed\n",
                   __func__, __LINE__, fd);
#endif /* SMACK_DEBUG */
    }

    subj->privileged = 1;
    strcpy(subj->command, "X11-server");
    SmackCopyLabel(&subj->smack, &ServerSmack);

    SmackCopyLabel(&obj->smack, &ServerSmack);

    srec.client = serverClient;
    srec.access_mode = DixCreateAccess;
    srec.status = Success;

    for (i = 0; i < screenInfo.numScreens; i++) {
	/* Do the screen object */
	srec.screen = screenInfo.screens[i];
	SmackScreen(NULL, NULL, &srec);

	/* Do the default colormap */
	dixLookupResourceByType(&unused, screenInfo.screens[i]->defColormap,
			  RT_COLORMAP, serverClient, DixCreateAccess);
    }
}

/*
 * Labels new resource objects.
 */
static void
SmackLabelResource(XaceResourceAccessRec *rec, SmackSubjectRec *subj,
		     SmackObjectRec *obj)
{
    SmackObjectRec *pobj;
    SmackLabel *label = &subj->smack;
    PrivateRec **privatePtr;
    int offset;
    int rc;

    /*
     * If this resource is being created by a privileged subject
     * make it world accessable.
     */
    if (subj->privileged) {
        SmackCopyString(&obj->smack, SMACK_STAR);
        return;
    }

    if (rec->parent)
	offset = dixLookupPrivateOffset(rec->ptype);

    /*
     * Casey says the whole parent thing is questionable ...
     * Use the label of the parent object in the labeling operation
     * if the subject can write there.
     */
    if (rec->parent && offset >= 0) {
	privatePtr = DEVPRIV_AT(rec->parent, offset);
	pobj = dixLookupPrivate(privatePtr, objectKey);
        rc = SmackDoCheck(__func__, __LINE__, subj, pobj, rec->access_mode);
        if (rc == Success)
            label = &pobj->smack;
    }

#ifdef SMACK_DEBUG
    ErrorF("%s:%d Use %s \"%s\" Subject is \"%s\"\n", __func__, __LINE__,
        (rec->parent && offset >= 0) ? "Parent" : "Subject",
        SmackString(label), SmackString(&subj->smack));
#endif /* SMACK_DEBUG */

    SmackObjectFromSubject(&obj->smack, label);
}

/*
 * XACE Callbacks
 */

static void
SmackDevice(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceDeviceAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackSubjectRec *dsubj;
    SmackObjectRec *obj;
    int rc;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&rec->dev->devPrivates, objectKey);

    /*
     * If this is a new object that needs labeling, do it now
     * If the subject is privileged make the device able to write
     * everywhere and be written by anyone.
     * Otherwise label the device directly with the process label
     */
    if (rec->access_mode & DixCreateAccess) {
	dsubj = dixLookupPrivate(&rec->dev->devPrivates, subjectKey);
        dsubj->privileged = subj->privileged;

        if (subj->privileged)
            SmackCopyString(&dsubj->smack, SMACK_WEB);
        else
            SmackCopyLabel(&dsubj->smack, &subj->smack);

        SmackCreateObject(subj, obj);
    }
#ifdef SMACK_DEBUG
    ErrorF("%s:%d %s %s %s \"%s\" to \"%s\"\n", __func__, __LINE__,
        subj->command, (subj->privileged) ? "P" : "U",
        (rec->access_mode & DixCreateAccess) ? "New" : "Old",
        SmackString(&subj->smack), SmackString(&obj->smack));
#endif /* SMACK_DEBUG */

    rc = SmackDoCheck(__func__, __LINE__, subj, obj, rec->access_mode);
    if (rc != Success)
	rec->status = rc;
}

static void
SmackSend(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceSendAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    int rc;

    if (rec->dev)
	subj = dixLookupPrivate(&rec->dev->devPrivates, subjectKey);
    else
	subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);

    obj = dixLookupPrivate(&rec->pWin->devPrivates, objectKey);

    /* Check send permission on window */
    rc = SmackDoCheck(__func__, __LINE__, subj, obj, DixSendAccess);
    if (rc != Success)
        rec->status = rc;

    return;
}

static void
SmackReceive(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceReceiveAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    int rc;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&rec->pWin->devPrivates, objectKey);

    /* Check receive permission on window */
    rc = SmackDoCheck(__func__, __LINE__, subj, obj, DixReceiveAccess);
    if (rc != Success)
        rec->status = rc;

    return;
}

static void
SmackExtension(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceExtAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackSubjectRec *serv;
    SmackObjectRec *obj;
    int rc;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&rec->ext->devPrivates, objectKey);

    /* If this is a new object that needs labeling, do it now */
    if (!SmackLabelSet(&obj->smack)) {
        serv = dixLookupPrivate(&serverClient->devPrivates, subjectKey);
        SmackCreateObject(serv, obj);
    }

    /* Perform the security check */
    rc = SmackDoCheck(__func__, __LINE__, subj, obj, rec->access_mode);
    if (rc != Success)
	rec->status = rc;
}

static void
SmackSelection(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceSelectionAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj, *data;
    Selection *pSel = *rec->ppSel;
    Mask access_mode = rec->access_mode;
    int rc;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&pSel->devPrivates, objectKey);

    /* If this is a new object that needs labeling, do it now */
    if (access_mode & DixCreateAccess) {
        SmackCreateObject(subj, obj);
	access_mode = DixSetAttrAccess;
    }

    /*
     * If the access fails don't pass the data along,
     * But don't crash the client, either.
     */
    rc = SmackDoCheck(__func__, __LINE__, subj, obj, access_mode);
    if (rc != Success)
	rec->status = /*CBSrcCBS*/BadMatch;

    /* Label the content (advisory only) */
    if (access_mode & DixSetAttrAccess) {
	data = dixLookupPrivate(&pSel->devPrivates, dataKey);
        SmackCopyLabel(&data->smack, &obj->smack);
    }
}

static void
SmackProperty(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XacePropertyAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj, *data;
    PropertyPtr pProp = *rec->ppProp;
    int rc;

    if (rec->access_mode & DixPostAccess)
	return;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&pProp->devPrivates, objectKey);

    /*
     * Label the property if it is new. If the creator is
     * privileged give everyone access.
     */
    if (rec->access_mode & DixCreateAccess)
        SmackCreateObject(subj, obj);

    /* Perform the security check */
    rc = SmackDoCheck(__func__, __LINE__, subj, obj, rec->access_mode);
    if (rc != Success)
	rec->status = BadMatch;

#ifdef SMACK_DEBUG
    ErrorF("%s:%d %s %s \"%s\" %d \"%s\" rc = %d\n", __func__, __LINE__,
        subj->command, (subj->privileged) ? "P" : "U",
        SmackString(&subj->smack), pProp->propertyName,
        SmackString(&obj->smack), rc);
#endif /* SMACK_DEBUG */

    /* Label the content (advisory only) */
    if (rec->access_mode & DixWriteAccess) {
	data = dixLookupPrivate(&pProp->devPrivates, dataKey);
        SmackCopyLabel(&data->smack, &obj->smack);
    }
}

static void
SmackResource(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceResourceAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackSubjectRec *ocli;
    SmackObjectRec *obj;
    Mask access_mode = rec->access_mode;
    PrivateRec **privatePtr;
    PrivateRec **clientPtr;
    int rc;
    int offset;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);

    /* Determine if the resource object has a devPrivates field */
    /* No: use the label of the owning client */
    /* Yes: use the label from the resource object itself */
    offset = dixLookupPrivateOffset(rec->rtype);
    clientPtr = &clients[CLIENT_ID(rec->id)]->devPrivates;

    if (offset < 0)
	privatePtr = clientPtr;
    else
	privatePtr = DEVPRIV_AT(rec->res, offset);

    obj = dixLookupPrivate(privatePtr, objectKey);
    ocli = dixLookupPrivate(clientPtr, subjectKey);
#ifdef SMACK_DEBUG
    ErrorF("%s:%d \"%s\" %s %s to \"%s\" %s %s %s(\"%s\")\n",
       __func__, __LINE__,
       SmackString(&subj->smack), subj->command,
       (subj->privileged) ? "P" : "U",
       SmackString(&ocli->smack), ocli->command,
       (ocli->privileged) ? "P" : "U",
       (offset < 0) ? "Other" : "Object",
       (access_mode & DixCreateAccess && offset >= 0) ?
           "-NEW-" : SmackString(&obj->smack)
       );
#endif /* SMACK_DEBUG */

    /* If this is a new object that needs labeling, do it now */
    if (access_mode & DixCreateAccess && offset >= 0)
	SmackLabelResource(rec, subj, obj);
#ifdef SMACK_DEBUG
    else
        ErrorF("%s:%d object \"%s\" from %s\n",
           __func__, __LINE__, SmackString(&obj->smack),
           (offset < 0) ? "owning client" : "resource");
#endif /* SMACK_DEBUG */

    /*
     * Perform the security check
     * If either end is privileged let the access through
     */

    rc = SmackDoCheck(__func__, __LINE__, subj, obj, access_mode);
    if (!subj->privileged && !ocli->privileged && rc != Success)
	rec->status = rc;

    /* Perform the background none check on windows */
    if (access_mode & DixCreateAccess && rec->rtype == RT_WINDOW) {
	rc = SmackDoCheck(__func__, __LINE__, subj, obj, DixBlendAccess);
	if (rc != Success)
	    ((WindowPtr)rec->res)->forcedBG = TRUE;
    }
}

static void
SmackScreen(CallbackListPtr *pcbl, pointer is_saver, pointer calldata)
{
    XaceScreenAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    Mask access_mode = rec->access_mode;
    int rc;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&rec->screen->devPrivates, objectKey);

    /* If this is a new object that needs labeling, do it now */
    if (access_mode & DixCreateAccess)
        SmackCreateObject(subj, obj);

    /*
     * This appears to be Black Magic.
     */
    if (is_saver)
	access_mode <<= 2;

    rc = SmackDoCheck(__func__, __LINE__, subj, obj, access_mode);
    if (rc != Success)
	rec->status = rc;
}

static void
SmackClient(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceClientAccessRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    int rc;

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&rec->target->devPrivates, objectKey);

    rc = SmackDoCheck(__func__, __LINE__, subj, obj, rec->access_mode);
    if (rc != Success)
	rec->status = rc;
}

static void
SmackServer(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XaceServerAccessRec *rec = calldata;
    SmackSubjectRec *servsubj;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    int rc;

    servsubj = dixLookupPrivate(&serverClient->devPrivates, subjectKey);
    if (servsubj->privileged) {
#ifdef SMACK_DEBUG
        ErrorF("%s:%d privileged server\n", __func__, __LINE__);
#endif /* SMACK_DEBUG */
        return;
    }

    subj = dixLookupPrivate(&rec->client->devPrivates, subjectKey);
    obj = dixLookupPrivate(&serverClient->devPrivates, objectKey);

    rc = SmackDoCheck(__func__, __LINE__, subj, obj, rec->access_mode);
    if (rc != Success)
	rec->status = rc;
}


/*
 * DIX Callbacks
 */

static void
SmackClientState(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    NewClientInfoRec *pci = calldata;

    switch (pci->client->clientState) {
    case ClientStateInitial:
	SmackLabelClient(pci->client);
	break;
    default:
	break;
    }
}

static void
SmackResourceState(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    ResourceStateInfoRec *rec = calldata;
    SmackSubjectRec *subj;
    SmackObjectRec *obj;
    WindowPtr pWin;
    int rc;

    if (rec->type != RT_WINDOW)
	return;
    if (rec->state != ResourceStateAdding)
	return;

    pWin = (WindowPtr)rec->value;
    subj = dixLookupPrivate(&wClient(pWin)->devPrivates, subjectKey);

    if (!SmackLabelSet(&subj->smack)) {
#ifdef SMACK_DEBUG
        ErrorF("Smack: Unexpected unlabeled client found\n");
#endif /* SMACK_DEBUG */
	SmackCopyString(&subj->smack, SMACK_UNEXPECTED);
    }

    rc = dixChangeWindowProperty(serverClient, pWin, atom_client_smack,
                                 XA_STRING, 8, PropModeReplace,
                                 SMACK_SIZE, SmackString(&subj->smack), FALSE);
    if (rc != Success) {
#ifdef SMACK_DEBUG
        ErrorF("Smack: Failed to set label property on window!\n");
#endif /* SMACK_DEBUG */
	return;
    }

    obj = dixLookupPrivate(&pWin->devPrivates, objectKey);

    if (!SmackLabelSet(&obj->smack)) {
#ifdef SMACK_DEBUG
        ErrorF("Smack: Unexpected unlabeled window found\n");
#endif /* SMACK_DEBUG */
	SmackCopyString(&obj->smack, SMACK_UNEXPECTED);
    }

    rc = dixChangeWindowProperty(serverClient, pWin, atom_smack,
                                 XA_STRING, 8, PropModeReplace,
                                 SMACK_SIZE, SmackString(&obj->smack), FALSE);
#ifdef SMACK_DEBUG
    if (rc != Success)
        ErrorF("Smack: Failed to set label property on window!\n");
#endif /* SMACK_DEBUG */
}


static void
SmackBlockHandler(void *data, struct timeval **tv, void *read_mask)
{
}

static void
SmackWakeupHandler(void *data, int err, void *read_mask)
{
}

static void
SmackRulesInit(void)
{
    int ret = TRUE;

    /* Allocate private storage */
    if (!dixRegisterPrivateKey(subjectKey, PRIVATE_XSMACK,
        sizeof(SmackSubjectRec)))
	FatalError("Smack: Failed to allocate private subject storage.\n");

    if (!dixRegisterPrivateKey(objectKey, PRIVATE_XSMACK,
        sizeof(SmackObjectRec)))
	FatalError("Smack: Failed to allocate private object storage.\n");

    if (!dixRegisterPrivateKey(dataKey, PRIVATE_XSMACK,
        sizeof(SmackObjectRec)))
	FatalError("Smack: Failed to allocate private data storage.\n");

    /* Create atoms for doing window labeling */
    atom_smack = MakeAtom("_SMACK_LABEL", 12, TRUE);
    if (atom_smack == BAD_RESOURCE)
	FatalError("Smack: Failed to create atom\n");

    atom_client_smack = MakeAtom("_SMACK_CLIENT_LABEL", 19, TRUE);
    if (atom_client_smack == BAD_RESOURCE)
	FatalError("Smack: Failed to create atom\n");

    RegisterBlockAndWakeupHandlers(SmackBlockHandler, SmackWakeupHandler, NULL);

    /* Register callbacks */
    ret &= AddCallback(&ClientStateCallback, SmackClientState, NULL);
    ret &= AddCallback(&ResourceStateCallback, SmackResourceState, NULL);

    ret &= XaceRegisterCallback(XACE_EXT_DISPATCH, SmackExtension, NULL);
    ret &= XaceRegisterCallback(XACE_RESOURCE_ACCESS, SmackResource, NULL);
    ret &= XaceRegisterCallback(XACE_DEVICE_ACCESS, SmackDevice, NULL);
    ret &= XaceRegisterCallback(XACE_PROPERTY_ACCESS, SmackProperty, NULL);
    ret &= XaceRegisterCallback(XACE_SEND_ACCESS, SmackSend, NULL);
    ret &= XaceRegisterCallback(XACE_RECEIVE_ACCESS, SmackReceive, NULL);
    ret &= XaceRegisterCallback(XACE_CLIENT_ACCESS, SmackClient, NULL);
    ret &= XaceRegisterCallback(XACE_EXT_ACCESS, SmackExtension, NULL);
    ret &= XaceRegisterCallback(XACE_SERVER_ACCESS, SmackServer, NULL);
    ret &= XaceRegisterCallback(XACE_SELECTION_ACCESS, SmackSelection, NULL);
    ret &= XaceRegisterCallback(XACE_SCREEN_ACCESS, SmackScreen, NULL);
    ret &= XaceRegisterCallback(XACE_SCREENSAVER_ACCESS, SmackScreen, truep);
    if (!ret)
	FatalError("Smack: Failed to register one or more callbacks\n");

    /* Label objects that were created before we could register ourself */
    SmackLabelInitial();
}

static int
ProcSmackDispatch(ClientPtr client)
{
#ifdef SMACK_DEBUG
    REQUEST(xReq);

    ErrorF("%s(%d)\n", __func__, stuff->data);
#endif /* SMACK_DEBUG */
    return 0;
}

static int
SProcSmackDispatch(ClientPtr client)
{
#ifdef SMACK_DEBUG
    REQUEST(xReq);

    ErrorF("%s(%d)\n", __func__, stuff->data);
#endif /* SMACK_DEBUG */
    return 0;
}

static void
SmackResetProc(ExtensionEntry *extEntry)
{
#ifdef SMACK_DEBUG
    ErrorF("%s(...)\n", __func__);
#endif /* SMACK_DEBUG */
}

void
SmackExtensionInit(void)
{
    ExtensionEntry *extEntry;

    SmackRulesInit();

    extEntry = AddExtension(SMACK_EXTENSION_NAME,
                            SmackNumberEvents, SmackNumberErrors,
                            ProcSmackDispatch, SProcSmackDispatch,
                            SmackResetProc, StandardMinorOpcode);

    AddExtensionAlias("Smack", extEntry);
}
