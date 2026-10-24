/* SPDX-License-Identifier: MIT */
/* The child windows of an X11 window on a plain X server (Xvfb): the child's declaration, the
 * parent's list, and the size one view is shown at, read back. */
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

/* The values of a property of the type, up to 8, or -1 when it is absent or of another type. */
static long property(Display *display, Window window, const char *name, Atom want, unsigned long values[8])
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    long result = -1;
    if (XGetWindowProperty(display, window, XInternAtom(display, name, False), 0, 8, False, want, &type, &format,
                           &count, &remaining, &data) == Success
        && type == want && format == 32) {
        for (unsigned long i = 0; i < count && i < 8; i++) {
            values[i] = ((unsigned long *)data)[i];
        }
        result = (long)count;
    }
    XFree(data);
    return result;
}

static void announce(Display *display, unsigned long version)
{
    XChangeProperty(display, DefaultRootWindow(display), XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False),
                    XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)&version, 1);
    XSync(display, False);
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "no X display\n");
        return 1;
    }
    Window root = DefaultRootWindow(display);
    Window parent = XCreateSimpleWindow(display, root, 0, 0, 300, 200, 0, 0, 0);
    Window frame = XCreateSimpleWindow(display, parent, 20, 30, 200, 150, 0, 0, 0);
    Window child = XCreateSimpleWindow(display, frame, 10, 5, 200, 80, 0, 0, 0);
    Window second = XCreateSimpleWindow(display, parent, 5, 6, 100, 80, 0, 0, 0);
    unsigned long values[8];
    const Atom cardinal = XA_CARDINAL;

    /* Not announced, or announced before child windows (version 5): nothing is written. */
    XDeleteProperty(display, root, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False));
    XSync(display, False);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 0, 0) == -ENOTSUP);
    announce(display, 4);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 0, 0) == -ENOTSUP);
    XSync(display, False);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT", cardinal, values) == -1);
    CHECK(property(display, parent, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", XA_WINDOW, values) == -1);

    /* Version 5: the child declares itself and is listed in its parent; its views are at its own size. */
    announce(display, 5);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, STEREO_SCIENTIFIC_STEREOGL, 0, 0) == 0);
    XSync(display, False);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT", cardinal, values) == 1 && values[0] == STEREO_SBS_FULL);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_CLASS", cardinal, values) == 2 && values[0] == 4 && values[1] == 2);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", cardinal, values) == -1);
    CHECK(property(display, parent, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", XA_WINDOW, values) == 1 && values[0] == child);
    /* A second call lists the child once; a second child is added to the list, the first stays. */
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_GAME, STEREO_GAME_3D, 0, 0) == 0);
    CHECK(stereo_declare_x11_child(display, parent, second, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0, 0, 0) == 0);
    XSync(display, False);
    CHECK(property(display, parent, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", XA_WINDOW, values) == 2 && values[0] == child && values[1] == second);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_CLASS", cardinal, values) == 2 && values[0] == 3 && values[1] == 2);

    /* The size one view is shown at needs version 8: older refuses and writes nothing at all. */
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 100, 80) == -ENOTSUP);
    XSync(display, False);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", cardinal, values) == -1);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_CLASS", cardinal, values) == 2 && values[0] == 3);
    announce(display, 8);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 100, 80) == 0);
    XSync(display, False);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", cardinal, values) == 2 && values[0] == 100 && values[1] == 80);
    CHECK(property(display, parent, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", XA_WINDOW, values) == 2);
    /* Zero and zero removes the size again; one side zero or beyond X11's coordinates is refused and changes nothing. */
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 0, 80) == -EINVAL);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 32768, 80) == -EINVAL);
    XSync(display, False);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", cardinal, values) == 2 && values[0] == 100);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 0, 0) == 0);
    XSync(display, False);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", cardinal, values) == -1);
    CHECK(stereo_declare_x11_child(display, parent, child, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, 0, 100, 80) == 0);

    /* Bad arguments. */
    CHECK(stereo_declare_x11_child(NULL, parent, child, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0, 0, 0) == -EINVAL);
    CHECK(stereo_declare_x11_child(display, 0, child, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0, 0, 0) == -EINVAL);
    CHECK(stereo_declare_x11_child(display, parent, 0, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0, 0, 0) == -EINVAL);
    CHECK(stereo_declare_x11_child(display, parent, parent, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0, 0, 0) == -EINVAL);
    CHECK(stereo_declare_x11_child(display, parent, child, (enum stereo_layout)1, STEREO_CLASS_GAME, 0, 0, 0) == -EINVAL);
    CHECK(stereo_remove_x11_child(display, parent, 0) == -EINVAL);

    /* A declared place (stereo_declare_x11_child_place) is the child's too, and removal takes it away. */
    {
        struct stereo_x11_child_place place = {4, 7, 50, 40, 1};
        CHECK(stereo_declare_x11_child_place(display, child, &place) == 0);
        XSync(display, False);
        CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", cardinal, values) == 5 && values[4] == 1);
    }
    /* Removal: the child leaves the list and loses its declaration; the other child stays. */
    CHECK(stereo_remove_x11_child(display, parent, child) == 0);
    XSync(display, False);
    CHECK(property(display, parent, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", XA_WINDOW, values) == 1 && values[0] == second);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT", cardinal, values) == -1);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_CLASS", cardinal, values) == -1);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", cardinal, values) == -1);
    CHECK(property(display, child, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", cardinal, values) == -1);
    /* The last child out deletes the list; removing a child that is not listed is no error. */
    CHECK(stereo_remove_x11_child(display, parent, second) == 0);
    CHECK(stereo_remove_x11_child(display, parent, second) == 0);
    XSync(display, False);
    CHECK(property(display, parent, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", XA_WINDOW, values) == -1);

    XDestroyWindow(display, parent);
    XCloseDisplay(display);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
