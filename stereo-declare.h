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
#define STEREO_DECLARE_VERSION_MINOR 12
#define STEREO_DECLARE_VERSION_PATCH 2
#define STEREO_DECLARE_VERSION_ENCODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define STEREO_DECLARE_VERSION \
    STEREO_DECLARE_VERSION_ENCODE(STEREO_DECLARE_VERSION_MAJOR, STEREO_DECLARE_VERSION_MINOR, STEREO_DECLARE_VERSION_PATCH)

struct wl_display;
struct wl_surface;
struct wl_event_queue;
struct wp_content_type_v1;

/* The one stereo format: full side by side, the two views next to each
 * other, the left view first, each at the resolution it has (never
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
/* Move only the child's membership between ordinary owners, without reading or
 * changing any property on the child. None attaches/detaches one end; equal
 * parents ensure one membership. Unrelated children and their order stay intact.
 * Adding needs support version 5; removal also works after support disappears.
 * Parents must be live X windows. The caller owns actual parenting, clipping,
 * content identity and destruction. Malformed lists return -EPROTO unchanged.
 * The list transaction excludes other clients; use XInitThreads when sharing
 * this Display across threads. Since 1.8.0. */
int stereo_move_x11_child(Display *display, Window old_parent, Window new_parent, Window child);

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
 * when offered, for the desktop depth calls below. Since 1.9.0 it binds version 4
 * when offered, for native per-view source geometry. Since 1.10.0 it binds version 5
 * when offered, for the person's depth limits; it never binds above the version it
 * knows, so a newer compositor is spoken to in the version this library was built for. */
unsigned stereo_supported_wayland(void);
/* For a surface that already has a wp_content_type_v1 of the caller's:
 * hand it over before the surface's first declaration, so the library sets
 * the kind through it instead of creating a second one (a protocol error).
 * The object stays the caller's; removal sets it back to none. */
int stereo_declare_wayland_use_content_type(struct wl_surface *surface, struct wp_content_type_v1 *content_type);
/* Declare the surface once; a new call updates the same declaration.
 * Effective with the surface's next commit. Size the surface at the size
 * one view is shown at (a wp_viewport destination) and attach a buffer
 * holding both views at their own resolution: the compositor resizes them
 * once to that size. */
int stereo_declare_wayland(struct wl_surface *surface, enum stereo_layout layout,
                           enum stereo_class content_class, unsigned subclass);
/* The format only: the surface's wp_content_type_v1 is left to the program
 * (the library neither makes nor sets one). Since 1.1.0. */
int stereo_declare_wayland_format(struct wl_surface *surface, enum stereo_layout layout);
/* Remove before destroying the wl_surface. Effective on its next commit. */
int stereo_remove_wayland(struct wl_surface *surface);
/* Nonzero when native per-view source geometry is supported (protocol 4).
 * Since 1.9.0. */
unsigned stereo_view_source_supported_wayland(void);
/* Select the same native source rectangle independently in each view of an
 * existing declaration, without resizing samples or committing the surface.
 * Coordinates use the protocol's 24.8 fixed-point precision. Set all four
 * values to -1 to unset the rectangle. Mono and removal also clear it.
 * Returns -ENOTSUP without protocol 4, -ENOENT without a declaration, or
 * -EINVAL for invalid/unrepresentable coordinates. The compositor checks
 * source bounds when the next buffer is committed. Since 1.9.0. */
int stereo_declare_wayland_view_source(struct wl_surface *surface,
                                      double x, double y, double width, double height);
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
 * comes nearer than the screen.
 *
 * How far the elements may go, the depth limits, are the person's: Stereo KWin
 * resolves them per output, from the person's sunk and popped limits built on the
 * output's display (the display's kind and physical size, libstereo-policy), and
 * hands them to the program, on Wayland as kde_stereo_depth_v1.depth_limits
 * (protocol version 5) and on X11 as the property _KDE_NET_WM_STEREO_DEPTH_LIMITS
 * of a window that declares stereo content (support version 10). The calls near
 * the end of this section hand them over; a program reads them before inventing
 * numbers of its own, never reads the compositor's settings itself and never
 * computes limits itself. */
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

/* The person's depth limits on the output a surface or window mainly sits on,
 * as Stereo KWin resolves them (since 1.10.0): how far behind the screen (sunk)
 * and in front of it (popped) anything may go, as the compositor applies them to
 * its own surfaces there, so an element drawn at the popped limit and a menu the
 * compositor pops lie at the same depth on the same screen. Both are magnitudes
 * in pixels of a view 1920 wide, as the protocol carries them (24.8 fixed
 * point). They are tailored to the screen, never to the size of a window, area, picture or
 * buffer: a program gets them in its buffer's pixels from stereo_depth_limits_for_buffer() (1.11.0).
 * The names are the compositor's, the browser's and the boot splash's.
 *
 * Every call returns 0 with the limits filled, or a negative errno value that
 * leaves the structure untouched, so no zeros that look like limits ever come
 * back:
 *   -ENOTSUP  the compositor does not offer them (Wayland: the manager the
 *             compositor offers is older than version 5, or none; X11: the
 *             support version is under 10). Show the depth the program would
 *             have without limits, say so, or draw flat; do not guess numbers.
 *   -ENODATA  offered, not delivered yet: Wayland before the compositor's first
 *             event (dispatch the queue and ask again, or use the listener);
 *             X11 while the window does not declare stereo content, or
 *             before the compositor has written them (a window that stops
 *             declaring loses them again).
 *   -EPROTO   (X11) the property is there but is not two CARDINAL values.
 *   -EINVAL   a null argument; (Wayland) a surface not of this display.
 * Read them live: each change reaches the Wayland listener, and an X11 program
 * selects PropertyChangeMask on its window and passes the events to
 * stereo_depth_x11_limits_event(). */
struct stereo_depth_limits {
    double sunk;   /* how far behind the screen, pixels of a view 1920 wide */
    double popped; /* how far in front of the screen, pixels of a view 1920 wide */
};

typedef void (*stereo_depth_limits_listener)(void *data, struct wl_surface *surface,
                                             const struct stereo_depth_limits *limits);

/* 1 once the compositor's kde_stereo_content_manager_v1 is bound at version 5
 * or later, which sends the depth limits, else 0. Since 1.10.0. */
unsigned stereo_depth_limits_supported_wayland(void);
/* The limits of the output the surface mainly sits on. The first call for a
 * surface starts following its depth (the same object as
 * stereo_depth_wayland_watch) and returns -ENODATA until the compositor's event
 * has been dispatched. Since 1.10.0. */
int stereo_depth_wayland_get_limits(struct wl_surface *surface, struct stereo_depth_limits *limits);
/* Call the listener each time the compositor sends the surface's limits: when
 * they are first known, when the person changes them, when the output's mode or
 * kind changes and when the surface moves to another output. It is not called for
 * limits already received; read those with stereo_depth_wayland_get_limits()
 * right after. Watching again replaces the listener and its data. Needs
 * protocol version 5: -ENOTSUP otherwise, and the listener is never called.
 * stereo_depth_wayland_unwatch() ends it with the rest. Since 1.10.0. */
int stereo_depth_wayland_watch_limits(struct wl_surface *surface, stereo_depth_limits_listener listener, void *data);

/* 1 when the compositor announces support version 10 or later
 * (_KDE_NET_WM_STEREO_CONTENT_SUPPORTED), which writes the property below, else
 * 0. Since 1.10.0. */
unsigned stereo_depth_limits_supported_x11(Display *display);
/* Read _KDE_NET_WM_STEREO_DEPTH_LIMITS of a window that declares stereo content
 * (24.8 fixed point, two CARDINAL values, sunk then popped). The same two numbers
 * as the Wayland listener, for an X11 or Xwayland window. Xlib errors use the
 * caller's handler. Since 1.10.0. */
int stereo_depth_x11_get_limits(Display *display, Window window, struct stereo_depth_limits *limits);
/* Inspect a PropertyNotify from the caller's event loop, as
 * stereo_x11_one_view_event() does: 1 for a changed property with valid limits
 * filled in, 0 for an unrelated event, -ENODATA when the property was deleted
 * (the window no longer declares), or another negative errno as above. Pass NULL
 * for event to read the current value (0 when valid). The caller selects
 * PropertyChangeMask on its window; this call never changes masks or consumes
 * events. Since 1.10.0. */
int stereo_depth_x11_limits_event(Display *display, Window window, const XEvent *event,
                                  struct stereo_depth_limits *limits);

/* The person's depth limits for one program's buffer (since 1.11.0): the one
 * conversion every program that draws its own stereo pair needs, in one place.
 *
 * The limits above are pixels of a view 1920 wide of the screen the window is
 * on, and the compositor applies lround(limit * output width / 1920) there. A
 * program draws into a buffer that the compositor then shows at some width, so
 * the same limit in the pixels of that buffer is
 *     limit * output_width / 1920 * buffer_width / shown_width
 * with output_width the width in pixels of the output the window or surface is
 * on (never the whole layout, never the primary output), buffer_width the width
 * of one eye's buffer and shown_width the width the compositor shows that eye
 * at on the output (the same as buffer_width when the program renders at the
 * size it is shown). Nothing else enters: not the window's, the 3D area's, the
 * picture's or the icon's own size. This adds no rule of its own.
 *
 * -EINVAL for a null argument or a width that is not positive and finite; the
 * structure is left untouched. */
int stereo_depth_limits_for_buffer(const struct stereo_depth_limits *screen_limits, double output_width,
                                   double buffer_width, double shown_width, struct stereo_depth_limits *buffer_limits);
/* The width in pixels of the output a window mainly sits on: the RandR monitor
 * (XRRGetMonitors, so Xwayland's outputs too) that the window overlaps most.
 * -ENOTSUP without RandR 1.5, -ENODATA when the window overlaps no monitor or is
 * not mapped, -EINVAL for a null argument. */
int stereo_depth_x11_output_width(Display *display, Window window, double *output_width);
/* stereo_depth_x11_get_limits() followed by stereo_depth_limits_for_buffer() with
 * the window's output; the errors of both, untouched structure on any. */
int stereo_depth_x11_get_buffer_limits(Display *display, Window window, double buffer_width, double shown_width,
                                       struct stereo_depth_limits *buffer_limits);
/* The same on Wayland. The library learns the width of an output from the
 * wl_output globals (current mode); which output a toolkit's surface entered is
 * the toolkit's, so with more than one output this returns -ENOTUNIQ and the
 * caller gives the width of its own screen to stereo_depth_limits_for_buffer()
 * (Qt: QWindow::screen()). With one output it is that output's. -ENODATA until the
 * output's mode has arrived. Needs stereo_declare_wayland_init(). */
int stereo_depth_wayland_output_width(double *output_width);
int stereo_depth_wayland_get_buffer_limits(struct wl_surface *surface, double buffer_width, double shown_width,
                                           struct stereo_depth_limits *buffer_limits);

#ifdef __cplusplus
}
#endif
#endif
