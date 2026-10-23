/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include "stereo-client.h"
#include "content_type-client.h"

#include <X11/Xatom.h>
#include <errno.h>
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

int stereo_remove_x11(Display *display, Window window)
{
    if (!display || !window) {
        return -EINVAL;
    }
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False));
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False));
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE", False));
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

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version)
{
    (void)data;
    if (!strcmp(interface, kde_stereo_content_manager_v1_interface.name) && version >= 1 && !state.manager) {
        /* Version 2 makes a declared surface one view wide when it has no viewport destination;
         * version 3 follows surfaces' desktop depth. */
        const uint32_t bound = version < 3 ? version : 3;
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

static const struct kde_stereo_depth_v1_listener depth_listener = {depth_state};

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
