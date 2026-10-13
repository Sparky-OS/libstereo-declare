/* SPDX-License-Identifier: MIT */
/* An X11 window that shows a full side-by-side picture: the left eye red,
 * the right eye blue. It is declared once, before it is mapped. Under
 * Xwayland it runs on the same compositor as a Wayland program. */
#include <stereo-declare.h>

#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>

enum { EYE_WIDTH = 640, EYE_HEIGHT = 360 };

static unsigned long colour(Display *display, const char *name)
{
    XColor exact, screen;
    XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)), name, &screen, &exact);
    return screen.pixel;
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "no X display\n");
        return 1;
    }
    const unsigned supported = stereo_supported_x11(display);
    if (supported < 2) {
        fprintf(stderr, "the window manager does not take stereo declarations\n");
        return 1;
    }
    /* From support version 3 the window manager sizes the X11 window to
     * hold both eyes: the program gives one eye's size. */
    const int width = supported >= 3 ? EYE_WIDTH : 2 * EYE_WIDTH;
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, width, EYE_HEIGHT, 0, 0,
                                        BlackPixel(display, DefaultScreen(display)));
    XStoreName(display, window, "declare-x11");
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &delete_window, 1);

    /* Declare once: full side by side, a still picture. */
    int result = stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_PHOTO, STEREO_SUBCLASS_UNSPECIFIED);
    if (result != 0) {
        fprintf(stderr, "declaration failed: %s\n", strerror(-result));
        return 1;
    }
    XMapWindow(display, window);

    GC gc = XCreateGC(display, window, 0, NULL);
    const unsigned long red = colour(display, "red"), blue = colour(display, "blue");
    int window_width = width, window_height = EYE_HEIGHT;
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == ConfigureNotify) {
            window_width = event.xconfigure.width;
            window_height = event.xconfigure.height;
        } else if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == delete_window) {
            break;
        } else if (event.type != Expose) {
            continue;
        }
        /* The left half is the left eye, the right half the right eye. */
        XSetForeground(display, gc, red);
        XFillRectangle(display, window, gc, 0, 0, window_width / 2, window_height);
        XSetForeground(display, gc, blue);
        XFillRectangle(display, window, gc, window_width / 2, 0, window_width - window_width / 2, window_height);
        XFlush(display);
    }

    stereo_remove_x11(display, window);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
