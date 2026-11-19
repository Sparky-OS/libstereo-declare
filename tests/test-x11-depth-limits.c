/* SPDX-License-Identifier: MIT */
/* The person's depth limits on X11, on a plain X server (Xvfb): the test writes
 * _KDE_NET_WM_STEREO_DEPTH_LIMITS as Stereo KWin does (CARDINAL/32, two values,
 * 24.8 fixed point) and reads it back through the library. */
#include <stereo-declare.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static int failures;
#define CHECK(condition)                                                                       \
    do {                                                                                        \
        if (!(condition)) {                                                                     \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #condition);               \
            failures++;                                                                         \
        }                                                                                       \
    } while (0)

static void announce(Display *display, unsigned long version)
{
    Atom support = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False);
    XChangeProperty(display, DefaultRootWindow(display), support, XA_CARDINAL, 32, PropModeReplace,
                    (const unsigned char *)&version, 1);
    XSync(display, False);
}

static void write_limits(Display *display, Window window, double sunk, double popped)
{
    const unsigned long values[2] = {(uint32_t)(int32_t)(sunk * 256), (uint32_t)(int32_t)(popped * 256)};
    XChangeProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", False), XA_CARDINAL, 32,
                    PropModeReplace, (const unsigned char *)values, 2);
    XSync(display, False);
}

/* Every pending event through stereo_depth_x11_limits_event(): how many were limits, the last result. */
static int drain(Display *display, Window window, struct stereo_depth_limits *limits, int *last)
{
    int limit_events = 0;
    XEvent event;
    while (XPending(display)) {
        XNextEvent(display, &event);
        const int result = stereo_depth_x11_limits_event(display, window, &event, limits);
        if (result == 1) {
            limit_events++;
        }
        if (result != 0) {
            *last = result;
        }
    }
    return limit_events;
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "no X display\n");
        return 1;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 64, 32, 0, 0, 0);
    Window other = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 64, 32, 0, 0, 0);
    struct stereo_depth_limits limits = {-7, -7};
    XSelectInput(display, window, PropertyChangeMask);
    XSync(display, False);

    /* No announcement, or under 10: not offered, whatever is written, and nothing is touched. */
    XDeleteProperty(display, DefaultRootWindow(display), XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False));
    write_limits(display, window, 10, 4);
    XSync(display, False);
    CHECK(stereo_depth_limits_supported_x11(display) == 0);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == -ENOTSUP);
    CHECK(limits.sunk == -7 && limits.popped == -7);
    announce(display, 9);
    CHECK(stereo_depth_limits_supported_x11(display) == 0);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == -ENOTSUP);
    CHECK(stereo_depth_x11_limits_event(display, window, NULL, &limits) == -ENOTSUP);
    CHECK(limits.sunk == -7 && limits.popped == -7);

    /* Support version 10, the window does not declare: offered, not delivered. */
    announce(display, 10);
    CHECK(stereo_depth_limits_supported_x11(display) == 1);
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", False));
    XSync(display, False);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == -ENODATA);
    CHECK(limits.sunk == -7 && limits.popped == -7);
    CHECK(stereo_depth_x11_limits_event(display, window, NULL, &limits) == -ENODATA);

    /* The two numbers, 24.8 fixed point, sunk then popped. */
    write_limits(display, window, 10.5, 4.25);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == 0);
    CHECK(limits.sunk == 10.5 && limits.popped == 4.25);
    write_limits(display, window, 14.00390625, 0.5);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == 0);
    CHECK(limits.sunk == 14.00390625 && limits.popped == 0.5);
    write_limits(display, window, -1.5, 3);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == 0 && limits.sunk == -1.5 && limits.popped == 3);
    /* another window has its own */
    CHECK(stereo_depth_x11_get_limits(display, other, &limits) == -ENODATA);
    write_limits(display, other, 20, 8);
    CHECK(stereo_depth_x11_get_limits(display, other, &limits) == 0 && limits.sunk == 20 && limits.popped == 8);
    CHECK(stereo_depth_x11_get_limits(display, window, &limits) == 0 && limits.sunk == -1.5);

    /* A property of the wrong shape is a protocol error, never numbers. */
    {
        Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", False);
        const unsigned long three[3] = {2560, 1024, 0};
        struct stereo_depth_limits kept = {-7, -7};
        XChangeProperty(display, window, atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)three, 3);
        XSync(display, False);
        CHECK(stereo_depth_x11_get_limits(display, window, &kept) == -EPROTO && kept.sunk == -7);
        XChangeProperty(display, window, atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)three, 1);
        XSync(display, False);
        CHECK(stereo_depth_x11_get_limits(display, window, &kept) == -EPROTO && kept.sunk == -7);
        XChangeProperty(display, window, atom, XA_INTEGER, 32, PropModeReplace, (const unsigned char *)three, 2);
        XSync(display, False);
        CHECK(stereo_depth_x11_get_limits(display, window, &kept) == -EPROTO && kept.sunk == -7);
        const unsigned short halves[2] = {10, 4};
        XChangeProperty(display, window, atom, XA_CARDINAL, 16, PropModeReplace, (const unsigned char *)halves, 2);
        XSync(display, False);
        CHECK(stereo_depth_x11_get_limits(display, window, &kept) == -EPROTO && kept.sunk == -7);
    }

    /* Arguments. */
    CHECK(stereo_depth_x11_get_limits(NULL, window, &limits) == -EINVAL);
    CHECK(stereo_depth_x11_get_limits(display, 0, &limits) == -EINVAL);
    CHECK(stereo_depth_x11_get_limits(display, window, NULL) == -EINVAL);
    CHECK(stereo_depth_x11_limits_event(NULL, window, NULL, &limits) == -EINVAL);
    CHECK(stereo_depth_limits_supported_x11(NULL) == 0);

    /* Live: each change is an event of the caller's loop; other properties and windows are unrelated. */
    {
        int last = 0;
        struct stereo_depth_limits heard = {-7, -7};
        drain(display, window, &heard, &last);
        write_limits(display, window, 6, 2);
        XChangeProperty(display, window, XInternAtom(display, "_NET_WM_NAME", False), XA_STRING, 8, PropModeReplace,
                        (const unsigned char *)"x", 1);
        write_limits(display, other, 30, 9);
        XSync(display, False);
        CHECK(drain(display, window, &heard, &last) == 1 && last == 1);
        CHECK(heard.sunk == 6 && heard.popped == 2);
        write_limits(display, window, 7.5, 3.5);
        CHECK(drain(display, window, &heard, &last) == 1 && heard.sunk == 7.5 && heard.popped == 3.5);
        /* the window stops declaring: the property goes */
        XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", False));
        XSync(display, False);
        last = 0;
        CHECK(drain(display, window, &heard, &last) == 0 && last == -ENODATA);
        CHECK(heard.sunk == 7.5);
        CHECK(stereo_depth_x11_get_limits(display, window, &limits) == -ENODATA);
    }

    XDestroyWindow(display, window);
    XDestroyWindow(display, other);
    XCloseDisplay(display);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
