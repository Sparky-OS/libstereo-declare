/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include "stereo-client.h"
#include "content_type-client.h"

#include <X11/Xatom.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int valid(enum stereo_layout layout, enum stereo_class content_class, unsigned subclass)
{
    return (unsigned)layout <= 8 && (unsigned)content_class <= 255 && subclass <= 255;
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

int stereo_remove_x11(Display *display, Window window)
{
    if (!display || !window) {
        return -EINVAL;
    }
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT", False));
    XDeleteProperty(display, window, XInternAtom(display, "_KDE_NET_WM_STEREO_CONTENT_CLASS", False));
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
} state;

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version)
{
    (void)data;
    if (!strcmp(interface, kde_stereo_content_manager_v1_interface.name) && version >= 1 && !state.manager) {
        state.manager = wl_registry_bind(registry, name, &kde_stereo_content_manager_v1_interface, 1);
        state.manager_name = name;
        state.version = state.manager ? 1 : 0;
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
    return state.version;
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

int stereo_declare_wayland(struct wl_surface *surface, enum stereo_layout layout,
                           enum stereo_class content_class, unsigned subclass)
{
    if (!surface || !valid(layout, content_class, subclass)) {
        return -EINVAL;
    }
    if (!state.manager) {
        return -ENOTSUP;
    }
    struct declaration *entry = state.declarations;
    while (entry && entry->surface != surface) {
        entry = entry->next;
    }
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) {
            return -ENOMEM;
        }
        entry->surface = surface;
        entry->next = state.declarations;
        state.declarations = entry;
    }
    if (!entry->stereo) {
        entry->stereo = kde_stereo_content_manager_v1_create(state.manager, surface);
        if (!entry->stereo) {
            return -ENOMEM;
        }
        if (state.content_manager && !entry->borrowed_content_type) {
            entry->content_type = wp_content_type_manager_v1_get_surface_content_type(state.content_manager, surface);
            if (!entry->content_type) {
                kde_stereo_content_v1_destroy(entry->stereo);
                entry->stereo = NULL;
                return -ENOMEM;
            }
        }
    }
    kde_stereo_content_v1_set_content(entry->stereo, layout);
    kde_stereo_content_v1_set_content_class(entry->stereo, content_class, subclass);
    if (entry->content_type) {
        wp_content_type_v1_set_content_type(entry->content_type, (unsigned)content_class <= 3 ? (unsigned)content_class : 0);
    }
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
