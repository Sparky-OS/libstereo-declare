/* SPDX-License-Identifier: MIT */
/* What a program meets of the library on its own Wayland connection (row 835), against a compositor in another
 * process that listens on a named socket as a real one does. Like KWin, it names in wl_surface.enter every wl_output
 * resource the client holds for the output the surface is on, and it refuses set_content on a declaration whose
 * surface is gone ("wl_surface was destroyed before a set_content request").
 *   enter:  a plain client with its own wl_surface listener aborts on an output it did not bind. Libraries 1.11.0 to
 *           1.12.1 bound outputs on the program's connection, and mpv 0.41 died of the same event at its first frame.
 *   width:  stereo_depth_wayland_output_width() answers 1920 with one output, follows a mode change to 1280, and says
 *           -ENOTUNIQ with two outputs.
 *   reuse:  a surface destroyed without withdrawing its declaration, then a new surface made at the same address and
 *           declared: nothing may be sent on the old declaration.
 * Each client runs in a child that may abort; its status is read here. */
#define _GNU_SOURCE
#include <stereo-declare.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include "stereo-server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#define SOCKET_NAME "stereo-declare-program"

/* ---- the compositor, in its own process ---- */

struct output_global {
    int width;
    struct wl_list resources;
};

static struct output_global outputs[2];
static int output_count;

static void unlink_resource(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void send_mode(struct wl_resource *resource, int width)
{
    wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, width, width * 9 / 16, 60000);
    if (wl_resource_get_version(resource) >= WL_OUTPUT_DONE_SINCE_VERSION) {
        wl_output_send_done(resource);
    }
}

static void bind_output(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    struct output_global *output = data;
    struct wl_resource *resource = wl_resource_create(c, &wl_output_interface, version, id);
    wl_resource_set_implementation(resource, NULL, output, unlink_resource);
    wl_list_insert(&output->resources, wl_resource_get_link(resource));
    wl_output_send_geometry(resource, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "test", "program", WL_OUTPUT_TRANSFORM_NORMAL);
    send_mode(resource, output->width);
}

static void surface_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

/* The surface is shown on the first output from its first commit: enter for each of the client's resources of it. */
static void surface_commit(struct wl_client *c, struct wl_resource *resource)
{
    if (wl_resource_get_user_data(resource)) {
        return;
    }
    wl_resource_set_user_data(resource, resource);
    struct wl_resource *output;
    wl_resource_for_each(output, &outputs[0].resources) {
        if (wl_resource_get_client(output) == c) {
            wl_surface_send_enter(resource, output);
        }
    }
}

static const struct wl_surface_interface surface_impl = {.destroy = surface_destroy, .commit = surface_commit};

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

/* A declaration knows whether its surface still exists, as Stereo KWin's does (a QPointer there). */
struct declaration {
    struct wl_resource *surface;
    struct wl_listener surface_gone;
};

static void declaration_surface_gone(struct wl_listener *listener, void *data)
{
    (void)data;
    struct declaration *declaration = wl_container_of(listener, declaration, surface_gone);
    declaration->surface = NULL;
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
}

static void content_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void content_set_content(struct wl_client *c, struct wl_resource *resource, uint32_t content)
{
    (void)c; (void)content;
    const struct declaration *declaration = wl_resource_get_user_data(resource);
    if (!declaration->surface) {
        wl_resource_post_error(resource, KDE_STEREO_CONTENT_V1_ERROR_NO_SURFACE,
                               "wl_surface was destroyed before a set_content request");
    }
}

static void content_set_view_source(struct wl_client *c, struct wl_resource *resource,
                                    wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    (void)c; (void)x; (void)y; (void)width; (void)height;
    const struct declaration *declaration = wl_resource_get_user_data(resource);
    if (!declaration->surface) {
        wl_resource_post_error(resource, KDE_STEREO_CONTENT_V1_ERROR_NO_SURFACE, "wl_surface was destroyed");
    }
}

static const struct kde_stereo_content_v1_interface content_impl = {
    .destroy = content_destroy, .set_content = content_set_content, .set_view_source = content_set_view_source};

static void content_resource_destroy(struct wl_resource *resource)
{
    struct declaration *declaration = wl_resource_get_user_data(resource);
    wl_list_remove(&declaration->surface_gone.link);
    free(declaration);
}

static void manager_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}

static void manager_create(struct wl_client *c, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
    struct declaration *declaration = calloc(1, sizeof(*declaration));
    declaration->surface = surface;
    declaration->surface_gone.notify = declaration_surface_gone;
    wl_resource_add_destroy_listener(surface, &declaration->surface_gone);
    struct wl_resource *content = wl_resource_create(c, &kde_stereo_content_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(content, &content_impl, declaration, content_resource_destroy);
}

static void depth_destroy(struct wl_client *c, struct wl_resource *resource)
{
    (void)c;
    wl_resource_destroy(resource);
}
static void depth_set_desktop_depth(struct wl_client *c, struct wl_resource *r, uint32_t v) { (void)c; (void)r; (void)v; }
static void depth_set_place(struct wl_client *c, struct wl_resource *r, wl_fixed_t v) { (void)c; (void)r; (void)v; }
static void depth_unset_place(struct wl_client *c, struct wl_resource *r) { (void)c; (void)r; }
static const struct kde_stereo_depth_v1_interface depth_impl = {depth_destroy, depth_set_desktop_depth, depth_set_place,
                                                                depth_unset_place};

static void manager_get_depth(struct wl_client *c, struct wl_resource *resource, uint32_t id, struct wl_resource *surface)
{
    (void)surface;
    struct wl_resource *depth = wl_resource_create(c, &kde_stereo_depth_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(depth, &depth_impl, NULL, NULL);
}

static const struct kde_stereo_content_manager_v1_interface manager_impl = {manager_destroy, manager_create, manager_get_depth};

static void bind_manager(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = wl_resource_create(c, &kde_stereo_content_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &manager_impl, NULL, NULL);
}

/* SIGUSR1: the first output changes its mode to 1280 wide. */
static int change_mode(int signal_number, void *data)
{
    (void)signal_number; (void)data;
    outputs[0].width = 1280;
    struct wl_resource *resource;
    wl_resource_for_each(resource, &outputs[0].resources) {
        send_mode(resource, outputs[0].width);
    }
    return 0;
}

static void run_compositor(int count)
{
    struct wl_display *server = wl_display_create();
    if (!server || wl_display_add_socket(server, SOCKET_NAME) != 0) {
        _exit(20);
    }
    wl_global_create(server, &wl_compositor_interface, 4, NULL, bind_compositor);
    wl_global_create(server, &kde_stereo_content_manager_v1_interface, kde_stereo_content_manager_v1_interface.version,
                     NULL, bind_manager);
    output_count = count;
    for (int i = 0; i < count; i++) {
        outputs[i].width = i == 0 ? 1920 : 1280;
        wl_list_init(&outputs[i].resources);
        wl_global_create(server, &wl_output_interface, 4, &outputs[i], bind_output);
    }
    wl_event_loop_add_signal(wl_display_get_event_loop(server), SIGUSR1, change_mode, NULL);
    wl_display_run(server);
    _exit(0);
}

/* ---- the program ---- */

enum { CLEAN = 0, NO_CONNECTION = 10, NO_INIT = 11, NO_ENTER = 12, WRONG_WIDTH = 13, NO_REUSE = 14, PROTOCOL_ERROR = 15 };

static struct wl_compositor *compositor;
static struct wl_output *own_outputs[2];
static int own_count, entered;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_output_interface.name) && own_count < 2) {
        /* the program's own outputs, as mpv binds them */
        own_outputs[own_count++] = wl_registry_bind(registry, name, &wl_output_interface, version < 4 ? version : 4);
    }
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {global, global_remove};

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub,
                            const char *make, const char *model, int32_t transform)
{
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sub; (void)make; (void)model; (void)transform;
}
static void output_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r)
{
    (void)d; (void)o; (void)f; (void)w; (void)h; (void)r;
}
static void output_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void output_scale(void *d, struct wl_output *o, int32_t f) { (void)d; (void)o; (void)f; }
static void output_name(void *d, struct wl_output *o, const char *n) { (void)d; (void)o; (void)n; }
static void output_description(void *d, struct wl_output *o, const char *n) { (void)d; (void)o; (void)n; }
static const struct wl_output_listener output_listener = {output_geometry, output_mode, output_done, output_scale,
                                                          output_name, output_description};

/* The program's own listener: an output it never bound is an object it knows nothing about. */
static void surface_enter(void *data, struct wl_surface *surface, struct wl_output *output)
{
    (void)data; (void)surface;
    for (int i = 0; i < own_count; i++) {
        if (output == own_outputs[i]) {
            entered++;
            return;
        }
    }
    fprintf(stderr, "wl_surface.enter named wl_output#%u, which this program never bound\n",
            output ? wl_proxy_get_id((struct wl_proxy *)output) : 0);
    abort();
}

static void surface_leave(void *data, struct wl_surface *surface, struct wl_output *output)
{
    (void)data; (void)surface; (void)output;
}

static const struct wl_surface_listener surface_listener = {.enter = surface_enter, .leave = surface_leave};

static struct wl_display *connect_program(void)
{
    struct wl_display *display = wl_display_connect(SOCKET_NAME);
    if (!display) {
        return NULL;
    }
    wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
    wl_display_roundtrip(display);
    for (int i = 0; i < own_count; i++) {
        wl_output_add_listener(own_outputs[i], &output_listener, NULL);
    }
    if (!compositor || stereo_declare_wayland_init(display, NULL) != 0) {
        return NULL;
    }
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    return display;
}

static int scenario_enter(void)
{
    struct wl_display *display = connect_program();
    if (!display) {
        return NO_CONNECTION;
    }
    double width = 0;
    stereo_depth_wayland_output_width(&width); /* the library learns the outputs, wherever it does */
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    wl_surface_add_listener(surface, &surface_listener, NULL);
    stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    if (wl_display_get_error(display)) {
        return PROTOCOL_ERROR;
    }
    if (entered == 0) {
        return NO_ENTER;
    }
    stereo_remove_wayland(surface);
    wl_surface_destroy(surface);
    stereo_declare_wayland_finish();
    wl_display_disconnect(display);
    return CLEAN;
}

static int scenario_width(int count, pid_t server)
{
    struct wl_display *display = connect_program();
    if (!display) {
        return NO_CONNECTION;
    }
    double width = 0;
    const int result = stereo_depth_wayland_output_width(&width);
    if (count == 2) {
        if (result != -ENOTUNIQ) {
            fprintf(stderr, "two outputs: %d (%s), width %g\n", result, strerror(-result), width);
            return WRONG_WIDTH;
        }
    } else {
        if (result != 0 || width != 1920) {
            fprintf(stderr, "one output: %d (%s), width %g\n", result, strerror(-result), width);
            return WRONG_WIDTH;
        }
        kill(server, SIGUSR1);
        /* The program dispatches its own connection meanwhile, as it would. */
        for (int i = 0; i < 100 && width != 1280; i++) {
            wl_display_roundtrip(display);
            if (stereo_depth_wayland_output_width(&width) != 0) {
                return WRONG_WIDTH;
            }
            if (width != 1280) {
                usleep(20000);
            }
        }
        if (width != 1280) {
            fprintf(stderr, "the mode change to 1280 was not seen: %g\n", width);
            return WRONG_WIDTH;
        }
    }
    stereo_declare_wayland_finish();
    wl_display_disconnect(display);
    return CLEAN;
}

static int scenario_reuse(void)
{
    struct wl_display *display = connect_program();
    if (!display) {
        return NO_CONNECTION;
    }
    struct wl_surface *old = wl_compositor_create_surface(compositor);
    for (int attempt = 0; attempt < 16; attempt++) {
        if (stereo_declare_wayland_format(old, STEREO_SBS_FULL) != 0) {
            return NO_INIT;
        }
        wl_display_roundtrip(display);
        /* destroyed without stereo_remove_wayland(): the program's fault, which API.md names; the next surface is made
         * at once, as a program does, so the allocator hands it the destroyed proxy's block */
        wl_surface_destroy(old);
        struct wl_surface *surface = wl_compositor_create_surface(compositor);
        if (surface == old) {
            stereo_declare_wayland_format(surface, STEREO_SBS_FULL);
            wl_display_roundtrip(display);
            if (wl_display_get_error(display)) {
                fprintf(stderr, "the new surface at the old address was declared on the old declaration\n");
                return PROTOCOL_ERROR;
            }
            stereo_remove_wayland(surface);
            wl_surface_destroy(surface);
            stereo_declare_wayland_finish();
            wl_display_disconnect(display);
            return CLEAN;
        }
        old = surface;
    }
    fprintf(stderr, "no new surface was made at a destroyed surface's address in 16 attempts\n");
    return NO_REUSE;
}

/* ---- the test ---- */

static pid_t start_compositor(const char *runtime, int count)
{
    char socket_path[512];
    snprintf(socket_path, sizeof(socket_path), "%s/%s", runtime, SOCKET_NAME);
    unlink(socket_path);
    fflush(NULL);
    const pid_t pid = fork();
    if (pid == 0) {
        run_compositor(count);
    }
    struct stat st;
    for (int i = 0; i < 500 && stat(socket_path, &st) != 0; i++) {
        usleep(10000);
    }
    return pid;
}

static int run(const char *runtime, const char *name, int count)
{
    const pid_t server = start_compositor(runtime, count);
    if (server < 0) {
        perror("fork");
        return 1;
    }
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0) {
#ifdef __linux__
        /* the abort a red run ends in is expected and must not reach the system's crash handler */
        prctl(PR_SET_DUMPABLE, 0);
#endif
        alarm(20);
        _exit(!strcmp(name, "enter") ? scenario_enter() : !strcmp(name, "reuse") ? scenario_reuse()
                                                                                  : scenario_width(count, server));
    }
    int status = 0;
    waitpid(child, &status, 0);
    kill(server, SIGTERM);
    waitpid(server, NULL, 0);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "FAIL %s (%d outputs): the program died of signal %d (%s)\n", name, count, WTERMSIG(status),
                strsignal(WTERMSIG(status)));
        return 1;
    }
    if (WEXITSTATUS(status) != CLEAN) {
        fprintf(stderr, "FAIL %s (%d outputs): step %d\n", name, count, WEXITSTATUS(status));
        return 1;
    }
    printf("PASS %s (%d outputs)\n", name, count);
    return 0;
}

int main(void)
{
    char runtime[] = "/tmp/stereo-declare-program-XXXXXX";
    const char *base = getenv("TMPDIR");
    char path[512];
    if (base && *base) {
        snprintf(path, sizeof(path), "%s/stereo-declare-program-XXXXXX", base);
    } else {
        snprintf(path, sizeof(path), "%s", runtime);
    }
    if (!mkdtemp(path)) {
        perror("mkdtemp");
        return 1;
    }
    setenv("XDG_RUNTIME_DIR", path, 1);
    int failures = 0;
    failures += run(path, "enter", 1);
    failures += run(path, "enter", 2);
    failures += run(path, "width", 1);
    failures += run(path, "width", 2);
    failures += run(path, "reuse", 1);
    char socket_path[600];
    snprintf(socket_path, sizeof(socket_path), "%s/%s", path, SOCKET_NAME);
    unlink(socket_path);
    snprintf(socket_path, sizeof(socket_path), "%s/%s.lock", path, SOCKET_NAME);
    unlink(socket_path);
    rmdir(path);
    return failures ? 1 : 0;
}
