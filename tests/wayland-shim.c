/* SPDX-License-Identifier: MIT */
/* A one-surface compositor in the test's own process, for the Python Wayland binding:
 * kde_stereo_content_manager_v1 at the version the test chooses, depth objects that send
 * depth_limits (only to version 5 or newer, as KWin does). The test calls it through ctypes. */
#include <wayland-client.h>
#include <wayland-server.h>
#include "stereo-server.h"

#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

static struct wl_display *server, *client;
static struct wl_event_loop *loop;
static struct wl_resource *last_depth;
static struct wl_compositor *compositor;
static struct wl_resource *last_output;
static unsigned output_binds;

static void noop(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static const struct wl_surface_interface surface_impl = {.destroy = noop};

static void create_surface(struct wl_client *c, struct wl_resource *resource, uint32_t id)
{
    struct wl_resource *surface = wl_resource_create(c, &wl_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(surface, &surface_impl, NULL, NULL);
}

static const struct wl_compositor_interface compositor_impl = {.create_surface = create_surface};

static void bind_compositor(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = wl_resource_create(c, &wl_compositor_interface, version, id);
    wl_resource_set_implementation(resource, &compositor_impl, NULL, NULL);
}

static void depth_set_desktop_depth(struct wl_client *c, struct wl_resource *r, uint32_t v)
{
    (void)c; (void)r; (void)v;
}

static void depth_set_place(struct wl_client *c, struct wl_resource *r, wl_fixed_t v)
{
    (void)c; (void)r; (void)v;
}

static void depth_unset_place(struct wl_client *c, struct wl_resource *r)
{
    (void)c; (void)r;
}

static const struct kde_stereo_depth_v1_interface depth_impl = {noop, depth_set_desktop_depth, depth_set_place, depth_unset_place};

static void depth_gone(struct wl_resource *resource)
{
    if (last_depth == resource) {
        last_depth = NULL;
    }
}

static void get_depth(struct wl_client *c, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
    (void)surface;
    last_depth = wl_resource_create(c, &kde_stereo_depth_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(last_depth, &depth_impl, NULL, depth_gone);
}

static void create_content(struct wl_client *c, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
    (void)c; (void)resource; (void)id; (void)surface;
}

static const struct kde_stereo_content_manager_v1_interface manager_impl = {noop, create_content, get_depth};

static void bind_manager(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = wl_resource_create(c, &kde_stereo_content_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &manager_impl, NULL, NULL);
}

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)data; (void)version;
    if (!compositor && !strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    }
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

void shim_pump_queue(struct wl_event_queue *queue)
{
    for (int i = 0; i < 4; i++) {
        wl_display_flush(client);
        wl_event_loop_dispatch(loop, 5);
        wl_display_flush_clients(server);
        while (wl_display_prepare_read_queue(client, queue) != 0) {
            wl_display_dispatch_queue_pending(client, queue);
        }
        struct pollfd fd = {wl_display_get_fd(client), POLLIN, 0};
        if (poll(&fd, 1, 5) > 0) {
            wl_display_read_events(client);
        } else {
            wl_display_cancel_read(client);
        }
        wl_display_dispatch_queue_pending(client, queue);
    }
}

void shim_pump(void)
{
    shim_pump_queue(NULL);
}

static void bind_output(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    last_output = wl_resource_create(c, &wl_output_interface, version, id);
    wl_resource_set_implementation(last_output, NULL, NULL, NULL);
    output_binds++;
    wl_output_send_geometry(last_output, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "test", "queue lifetime", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(last_output, WL_OUTPUT_MODE_CURRENT, 1920, 1080, 60000);
    wl_output_send_done(last_output);
}

void shim_add_output(void)
{
    wl_global_create(server, &wl_output_interface, 2, NULL, bind_output);
}

unsigned shim_output_binds(void)
{
    return output_binds;
}

void shim_send_output_mode(void)
{
    if (last_output) {
        wl_output_send_mode(last_output, WL_OUTPUT_MODE_CURRENT, 1280, 800, 60000);
        wl_output_send_done(last_output);
    }
}

struct wl_display *shim_start(unsigned manager_version)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) {
        return NULL;
    }
    server = wl_display_create();
    loop = wl_display_get_event_loop(server);
    wl_global_create(server, &wl_compositor_interface, 4, NULL, bind_compositor);
    /* a compositor newer than the protocol copy: the interface description is the same, only its version is higher */
    static struct wl_interface future;
    future = kde_stereo_content_manager_v1_interface;
    future.version = manager_version > (unsigned)kde_stereo_content_manager_v1_interface.version
        ? (int)manager_version : kde_stereo_content_manager_v1_interface.version;
    wl_global_create(server, &future, manager_version, NULL, bind_manager);
    wl_client_create(server, fds[0]);
    client = wl_display_connect_to_fd(fds[1]);
    static const struct wl_registry_listener listener = {global, global_remove};
    wl_registry_add_listener(wl_display_get_registry(client), &listener, NULL);
    shim_pump();
    return client;
}

struct wl_surface *shim_surface(void)
{
    return wl_compositor_create_surface(compositor);
}

void shim_send_limits(double sunk, double popped)
{
    if (last_depth && wl_resource_get_version(last_depth) >= KDE_STEREO_DEPTH_V1_DEPTH_LIMITS_SINCE_VERSION) {
        kde_stereo_depth_v1_send_depth_limits(last_depth, wl_fixed_from_double(sunk), wl_fixed_from_double(popped));
    }
}

void shim_stop(void)
{
    wl_display_disconnect(client);
    wl_display_destroy(server);
    compositor = NULL;
    last_depth = NULL;
    last_output = NULL;
    output_binds = 0;
}
