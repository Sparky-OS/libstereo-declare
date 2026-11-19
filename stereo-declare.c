/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include "stereo-client.h"
#include "content_type-client.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <wayland-client-protocol.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

unsigned stereo_declare_version(void)
{
    return STEREO_DECLARE_VERSION;
}

static int valid(enum stereo_layout layout, enum stereo_class content_class, unsigned subclass)
{
    return (layout == STEREO_NONE || layout == STEREO_SBS_FULL)
        && (unsigned)content_class <= 255 && subclass <= 255;
}

unsigned stereo_supported_x11(Display *display)
{
    if (!display) {
        return 0;
    }
    Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_SUPPORTED", True);
    if (atom == None) {
        return 0;
    }
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    unsigned version = 0;
    if (XGetWindowProperty(display, DefaultRootWindow(display), atom, 0, 1, False,
                           XA_CARDINAL, &type, &format, &count, &remaining, &data) == Success
        && type == XA_CARDINAL && format == 32 && count == 1 && remaining == 0) {
        version = (unsigned)*(unsigned long *)data;
    }
    XFree(data);
    return version;
}

int stereo_declare_x11(Display *display, Window window, enum stereo_layout layout,
                       enum stereo_class content_class, unsigned subclass)
{
    if (!display || !window || !valid(layout, content_class, subclass)) {
        return -EINVAL;
    }
    if (stereo_supported_x11(display) < 2) {
        return -ENOTSUP;
    }
    const unsigned long packing = layout;
    const unsigned long kind[2] = {content_class, subclass};
    XChangeProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False),
                    XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)kind, 2);
    XChangeProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False),
                    XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)&packing, 1);
    XFlush(display);
    return 0;
}

int stereo_declare_x11_format(Display *display, Window window, enum stereo_layout layout)
{
    if (!display || !window || (layout != STEREO_NONE && layout != STEREO_SBS_FULL)) {
        return -EINVAL;
    }
    if (stereo_supported_x11(display) < 1) {
        return -ENOTSUP;
    }
    const unsigned long packing = layout;
    XChangeProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False),
                    XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)&packing, 1);
    XFlush(display);
    return 0;
}

int stereo_declare_x11_one_view_size(Display *display, Window window, unsigned width, unsigned height)
{
    if (!display || !window || (!width) != (!height) || width > 32767 || height > 32767) {
        return -EINVAL;
    }
    if (stereo_supported_x11(display) < 6) {
        return -ENOTSUP;
    }
    const Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", False);
    if (!width) {
        XDeleteProperty(display, window, atom);
    } else {
        const unsigned long size[2] = {width, height};
        XChangeProperty(display, window, atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)size, 2);
    }
    XFlush(display);
    return 0;
}

int stereo_declare_x11_child_place(Display *display, Window window,
                                   const struct stereo_x11_child_place *place)
{
    if (!display || !window || (place && (place->x < -32767 || place->x > 32767
            || place->y < -32767 || place->y > 32767 || !place->width || !place->height
            || place->width > 32767 || place->height > 32767 || place->below_parent > 1))) {
        return -EINVAL;
    }
    if (stereo_supported_x11(display) < 8) {
        return -ENOTSUP;
    }
    const Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", False);
    if (place) {
        const unsigned long values[5] = {(uint32_t)place->x, (uint32_t)place->y,
                                        place->width, place->height, place->below_parent};
        XChangeProperty(display, window, atom, XA_CARDINAL, 32, PropModeReplace,
                        (const unsigned char *)values, 5);
    } else {
        XDeleteProperty(display, window, atom);
    }
    XFlush(display);
    return 0;
}

int stereo_x11_one_view_event(Display *display, Window window, const XEvent *event,
                              struct stereo_x11_one_view_request *request)
{
    if (!display || !window || !request) {
        return -EINVAL;
    }
    if (event && (event->type != PropertyNotify || event->xproperty.window != window
                  || event->xproperty.state != PropertyNewValue)) {
        return 0;
    }
    Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_CONFIGURE", True);
    if (event && event->xproperty.atom != atom) {
        return 0;
    }
    if (stereo_supported_x11(display) < 7) {
        return -ENOTSUP;
    }
    if (atom == None) {
        return 0;
    }
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    int result = -EINVAL;
    if (XGetWindowProperty(display, window, atom, 0, 3, False, XA_CARDINAL,
                           &type, &format, &count, &remaining, &data) == Success) {
        if (type == None) {
            result = 0;
        } else if (type == XA_CARDINAL && format == 32 && count == 3 && remaining == 0) {
            const unsigned long *values = (const unsigned long *)data;
            if (values[0] && values[0] <= UINT32_MAX && values[1] && values[1] <= 32767
                && values[2] && values[2] <= 32767) {
                *request = (struct stereo_x11_one_view_request){values[0], values[1], values[2]};
                result = 1;
            }
        }
    }
    XFree(data);
    return result;
}

int stereo_declare_x11_one_view_commit(Display *display, Window window,
                                      const struct stereo_x11_one_view_request *request,
                                      unsigned width, unsigned height)
{
    if (!display || !window || !request || !request->serial || !width || !height || width > 32767 || height > 32767) {
        return -EINVAL;
    }
    if (stereo_supported_x11(display) < 7) {
        return -ENOTSUP;
    }
    int result = stereo_declare_x11_one_view_size(display, window, width, height);
    if (result) {
        return result;
    }
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_COMMIT", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = request->serial;
    if (!XSendEvent(display, DefaultRootWindow(display), False,
                    SubstructureRedirectMask | SubstructureNotifyMask, &event)) {
        return -EIO;
    }
    XFlush(display);
    return 0;
}

/* The windows listed in the parent's _KDE_NET_WM_STEREO_CONTENT_CHILDREN; false when it is
 * absent or of another type. */
static int children_list(Display *display, Window parent, Atom atom, unsigned long **windows, unsigned long *count)
{
    Atom type;
    int format;
    unsigned long remaining;
    unsigned char *data = NULL;
    *windows = NULL;
    *count = 0;
    if (XGetWindowProperty(display, parent, atom, 0, 1024, False, XA_WINDOW, &type, &format, count, &remaining, &data) != Success
        || type != XA_WINDOW || format != 32) {
        XFree(data);
        *count = 0;
        return 0;
    }
    *windows = (unsigned long *)data;
    return 1;
}

/* Read the complete list while the server is grabbed. A truncated list must
 * never replace the unobserved tail of another child's membership. */
static int membership_list(Display *display, Window parent, Atom atom,
                           unsigned long **windows, unsigned long *count)
{
    Atom type;
    int format;
    unsigned long remaining;
    unsigned char *data = NULL;
    *windows = NULL;
    *count = 0;
    if (!parent) {
        return 0;
    }
    int status = XGetWindowProperty(display, parent, atom, 0, 0, False, AnyPropertyType,
                                    &type, &format, count, &remaining, &data);
    XFree(data);
    if (status != Success) {
        return -EIO;
    }
    if (type == None) {
        return 0;
    }
    if (type != XA_WINDOW || format != 32 || remaining % 4) {
        return -EPROTO;
    }
    if (remaining / 4 >= INT_MAX) {
        return -E2BIG;
    }
    status = XGetWindowProperty(display, parent, atom, 0, remaining / 4, False, XA_WINDOW,
                                &type, &format, count, &remaining, &data);
    if (status != Success || type != XA_WINDOW || format != 32 || remaining) {
        XFree(data);
        return -EIO;
    }
    *windows = (unsigned long *)data;
    return 0;
}

static void update_membership(Display *display, Window parent, Atom atom,
                              unsigned long *windows, unsigned long count, Window child, int add)
{
    unsigned long kept = 0;
    int found = 0;
    for (unsigned long i = 0; i < count; i++) {
        if (windows[i] != child || (add && !found)) {
            windows[kept++] = windows[i];
        }
        found |= windows[i] == child;
    }
    if (kept != count) {
        if (kept) {
            XChangeProperty(display, parent, atom, XA_WINDOW, 32, PropModeReplace,
                            (const unsigned char *)windows, (int)kept);
        } else {
            XDeleteProperty(display, parent, atom);
        }
    }
    if (add && !found) {
        const unsigned long value = child;
        XChangeProperty(display, parent, atom, XA_WINDOW, 32, PropModeAppend,
                        (const unsigned char *)&value, 1);
    }
}

int stereo_move_x11_child(Display *display, Window old_parent, Window new_parent, Window child)
{
    if (!display || !child || old_parent == child || new_parent == child) {
        return -EINVAL;
    }
    XLockDisplay(display);
    XGrabServer(display);
    int result = 0;
    unsigned long *old_list = NULL, *new_list = NULL;
    unsigned long old_count = 0, new_count = 0;
    const Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", False);
    if (new_parent && stereo_supported_x11(display) < 5) {
        result = -ENOTSUP;
    } else if ((result = membership_list(display, old_parent, atom, &old_list, &old_count)) == 0
               && (old_parent == new_parent
                   || (result = membership_list(display, new_parent, atom, &new_list, &new_count)) == 0)) {
        if (old_parent) {
            update_membership(display, old_parent, atom, old_list, old_count, child, old_parent == new_parent);
        }
        if (new_parent && old_parent != new_parent) {
            update_membership(display, new_parent, atom, new_list, new_count, child, 1);
        }
    }
    XFree(old_list);
    XFree(new_list);
    XUngrabServer(display);
    XFlush(display);
    XUnlockDisplay(display);
    return result;
}

int stereo_declare_x11_child(Display *display, Window parent, Window child, enum stereo_layout layout,
                             enum stereo_class content_class, unsigned subclass,
                             unsigned view_width, unsigned view_height)
{
    if (!display || !parent || !child || child == parent || !valid(layout, content_class, subclass)
        || (!view_width) != (!view_height) || view_width > 32767 || view_height > 32767) {
        return -EINVAL;
    }
    const unsigned supported = stereo_supported_x11(display);
    if (supported < 5 || (view_width && supported < 8)) {
        return -ENOTSUP;
    }
    const unsigned long packing = layout;
    const unsigned long kind[2] = {content_class, subclass};
    XChangeProperty(display, child, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False),
                    XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)kind, 2);
    XChangeProperty(display, child, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False),
                    XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)&packing, 1);
    const Atom size_atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", False);
    if (view_width) {
        const unsigned long size[2] = {view_width, view_height};
        XChangeProperty(display, child, size_atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)size, 2);
    } else {
        XDeleteProperty(display, child, size_atom);
    }
    /* The child is listed once, after its own declaration is in place; an append keeps the others. */
    const Atom list_atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", False);
    unsigned long *listed;
    unsigned long count;
    int found = 0;
    children_list(display, parent, list_atom, &listed, &count);
    for (unsigned long i = 0; i < count; i++) {
        found |= listed[i] == child;
    }
    XFree(listed);
    if (!found) {
        const unsigned long window = child;
        XChangeProperty(display, parent, list_atom, XA_WINDOW, 32, PropModeAppend, (const unsigned char *)&window, 1);
    }
    XFlush(display);
    return 0;
}

int stereo_remove_x11_child(Display *display, Window parent, Window child)
{
    if (!display || !parent || !child) {
        return -EINVAL;
    }
    XDeleteProperty(display, child, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False));
    XDeleteProperty(display, child, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False));
    XDeleteProperty(display, child, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", False));
    XDeleteProperty(display, child, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", False));
    const Atom list_atom = XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CHILDREN", False);
    unsigned long *listed;
    unsigned long count;
    if (children_list(display, parent, list_atom, &listed, &count)) {
        unsigned long kept = 0;
        for (unsigned long i = 0; i < count; i++) {
            if (listed[i] != child) {
                listed[kept++] = listed[i];
            }
        }
        if (kept == 0) {
            XDeleteProperty(display, parent, list_atom);
        } else if (kept != count) {
            XChangeProperty(display, parent, list_atom, XA_WINDOW, 32, PropModeReplace, (const unsigned char *)listed, (int)kept);
        }
    }
    XFree(listed);
    XFlush(display);
    return 0;
}

int stereo_remove_x11(Display *display, Window window)
{
    if (!display || !window) {
        return -EINVAL;
    }
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False));
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False));
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", False));
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE", False));
    XFlush(display);
    return 0;
}

struct declaration {
    struct wl_surface *surface;
    struct kde_stereo_content_v1 *stereo;
    struct wp_content_type_v1 *content_type;
    int borrowed_content_type;
    struct declaration *next;
};

/* One surface's kde_stereo_depth_v1, made by watching it or by marking it. */
struct depth_follower {
    struct wl_surface *surface;
    struct kde_stereo_depth_v1 *depth;
    stereo_depth_listener listener;
    void *data;
    int limits_known;
    uint32_t output_width;
    struct stereo_depth_limits limits;
    stereo_depth_limits_listener limits_listener;
    void *limits_data;
    struct depth_follower *next;
};

static _Thread_local struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_event_queue *queue;
    struct kde_stereo_content_manager_v1 *manager;
    struct wp_content_type_manager_v1 *content_manager;
    uint32_t manager_name;
    uint32_t content_manager_name;
    unsigned version;
    struct declaration *declarations;
    struct depth_follower *followers;
} state;

/* The kde_stereo_content_manager_v1 version of protocol/kde-stereo-content-v1.xml. */
#define STEREO_PROTOCOL_VERSION 6

#define STEREO_MAX_OUTPUTS 16
static struct {
    struct wl_output *output;
    uint32_t name;
    int width; /* current mode, 0 until it arrives */
} outputs[STEREO_MAX_OUTPUTS];

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub,
                            const char *make, const char *model, int32_t transform)
{
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sub; (void)make; (void)model; (void)transform;
}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    (void)d; (void)height; (void)refresh;
    if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
        return;
    }
    for (int i = 0; i < STEREO_MAX_OUTPUTS; i++) {
        if (outputs[i].output == o) {
            outputs[i].width = width;
        }
    }
}
static void output_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void output_scale(void *d, struct wl_output *o, int32_t f) { (void)d; (void)o; (void)f; }
static const struct wl_output_listener output_listener = {.geometry = output_geometry, .mode = output_mode, .done = output_done, .scale = output_scale};

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version)
{
    (void)data;
    if (!strcmp(interface, wl_output_interface.name) && version >= 2) {
        for (int i = 0; i < STEREO_MAX_OUTPUTS; i++) {
            if (!outputs[i].output) {
                outputs[i].output = wl_registry_bind(registry, name, &wl_output_interface, 2);
                outputs[i].name = name;
                outputs[i].width = 0;
                if (outputs[i].output) {
                    wl_output_add_listener(outputs[i].output, &output_listener, NULL);
                }
                return;
            }
        }
        return;
    }
    if (!strcmp(interface, kde_stereo_content_manager_v1_interface.name) && version >= 1 && !state.manager) {
        /* Version 2 makes a declared surface one view wide when it has no viewport destination;
         * version 3 follows surfaces' desktop depth; version 4 carries native view sources;
         * version 5 sends the depth limits. Never bind above what this copy of the protocol
         * knows: the compositor then sends no event the generated interface lacks. */
        const uint32_t bound = version < STEREO_PROTOCOL_VERSION ? version : STEREO_PROTOCOL_VERSION;
        state.manager = wl_registry_bind(registry, name, &kde_stereo_content_manager_v1_interface, bound);
        state.manager_name = name;
        state.version = state.manager ? bound : 0;
    } else if (!strcmp(interface, wp_content_type_manager_v1_interface.name) && version >= 1 && !state.content_manager) {
        state.content_manager = wl_registry_bind(registry, name, &wp_content_type_manager_v1_interface, 1);
        state.content_manager_name = name;
    }
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    for (int i = 0; i < STEREO_MAX_OUTPUTS; i++) {
        if (outputs[i].output && outputs[i].name == name) {
            wl_output_destroy(outputs[i].output);
            outputs[i].output = NULL;
            outputs[i].width = 0;
        }
    }
    if (name == state.manager_name && state.manager) {
        kde_stereo_content_manager_v1_destroy(state.manager);
        state.manager = NULL;
        state.version = 0;
    }
    if (name == state.content_manager_name && state.content_manager) {
        wp_content_type_manager_v1_destroy(state.content_manager);
        state.content_manager = NULL;
    }
}

int stereo_declare_wayland_init(struct wl_display *display, struct wl_event_queue *queue)
{
    static const struct wl_registry_listener listener = {global, global_remove};
    if (!display) {
        return -EINVAL;
    }
    if (state.display) {
        return -EBUSY;
    }
    state.display = display;
    state.queue = queue;
    struct wl_display *wrapper = wl_proxy_create_wrapper(display);
    if (!wrapper) {
        memset(&state, 0, sizeof(state));
        return -ENOMEM;
    }
    wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
    state.registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    if (!state.registry) {
        memset(&state, 0, sizeof(state));
        return -ENOMEM;
    }
    wl_registry_add_listener(state.registry, &listener, NULL);
    return 0;
}

unsigned stereo_supported_wayland(void)
{
    return state.version ? 1 : 0;
}

int stereo_declare_wayland_use_content_type(struct wl_surface *surface, struct wp_content_type_v1 *content_type)
{
    if (!surface || !content_type || !state.display) {
        return -EINVAL;
    }
    for (struct declaration *entry = state.declarations; entry; entry = entry->next) {
        if (entry->surface == surface) {
            return -EBUSY;
        }
    }
    struct declaration *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return -ENOMEM;
    }
    entry->surface = surface;
    entry->content_type = content_type;
    entry->borrowed_content_type = 1;
    entry->next = state.declarations;
    state.declarations = entry;
    return 0;
}

/* kind < 0: the format only, the surface's content type is left alone. */
static int declare_wayland(struct wl_surface *surface, enum stereo_layout layout, int kind)
{
    if (!state.manager) {
        return -ENOTSUP;
    }
    struct declaration *entry = state.declarations;
    while (entry && entry->surface != surface) {
        entry = entry->next;
    }
    int fresh = 0;
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) {
            return -ENOMEM;
        }
        entry->surface = surface;
        entry->next = state.declarations;
        state.declarations = entry;
        fresh = 1;
    }
    if (!entry->stereo) {
        entry->stereo = kde_stereo_content_manager_v1_create(state.manager, surface);
        if (!entry->stereo) {
            if (fresh) {
                stereo_remove_wayland(surface);
            }
            return -ENOMEM;
        }
    }
    kde_stereo_content_v1_set_content(entry->stereo, layout);
    if (kind >= 0) {
        /* The content type manager may be announced after the stereo one. */
        if (!entry->content_type && state.content_manager) {
            entry->content_type = wp_content_type_manager_v1_get_surface_content_type(state.content_manager, surface);
        }
        if (entry->content_type) {
            wp_content_type_v1_set_content_type(entry->content_type, (unsigned)kind);
        }
    }
    return 0;
}

int stereo_declare_wayland(struct wl_surface *surface, enum stereo_layout layout,
                           enum stereo_class content_class, unsigned subclass)
{
    if (!surface || !valid(layout, content_class, subclass)) {
        return -EINVAL;
    }
    /* wp_content_type_v1 has none, photo, video and game; scientific is none there. */
    return declare_wayland(surface, layout, (unsigned)content_class <= 3 ? (int)content_class : 0);
}

int stereo_declare_wayland_format(struct wl_surface *surface, enum stereo_layout layout)
{
    if (!surface || (layout != STEREO_NONE && layout != STEREO_SBS_FULL)) {
        return -EINVAL;
    }
    return declare_wayland(surface, layout, -1);
}

unsigned stereo_view_source_supported_wayland(void)
{
    return state.manager && state.version >= 4 ? 1 : 0;
}

int stereo_declare_wayland_view_source(struct wl_surface *surface,
                                      double x, double y, double width, double height)
{
    if (!surface) {
        return -EINVAL;
    }
    if (!stereo_view_source_supported_wayland()) {
        return -ENOTSUP;
    }
    const int unset = x == -1 && y == -1 && width == -1 && height == -1;
    const double maximum = INT32_MAX / 256.0;
    if (!unset && (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height)
                   || x < 0 || y < 0 || width <= 0 || height <= 0
                   || x > maximum || y > maximum || width > maximum || height > maximum)) {
        return -EINVAL;
    }
    const wl_fixed_t fw = wl_fixed_from_double(width), fh = wl_fixed_from_double(height);
    if (!unset && (fw <= 0 || fh <= 0)) {
        return -EINVAL;
    }
    struct declaration *entry = state.declarations;
    while (entry && entry->surface != surface) {
        entry = entry->next;
    }
    if (!entry || !entry->stereo) {
        return -ENOENT;
    }
    kde_stereo_content_v1_set_view_source(entry->stereo,
        wl_fixed_from_double(x), wl_fixed_from_double(y), fw, fh);
    return 0;
}

int stereo_remove_wayland(struct wl_surface *surface)
{
    if (!surface) {
        return -EINVAL;
    }
    struct declaration **link = &state.declarations;
    while (*link && (*link)->surface != surface) {
        link = &(*link)->next;
    }
    if (*link) {
        struct declaration *entry = *link;
        *link = entry->next;
        if (entry->stereo) {
            kde_stereo_content_v1_destroy(entry->stereo);
        }
        if (entry->content_type) {
            if (entry->borrowed_content_type) {
                wp_content_type_v1_set_content_type(entry->content_type, WP_CONTENT_TYPE_V1_TYPE_NONE);
            } else {
                wp_content_type_v1_destroy(entry->content_type);
            }
        }
        free(entry);
    }
    return 0;
}

void stereo_declare_wayland_finish(void)
{
    while (state.declarations) {
        stereo_remove_wayland(state.declarations->surface);
    }
    while (state.followers) {
        stereo_depth_wayland_unwatch(state.followers->surface);
    }
    if (state.manager) {
        kde_stereo_content_manager_v1_destroy(state.manager);
    }
    if (state.content_manager) {
        wp_content_type_manager_v1_destroy(state.content_manager);
    }
    /* The outputs bound for their widths are this library's objects too. A caller that set the library up on a
     * queue of its own destroys that queue after this call, and libwayland aborts the program when an event
     * arrives for an object still attached to a destroyed queue (ORDER 767). */
    for (int i = 0; i < STEREO_MAX_OUTPUTS; i++) {
        if (outputs[i].output) {
            wl_output_destroy(outputs[i].output);
        }
    }
    memset(outputs, 0, sizeof(outputs));
    if (state.registry) {
        wl_registry_destroy(state.registry);
    }
    memset(&state, 0, sizeof(state));
}

unsigned stereo_depth_supported_wayland(void)
{
    return state.manager && state.version >= 3 ? 1 : 0;
}

static void depth_state(void *data, struct kde_stereo_depth_v1 *depth, uint32_t awake, uint32_t output_edges)
{
    (void)depth;
    const struct depth_follower *follower = data;
    if (follower->listener) {
        const struct stereo_depth_state current = {awake, output_edges};
        follower->listener(follower->data, follower->surface, &current);
    }
}

static void depth_limits(void *data, struct kde_stereo_depth_v1 *depth, wl_fixed_t sunk, wl_fixed_t popped)
{
    (void)depth;
    struct depth_follower *follower = data;
    follower->limits = (struct stereo_depth_limits){wl_fixed_to_double(sunk), wl_fixed_to_double(popped)};
    follower->limits_known = 1;
    if (follower->limits_listener) {
        follower->limits_listener(follower->limits_data, follower->surface, &follower->limits);
    }
}

static void depth_output_width(void *data, struct kde_stereo_depth_v1 *depth, uint32_t width)
{
    (void)depth;
    struct depth_follower *follower = data;
    follower->output_width = width;
}

static const struct kde_stereo_depth_v1_listener depth_listener = {
    .depth_state = depth_state,
    .depth_limits = depth_limits,
    .output_width = depth_output_width,
};

/* The surface's follower, made when @a create; NULL with errno-style @a error set otherwise. */
static struct depth_follower *follower_of(struct wl_surface *surface, int create, int *error)
{
    if (!surface) {
        *error = -EINVAL;
        return NULL;
    }
    if (!stereo_depth_supported_wayland()) {
        *error = -ENOTSUP;
        return NULL;
    }
    for (struct depth_follower *entry = state.followers; entry; entry = entry->next) {
        if (entry->surface == surface) {
            return entry;
        }
    }
    if (!create) {
        *error = -EINVAL;
        return NULL;
    }
    struct depth_follower *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        *error = -ENOMEM;
        return NULL;
    }
    entry->surface = surface;
    entry->depth = kde_stereo_content_manager_v1_get_depth(state.manager, surface);
    if (!entry->depth) {
        free(entry);
        *error = -ENOMEM;
        return NULL;
    }
    kde_stereo_depth_v1_add_listener(entry->depth, &depth_listener, entry);
    entry->next = state.followers;
    state.followers = entry;
    return entry;
}

int stereo_depth_wayland_watch(struct wl_surface *surface, stereo_depth_listener listener, void *data)
{
    if (!surface || !listener) {
        return -EINVAL;
    }
    int error = 0;
    struct depth_follower *entry = follower_of(surface, 1, &error);
    if (!entry) {
        return error;
    }
    entry->listener = listener;
    entry->data = data;
    return 0;
}

int stereo_depth_wayland_unwatch(struct wl_surface *surface)
{
    if (!surface) {
        return -EINVAL;
    }
    struct depth_follower **link = &state.followers;
    while (*link && (*link)->surface != surface) {
        link = &(*link)->next;
    }
    if (!*link) {
        return -EINVAL;
    }
    struct depth_follower *entry = *link;
    *link = entry->next;
    kde_stereo_depth_v1_destroy(entry->depth);
    free(entry);
    return 0;
}

unsigned stereo_depth_limits_supported_wayland(void)
{
    return state.manager && state.version >= 5 ? 1 : 0;
}

int stereo_depth_wayland_get_limits(struct wl_surface *surface, struct stereo_depth_limits *limits)
{
    if (!surface || !limits) {
        return -EINVAL;
    }
    if (!stereo_depth_limits_supported_wayland()) {
        return -ENOTSUP;
    }
    int error = 0;
    const struct depth_follower *entry = follower_of(surface, 1, &error);
    if (!entry) {
        return error;
    }
    if (!entry->limits_known) {
        return -ENODATA;
    }
    *limits = entry->limits;
    return 0;
}

int stereo_depth_wayland_watch_limits(struct wl_surface *surface, stereo_depth_limits_listener listener, void *data)
{
    if (!surface || !listener) {
        return -EINVAL;
    }
    if (!stereo_depth_limits_supported_wayland()) {
        return -ENOTSUP;
    }
    int error = 0;
    struct depth_follower *entry = follower_of(surface, 1, &error);
    if (!entry) {
        return error;
    }
    entry->limits_listener = listener;
    entry->limits_data = data;
    return 0;
}

int stereo_depth_wayland_set_desktop_depth(struct wl_surface *surface, int desktop_depth)
{
    int error = 0;
    struct depth_follower *entry = follower_of(surface, 1, &error);
    if (!entry) {
        return error;
    }
    kde_stereo_depth_v1_set_desktop_depth(entry->depth, desktop_depth ? 1 : 0);
    return 0;
}

int stereo_depth_wayland_set_place(struct wl_surface *surface, double depth)
{
    int error = 0;
    struct depth_follower *entry = follower_of(surface, 1, &error);
    if (!entry) {
        return error;
    }
    if (!(depth >= -1.0)) {
        depth = -1.0;
    } else if (depth > 1.0) {
        depth = 1.0;
    }
    kde_stereo_depth_v1_set_place(entry->depth, wl_fixed_from_double(depth));
    return 0;
}

int stereo_depth_wayland_unset_place(struct wl_surface *surface)
{
    int error = 0;
    struct depth_follower *entry = follower_of(surface, 1, &error);
    if (!entry) {
        return error;
    }
    kde_stereo_depth_v1_unset_place(entry->depth);
    return 0;
}

unsigned stereo_depth_limits_supported_x11(Display *display)
{
    return stereo_supported_x11(display) >= 10 ? 1 : 0;
}

/* _KDE_NET_WM_STEREO_DEPTH_LIMITS: two CARDINAL/32 values, 24.8 fixed point (wl_fixed_t). */
static int read_depth_limits(Display *display, Window window, Atom atom, struct stereo_depth_limits *limits)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    int result = -ENOENT;
    if (XGetWindowProperty(display, window, atom, 0, 3, False, XA_CARDINAL,
                           &type, &format, &count, &remaining, &data) == Success) {
        if (type == None) {
            result = -ENODATA;
        } else if (type == XA_CARDINAL && format == 32 && count == 2 && remaining == 0) {
            const unsigned long *values = (const unsigned long *)data;
            limits->sunk = (int32_t)(uint32_t)values[0] / 256.0;
            limits->popped = (int32_t)(uint32_t)values[1] / 256.0;
            result = 0;
        } else {
            result = -EPROTO;
        }
    }
    XFree(data);
    return result;
}

int stereo_depth_x11_get_limits(Display *display, Window window, struct stereo_depth_limits *limits)
{
    if (!display || !window || !limits) {
        return -EINVAL;
    }
    if (!stereo_depth_limits_supported_x11(display)) {
        return -ENOTSUP;
    }
    const Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", True);
    if (atom == None) {
        return -ENODATA;
    }
    return read_depth_limits(display, window, atom, limits);
}

int stereo_depth_x11_limits_event(Display *display, Window window, const XEvent *event,
                                  struct stereo_depth_limits *limits)
{
    if (!display || !window || !limits) {
        return -EINVAL;
    }
    if (event && (event->type != PropertyNotify || event->xproperty.window != window)) {
        return 0;
    }
    const Atom atom = XInternAtom(display, "_KDE_NET_WM_STEREO_DEPTH_LIMITS", True);
    if (event && event->xproperty.atom != atom) {
        return 0;
    }
    if (!stereo_depth_limits_supported_x11(display)) {
        return -ENOTSUP;
    }
    if (event && event->xproperty.state == PropertyDelete) {
        return -ENODATA;
    }
    if (atom == None) {
        return -ENODATA;
    }
    const int result = read_depth_limits(display, window, atom, limits);
    return result == 0 && event ? 1 : result;
}

/* since 1.11.0: the person's limits in the pixels of a program's buffer */
int stereo_depth_limits_for_buffer(const struct stereo_depth_limits *screen_limits, double output_width,
                                   double buffer_width, double shown_width, struct stereo_depth_limits *buffer_limits)
{
    if (!screen_limits || !buffer_limits || !(output_width > 0) || !(buffer_width > 0) || !(shown_width > 0) ||
        !isfinite(output_width) || !isfinite(buffer_width) || !isfinite(shown_width)) {
        return -EINVAL;
    }
    const double k = output_width / 1920.0 * buffer_width / shown_width;
    buffer_limits->sunk = screen_limits->sunk * k;
    buffer_limits->popped = screen_limits->popped * k;
    return 0;
}

int stereo_depth_x11_output_width(Display *display, Window window, double *output_width)
{
    if (!display || !window || !output_width) {
        return -EINVAL;
    }
    int major = 0, minor = 0, event_base = 0, error_base = 0;
    if (!XRRQueryExtension(display, &event_base, &error_base) || !XRRQueryVersion(display, &major, &minor) ||
        major < 1 || (major == 1 && minor < 5)) {
        return -ENOTSUP;
    }
    XWindowAttributes attributes;
    Window root = 0;
    int x = 0, y = 0;
    Window child;
    if (!XGetWindowAttributes(display, window, &attributes) || attributes.map_state == IsUnmapped) {
        return -ENODATA;
    }
    root = attributes.root;
    if (!XTranslateCoordinates(display, window, root, 0, 0, &x, &y, &child)) {
        return -ENODATA;
    }
    int count = 0;
    XRRMonitorInfo *monitors = XRRGetMonitors(display, root, True, &count);
    if (!monitors) {
        return -ENODATA;
    }
    long best = 0;
    int width = 0;
    for (int i = 0; i < count; i++) {
        const long ox = (long)(x + attributes.width < monitors[i].x + monitors[i].width ? x + attributes.width : monitors[i].x + monitors[i].width) -
                        (x > monitors[i].x ? x : monitors[i].x);
        const long oy = (long)(y + attributes.height < monitors[i].y + monitors[i].height ? y + attributes.height : monitors[i].y + monitors[i].height) -
                        (y > monitors[i].y ? y : monitors[i].y);
        if (ox > 0 && oy > 0 && ox * oy > best) {
            best = ox * oy;
            width = monitors[i].width;
        }
    }
    XRRFreeMonitors(monitors);
    if (width <= 0) {
        return -ENODATA;
    }
    *output_width = width;
    return 0;
}

int stereo_depth_x11_get_buffer_limits(Display *display, Window window, double buffer_width, double shown_width,
                                       struct stereo_depth_limits *buffer_limits)
{
    struct stereo_depth_limits limits;
    double output_width = 0;
    int result = stereo_depth_x11_get_limits(display, window, &limits);
    if (result == 0) {
        result = stereo_depth_x11_output_width(display, window, &output_width);
    }
    if (result == 0) {
        result = stereo_depth_limits_for_buffer(&limits, output_width, buffer_width, shown_width, buffer_limits);
    }
    return result;
}

int stereo_depth_wayland_output_width(double *output_width)
{
    if (!output_width) {
        return -EINVAL;
    }
    int found = 0, width = 0;
    for (int i = 0; i < STEREO_MAX_OUTPUTS; i++) {
        if (outputs[i].output) {
            found++;
            width = outputs[i].width;
        }
    }
    if (found == 0 || width <= 0) {
        return -ENODATA;
    }
    if (found > 1) {
        return -ENOTUNIQ;
    }
    *output_width = width;
    return 0;
}

int stereo_depth_wayland_get_buffer_limits(struct wl_surface *surface, double buffer_width, double shown_width,
                                           struct stereo_depth_limits *buffer_limits)
{
    struct stereo_depth_limits limits;
    double output_width = 0;
    int result = stereo_depth_wayland_get_limits(surface, &limits);
    if (result == 0) {
        if (state.version >= 6) {
            int error = 0;
            const struct depth_follower *entry = follower_of(surface, 0, &error);
            if (entry && entry->output_width > 0) {
                output_width = entry->output_width;
            } else {
                result = entry ? -ENODATA : error;
            }
        } else {
            result = stereo_depth_wayland_output_width(&output_width);
        }
    }
    if (result == 0) {
        result = stereo_depth_limits_for_buffer(&limits, output_width, buffer_width, shown_width, buffer_limits);
    }
    return result;
}
