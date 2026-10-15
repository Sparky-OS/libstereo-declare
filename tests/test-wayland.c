/* SPDX-License-Identifier: MIT */
/* The Wayland side against a small in-process server that records every
 * request and raises the protocols' own errors. */
#define _GNU_SOURCE
#include <stereo-declare.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include "content_type-client.h"
#include "content_type-server.h"
#include "stereo-server.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static int failures;
#define CHECK(condition)                                                                       \
    do {                                                                                        \
        if (!(condition)) {                                                                     \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #condition);               \
            failures++;                                                                         \
        }                                                                                       \
    } while (0)

/* What the server saw. */
static struct {
    int stereo_creates, stereo_destroys, set_contents;
    unsigned last_content;
    int type_creates, type_destroys, set_types;
    unsigned last_type;
} seen;

struct surface_state {
    struct wl_resource *stereo, *content_type;
};

static struct wl_display *server;
static struct wl_event_loop *loop;
static struct wl_display *client;

/* wl_compositor and wl_surface, only what the library needs. */
static void surface_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void surface_commit(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    (void)resource;
}

static const struct wl_surface_interface surface_impl = {.destroy = surface_destroy, .commit = surface_commit};

static void surface_resource_destroy(struct wl_resource *resource)
{
    free(wl_resource_get_user_data(resource));
}

static void create_surface(struct wl_client *c, struct wl_resource *resource, uint32_t id)
{
    struct wl_resource *surface = wl_resource_create(c, &wl_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(surface, &surface_impl, calloc(1, sizeof(struct surface_state)), surface_resource_destroy);
}

static const struct wl_compositor_interface compositor_impl = {.create_surface = create_surface};

static void bind_compositor(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = wl_resource_create(c, &wl_compositor_interface, version, id);
    wl_resource_set_implementation(resource, &compositor_impl, NULL, NULL);
}

/* kde_stereo_content_manager_v1 */
static void stereo_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void stereo_set_content(struct wl_client *c, struct wl_resource *resource, uint32_t content)
{
    (void)c;
    (void)resource;
    seen.set_contents++;
    seen.last_content = content;
}

static const struct kde_stereo_content_v1_interface stereo_impl = {stereo_destroy, stereo_set_content};

static void stereo_resource_destroy(struct wl_resource *resource)
{
    struct wl_resource *surface = wl_resource_get_user_data(resource);
    seen.stereo_destroys++;
    if (surface) {
        ((struct surface_state *)wl_resource_get_user_data(surface))->stereo = NULL;
    }
}

static void manager_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void manager_create(struct wl_client *c, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
    struct surface_state *state = wl_resource_get_user_data(surface);
    if (state->stereo) {
        wl_resource_post_error(resource, KDE_STEREO_CONTENT_MANAGER_V1_ERROR_ALREADY_DECLARED, "already declared");
        return;
    }
    state->stereo = wl_resource_create(c, &kde_stereo_content_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(state->stereo, &stereo_impl, surface, stereo_resource_destroy);
    seen.stereo_creates++;
}

static const struct kde_stereo_content_manager_v1_interface manager_impl = {manager_destroy, manager_create};

static void bind_manager(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = wl_resource_create(c, &kde_stereo_content_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &manager_impl, NULL, NULL);
}

/* wp_content_type_manager_v1 */
static void type_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void type_set(struct wl_client *c, struct wl_resource *resource, uint32_t type)
{
    (void)c;
    (void)resource;
    seen.set_types++;
    seen.last_type = type;
}

static const struct wp_content_type_v1_interface type_impl = {type_destroy, type_set};

static void type_resource_destroy(struct wl_resource *resource)
{
    struct wl_resource *surface = wl_resource_get_user_data(resource);
    seen.type_destroys++;
    if (surface) {
        ((struct surface_state *)wl_resource_get_user_data(surface))->content_type = NULL;
    }
}

static void type_manager_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void type_manager_get(struct wl_client *c, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
    struct surface_state *state = wl_resource_get_user_data(surface);
    if (state->content_type) {
        wl_resource_post_error(resource, WP_CONTENT_TYPE_MANAGER_V1_ERROR_ALREADY_CONSTRUCTED, "already constructed");
        return;
    }
    state->content_type = wl_resource_create(c, &wp_content_type_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(state->content_type, &type_impl, surface, type_resource_destroy);
    seen.type_creates++;
}

static const struct wp_content_type_manager_v1_interface type_manager_impl = {type_manager_destroy, type_manager_get};

static void bind_type_manager(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = wl_resource_create(c, &wp_content_type_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &type_manager_impl, NULL, NULL);
}

/* The test's own client objects. */
static struct wl_compositor *compositor;
static struct wp_content_type_manager_v1 *type_manager;

static void client_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    (void)version;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (!strcmp(interface, wp_content_type_manager_v1_interface.name) && !type_manager) {
        type_manager = wl_registry_bind(registry, name, &wp_content_type_manager_v1_interface, 1);
    }
}

static void client_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

/* Let requests reach the server and events reach the client, in one thread. */
static void pump(void)
{
    for (int i = 0; i < 4; i++) {
        wl_display_flush(client);
        wl_event_loop_dispatch(loop, 5);
        wl_display_flush_clients(server);
        while (wl_display_prepare_read(client) != 0) {
            wl_display_dispatch_pending(client);
        }
        struct pollfd fd = {wl_display_get_fd(client), POLLIN, 0};
        if (poll(&fd, 1, 5) > 0) {
            wl_display_read_events(client);
        } else {
            wl_display_cancel_read(client);
        }
        wl_display_dispatch_pending(client);
    }
}

int main(void)
{
    CHECK(stereo_declare_version() == STEREO_DECLARE_VERSION);
    CHECK(STEREO_DECLARE_VERSION == STEREO_DECLARE_VERSION_ENCODE(1, 3, 0));

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) {
        return 1;
    }
    server = wl_display_create();
    loop = wl_display_get_event_loop(server);
    wl_global_create(server, &wl_compositor_interface, 4, NULL, bind_compositor);
    wl_global_create(server, &kde_stereo_content_manager_v1_interface, 1, NULL, bind_manager);
    struct wl_global *type_global = wl_global_create(server, &wp_content_type_manager_v1_interface, 1, NULL, bind_type_manager);
    wl_client_create(server, fds[0]);
    client = wl_display_connect_to_fd(fds[1]);

    static const struct wl_registry_listener listener = {client_global, client_global_remove};
    struct wl_registry *registry = wl_display_get_registry(client);
    wl_registry_add_listener(registry, &listener, NULL);

    CHECK(stereo_supported_wayland() == 0);
    CHECK(stereo_declare_wayland_init(NULL, NULL) == -EINVAL);
    CHECK(stereo_declare_wayland_init(client, NULL) == 0);
    CHECK(stereo_declare_wayland_init(client, NULL) == -EBUSY);
    pump();
    CHECK(stereo_supported_wayland() == 1);
    CHECK(compositor && type_manager);

    /* One declaration per surface, the kind through wp_content_type_v1. */
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_GAME, STEREO_GAME_3D) == 0);
    pump();
    CHECK(seen.stereo_creates == 1 && seen.set_contents == 1 && seen.last_content == 3);
    CHECK(seen.type_creates == 1 && seen.set_types == 1 && seen.last_type == WP_CONTENT_TYPE_V1_TYPE_GAME);

    /* A second call updates the same objects. */
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, STEREO_VIDEO_CURRENT) == 0);
    pump();
    CHECK(seen.stereo_creates == 1 && seen.type_creates == 1 && seen.set_contents == 2);
    CHECK(seen.last_type == WP_CONTENT_TYPE_V1_TYPE_VIDEO);

    /* Scientific has no content type of its own: none. */
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_SCIENTIFIC, STEREO_SCIENTIFIC_VR) == 0);
    pump();
    CHECK(seen.last_type == WP_CONTENT_TYPE_V1_TYPE_NONE);

    /* Nothing but none and full side by side; nothing is sent for the rest. */
    for (unsigned layout = 1; layout < 9; layout++) {
        if (layout != STEREO_SBS_FULL) {
            CHECK(stereo_declare_wayland(surface, (enum stereo_layout)layout, STEREO_CLASS_VIDEO, 0) == -EINVAL);
        }
    }
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, (enum stereo_class)256, 0) == -EINVAL);
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 256) == -EINVAL);
    CHECK(stereo_declare_wayland(NULL, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0) == -EINVAL);
    pump();
    CHECK(seen.set_contents == 3 && seen.last_content == 3);

    /* Declaring none keeps the object. */
    CHECK(stereo_declare_wayland(surface, STEREO_NONE, STEREO_CLASS_NONE, 0) == 0);
    pump();
    CHECK(seen.last_content == 0 && seen.stereo_destroys == 0);

    /* Removal destroys what the library created; a second removal is harmless. */
    CHECK(stereo_remove_wayland(surface) == 0);
    CHECK(stereo_remove_wayland(surface) == 0);
    pump();
    CHECK(seen.stereo_destroys == 1 && seen.type_destroys == 1);

    /* A program that has its own wp_content_type_v1 hands it over: no second
     * object (already_constructed), and removal sets it back to none. */
    struct wl_surface *owned = wl_compositor_create_surface(compositor);
    struct wp_content_type_v1 *own_type = wp_content_type_manager_v1_get_surface_content_type(type_manager, owned);
    pump();
    CHECK(seen.type_creates == 2);
    CHECK(stereo_declare_wayland_use_content_type(owned, own_type) == 0);
    CHECK(stereo_declare_wayland_use_content_type(owned, own_type) == -EBUSY);
    CHECK(stereo_declare_wayland(owned, STEREO_SBS_FULL, STEREO_CLASS_PHOTO, 0) == 0);
    pump();
    CHECK(wl_display_get_error(client) == 0);
    CHECK(seen.type_creates == 2 && seen.stereo_creates == 2 && seen.last_type == WP_CONTENT_TYPE_V1_TYPE_PHOTO);
    CHECK(stereo_remove_wayland(owned) == 0);
    pump();
    CHECK(seen.last_type == WP_CONTENT_TYPE_V1_TYPE_NONE && seen.type_destroys == 1 && seen.stereo_destroys == 2);

    /* The format only: the program's own content type stays its own, and no
     * second one is made (already_constructed). */
    struct wl_surface *formatted = wl_compositor_create_surface(compositor);
    struct wp_content_type_v1 *formatted_type = wp_content_type_manager_v1_get_surface_content_type(type_manager, formatted);
    wp_content_type_v1_set_content_type(formatted_type, WP_CONTENT_TYPE_V1_TYPE_GAME);
    pump();
    const int types_before = seen.set_types;
    CHECK(seen.type_creates == 3);
    CHECK(stereo_declare_wayland_format(formatted, STEREO_SBS_FULL) == 0);
    for (unsigned layout = 1; layout < 9; layout++) {
        if (layout != STEREO_SBS_FULL) {
            CHECK(stereo_declare_wayland_format(formatted, (enum stereo_layout)layout) == -EINVAL);
        }
    }
    CHECK(stereo_declare_wayland_format(NULL, STEREO_SBS_FULL) == -EINVAL);
    pump();
    CHECK(wl_display_get_error(client) == 0);
    CHECK(seen.stereo_creates == 3 && seen.type_creates == 3 && seen.set_types == types_before);
    CHECK(seen.last_content == 3 && seen.last_type == WP_CONTENT_TYPE_V1_TYPE_GAME);
    CHECK(stereo_declare_wayland_format(formatted, STEREO_NONE) == 0);
    pump();
    CHECK(seen.last_content == 0 && seen.stereo_destroys == 2);
    CHECK(stereo_remove_wayland(formatted) == 0);
    pump();
    CHECK(seen.stereo_destroys == 3 && seen.type_destroys == 1 && seen.set_types == types_before);

    /* finish destroys the rest and forgets the display. */
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0) == 0);
    pump();
    const int stereo_alive = seen.stereo_creates - seen.stereo_destroys, types_alive = seen.type_creates - seen.type_destroys;
    stereo_declare_wayland_finish();
    pump();
    CHECK(seen.stereo_creates - seen.stereo_destroys == stereo_alive - 1);
    CHECK(seen.type_creates - seen.type_destroys == types_alive - 1);
    CHECK(stereo_supported_wayland() == 0);
    CHECK(stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_GAME, 0) == -ENOTSUP);

    /* The content type manager announced after the stereo one is still used. */
    wl_global_destroy(type_global);
    pump();
    CHECK(stereo_declare_wayland_init(client, NULL) == 0);
    pump();
    struct wl_surface *late = wl_compositor_create_surface(compositor);
    const int late_stereo = seen.stereo_creates, late_types = seen.type_creates;
    CHECK(stereo_declare_wayland(late, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0) == 0);
    pump();
    CHECK(seen.stereo_creates == late_stereo + 1 && seen.type_creates == late_types);
    wl_global_create(server, &wp_content_type_manager_v1_interface, 1, NULL, bind_type_manager);
    pump();
    CHECK(stereo_declare_wayland(late, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0) == 0);
    pump();
    CHECK(seen.stereo_creates == late_stereo + 1 && seen.type_creates == late_types + 1);
    CHECK(seen.last_type == WP_CONTENT_TYPE_V1_TYPE_VIDEO);
    stereo_remove_wayland(late);
    stereo_declare_wayland_finish();
    pump();

    CHECK(wl_display_get_error(client) == 0);
    wp_content_type_v1_destroy(own_type);
    wp_content_type_v1_destroy(formatted_type);
    wl_surface_destroy(formatted);
    wl_surface_destroy(late);
    wl_surface_destroy(owned);
    wl_surface_destroy(surface);
    pump();
    wl_display_disconnect(client);
    wl_display_destroy(server);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
