/* SPDX-License-Identifier: MIT */
#include <stereo-declare.h>
#include <X11/Xatom.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: FAIL: %s\n", __LINE__, #x); failures++; } } while (0)
static int (*declare_child)(Display *, Window, Window, enum stereo_layout, enum stereo_class, unsigned, unsigned, unsigned);
static int (*remove_child)(Display *, Window, Window);

/* The old remove/redeclare workaround is an executable negative control. */
static int legacy_move(Display *d, Window old, Window next, Window child)
{
    if (old) remove_child(d, old, child);
    return next ? declare_child(d, next, child, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0, 80, 60) : 0;
}

static long values(Display *d, Window w, Atom atom, Atom wanted, unsigned long *out, unsigned long capacity)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    int status = XGetWindowProperty(d, w, atom, 0, (long)capacity, False, wanted,
                                    &type, &format, &count, &remaining, &data);
    long result = -1;
    if (status == Success && type == wanted && format == 32 && !remaining && count <= capacity) {
        for (unsigned long i = 0; i < count; i++) out[i] = ((unsigned long *)data)[i];
        result = (long)count;
    }
    XFree(data);
    return result;
}

static unsigned events(Display *d, Window child, unsigned *owner_events)
{
    unsigned count = 0;
    *owner_events = 0;
    XSync(d, False);
    while (XPending(d)) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type == PropertyNotify) {
            if (e.xproperty.window == child) count++;
            else (*owner_events)++;
        }
    }
    return count;
}

int main(int argc, char **argv)
{
    void *library = dlopen("libstereo-declare.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    declare_child = dlsym(library, "stereo_declare_x11_child");
    remove_child = dlsym(library, "stereo_remove_x11_child");
    int (*move)(Display *, Window, Window, Window) = dlsym(library, "stereo_move_x11_child");
    int (*place)(Display *, Window, const struct stereo_x11_child_place *) = dlsym(library, "stereo_declare_x11_child_place");
    if (argc == 2 && !strcmp(argv[1], "--legacy")) move = legacy_move;
    if (!move || !declare_child || !remove_child || !place) {
        fprintf(stderr, "required membership API missing\n"); return 1;
    }
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
    Window root = DefaultRootWindow(d);
    Window a = XCreateSimpleWindow(d, root, 0, 0, 200, 100, 0, 0, 0);
    Window b = XCreateSimpleWindow(d, root, 0, 0, 200, 100, 0, 0, 0);
    Window child = XCreateSimpleWindow(d, a, 0, 0, 160, 60, 0, 0, 0);
    Window sibling = XCreateSimpleWindow(d, a, 0, 0, 10, 10, 0, 0, 0);
    Atom support = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", False);
    Atom list = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", False);
    Atom content = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT", False);
    Atom placement = XInternAtom(d, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", False);
    unsigned long version = 8, out[1100];
    XChangeProperty(d, root, support, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    CHECK(declare_child(d, a, child, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0, 80, 60) == 0);
    CHECK(declare_child(d, a, sibling, STEREO_NONE, STEREO_CLASS_GAME, 0, 0, 0) == 0);
    struct stereo_x11_child_place location = {3, 4, 80, 60, 1};
    CHECK(place(d, child, &location) == 0);
    XSelectInput(d, child, PropertyChangeMask);
    XSelectInput(d, a, PropertyChangeMask);
    XSelectInput(d, b, PropertyChangeMask);
    unsigned writes;
    events(d, child, &writes);

    CHECK(move(d, a, b, child) == 0);
    unsigned changed = events(d, child, &writes);
    printf("migration child_property_events=%u owner_property_events=%u\n", changed, writes);
    CHECK(changed == 0 && writes == 2);
    CHECK(values(d, a, list, XA_WINDOW, out, 1100) == 1 && out[0] == sibling);
    CHECK(values(d, b, list, XA_WINDOW, out, 1100) == 1 && out[0] == child);
    CHECK(values(d, child, content, XA_CARDINAL, out, 1100) == 1 && out[0] == STEREO_SBS_FULL);
    CHECK(values(d, child, placement, XA_CARDINAL, out, 1100) == 5 && out[0] == 3 && out[4] == 1);
    CHECK(move(d, a, b, child) == 0);
    CHECK(move(d, b, b, child) == 0);
    CHECK(events(d, child, &writes) == 0 && writes == 0);
    CHECK(move(d, b, None, child) == 0);
    CHECK(move(d, b, None, child) == 0);
    CHECK(events(d, child, &writes) == 0 && writes == 1);
    CHECK(values(d, child, content, XA_CARDINAL, out, 1100) == 1);
    CHECK(values(d, b, list, XA_WINDOW, out, 1100) == -1);
    CHECK(move(d, None, b, child) == 0);
    CHECK(events(d, child, &writes) == 0 && writes == 1);

    /* A malformed destination must not remove the child's old membership. */
    XChangeProperty(d, a, list, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    events(d, child, &writes);
    CHECK(move(d, b, a, child) == -EPROTO);
    CHECK(events(d, child, &writes) == 0 && writes == 0);
    CHECK(values(d, b, list, XA_WINDOW, out, 1100) == 1 && out[0] == child);
    XDeleteProperty(d, a, list);

    /* All entries, not only the first 1024, survive a removal. */
    unsigned long many[1052];
    for (unsigned i = 0; i < 1050; i++) many[i] = 0x100000 + i;
    many[1050] = child; many[1051] = child;
    XChangeProperty(d, a, list, XA_WINDOW, 32, PropModeReplace, (unsigned char *)many, 1052);
    events(d, child, &writes);
    CHECK(move(d, a, None, child) == 0);
    CHECK(values(d, a, list, XA_WINDOW, out, 1100) == 1050);
    CHECK(!memcmp(out, many, 1050 * sizeof(*many)));
    CHECK(events(d, child, &writes) == 0 && writes == 1);
    XChangeProperty(d, a, list, XA_WINDOW, 32, PropModeReplace, (unsigned char *)many, 1052);
    events(d, child, &writes);
    CHECK(move(d, a, a, child) == 0);
    CHECK(values(d, a, list, XA_WINDOW, out, 1100) == 1051 && out[1050] == child);
    CHECK(events(d, child, &writes) == 0 && writes == 1);

    version = 4;
    XChangeProperty(d, root, support, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&version, 1);
    events(d, child, &writes);
    CHECK(move(d, a, b, child) == -ENOTSUP);
    CHECK(events(d, child, &writes) == 0 && writes == 0);
    CHECK(move(d, a, None, child) == 0);
    CHECK(events(d, child, &writes) == 0 && writes == 1);
    CHECK(move(NULL, a, b, child) == -EINVAL);
    CHECK(move(d, a, b, None) == -EINVAL);
    CHECK(move(d, child, b, child) == -EINVAL);
    CHECK(move(d, a, child, child) == -EINVAL);

    XDestroyWindow(d, child);
    CHECK(move(d, b, None, child) == 0); /* No child access, even for a destroyed child. */
    XDestroyWindow(d, a); XDestroyWindow(d, b);
    XCloseDisplay(d);
    dlclose(library);
    printf("%s membership checks (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
