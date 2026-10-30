/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include <X11/Xatom.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SUPPORT "_KDE_NET_WM_STEREO_PRESENTATION_SUPPORTED"
#define REQUEST "_KDE_NET_WM_STEREO_PRESENTATION_REQUEST"
#define RECEIPT "_KDE_NET_WM_STEREO_PRESENTATION_RECEIPT"

struct stereo_x11_presentation {
    Display *display;
    Window window;
    Atom request_atom, receipt_atom;
    uint64_t session, request, last_request;
    uint32_t serial;
    int occupied, terminal, written;
};

static int words(Display *d, Window w, Atom atom, unsigned count, uint32_t *out)
{
    Atom type;
    int format;
    unsigned long n, extra;
    unsigned char *data = NULL;
    if (atom == None)
        return 0;
    int rc = XGetWindowProperty(d, w, atom, 0, count, False, AnyPropertyType,
                               &type, &format, &n, &extra, &data);
    if (rc != Success) { XFree(data); return -EIO; }
    if (type == None) { XFree(data); return 0; }
    if (type != XA_CARDINAL || format != 32 || n != count || extra) {
        XFree(data);
        return -EPROTO;
    }
    for (unsigned i = 0; i < count; i++)
        out[i] = (uint32_t)((unsigned long *)data)[i];
    XFree(data);
    return 1;
}

int stereo_x11_presentation_supported(Display *d, Window w, uint32_t *clock_id)
{
    uint32_t v[8];
    XWindowAttributes attr;
    if (!d || !w || !clock_id)
        return -EINVAL;
    if (!XGetWindowAttributes(d, w, &attr))
        return -EIO;
    int rc = words(d, attr.root, XInternAtom(d, SUPPORT, True), 8, v);
    if (rc <= 0)
        return rc;
    if (v[0] != 1 || v[2] != 1 || v[3] != 16 || v[4] != 12 || v[5] || v[6] || v[7])
        return -EPROTO;
    *clock_id = v[1];
    return 1;
}

int stereo_x11_presentation_create(Display *d, Window w, uint64_t session,
                                   struct stereo_x11_presentation **out)
{
    uint32_t clock_id;
    if (!out || !session)
        return -EINVAL;
    *out = NULL;
    int rc = stereo_x11_presentation_supported(d, w, &clock_id);
    if (rc != 1)
        return rc < 0 ? rc : -ENOTSUP;
    Atom type;
    int format;
    unsigned long n, extra;
    unsigned char *data = NULL;
    Atom request_atom = XInternAtom(d, REQUEST, True);
    if (request_atom != None) {
        if (XGetWindowProperty(d, w, request_atom, 0, 0, False, AnyPropertyType,
                              &type, &format, &n, &extra, &data) != Success) {
            XFree(data);
            return -EIO;
        }
        XFree(data);
        if (type != None)
            return -EBUSY;
    }
    struct stereo_x11_presentation *p = calloc(1, sizeof(*p));
    if (!p)
        return -ENOMEM;
    p->display = d;
    p->window = w;
    p->session = session;
    p->request_atom = XInternAtom(d, REQUEST, False);
    p->receipt_atom = XInternAtom(d, RECEIPT, False);
    *out = p;
    return 0;
}

static void send_request(struct stereo_x11_presentation *p, unsigned op)
{
    unsigned long v[12] = {1, op, p->session >> 32, (uint32_t)p->session,
                          p->request >> 32, (uint32_t)p->request, p->serial};
    XChangeProperty(p->display, p->window, p->request_atom, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)v, 12);
    XFlush(p->display);
    p->written = 1;
}

int stereo_x11_presentation_arm(struct stereo_x11_presentation *p, uint64_t request,
                                uint32_t serial)
{
    uint32_t clock_id;
    if (!p || !request)
        return -EINVAL;
    if (p->occupied)
        return p->request == request && p->serial == serial ? 0 : -EAGAIN;
    if (request <= p->last_request)
        return -EINVAL;
    int rc = stereo_x11_presentation_supported(p->display, p->window, &clock_id);
    if (rc != 1)
        return rc < 0 ? rc : -ENOTSUP;
    p->request = p->last_request = request;
    p->serial = serial;
    p->occupied = 1;
    p->terminal = 0;
    send_request(p, 1);
    return 0;
}

static uint64_t pair(const uint32_t *v)
{
    return ((uint64_t)v[0] << 32) | v[1];
}

int stereo_x11_presentation_read(struct stereo_x11_presentation *p, const XEvent *event,
                                 struct stereo_x11_presentation_receipt *out)
{
    uint32_t v[16];
    if (!p || !out)
        return -EINVAL;
    if (!p->occupied)
        return 0;
    if (event && (event->type != PropertyNotify || event->xproperty.display != p->display ||
                  event->xproperty.window != p->window || event->xproperty.atom != p->receipt_atom ||
                  event->xproperty.state != PropertyNewValue))
        return 0;
    int rc = words(p->display, p->window, p->receipt_atom, 16, v);
    if (rc <= 0)
        return rc;
    if (v[0] != 1 || v[15] || v[1] < 1 || v[1] > 3 ||
        (v[1] == 1 && (v[7] || v[10] >= 1000000000)) ||
        (v[1] == 2 && (v[7] < 1 || v[7] > 8)) ||
        (v[1] == 3 && (v[7] < 9 || v[7] > 12)))
        return -EPROTO;
    if (v[1] != 1)
        for (unsigned i = 8; i < 15; i++)
            if (v[i]) return -EPROTO;
    if (pair(v + 2) != p->session || pair(v + 4) != p->request || v[6] != p->serial)
        return 0;
    *out = (struct stereo_x11_presentation_receipt){
        .session = pair(v + 2), .request = pair(v + 4), .serial = v[6],
        .outcome = v[1], .reason = v[7], .seconds = pair(v + 8),
        .nanoseconds = v[10], .refresh = v[11], .sequence = pair(v + 12), .flags = v[14]
    };
    p->terminal = 1;
    return 1;
}

int stereo_x11_presentation_cancel(struct stereo_x11_presentation *p)
{
    if (!p || !p->occupied)
        return -EINVAL;
    if (!p->terminal)
        send_request(p, 2);
    return 0;
}

int stereo_x11_presentation_ack(struct stereo_x11_presentation *p)
{
    if (!p || !p->occupied || !p->terminal)
        return -EINVAL;
    send_request(p, 3);
    p->occupied = p->terminal = 0;
    return 0;
}

void stereo_x11_presentation_close(struct stereo_x11_presentation *p)
{
    if (!p)
        return;
    if (p->written) {
        XDeleteProperty(p->display, p->window, p->request_atom);
        XDeleteProperty(p->display, p->window, p->receipt_atom);
        XFlush(p->display);
    }
    free(p);
}
