/************************************************************
Copyright (c) 1993 by Silicon Graphics Computer Systems, Inc.

Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting
documentation, and that the name of Silicon Graphics not be 
used in advertising or publicity pertaining to distribution 
of the software without specific prior written permission.
Silicon Graphics makes no representation about the suitability 
of this software for any purpose. It is provided "as is"
without any express or implied warranty.

SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS 
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL 
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, 
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.

********************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <xkb-config.h>

#include <stdio.h>
#include <ctype.h>
#include <X11/X.h>
#include <X11/Xos.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/extensions/XKM.h>
#include "inputstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#define	XKBSRV_NEED_FILE_FUNCS
#include <xkbsrv.h>
#include <X11/extensions/XI.h>
#include "xkb.h"

        /*
         * If XKM_OUTPUT_DIR specifies a path without a leading slash, it is
         * relative to the top-level XKB configuration directory.
         * Making the server write to a subdirectory of that directory
         * requires some work in the general case (install procedure
         * has to create links to /var or somesuch on many machines),
         * so we just compile into /usr/tmp for now.
         */
#ifndef XKM_OUTPUT_DIR
#define	XKM_OUTPUT_DIR	"compiled/"
#endif

#define	PRE_ERROR_MSG "\"The XKEYBOARD keymap compiler (xkbcomp) reports:\""
#define	ERROR_PREFIX	"\"> \""
#define	POST_ERROR_MSG1 "\"Errors from xkbcomp are not fatal to the X server\""
#define	POST_ERROR_MSG2 "\"End of messages from xkbcomp\""

#if defined(WIN32)
#define PATHSEPARATOR "\\"
#else
#define PATHSEPARATOR "/"
#endif

static void
OutputDirectory(char *outdir, size_t size, Bool *is_private_directory)
{
#ifndef WIN32
    /* Can we write an xkm and then open it too? */
    if (access(XKM_OUTPUT_DIR, W_OK | X_OK) == 0 &&
        (strlen(XKM_OUTPUT_DIR) < size)) {
        (void) strcpy(outdir, XKM_OUTPUT_DIR);
        if (is_private_directory)
            *is_private_directory = TRUE;
    }
    else
#else
    if (strlen(Win32TempDir()) + 1 < size) {
        (void) strcpy(outdir, Win32TempDir());
        (void) strcat(outdir, "\\");
        if (is_private_directory)
            *is_private_directory = FALSE;
    }
    else
#endif
    if (strlen("/tmp/") < size) {
        (void) strcpy(outdir, "/tmp/");
        if (is_private_directory)
            *is_private_directory = FALSE;
    }
}

static Bool
XkbDDXCompileKeymapByNames(XkbDescPtr xkb,
                           XkbComponentNamesPtr names,
                           unsigned want,
                           unsigned need, char *file_name)
{
    FILE *out;
    char *buf = NULL;

    const char *emptystring = "";
    char *xkbbasedirflag = NULL;
    const char *xkbbindir = emptystring;
    const char *xkbbindirsep = emptystring;

#ifdef WIN32
    /* WIN32 has no popen. The input must be stored in a file which is
       used as input for xkbcomp. xkbcomp does not read from stdin. */
    char tmpname[PATH_MAX];
    const char *xkmfile = tmpname;
#else
    const char *xkmfile = "-";
#endif

#ifdef WIN32
    strcpy(tmpname, Win32TempDir());
    strcat(tmpname, "\\xkb_XXXXXX");
    (void) mktemp(tmpname);
#endif

    if (XkbBaseDirectory != NULL) {
        if (asprintf(&xkbbasedirflag, "\"-R%s\"", XkbBaseDirectory) == -1)
            xkbbasedirflag = NULL;
    }

    if (XkbBinDirectory != NULL) {
        int ld = strlen(XkbBinDirectory);
        int lps = strlen(PATHSEPARATOR);

        xkbbindir = XkbBinDirectory;

        if ((ld >= lps) && (strcmp(xkbbindir + ld - lps, PATHSEPARATOR) != 0)) {
            xkbbindirsep = PATHSEPARATOR;
        }
    }

    if (asprintf(&buf,
                 "\"%s%sxkbcomp\" -w %d %s -xkm \"%s\" "
                 "-em1 %s -emp %s -eml %s \"%s\"",
                 xkbbindir, xkbbindirsep,
                 ((xkbDebugFlags < 2) ? 1 :
                  ((xkbDebugFlags > 10) ? 10 : (int) xkbDebugFlags)),
                 xkbbasedirflag ? xkbbasedirflag : "", xkmfile,
                 PRE_ERROR_MSG, ERROR_PREFIX, POST_ERROR_MSG1,
                 file_name) == -1)
        buf = NULL;

    free(xkbbasedirflag);

    if (!buf) {
        LogMessage(X_ERROR,
                   "XKB: Could not invoke xkbcomp: not enough memory\n");
        return FALSE;
    }

#ifndef WIN32
    out = Popen(buf, "w");
#else
    out = fopen(tmpname, "w");
#endif

    if (out != NULL) {
#ifdef DEBUG
        if (xkbDebugFlags) {
            ErrorF("[xkb] XkbDDXCompileKeymapByNames compiling keymap:\n");
            XkbWriteXKBKeymapForNames(stderr, names, xkb, want, need);
        }
#endif
        XkbWriteXKBKeymapForNames(out, names, xkb, want, need);
#ifndef WIN32
        if (Pclose(out) == 0)
#else
        if (fclose(out) == 0 && System(buf) >= 0)
#endif
        {
            if (xkbDebugFlags)
                DebugF("[xkb] xkb executes: %s\n", buf);
            free(buf);
#ifdef WIN32
            unlink(tmpname);
#endif
            return TRUE;
        }
        else
            LogMessage(X_ERROR, "Error compiling keymap (%s)\n", file_name);
#ifdef WIN32
        /* remove the temporary file */
        unlink(tmpname);
#endif
    }
    else {
#ifndef WIN32
        LogMessage(X_ERROR, "XKB: Could not invoke xkbcomp\n");
#else
        LogMessage(X_ERROR, "Could not open file %s\n", tmpname);
#endif
    }
    free(buf);
    return FALSE;
}

static char *
xkb_config_pathname(char *filename, Bool *is_private_directory)
{
    char        xkm_output_dir[PATH_MAX];
    char        *pathname;

    OutputDirectory(xkm_output_dir, sizeof (xkm_output_dir), is_private_directory);

    if ((XkbBaseDirectory != NULL) && (xkm_output_dir[0] != '/')
#ifdef WIN32
        && (!isalpha(xkm_output_dir[0]) || xkm_output_dir[1] != ':')
#endif
        ) {
        if (asprintf (&pathname, "%s%s%s", XkbBaseDirectory,
                      xkm_output_dir, filename) <= 0)
            pathname = NULL;
    }
    else {
        if (asprintf (&pathname, "%s%s", xkm_output_dir, filename) <= 0)
            pathname = NULL;
    }
    return pathname;
}

static unsigned
xkb_load_keymap_file(char *file_name, unsigned want, unsigned need, XkbDescPtr *xkbRtrn)
{
    FILE *file;
    unsigned missing;

    file = fopen(file_name, "r");
    if (file == NULL) {
        LogMessage(X_ERROR, "Couldn't open compiled keymap file %s\n",
                   file_name);
        return 0;
    }
    missing = XkmReadFile(file, need, want, xkbRtrn);
    if (*xkbRtrn == NULL) {
        LogMessage(X_ERROR, "Error loading keymap %s\n", file_name);
        fclose(file);
        return 0;
    }
    else {
        DebugF("Loaded XKB keymap %s, defined=0x%x\n", file_name,
               (*xkbRtrn)->defined);
    }
    fclose(file);
    return (need | want) & (~missing);
}

unsigned
XkbDDXLoadKeymapByNames(DeviceIntPtr keybd,
                        XkbComponentNamesPtr names,
                        unsigned want,
                        unsigned need,
                        XkbDescPtr *xkbRtrn, char *file_name)
{
    XkbDescPtr xkb;
    unsigned provided;
    char *local_file_name = file_name;

    if (!file_name) {
        char    local_name[PATH_MAX];

        if (snprintf(local_name, sizeof (local_name), "server-%s.xkm", display) >= sizeof (local_name))
            return 0;
        local_file_name = xkb_config_pathname (local_name, NULL);
        if (local_file_name == NULL)
            return 0;
    }

    *xkbRtrn = NULL;
    if ((keybd == NULL) || (keybd->key == NULL) ||
        (keybd->key->xkbInfo == NULL))
        xkb = NULL;
    else
        xkb = keybd->key->xkbInfo->desc;
    if ((names->keycodes == NULL) && (names->types == NULL) &&
        (names->compat == NULL) && (names->symbols == NULL) &&
        (names->geometry == NULL)) {
        LogMessage(X_ERROR, "XKB: No components provided for device %s\n",
                   keybd->name ? keybd->name : "(unnamed keyboard)");
        if (local_file_name != file_name)
            free(local_file_name);
        return 0;
    }
    else if (!XkbDDXCompileKeymapByNames(xkb, names, want, need,
                                         file_name)) {
        LogMessage(X_ERROR, "XKB: Couldn't compile keymap\n");
        if (local_file_name != file_name)
            free(local_file_name);
        return 0;
    }

    provided = xkb_load_keymap_file(file_name, want, need, xkbRtrn);

    if (!xkbRtrn) {
        unlink(local_file_name);
        if (local_file_name != file_name)
            free(local_file_name);
        return 0;
    }
    if (local_file_name != file_name) {
        unlink(local_file_name);
        free(local_file_name);
    }
    return provided;
}

Bool
XkbDDXNamesFromRules(DeviceIntPtr keybd,
                     char *rules_name,
                     XkbRF_VarDefsPtr defs, XkbComponentNamesPtr names)
{
    char buf[PATH_MAX];
    FILE *file;
    Bool complete;
    XkbRF_RulesPtr rules;

    if (!rules_name)
        return FALSE;

    if (snprintf(buf, PATH_MAX, "%s/rules/%s", XkbBaseDirectory, rules_name)
        >= PATH_MAX) {
        LogMessage(X_ERROR, "XKB: Rules name is too long\n");
        return FALSE;
    }

    file = fopen(buf, "r");
    if (!file) {
        LogMessage(X_ERROR, "XKB: Couldn't open rules file %s\n", buf);
        return FALSE;
    }

    rules = XkbRF_Create();
    if (!rules) {
        LogMessage(X_ERROR, "XKB: Couldn't create rules struct\n");
        fclose(file);
        return FALSE;
    }

    if (!XkbRF_LoadRules(file, rules)) {
        LogMessage(X_ERROR, "XKB: Couldn't parse rules file %s\n", rules_name);
        fclose(file);
        XkbRF_Free(rules, TRUE);
        return FALSE;
    }

    memset(names, 0, sizeof(*names));
    complete = XkbRF_GetComponents(rules, defs, names);
    fclose(file);
    XkbRF_Free(rules, TRUE);

    if (!complete)
        LogMessage(X_ERROR, "XKB: Rules returned no components\n");

    return complete;
}

static Bool
XkbRMLVOtoKcCGST(DeviceIntPtr dev, XkbRMLVOSet * rmlvo,
                 XkbComponentNamesPtr kccgst)
{
    XkbRF_VarDefsRec mlvo;

    mlvo.model = rmlvo->model;
    mlvo.layout = rmlvo->layout;
    mlvo.variant = rmlvo->variant;
    mlvo.options = rmlvo->options;

    return XkbDDXNamesFromRules(dev, rmlvo->rules, &mlvo, kccgst);
}

/**
 * Compile the given RMLVO keymap and return it. Returns the XkbDescPtr on
 * success or NULL on failure. If the components compiled are not a superset
 * or equal to need, the compiliation is treated as failure.
 */
static XkbDescPtr
XkbCompileKeymapForDevice(DeviceIntPtr dev, XkbRMLVOSet * rmlvo, int need)
{
    XkbDescPtr xkb = NULL;
    unsigned int provided = 0;
    XkbComponentNamesRec kccgst = { 0 };
    char *filename, *pathname;
    Bool is_private_directory;
    Bool unlink_on_success = TRUE;

    if (asprintf(&filename, "server-%s-%s-%s-%s-%s-%s.xkm",
                 display,
                 rmlvo->rules,
                 rmlvo->model,
                 rmlvo->layout,
                 rmlvo->variant,
                 rmlvo->options) <= 0)
        return NULL;

    pathname = xkb_config_pathname(filename, &is_private_directory);
    if (pathname == NULL)
        return NULL;

#if XKM_CACHE_FILES
    unlink_on_success = !is_private_directory;
    if (is_private_directory)
        provided = xkb_load_keymap_file(pathname, XkmAllIndicesMask, need, &xkb);
#endif

    if ((need & provided) != need) {
        (void) unlink(pathname);
        if (xkb)
            XkbFreeKeyboard(xkb, 0, TRUE);
        if (XkbRMLVOtoKcCGST(dev, rmlvo, &kccgst)) {
            provided = XkbDDXLoadKeymapByNames(dev, &kccgst, XkmAllIndicesMask,
                                               need, &xkb, pathname);
            if ((need & provided) != need) {
                if (xkb) {
                    (void) unlink(pathname);
                    XkbFreeKeyboard(xkb, 0, TRUE);
                    xkb = NULL;
                }
            }
        }
    }

    if (unlink_on_success)
        (void) unlink(pathname);
    free (pathname);

    XkbFreeComponentNames(&kccgst, FALSE);
    return xkb;
}

XkbDescPtr
XkbCompileKeymap(DeviceIntPtr dev, XkbRMLVOSet * rmlvo)
{
    XkbDescPtr xkb;
    unsigned int need;

    if (!dev || !rmlvo) {
        LogMessage(X_ERROR, "XKB: No device or RMLVO specified\n");
        return NULL;
    }

    /* These are the components we really really need */
    need = XkmSymbolsMask | XkmCompatMapMask | XkmTypesMask |
        XkmKeyNamesMask | XkmVirtualModsMask;

    xkb = XkbCompileKeymapForDevice(dev, rmlvo, need);

    if (!xkb) {
        XkbRMLVOSet dflts;

        /* we didn't get what we really needed. And that will likely leave
         * us with a keyboard that doesn't work. Use the defaults instead */
        LogMessage(X_ERROR, "XKB: Failed to load keymap. Loading default "
                   "keymap instead.\n");

        XkbGetRulesDflts(&dflts);

        xkb = XkbCompileKeymapForDevice(dev, &dflts, 0);

        XkbFreeRMLVOSet(&dflts, FALSE);
    }

    return xkb;
}
