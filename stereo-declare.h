/* SPDX-License-Identifier: MIT */
#ifndef STEREO_DECLARE_H
#define STEREO_DECLARE_H

#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semantic versioning: the same major version keeps every call, value and
 * meaning below; a minor version only adds. STEREO_DECLARE_VERSION is the
 * header's version, stereo_declare_version() the loaded library's. */
#define STEREO_DECLARE_VERSION_MAJOR 1
#define STEREO_DECLARE_VERSION_MINOR 2
#define STEREO_DECLARE_VERSION_PATCH 0
#define STEREO_DECLARE_VERSION_ENCODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define STEREO_DECLARE_VERSION \
    STEREO_DECLARE_VERSION_ENCODE(STEREO_DECLARE_VERSION_MAJOR, STEREO_DECLARE_VERSION_MINOR, STEREO_DECLARE_VERSION_PATCH)

struct wl_display;
struct wl_surface;
struct wl_event_queue;
struct wp_content_type_v1;

/* The one stereo format: full side by side, both eyes at full size, the
 * left eye first. Values of kde_stereo_content_v1 and of the X11 property
 * _KDE_NET_WM_STEREO_CONTENT. */
enum stereo_layout {
    STEREO_NONE = 0,
    STEREO_SBS_FULL = 3
};

/* The kind of content. On Wayland it is the surface's wp_content_type_v1
 * (none, photo, video and game have that protocol's values; scientific is
 * sent as none). On X11 it is the first value of the property
 * _KDE_NET_WM_STEREO_CONTENT_CLASS. */
enum stereo_class {
    STEREO_CLASS_NONE = 0,
    STEREO_CLASS_PHOTO = 1,
    STEREO_CLASS_VIDEO = 2,
    STEREO_CLASS_GAME = 3,
    STEREO_CLASS_SCIENTIFIC = 4
};

/* Sub-class within a class, 0 when unspecified. Only the X11 property
 * carries it (its second value); Wayland has no place for it. */
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

/* Every call returns 0 on success or a negative errno value. */

/* The loaded library's version, as STEREO_DECLARE_VERSION_ENCODE gives it.
 * Since 1.1.0. */
unsigned stereo_declare_version(void);

/* X11 (and Xwayland): the support version announced on the root window by
 * _KDE_NET_WM_STEREO_CONTENT_SUPPORTED, or zero when absent. Declaring
 * needs version 2 or later. Xlib errors use the caller's handler. */
unsigned stereo_supported_x11(Display *display);
/* Declare the window once, before or after mapping it; a new call replaces
 * the values. Size the window at one eye's size: from support version 3 the
 * compositor makes the X11 window hold both eyes. Requests are flushed;
 * the two properties are separate requests. */
int stereo_declare_x11(Display *display, Window window, enum stereo_layout layout,
                       enum stereo_class content_class, unsigned subclass);
/* The format only: writes _KDE_NET_WM_STEREO_CONTENT and leaves the class
 * property as it is (the program's own, or none). Needs support version 1
 * or later. Declaring STEREO_NONE clears the format. Since 1.1.0. */
int stereo_declare_x11_format(Display *display, Window window, enum stereo_layout layout);
/* Delete both properties. */
int stereo_remove_x11(Display *display, Window window);

/* Wayland: one caller-owned display per thread. Call on the display's
 * dispatch thread. Initialization is asynchronous: dispatch the queue
 * (NULL = the default queue) before testing support. The library never
 * dispatches, commits or disconnects the display. Every surface passed
 * below must belong to this display. */
int stereo_declare_wayland_init(struct wl_display *display, struct wl_event_queue *queue);
/* 1 once the compositor's kde_stereo_content_manager_v1 is bound, else 0. */
unsigned stereo_supported_wayland(void);
/* For a surface that already has a wp_content_type_v1 of the caller's:
 * hand it over before the surface's first declaration, so the library sets
 * the kind through it instead of creating a second one (a protocol error).
 * The object stays the caller's; removal sets it back to none. */
int stereo_declare_wayland_use_content_type(struct wl_surface *surface, struct wp_content_type_v1 *content_type);
/* Declare the surface once; a new call updates the same declaration.
 * Effective with the surface's next commit. Size the surface at one eye's
 * size (wp_viewporter) and attach a buffer holding both eyes. */
int stereo_declare_wayland(struct wl_surface *surface, enum stereo_layout layout,
                           enum stereo_class content_class, unsigned subclass);
/* The format only: the surface's wp_content_type_v1 is left to the program
 * (the library neither makes nor sets one). Since 1.1.0. */
int stereo_declare_wayland_format(struct wl_surface *surface, enum stereo_layout layout);
/* Remove before destroying the wl_surface. Effective on its next commit. */
int stereo_remove_wayland(struct wl_surface *surface);
/* Call before disconnecting the display. Destroys all library-owned proxies. */
void stereo_declare_wayland_finish(void);

#ifdef __cplusplus
}
#endif
#endif
