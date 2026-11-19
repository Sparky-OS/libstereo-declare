/* SPDX-License-Identifier: MIT */
/* ORDER 767: the caller may destroy its queue after library finish. */
#include <stereo-declare.h>
#include <wayland-client.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct wl_display *shim_start(unsigned manager_version);
void shim_add_output(void);
void shim_pump_queue(struct wl_event_queue *queue);
void shim_pump(void);
unsigned shim_output_binds(void);
void shim_send_output_mode(void);
void shim_stop(void);

static int queue_warning;

static void client_log(const char *format, va_list args)
{
    char message[2048];
    vsnprintf(message, sizeof(message), format, args);
    fputs(message, stderr);
    if (strstr(message, "still attached") || strstr(message, "destroyed queue")) {
        queue_warning = 1;
    }
}

int main(void)
{
    wl_log_set_handler_client(client_log);
    struct wl_display *display = shim_start(6);
    if (!display) {
        return 1;
    }
    shim_add_output();
    struct wl_event_queue *queue = wl_display_create_queue(display);
    if (!queue || stereo_declare_wayland_init(display, queue) != 0) {
        return 1;
    }
    shim_pump_queue(queue);
    double width = 0;
    if (shim_output_binds() != 1 || stereo_depth_wayland_output_width(&width) != 0 || width != 1920) {
        fprintf(stderr, "FAIL: library did not bind the private-queue output\n");
        return 1;
    }
    stereo_declare_wayland_finish();
    wl_event_queue_destroy(queue);
    /* Deliver a new output event after queue destruction, as the real server can. */
    shim_send_output_mode();
    shim_pump();
    shim_stop();
    wl_log_set_handler_client(NULL);
    if (queue_warning) {
        fprintf(stderr, "FAIL: a proxy survived library finish on the caller's queue\n");
        return 1;
    }
    puts("PASS: finish, queue destroy and later output dispatch are clean");
    return 0;
}
