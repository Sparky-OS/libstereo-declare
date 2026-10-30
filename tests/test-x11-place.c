/* SPDX-License-Identifier: MIT */
#include <stereo-declare.h>
#include <X11/Xatom.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Resolve the additive API at runtime so the same test fails on 1.5.0. */
typedef int (*event_fn)(Display *, Window, const XEvent *, struct stereo_x11_one_view_request *);
typedef int (*commit_fn)(Display *, Window, const struct stereo_x11_one_view_request *, unsigned, unsigned);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    void *lib = dlopen("libstereo-declare.so.1", RTLD_NOW);
    CHECK(lib);
    event_fn read_event = (event_fn)dlsym(lib, "stereo_x11_one_view_event");
    commit_fn commit = (commit_fn)dlsym(lib, "stereo_declare_x11_one_view_commit");
    CHECK(read_event && commit);
    Display *d = XOpenDisplay(NULL);
    CHECK(d);
    Window root = DefaultRootWindow(d);
    Window w = XCreateSimpleWindow(d, root, 0, 0, 1280, 360, 0, 0, 0);
    Atom support = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False);
    Atom request = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_CONFIGURE", False);
    Atom ack = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_COMMIT", False);
    unsigned long version = 6;
    XChangeProperty(d, root, support, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    XEvent e = {0};
    e.type = PropertyNotify; e.xproperty.window = w; e.xproperty.atom = request; e.xproperty.state = PropertyNewValue;
    struct stereo_x11_one_view_request r = {1, 700, 500};
    CHECK(read_event(d, w, &e, &r) == -ENOTSUP);
    CHECK(commit(d, w, &r, 700, 500) == -ENOTSUP);
    version = 7;
    XChangeProperty(d, root, support, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    unsigned long values[] = {17, 700, 500, 42};
    XChangeProperty(d, w, request, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)values, 3);
    CHECK(read_event(d, w, &e, &r) == 1);
    CHECK(r.serial == 17 && r.width == 700 && r.height == 500);
    e.xproperty.window = root;
    CHECK(read_event(d, w, &e, &r) == 0);
    e.xproperty.window = w; e.xproperty.state = PropertyDelete;
    CHECK(read_event(d, w, &e, &r) == 0);
    e.xproperty.state = PropertyNewValue;
    XChangeProperty(d, w, request, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)values, 4);
    CHECK(read_event(d, w, &e, &r) == -EINVAL);
    values[1] = 32768;
    XChangeProperty(d, w, request, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)values, 3);
    CHECK(read_event(d, w, &e, &r) == -EINVAL);
    CHECK(commit(d, w, &r, 0, 500) == -EINVAL);
    CHECK(commit(d, w, &r, 700, 32768) == -EINVAL);
    CHECK(read_event(NULL, w, &e, &r) == -EINVAL);
    CHECK(commit(d, w, NULL, 700, 500) == -EINVAL);
    XSelectInput(d, root, SubstructureNotifyMask);
    CHECK(commit(d, w, &r, 704, 396) == 0);
    XEvent reply;
    XNextEvent(d, &reply);
    CHECK(reply.type == ClientMessage && reply.xclient.window == w && reply.xclient.message_type == ack);
    CHECK(reply.xclient.format == 32 && reply.xclient.data.l[0] == 17);
    for (int i = 1; i < 5; ++i) CHECK((unsigned int)reply.xclient.data.l[i] == 0);
    Atom type; int format; unsigned long count, extra; unsigned char *data = NULL;
    CHECK(XGetWindowProperty(d, w, XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", False), 0, 2, False, XA_CARDINAL,
                            &type, &format, &count, &extra, &data) == Success);
    CHECK(type == XA_CARDINAL && format == 32 && count == 2 && extra == 0);
    CHECK(((unsigned long *)data)[0] == 704 && ((unsigned long *)data)[1] == 396);
    XFree(data); XDestroyWindow(d, w); XCloseDisplay(d); dlclose(lib);
    puts("version gate, parsing, malformed requests, event routing and client answer passed");
    return 0;
}
