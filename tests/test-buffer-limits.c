/* SPDX-License-Identifier: MIT */
/* The person's depth limits in the pixels of a program's buffer (1.11.0), on two outputs of different widths:
 * the conversion is the screen's width over 1920 times the buffer over the width it is shown at, and never the
 * size of the window. The X11 half runs on Xvfb with two RandR monitors (1920 and 3840 wide) and a window on each. */
#include <stereo-declare.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <errno.h>
#include <math.h>
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
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

static void monitor(Display *d, Window root, const char *name, int x, int w, int h)
{
    XRRMonitorInfo info = {0};
    info.name = XInternAtom(d, name, False);
    info.x = x; info.y = 0; info.width = w; info.height = h; info.mwidth = w / 4; info.mheight = h / 4;
    info.primary = x == 0; info.automatic = False;
    XRRSetMonitor(d, root, &info);
}

int main(void)
{
    const struct stereo_depth_limits screen = {10, 4}, none = {0, 0};
    struct stereo_depth_limits out = {-1, -1};

    /* the pure conversion: two outputs, the same limits, a window of any size shown 1:1 or rendered at half */
    CHECK(stereo_depth_limits_for_buffer(&screen, 1920, 1920, 1920, &out) == 0 && NEAR(out.sunk, 10) && NEAR(out.popped, 4));
    CHECK(stereo_depth_limits_for_buffer(&screen, 1920, 960, 960, &out) == 0 && NEAR(out.sunk, 10) && NEAR(out.popped, 4));
    CHECK(stereo_depth_limits_for_buffer(&screen, 1920, 200, 200, &out) == 0 && NEAR(out.sunk, 10) && NEAR(out.popped, 4));
    CHECK(stereo_depth_limits_for_buffer(&screen, 3840, 3840, 3840, &out) == 0 && NEAR(out.sunk, 20) && NEAR(out.popped, 8));
    CHECK(stereo_depth_limits_for_buffer(&screen, 3840, 960, 960, &out) == 0 && NEAR(out.sunk, 20) && NEAR(out.popped, 8));
    CHECK(stereo_depth_limits_for_buffer(&screen, 3840, 960, 1920, &out) == 0 && NEAR(out.sunk, 10) && NEAR(out.popped, 4)); /* rendered at half */
    CHECK(stereo_depth_limits_for_buffer(&none, 3840, 960, 1920, &out) == 0 && NEAR(out.sunk, 0) && NEAR(out.popped, 0));
    out.sunk = 7;
    CHECK(stereo_depth_limits_for_buffer(&screen, 0, 1, 1, &out) == -EINVAL && out.sunk == 7);
    CHECK(stereo_depth_limits_for_buffer(&screen, 1920, 1, 0, &out) == -EINVAL && out.sunk == 7);
    CHECK(stereo_depth_limits_for_buffer(&screen, NAN, 1, 1, &out) == -EINVAL && out.sunk == 7);
    CHECK(stereo_depth_limits_for_buffer(NULL, 1920, 1, 1, &out) == -EINVAL);

    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no X display\n"); return 77; }
    Window root = DefaultRootWindow(d);
    monitor(d, root, "LEFT", 0, 1920, 1080);
    monitor(d, root, "RIGHT", 1920, 3840, 2160);
    XSync(d, False);
    Window a = XCreateSimpleWindow(d, root, 100, 100, 800, 600, 0, 0, 0);       /* on the 1920 output, a window */
    Window b = XCreateSimpleWindow(d, root, 1920 + 200, 100, 1000, 700, 0, 0, 0); /* on the 3840 output */
    Window c = XCreateSimpleWindow(d, root, 2000, 100, 300, 200, 0, 0, 0);        /* small, on the 3840 output */
    Window u = XCreateSimpleWindow(d, root, 100, 100, 50, 50, 0, 0, 0);           /* never mapped */
    XMapWindow(d, a); XMapWindow(d, b); XMapWindow(d, c);
    XSync(d, False);
    double w = 0;
    CHECK(stereo_depth_x11_output_width(d, a, &w) == 0 && NEAR(w, 1920));
    CHECK(stereo_depth_x11_output_width(d, b, &w) == 0 && NEAR(w, 3840));
    CHECK(stereo_depth_x11_output_width(d, c, &w) == 0 && NEAR(w, 3840));
    CHECK(stereo_depth_x11_output_width(d, u, &w) == -ENODATA);
    CHECK(stereo_depth_x11_output_width(d, 0, &w) == -EINVAL);

    /* limits as the compositor writes them, then the buffer limits of each window */
    const unsigned long support = 10;
    XChangeProperty(d, root, XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False), XA_CARDINAL, 32,
                    PropModeReplace, (const unsigned char *)&support, 1);
    const unsigned long values[2] = {(uint32_t)(10 * 256), (uint32_t)(4 * 256)};
    Atom limits_atom = XInternAtom(d, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", False);
    XChangeProperty(d, a, limits_atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)values, 2);
    XChangeProperty(d, b, limits_atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)values, 2);
    XChangeProperty(d, c, limits_atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)values, 2);
    XSync(d, False);
    /* each shown 1:1 at its own width: any window size on one output gives one value; the wide output twice that */
    CHECK(stereo_depth_x11_get_buffer_limits(d, a, 800, 800, &out) == 0 && NEAR(out.sunk, 10) && NEAR(out.popped, 4));
    CHECK(stereo_depth_x11_get_buffer_limits(d, b, 1000, 1000, &out) == 0 && NEAR(out.sunk, 20) && NEAR(out.popped, 8));
    CHECK(stereo_depth_x11_get_buffer_limits(d, c, 300, 300, &out) == 0 && NEAR(out.sunk, 20) && NEAR(out.popped, 8));
    CHECK(stereo_depth_x11_get_buffer_limits(d, c, 150, 300, &out) == 0 && NEAR(out.sunk, 10) && NEAR(out.popped, 4)); /* half resolution */
    out.sunk = 5;
    CHECK(stereo_depth_x11_get_buffer_limits(d, u, 50, 50, &out) == -ENODATA && out.sunk == 5);

    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("buffer limits: ok");
    return 0;
}
