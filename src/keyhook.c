#include "keyhook.h"
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct Keyhook {
    Display       *ctrl;   /* control connection: sets up the context */
    Display       *data;   /* data connection: delivers recorded events */
    XRecordContext ctx;
    XRecordRange  *range;
    KeyhookCb      cb;
    void          *user;
};

/* RECORD delivers raw X protocol bytes. KeyPress/KeyRelease events start with
 * a u_char type field; byte 1 is the keycode (detail). */
static void intercept(XPointer closure, XRecordInterceptData *d) {
    Keyhook *kh = (Keyhook *)closure;
    if (d->category == XRecordFromServer && d->data && d->data_len > 0) {
        const unsigned char *p = (const unsigned char *)d->data;
        int type = p[0] & 0x7f;
        if (type == KeyPress || type == KeyRelease) {
            int keycode = p[1];
            kh->cb(keycode, type == KeyPress, kh->user);
        }
    }
    XRecordFreeData(d);
}

Keyhook *keyhook_create(KeyhookCb cb, void *user) {
    Keyhook *kh = calloc(1, sizeof *kh);
    if (!kh) return NULL;
    kh->cb = cb;
    kh->user = user;

    kh->ctrl = XOpenDisplay(NULL);
    kh->data = XOpenDisplay(NULL);
    if (!kh->ctrl || !kh->data) { keyhook_destroy(kh); return NULL; }

    int major, minor;
    if (!XRecordQueryVersion(kh->ctrl, &major, &minor)) {
        fprintf(stderr, "keyhook: RECORD extension unavailable\n");
        keyhook_destroy(kh);
        return NULL;
    }

    kh->range = XRecordAllocRange();
    if (!kh->range) { keyhook_destroy(kh); return NULL; }
    kh->range->device_events.first = KeyPress;
    kh->range->device_events.last  = KeyRelease;

    XRecordClientSpec clients = XRecordAllClients;
    kh->ctx = XRecordCreateContext(kh->ctrl, 0, &clients, 1, &kh->range, 1);
    if (!kh->ctx) {
        fprintf(stderr, "keyhook: cannot create RECORD context\n");
        keyhook_destroy(kh);
        return NULL;
    }
    XSync(kh->ctrl, False);

    /* Async enable: returns immediately, events arrive on the data conn. */
    if (!XRecordEnableContextAsync(kh->data, kh->ctx, intercept,
                                   (XPointer)kh)) {
        fprintf(stderr, "keyhook: cannot enable RECORD context\n");
        keyhook_destroy(kh);
        return NULL;
    }
    return kh;
}

void keyhook_destroy(Keyhook *kh) {
    if (!kh) return;
    if (kh->ctx && kh->ctrl) {
        XRecordDisableContext(kh->ctrl, kh->ctx);
        XRecordFreeContext(kh->ctrl, kh->ctx);
    }
    if (kh->range) XFree(kh->range);
    if (kh->data) XCloseDisplay(kh->data);
    if (kh->ctrl) XCloseDisplay(kh->ctrl);
    free(kh);
}

int keyhook_fd(const Keyhook *kh) {
    return ConnectionNumber(kh->data);
}

void keyhook_process(Keyhook *kh) {
    /* Pump the data connection; XRecordProcessReplies dispatches to intercept */
    XRecordProcessReplies(kh->data);
}
