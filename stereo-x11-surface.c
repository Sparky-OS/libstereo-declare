/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include <X11/Xutil.h>
#include <X11/extensions/Xpresent.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/shape.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Xlib's error handler is process-wide. Delegate errors from other displays;
 * serialize our short, synchronous traps without changing toolkit event queues. */
static pthread_mutex_t trap_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic(Display *) trap_display;
static _Atomic(XErrorHandler) previous_handler;
static _Atomic int trapped;
static int error_handler(Display *display, XErrorEvent *event)
{
    if (display == trap_display) { trapped = event->error_code; return 0; }
    XErrorHandler handler = previous_handler;
    return handler ? handler(display, event) : 0;
}
static void begin(Display *display)
{
    pthread_mutex_lock(&trap_mutex);
    trap_display = display;
    trapped = 0;
    previous_handler = XSetErrorHandler(error_handler);
}
static int end(Display *display)
{
    XSync(display, False);
    XSetErrorHandler(previous_handler);
    trap_display = NULL;
    int failed = trapped;
    pthread_mutex_unlock(&trap_mutex);
    return failed ? -EIO : 0;
}

struct surface_buffer { Pixmap pixmap; int width, height, busy; };
struct stereo_x11_surface {
    Display *display;
    Window parent, child;
    Colormap colormap;
    Visual *visual;
    int depth, present_opcode;
    struct surface_buffer buffers[2];
    struct stereo_x11_presentation *receipt;
    uint64_t sequence, tag;
    int pending, occupied, watching;
    uint32_t *pixels;
    int width, height;
    struct stereo_x11_child_place place;
};

static void drain(struct stereo_x11_surface *s)
{
    while (XPending(s->display)) {
        XEvent event;
        XNextEvent(s->display, &event);
        if (event.type == DestroyNotify && event.xdestroywindow.window == s->child)
            s->child = None;
        if (s->watching && ((event.type == UnmapNotify && event.xunmap.window == s->child) ||
            (event.type == ReparentNotify && event.xreparent.window == s->child)))
            s->watching = -1;
        if (event.type == GenericEvent && event.xcookie.extension == s->present_opcode &&
            XGetEventData(s->display, &event.xcookie)) {
            if (event.xcookie.evtype == PresentIdleNotify) {
                XPresentIdleNotifyEvent *idle = event.xcookie.data;
                for (unsigned i = 0; i < 2; i++)
                    if (idle->pixmap == s->buffers[i].pixmap)
                        s->buffers[i].busy = 0;
            }
            XFreeEventData(s->display, &event.xcookie);
        }
    }
}

static void cancel(struct stereo_x11_surface *s)
{
    if (s->occupied && s->child) {
        struct stereo_x11_presentation_receipt receipt;
        stereo_x11_presentation_cancel(s->receipt);
        if (stereo_x11_presentation_read(s->receipt, NULL, &receipt) == 1)
            stereo_x11_presentation_ack(s->receipt);
    }
    s->occupied = 0;
}

static int submit(struct stereo_x11_surface *s)
{
    if (!s->pending) return 0;
    XMoveResizeWindow(s->display, s->child, s->place.x, s->place.y, s->width, s->height);
    if (stereo_declare_x11_child(s->display, s->parent, s->child, STEREO_SBS_FULL,
                               STEREO_CLASS_NONE, 0, s->place.width, s->place.height) < 0 ||
        stereo_declare_x11_child_place(s->display, s->child, &s->place) < 0)
        return -ENOTSUP;
    XMapWindow(s->display, s->child);
    /* KWin redirects a newly declared child asynchronously. Composite's
     * named pixmap exists only after redirection of a realized window. Do not
     * send Present into the parent's backing store while that is pending.
     * This is only submission eligibility, never presentation completion. */
    XSync(s->display, False);
    if (trapped) return -EIO;
    Pixmap redirected = XCompositeNameWindowPixmap(s->display, s->child);
    XSync(s->display, False);
    if (trapped == BadMatch) { trapped = 0; return 0; }
    if (trapped) return -EIO;
    XFreePixmap(s->display, redirected);
    struct surface_buffer *b = NULL;
    for (unsigned i = 0; i < 2; i++)
        if (!s->buffers[i].busy) { b = &s->buffers[i]; break; }
    if (!b) return 0; /* one retained native frame, replaced by the next show */
    if (b->pixmap && (b->width != s->width || b->height != s->height)) {
        XFreePixmap(s->display, b->pixmap);
        b->pixmap = None;
    }
    if (!b->pixmap) {
        b->pixmap = XCreatePixmap(s->display, s->child, s->width, s->height, s->depth);
        b->width = s->width; b->height = s->height;
    }
    XImage *image = XCreateImage(s->display, s->visual, s->depth, ZPixmap, 0,
                                (char *)s->pixels, s->width, s->height, 32, s->width * 4);
    if (!image) return -ENOMEM;
    if (image->bits_per_pixel != 32) {
        image->data = NULL;
        XDestroyImage(image);
        return -ENOTSUP;
    }
    const unsigned one = 1;
    image->byte_order = *(const unsigned char *)&one ? LSBFirst : MSBFirst;
    GC gc = XCreateGC(s->display, b->pixmap, 0, NULL);
    if (!gc) {
        image->data = NULL;
        XDestroyImage(image);
        return -ENOMEM;
    }
    XPutImage(s->display, b->pixmap, gc, image, 0, 0, 0, 0, s->width, s->height);
    XFreeGC(s->display, gc);
    image->data = NULL;
    XDestroyImage(image);
    if (++s->sequence == 0 || s->sequence > UINT32_MAX) return -EOVERFLOW;
    int rc = stereo_x11_presentation_arm(s->receipt, s->sequence, (uint32_t)s->sequence);
    if (rc < 0) return rc;
    s->occupied = 1;
    XPresentPixmap(s->display, s->child, b->pixmap, (uint32_t)s->sequence,
                   None, None, 0, 0, None, None, None, PresentOptionCopy, 0, 0, 0, NULL, 0);
    b->busy = 1;
    s->pending = 0;
    free(s->pixels); s->pixels = NULL;
    return 0;
}

int stereo_x11_surface_create(const char *display_name, Window parent,
                              struct stereo_x11_surface **out)
{
    if (!out || !parent) return -EINVAL;
    *out = NULL;
    struct stereo_x11_surface *s = calloc(1, sizeof(*s));
    if (!s) return -ENOMEM;
    s->display = XOpenDisplay(display_name);
    if (!s->display) { free(s); return -ENODEV; }
    begin(s->display);
    XWindowAttributes attr;
    int event, error;
    uint32_t clock;
    int rc = -ENOTSUP;
    if (stereo_supported_x11(s->display) < 8 ||
        stereo_x11_presentation_supported(s->display, parent, &clock) != 1 ||
        !XPresentQueryExtension(s->display, &s->present_opcode, &event, &error) ||
        !XShapeQueryExtension(s->display, &event, &error) ||
        !XCompositeQueryExtension(s->display, &event, &error) ||
        !XGetWindowAttributes(s->display, parent, &attr)) goto done;
    /* Use an opaque RGB visual even when the toolkit parent is ARGB. */
    XVisualInfo visual;
    if (!XMatchVisualInfo(s->display, XScreenNumberOfScreen(attr.screen), 24, TrueColor, &visual) ||
        visual.red_mask != 0xff0000 || visual.green_mask != 0xff00 || visual.blue_mask != 0xff)
        goto done;
    s->parent = parent; s->visual = visual.visual; s->depth = visual.depth;
    s->colormap = XCreateColormap(s->display, attr.root, s->visual, AllocNone);
    XSetWindowAttributes attrs = {.event_mask = StructureNotifyMask | PropertyChangeMask,
                                  .colormap = s->colormap, .border_pixel = 0};
    s->child = XCreateWindow(s->display, parent, 0, 0, 2, 1, 0, s->depth,
                             InputOutput, s->visual, CWEventMask | CWColormap | CWBorderPixel, &attrs);
    XShapeCombineRectangles(s->display, s->child, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XPresentSelectInput(s->display, s->child, PresentIdleNotifyMask);
    rc = stereo_x11_presentation_create(s->display, s->child, s->child, &s->receipt);
done:
    if (end(s->display) < 0) rc = -EIO;
    if (rc < 0) { stereo_x11_surface_close(s); return rc; }
    *out = s;
    return 0;
}

int stereo_x11_surface_show(struct stereo_x11_surface *s, const uint32_t *pixels,
                            int width, int height, int stride,
                            const struct stereo_x11_child_place *place, uint64_t tag)
{
    if (!s || !pixels || !place || !tag || width < 2 || width > 32766 || (width & 1) ||
        height <= 0 || height > 32767 || stride < width * 4 ||
        height > INT_MAX / (width * 4) ||
        !place->width || place->width > 32767 || !place->height || place->height > 32767 ||
        place->x < SHRT_MIN || place->x > SHRT_MAX || place->y < SHRT_MIN || place->y > SHRT_MAX ||
        place->below_parent > 1 || (size_t)height > SIZE_MAX / (size_t)stride)
        return -EINVAL;
    size_t bytes = (size_t)width * height * 4;
    uint32_t *copy = malloc(bytes);
    if (!copy) return -ENOMEM;
    for (int y = 0; y < height; y++)
        memcpy(copy + (size_t)y * width, (const char *)pixels + (size_t)y * stride, (size_t)width * 4);
    begin(s->display);
    drain(s);
    uint32_t clock;
    int rc = !s->child || stereo_x11_presentation_supported(s->display, s->child, &clock) != 1 ? -ENOTSUP : 0;
    if (!rc) {
        cancel(s);
        free(s->pixels); s->pixels = copy; copy = NULL;
        s->width = width; s->height = height; s->place = *place; s->tag = tag;
        s->pending = 1; s->watching = 1;
        rc = submit(s);
    }
    if (end(s->display) < 0) rc = -EIO;
    free(copy);
    if (rc < 0) stereo_x11_surface_hide(s);
    return rc;
}

int stereo_x11_surface_poll(struct stereo_x11_surface *s, uint64_t *tag, unsigned *outcome)
{
    if (!s || !tag || !outcome) return -EINVAL;
    begin(s->display);
    drain(s);
    int rc = 0;
    uint32_t clock;
    if (s->watching && (!s->child || s->watching < 0 ||
        stereo_x11_presentation_supported(s->display, s->child, &clock) != 1)) {
        *tag = s->tag; *outcome = 2; s->watching = 0; rc = 1;
    } else if (s->occupied) {
        struct stereo_x11_presentation_receipt receipt;
        rc = stereo_x11_presentation_read(s->receipt, NULL, &receipt);
        if (rc == 1) {
            *tag = s->tag; *outcome = receipt.outcome;
            stereo_x11_presentation_ack(s->receipt);
            s->occupied = 0;
        }
    }
    if (!rc && s->pending) rc = submit(s);
    if (end(s->display) < 0) rc = -EIO;
    if (rc < 0 || (rc == 1 && *outcome != 1)) stereo_x11_surface_hide(s);
    return rc;
}

void stereo_x11_surface_hide(struct stereo_x11_surface *s)
{
    if (!s) return;
    begin(s->display);
    drain(s);
    cancel(s);
    s->watching = s->pending = 0;
    free(s->pixels); s->pixels = NULL;
    if (s->child) XUnmapWindow(s->display, s->child);
    end(s->display);
}

void stereo_x11_surface_close(struct stereo_x11_surface *s)
{
    if (!s) return;
    stereo_x11_surface_hide(s);
    begin(s->display);
    stereo_x11_presentation_close(s->receipt);
    if (s->child) {
        stereo_remove_x11_child(s->display, s->parent, s->child);
        XDestroyWindow(s->display, s->child);
    }
    for (unsigned i = 0; i < 2; i++)
        if (s->buffers[i].pixmap) XFreePixmap(s->display, s->buffers[i].pixmap);
    if (s->colormap) XFreeColormap(s->display, s->colormap);
    end(s->display);
    XCloseDisplay(s->display);
    free(s);
}
