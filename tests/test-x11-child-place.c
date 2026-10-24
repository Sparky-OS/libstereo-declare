/* SPDX-License-Identifier: MIT */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

struct child_place { int x, y; unsigned width, height, below_parent; };
typedef int (*place_fn)(Display *, Window, const struct child_place *);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    void *lib = dlopen("libstereo-declare.so.1", RTLD_NOW);
    CHECK(lib);
    place_fn place = (place_fn)dlsym(lib, "stereo_declare_x11_child_place");
    CHECK(place);
    Display *d = XOpenDisplay(NULL);
    CHECK(d);
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root, 0, 0, 640, 180, 0, 0, 0);
    Atom support = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False);
    Atom atom = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", False);
    unsigned long version = 7;
    XChangeProperty(d, root, support, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    struct child_place p = {-12, 23, 640, 360, 1};
    CHECK(place(d, w, &p) == -ENOTSUP);
    version = 8;
    XChangeProperty(d, root, support, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    CHECK(place(NULL, w, &p) == -EINVAL);
    CHECK(place(d, 0, &p) == -EINVAL);
    p.x = -32768; CHECK(place(d, w, &p) == -EINVAL); p.x = -12;
    p.y = 32768; CHECK(place(d, w, &p) == -EINVAL); p.y = 23;
    p.width = 0; CHECK(place(d, w, &p) == -EINVAL); p.width = 640;
    p.height = 32768; CHECK(place(d, w, &p) == -EINVAL); p.height = 360;
    p.below_parent = 2; CHECK(place(d, w, &p) == -EINVAL); p.below_parent = 1;
    CHECK(place(d, w, &p) == 0);
    Atom type; int format; unsigned long count, extra; unsigned char *data = NULL;
    CHECK(XGetWindowProperty(d, w, atom, 0, 6, False, XA_CARDINAL,
                            &type, &format, &count, &extra, &data) == Success);
    CHECK(type == XA_CARDINAL && format == 32 && count == 5 && extra == 0);
    unsigned long *values = (unsigned long *)data;
    CHECK((int32_t)values[0] == -12 && values[1] == 23 && values[2] == 640 && values[3] == 360 && values[4] == 1);
    XFree(data);
    Window ignored; int x, y; unsigned width, height, border, depth;
    CHECK(XGetGeometry(d, w, &ignored, &x, &y, &width, &height, &border, &depth));
    CHECK(width == 640 && height == 180);
    CHECK(place(d, w, NULL) == 0);
    CHECK(XGetWindowProperty(d, w, atom, 0, 6, False, XA_CARDINAL,
                            &type, &format, &count, &extra, &data) == Success);
    CHECK(type == None && count == 0);
    XFree(data);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    dlclose(lib);
    puts("child placement: version gate, invalid ranges, signed origin, exact property, native dimensions and removal passed");
    return 0;
}
