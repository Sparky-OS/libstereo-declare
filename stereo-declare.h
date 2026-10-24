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
#define STEREO_DECLARE_VERSION_MINOR 7
#define STEREO_DECLARE_VERSION_PATCH 0
#define STEREO_DECLARE_VERSION_ENCODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define STEREO_DECLARE_VERSION \
    STEREO_DECLARE_VERSION_ENCODE(STEREO_DECLARE_VERSION_MAJOR, STEREO_DECLARE_VERSION_MINOR, STEREO_DECLARE_VERSION_PATCH)

struct wl_display;
struct wl_surface;
struct wl_event_queue;
struct wp_content_type_v1;

/* The one stereo format: full side by side, the two eyes next to each
 * other, the left eye first, each at the resolution it has (never
 * stretched or squeezed by the program). Values of kde_stereo_content_v1
 * and of the X11 property _KDE_NET_WM_STEREO_CONTENT. */
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
 * needs version 2 or later, the size one view is shown at version 6.
 * Xlib errors use the caller's handler. */
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
/* The size one view is shown at, in display pixels, for a declared window
 * whose views have another resolution. A half top-and-bottom 1920x1080
 * video has two views of 1920x540: the program sizes its window at one
 * view's resolution, 1920x540 (the compositor makes the X11 window 3840x540
 * to hold both), and gives 1920x1080 here. The X11 window then keeps the
 * program's own size, its place on screen is one view at this size, and the
 * compositor resizes each view once, as a Wayland surface's viewport
 * destination does. Writes _KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE; needs
 * support version 6. Width and height 0 remove it; each is at most 32767.
 * Since 1.4.0. */
int stereo_declare_x11_one_view_size(Display *display, Window window, unsigned width, unsigned height);
/* Independent placement of an already listed native child (one declared with
 * stereo_declare_x11_child() or listed by the program): the rectangle it is shown
 * at, and whether below the parent's own surface, so that the ordinary window paints
 * its controls above the video. It wins over the size one view is shown at. Coordinates
 * are relative to the top-level client, in display pixels; see CHILD-PLACE.md.
 * Since 1.7.0. */
struct stereo_x11_child_place {
    int x;
    int y;
    unsigned width;
    unsigned height;
    unsigned below_parent;
};
/* Needs support version 8. A null place removes the property. */
int stereo_declare_x11_child_place(Display *display, Window window,
                                   const struct stereo_x11_child_place *place);
/* A version-7 proposal for the window's place, in the same display pixels
 * as one_view_size. The serial identifies this proposal, never zero. */
struct stereo_x11_one_view_request {
    unsigned serial;
    unsigned width;
    unsigned height;
};
/* Inspect a PropertyNotify from the caller's event loop. Returns 1 for a
 * valid proposal, 0 for an unrelated event, or a negative errno. The caller
 * selects PropertyChangeMask on its window; this call never changes masks,
 * consumes events or resizes anything. Pass NULL for event to read the
 * current proposal (0 if absent). Since 1.6.0; needs support version 7. */
int stereo_x11_one_view_event(Display *display, Window window, const XEvent *event,
                              struct stereo_x11_one_view_request *request);
/* After choosing and drawing the buffer, answer the proposal with the size
 * one view should be shown at. Writes ONE_VIEW_SIZE then sends the serial
 * acknowledgement. A superseded serial is ignored by the compositor.
 * Width and height must each be 1..32767. Since 1.6.0. */
int stereo_declare_x11_one_view_commit(Display *display, Window window,
                                      const struct stereo_x11_one_view_request *request,
                                      unsigned width, unsigned height);
/* A child window of an X11 window that holds the stereo content, such as the
 * OpenGL area of a program whose interface is drawn in 2D around it: the
 * child is declared with its own format and class, and listed in the
 * parent's _KDE_NET_WM_STEREO_CONTENT_CHILDREN, which makes the compositor
 * show the child's two views one for each eye and the rest of the window the
 * same in both eyes. The child must be inside the parent window. Needs
 * support version 5.
 * A child that holds its two views at full width, an X11 window twice as
 * wide as the place it is shown at, gives the size one view is shown at in
 * view_width and view_height: the library writes the child's
 * _KDE_NET_WM_STEREO_CONTENT_ONE_VIEW_SIZE (support version 8; -ENOTSUP when
 * the compositor is older, and nothing is written then). Zero and zero means
 * the child holds its views at its own size, and removes an earlier size.
 * A new call replaces the child's values and lists it once. Since 1.7.0. */
int stereo_declare_x11_child(Display *display, Window parent, Window child, enum stereo_layout layout,
                             enum stereo_class content_class, unsigned subclass,
                             unsigned view_width, unsigned view_height);
/* Take the child out of the parent's list and delete its declaration (the
 * format, the class, the size one view is shown at and its declared place), which gives it back to
 * the parent window. Since 1.7.0. */
int stereo_remove_x11_child(Display *display, Window parent, Window child);
/* Delete the declaration's properties: the layout, the class and the size
 * one view is shown at. */
int stereo_remove_x11(Display *display, Window window);

/* Wayland: one caller-owned display per thread. Call on the display's
 * dispatch thread. Initialization is asynchronous: dispatch the queue
 * (NULL = the default queue) before testing support. The library never
 * dispatches, commits or disconnects the display. Every surface passed
 * below must belong to this display. */
int stereo_declare_wayland_init(struct wl_display *display, struct wl_event_queue *queue);
/* 1 once the compositor's kde_stereo_content_manager_v1 is bound, else 0.
 * Since 1.4.0 the library binds version 2 of the protocol when the
 * compositor offers it: a declared surface without a viewport destination is
 * then one view wide, half its buffer's width. Since 1.5.0 it binds version 3
 * when offered, for the desktop depth calls below. */
unsigned stereo_supported_wayland(void);
/* For a surface that already has a wp_content_type_v1 of the caller's:
 * hand it over before the surface's first declaration, so the library sets
 * the kind through it instead of creating a second one (a protocol error).
 * The object stays the caller's; removal sets it back to none. */
int stereo_declare_wayland_use_content_type(struct wl_surface *surface, struct wp_content_type_v1 *content_type);
/* Declare the surface once; a new call updates the same declaration.
 * Effective with the surface's next commit. Size the surface at the size
 * one eye is shown at (a wp_viewport destination) and attach a buffer
 * holding both eyes at their own resolution: the compositor resizes them
 * once to that size. */
int stereo_declare_wayland(struct wl_surface *surface, enum stereo_layout layout,
                           enum stereo_class content_class, unsigned subclass);
/* The format only: the surface's wp_content_type_v1 is left to the program
 * (the library neither makes nor sets one). Since 1.1.0. */
int stereo_declare_wayland_format(struct wl_surface *surface, enum stereo_layout layout);
/* Remove before destroying the wl_surface. Effective on its next commit. */
int stereo_remove_wayland(struct wl_surface *surface);
/* Call before disconnecting the display. Destroys all library-owned proxies. */
void stereo_declare_wayland_finish(void);

/* Desktop depth (since 1.5.0, kde_stereo_content_manager_v1 version 3).
 * The compositor shows the desktop's own surfaces at depth levels: the
 * active window at the screen, the windows lying behind it sunk, menus and
 * popups popped. A toolkit may show depth inside a surface too, by drawing
 * its elements once per eye at their own depths and handing the two views
 * over full side by side. The compositor tells it whether the surface may
 * show depth inside it now: the surface is on a stereo output and awake (the
 * active window on top of what it overlaps, a popup, a panel, a desktop
 * widget), not one of the windows lying flat behind the active one. It also
 * says which of the surface's edges lie on its output's edge, where nothing
 * comes nearer than the screen. The depth limits are the user's settings,
 * which the toolkit reads itself. */
enum stereo_depth_edge {
    STEREO_DEPTH_EDGE_LEFT = 1,
    STEREO_DEPTH_EDGE_TOP = 2,
    STEREO_DEPTH_EDGE_RIGHT = 4,
    STEREO_DEPTH_EDGE_BOTTOM = 8
};

struct stereo_depth_state {
    unsigned awake;        /* nonzero while the surface's elements may be shown in depth */
    unsigned output_edges; /* the surface's edges on its output's edge, stereo_depth_edge flags */
};

typedef void (*stereo_depth_listener)(void *data, struct wl_surface *surface, const struct stereo_depth_state *state);

/* 1 once the compositor's kde_stereo_content_manager_v1 is bound at version
 * 3 or later, which follows surfaces' desktop depth, else 0. */
unsigned stereo_depth_supported_wayland(void);
/* Follow the surface's desktop depth: the listener is called once the
 * compositor has sent its state, and on every change, while the display's
 * queue is dispatched. Watching again replaces the listener and its data.
 * Returns -ENOTSUP when the compositor does not follow desktop depth; the
 * surface is then never awake. */
int stereo_depth_wayland_watch(struct wl_surface *surface, stereo_depth_listener listener, void *data);
/* Stop following it, before the surface is destroyed. What
 * stereo_depth_wayland_set_desktop_depth and stereo_depth_wayland_set_place
 * set for the surface is withdrawn with it. Returns -EINVAL for a surface the
 * library does not follow. */
int stereo_depth_wayland_unwatch(struct wl_surface *surface);
/* Mark the full side by side pair the surface shows as the desktop's own
 * elements drawn once per eye, not stereo content (nonzero), or withdraw the
 * mark (zero). Effective with the surface's next commit. */
int stereo_depth_wayland_set_desktop_depth(struct wl_surface *surface, int desktop_depth);
/* Declare where a popup surface (a menu, tooltip, popup or dialog) sits, as a
 * fraction of the user's limits from -1 (the sunk limit) to 1 (the popped
 * limit); values beyond are limited to that range. The compositor places it
 * there under its own rules. Effective with the surface's next commit. */
int stereo_depth_wayland_set_place(struct wl_surface *surface, double depth);
/* Withdraw the declared place. Effective with the surface's next commit. */
int stereo_depth_wayland_unset_place(struct wl_surface *surface);

#ifdef __cplusplus
}
#endif
#endif
