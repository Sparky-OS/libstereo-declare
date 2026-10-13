/* SPDX-License-Identifier: MIT */
/* The X11 side on a plain X server (Xvfb): the support check and the two
 * properties, read back. */
#include <stereo-declare.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <errno.h>
#include <stdio.h>

static int failures;
#define CHECK(condition)                                                                       \
    do {                                                                                        \
        if (!(condition)) {                                                                     \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #condition);               \
            failures++;                                                                         \
        }                                                                                       \
    } while (0)

/* The values of a CARDINAL/32 property, or -1 when it is absent. */
static long property(Display *display, Window window, const char *name, unsigned long values[2])
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    long result = -1;
    if (XGetWindowProperty(display, window, XInternAtom(display, name, False), 0, 2, False, XA_CARDINAL, &type, &format,
                           &count, &remaining, &data) == Success
        && type == XA_CARDINAL && format == 32) {
        for (unsigned long i = 0; i < count && i < 2; i++) {
            values[i] = ((unsigned long *)data)[i];
        }
        result = (long)count;
    }
    XFree(data);
    return result;
}

static void announce(Display *display, unsigned long version)
{
    Atom support = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False);
    XChangeProperty(display, DefaultRootWindow(display), support, XA_CARDINAL, 32, PropModeReplace,
                    (const unsigned char *)&version, 1);
    XSync(display, False);
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "no X display\n");
        return 1;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 64, 32, 0, 0, 0);
    unsigned long values[2] = {0, 0};

    /* No window manager announcement: nothing is written. */
    XDeleteProperty(display, DefaultRootWindow(display), XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False));
    XSync(display, False);
    CHECK(stereo_supported_x11(display) == 0);
    CHECK(stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0) == -ENOTSUP);
    announce(display, 1);
    CHECK(stereo_supported_x11(display) == 1);
    CHECK(stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0) == -ENOTSUP);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT", values) == -1);

    /* Stereo KWin announces 3. */
    announce(display, 3);
    CHECK(stereo_supported_x11(display) == 3);
    CHECK(stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, STEREO_VIDEO_LEGACY) == 0);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT", values) == 1 && values[0] == 3);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == 2 && values[0] == 2 && values[1] == 1);

    /* Only none and full side by side. */
    for (unsigned layout = 1; layout < 9; layout++) {
        if (layout != STEREO_SBS_FULL) {
            CHECK(stereo_declare_x11(display, window, (enum stereo_layout)layout, STEREO_CLASS_GAME, 0) == -EINVAL);
        }
    }
    CHECK(stereo_declare_x11(display, window, STEREO_SBS_FULL, (enum stereo_class)256, 0) == -EINVAL);
    CHECK(stereo_declare_x11(display, 0, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0) == -EINVAL);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == 2 && values[0] == 2);

    /* A new declaration replaces the values; removal deletes both. */
    CHECK(stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, STEREO_SCIENTIFIC_STEREOGL) == 0);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == 2 && values[0] == 4 && values[1] == 2);
    CHECK(stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_GAME, STEREO_GAME_STEREOGL) == 0);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == 2 && values[0] == 3 && values[1] == 4);
    CHECK(stereo_remove_x11(display, window) == 0);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT", values) == -1);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == -1);

    /* The format only: the class property is the program's and stays. */
    CHECK(stereo_declare_x11_format(display, window, STEREO_SBS_FULL) == 0);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT", values) == 1 && values[0] == 3);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == -1);
    const unsigned long own_class[2] = {STEREO_CLASS_GAME, STEREO_GAME_3D};
    XChangeProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False), XA_CARDINAL, 32,
                    PropModeReplace, (const unsigned char *)own_class, 2);
    CHECK(stereo_declare_x11_format(display, window, STEREO_NONE) == 0);
    XSync(display, False);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT", values) == 1 && values[0] == 0);
    CHECK(property(display, window, "_KDE_NET_WM_STEREO_CONTENT_CLASS", values) == 2 && values[0] == 3 && values[1] == 2);
    for (unsigned layout = 1; layout < 9; layout++) {
        if (layout != STEREO_SBS_FULL) {
            CHECK(stereo_declare_x11_format(display, window, (enum stereo_layout)layout) == -EINVAL);
        }
    }
    CHECK(stereo_declare_x11_format(display, 0, STEREO_SBS_FULL) == -EINVAL);
    /* It needs support version 1 only; none at all refuses it. */
    announce(display, 1);
    CHECK(stereo_declare_x11_format(display, window, STEREO_SBS_FULL) == 0);
    XDeleteProperty(display, DefaultRootWindow(display), XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False));
    XSync(display, False);
    CHECK(stereo_declare_x11_format(display, window, STEREO_SBS_FULL) == -ENOTSUP);
    CHECK(stereo_remove_x11(display, window) == 0);

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
