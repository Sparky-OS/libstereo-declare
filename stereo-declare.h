/* SPDX-License-Identifier: MIT */
#ifndef STEREO_DECLARE_H
#define STEREO_DECLARE_H

#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_surface;
struct wl_event_queue;
struct wp_content_type_v1;

enum stereo_layout {
    STEREO_NONE = 0,
    STEREO_SBS_HALF = 1,
    STEREO_SBS_HALF_RIGHT_FIRST = 2,
    STEREO_SBS_FULL = 3,
    STEREO_SBS_FULL_RIGHT_FIRST = 4,
    STEREO_TAB_HALF = 5,
    STEREO_TAB_HALF_RIGHT_FIRST = 6,
    STEREO_TAB_FULL = 7,
    STEREO_TAB_FULL_RIGHT_FIRST = 8
};

enum stereo_class {
    STEREO_CLASS_NONE = 0,
    STEREO_CLASS_PHOTO = 1,
    STEREO_CLASS_VIDEO = 2,
    STEREO_CLASS_GAME = 3,
    STEREO_CLASS_SCIENTIFIC = 4
};

enum stereo_subclass {
    STEREO_SUBCLASS_UNSPECIFIED = 0,
    STEREO_GAME_VR = 1,
    STEREO_GAME_3D = 2,
    STEREO_GAME_GL = 3,
    STEREO_GAME_STEREOGL = 4,
    STEREO_VIDEO_LEGACY = 1,
    STEREO_VIDEO_CURRENT = 2,
    STEREO_SCIENTIFIC_VR = 1,
    STEREO_SCIENTIFIC_STEREOGL = 2
};

/* Support version, or zero when absent. Xlib errors use the caller's handler. */
unsigned stereo_supported_x11(Display *display);
/* Returns 0 on success or a negative errno value. Requests are flushed. */
int stereo_declare_x11(Display *display, Window window, enum stereo_layout layout,
                       enum stereo_class content_class, unsigned subclass);
int stereo_remove_x11(Display *display, Window window);

/* One caller-owned display per thread. Call on the display's dispatch thread.
 * Initialization is asynchronous: dispatch queue (NULL = default) before
 * testing support. Never dispatches, commits, or disconnects the display.
 * All surfaces passed below must belong to this display. */
int stereo_declare_wayland_init(struct wl_display *display, struct wl_event_queue *queue);
unsigned stereo_supported_wayland(void);
/* Optional: supply a toolkit-owned content-type object before first declaration.
 * The helper sets it but never destroys it. It must outlive the declaration. */
int stereo_declare_wayland_use_content_type(struct wl_surface *surface, struct wp_content_type_v1 *content_type);
int stereo_declare_wayland(struct wl_surface *surface, enum stereo_layout layout,
                           enum stereo_class content_class, unsigned subclass);
/* Remove before destroying the wl_surface. Effective on its next commit. */
int stereo_remove_wayland(struct wl_surface *surface);
/* Call before disconnecting the display. Destroys all helper-owned proxies. */
void stereo_declare_wayland_finish(void);

#ifdef __cplusplus
}
#endif
#endif
