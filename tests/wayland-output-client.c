/* SPDX-License-Identifier: MIT */
/* A client for a real compositor session (Stereo KWin on a virtual output, run by a person's script, not by ctest):
 * prints what stereo_depth_wayland_output_width() says after the outputs' modes arrived.
 * usage: wayland-output-client   (WAYLAND_DISPLAY from the environment)
 * exit 0 and "width W" for one output, "several outputs" (exit 0) for -ENOTUNIQ, 1 otherwise. */
#include <stereo-declare.h>

#include <errno.h>
#include <stdio.h>
#include <wayland-client.h>

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "no wayland display\n");
        return 1;
    }
    if (stereo_declare_wayland_init(display, NULL) != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    double width = 0;
    int result = -ENODATA;
    for (int i = 0; i < 20 && result == -ENODATA; i++) {
        wl_display_roundtrip(display);
        result = stereo_depth_wayland_output_width(&width);
    }
    if (result == 0) {
        printf("width %.0f\n", width);
        return 0;
    }
    if (result == -ENOTUNIQ) {
        printf("several outputs\n");
        return 0;
    }
    printf("error %d\n", result);
    return 1;
}
