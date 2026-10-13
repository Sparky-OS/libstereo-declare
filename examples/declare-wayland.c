/* SPDX-License-Identifier: MIT */
/* A Wayland window that shows a full side-by-side picture: the left eye red,
 * the right eye blue. The surface is one eye's size (wp_viewporter), its
 * buffer holds both eyes, and it is declared once. */
#define _GNU_SOURCE
#include <stereo-declare.h>
#include <wayland-client.h>
#include "viewporter-client.h"
#include "xdg-shell-client.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { EYE_WIDTH = 640, EYE_HEIGHT = 360 };

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wp_viewporter *viewporter;
static int configured, closed;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    (void)version;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    } else if (!strcmp(interface, wp_viewporter_interface.name)) {
        viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static void ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static void surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    configured = 1;
}

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data;
    (void)toplevel;
    closed = 1;
}

/* Both eyes side by side at full size: left red, right blue. */
static struct wl_buffer *draw_both_eyes(void)
{
    const int width = 2 * EYE_WIDTH, height = EYE_HEIGHT, stride = width * 4, size = stride * height;
    int fd = memfd_create("declare-wayland", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        return NULL;
    }
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            pixels[y * width + x] = x < EYE_WIDTH ? 0xffff0000 : 0xff0000ff;
        }
    }
    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

int main(void)
{
    static const struct wl_registry_listener registry_listener = {global, global_remove};
    static const struct xdg_wm_base_listener wm_base_listener = {ping};
    static const struct xdg_surface_listener surface_listener = {surface_configure};
    static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure, .close = toplevel_close};

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "no Wayland display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    /* The library binds its globals on the same (default) queue. */
    if (stereo_declare_wayland_init(display, NULL) != 0) {
        return 1;
    }
    wl_display_roundtrip(display);
    if (!compositor || !shm || !wm_base || !viewporter) {
        fprintf(stderr, "the compositor lacks wl_compositor, wl_shm, xdg_wm_base or wp_viewporter\n");
        return 1;
    }
    if (!stereo_supported_wayland()) {
        fprintf(stderr, "the compositor does not take stereo declarations\n");
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, NULL);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "declare-wayland");

    /* Declare once: full side by side, a still picture. Takes effect with
     * the commit that brings the first buffer. */
    int result = stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_PHOTO, STEREO_SUBCLASS_UNSPECIFIED);
    if (result != 0) {
        fprintf(stderr, "declaration failed: %s\n", strerror(-result));
        return 1;
    }
    wl_surface_commit(surface);
    while (!configured && wl_display_dispatch(display) >= 0) {
    }

    /* The window is one eye's size; the buffer holds both eyes. */
    struct wp_viewport *viewport = wp_viewporter_get_viewport(viewporter, surface);
    wp_viewport_set_destination(viewport, EYE_WIDTH, EYE_HEIGHT);
    struct wl_buffer *buffer = draw_both_eyes();
    if (!buffer) {
        return 1;
    }
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 2 * EYE_WIDTH, EYE_HEIGHT);
    wl_surface_commit(surface);

    while (!closed && wl_display_dispatch(display) >= 0) {
    }

    stereo_remove_wayland(surface);
    wl_surface_commit(surface);
    stereo_declare_wayland_finish();
    wp_viewport_destroy(viewport);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    wl_display_disconnect(display);
    return 0;
}
