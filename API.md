# stereo-declare API, version 1.1

**For programmers:** how a program declares a 3D window or surface once, on X11 and on Wayland.

## What a declaration says

One thing: "this window's picture holds both eyes, full side by side, the left eye first".
Full side by side means each half holds one eye's complete picture at full size, at whatever resolution the program renders: a 1920x1080 view per eye makes a 3840x1080 picture.
It is HDMI 1.4b's 3D structure "Side-by-Side (Full)" (3D_Structure 0011, the same value 3 that the declaration uses), handed to the compositor instead of a cable.
A program that reads or makes stereo in any other packing converts it to this one; the compositor then turns it into what each screen needs.

A program declares only the part that is 3D: the video, the viewport, the canvas.
Menus, toolbars and on-screen displays stay an ordinary 2D surface, declared nothing.

The kind of content (photo, video, game) travels separately: on Wayland as the surface's `wp_content_type_v1`, the standard protocol for it, and on X11 as a second window property.

## Versions

- **Semantic versioning.** Within major version 1, every call, value and meaning on this page stays; a minor version only adds. The shared library's SONAME is `libstereo-declare.so.1`, so programs built against 1.0 run unchanged on 1.1.
- **In code:** `STEREO_DECLARE_VERSION_MAJOR`, `_MINOR` and `_PATCH` give the header's version, and `STEREO_DECLARE_VERSION` packs them as `(major << 16) | (minor << 8) | patch`. `stereo_declare_version()` returns the loaded library's version in the same form (since 1.1.0).
- **CMake:** `find_package(StereoDeclare 1.1 CONFIG REQUIRED)` accepts any 1.x from 1.1 on.
- **pkg-config:** `pkg-config --atleast-version=1.1 stereo-declare`.

## Calls

Every call returns 0 or a negative `errno` value (`-EINVAL` for a bad argument, `-ENOTSUP` when the desktop does not take declarations, `-ENOMEM`, `-EBUSY`).

| Call | Since | What it does |
|---|---|---|
| `stereo_declare_version()` | 1.1.0 | The loaded library's version |
| `stereo_supported_x11(display)` | 1.0.0 | The version the window manager announces on the root window, 0 when none |
| `stereo_declare_x11(display, window, layout, class, subclass)` | 1.0.0 | Declare an X11 window; a new call replaces the values |
| `stereo_declare_x11_format(display, window, layout)` | 1.1.0 | Declare the format only; the class property stays the program's |
| `stereo_remove_x11(display, window)` | 1.0.0 | Remove the declaration (both properties) |
| `stereo_declare_wayland_init(display, queue)` | 1.0.0 | Start, once per thread, on the display's dispatch thread |
| `stereo_supported_wayland()` | 1.0.0 | 1 once the compositor's declaration global is bound, else 0 |
| `stereo_declare_wayland_use_content_type(surface, content_type)` | 1.0.0 | Hand over the surface's existing `wp_content_type_v1` |
| `stereo_declare_wayland(surface, layout, class, subclass)` | 1.0.0 | Declare a surface; a new call updates the same declaration |
| `stereo_declare_wayland_format(surface, layout)` | 1.1.0 | Declare the format only; the surface's content type stays the program's |
| `stereo_remove_wayland(surface)` | 1.0.0 | Remove the declaration, before the surface is destroyed |
| `stereo_declare_wayland_finish()` | 1.0.0 | Destroy everything the library made, before disconnecting |

## Values

| Argument | Values | Where it goes |
|---|---|---|
| `layout` | `STEREO_SBS_FULL` (3), or `STEREO_NONE` (0) to clear it | `kde_stereo_content_v1.set_content`; X11 `_KDE_NET_WM_STEREO_CONTENT` |
| `class`, the kind | `STEREO_CLASS_NONE` 0, `_PHOTO` 1, `_VIDEO` 2, `_GAME` 3, `_SCIENTIFIC` 4 | Wayland: `wp_content_type_v1.set_content_type` with the same value (scientific is sent as none). X11: the first value of `_KDE_NET_WM_STEREO_CONTENT_CLASS` |
| `subclass` | 0 unspecified; game: `STEREO_GAME_VR` 1, `_3D` 2, `_GL` 3, `_STEREOGL` 4; video: `STEREO_VIDEO_LEGACY` 1, `_CURRENT` 2; scientific: `STEREO_SCIENTIFIC_VR` 1, `_STEREOGL` 2 | X11 only, the second value of `_KDE_NET_WM_STEREO_CONTENT_CLASS`; Wayland has no place for it |

Any other layout is refused with `-EINVAL`, and nothing is sent; class and sub-class must fit in 8 bits.

## The format only

The two calls with a kind (`stereo_declare_x11()`, `stereo_declare_wayland()`) also set the kind of content.
A program, layer or wrapper that has no kind to give, or that sets the kind itself, declares the format only:

- `stereo_declare_wayland_format(surface, STEREO_SBS_FULL)` sends the one value and never makes or touches the surface's `wp_content_type_v1`, so a program that has its own keeps it (and needs no hand-over).
- `stereo_declare_x11_format(display, window, STEREO_SBS_FULL)` writes `_KDE_NET_WM_STEREO_CONTENT` only, and needs support version 1; a class property the program wrote stays as it is. The gl-stereo layer, which makes a stereo pair of an OpenGL program's frames, is written for this call.
- Declaring `STEREO_NONE` with them clears the format and keeps the rest.

## Wayland example

The complete program is [examples/declare-wayland.c](examples/declare-wayland.c); this is its core.

```c
#include <stereo-declare.h>
#include <wayland-client.h>

/* Once, on the thread that dispatches the display; the library binds its
 * globals on the given queue (NULL: the default one). */
stereo_declare_wayland_init(display, NULL);
wl_display_roundtrip(display); /* a toolkit's own event loop does this instead */

if (stereo_supported_wayland()) {
    /* Declare once, before the first buffer: full side by side, a game. */
    stereo_declare_wayland(surface, STEREO_SBS_FULL, STEREO_CLASS_GAME, STEREO_SUBCLASS_UNSPECIFIED);
}
/* The surface keeps one eye's size; its buffer holds both eyes. */
wp_viewport_set_destination(viewport, eye_width, eye_height);
wl_surface_attach(surface, both_eyes_buffer, 0, 0); /* 2 * eye_width wide */
wl_surface_commit(surface);                         /* the declaration takes effect here */

/* Before destroying the surface, and before disconnecting: */
stereo_remove_wayland(surface);
wl_surface_commit(surface);
stereo_declare_wayland_finish();
```

Rules:
- **One display per thread.** The library's state is per thread: initialise and declare on the thread that dispatches the display, with surfaces of that display. A custom queue must be dispatched on that thread too.
- **Initialisation is asynchronous.** Nothing is known until the registry's events have been dispatched; a standalone program does one roundtrip, a toolkit lets its event loop run.
- **Everything takes effect with the surface's next commit,** which stays the program's: the library never commits, dispatches or disconnects.
- **A surface that already has a `wp_content_type_v1`** (mpv makes one for its window, for example) is declared with `stereo_declare_wayland_format()`, or hands its object over with `stereo_declare_wayland_use_content_type()` before its first `stereo_declare_wayland()`. Otherwise the library makes a second one, and the protocol answers with an `already_constructed` error that ends the connection. A handed-over object stays the program's; removal sets it back to none.
- **The 3D area of an ordinary window** is a subsurface of its own, placed below the window's surface, sized with `wp_viewporter` and declared; the window stays 2D and transparent over the area. Sparky Stereo OS's [stereo-window-3d-area](https://github.com/Sparky-OS/sparky-stereo-os/blob/main/skills/stereo-window-3d-area/SKILL.md) skill shows the pattern in Kdenlive, Marble, Krita and MpvQt.

## X11 example

The complete program is [examples/declare-x11.c](examples/declare-x11.c); this is its core.

```c
#include <stereo-declare.h>

const unsigned supported = stereo_supported_x11(display);
if (supported >= 2) {
    /* From version 3 the window manager makes the window hold both eyes:
     * create it at one eye's size. */
    const int width = supported >= 3 ? eye_width : 2 * eye_width;
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, width, eye_height, 0, 0, 0);
    /* Declare once, before mapping: full side by side, a video. */
    stereo_declare_x11(display, window, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, STEREO_SUBCLASS_UNSPECIFIED);
    XMapWindow(display, window);
    /* Draw the left eye in the left half of the window's current width,
     * the right eye in the right half. */
}
/* Removal deletes both properties: */
stereo_remove_x11(display, window);
```

Rules:
- **The properties:** `_KDE_NET_WM_STEREO_CONTENT` is CARDINAL/32 with one value (0 or 3); `_KDE_NET_WM_STEREO_CONTENT_CLASS` is CARDINAL/32 with two values, class then sub-class. They are written as two requests and flushed, not as one transaction.
- **The root window's `_KDE_NET_WM_STEREO_CONTENT_SUPPORTED`** gives the version the window manager understands: 1 the layout, 2 also the class and sub-class, 3 also sizing (the program's own sizes are one eye's, and the window manager makes the X11 window hold both). `stereo_declare_x11()` declares from version 2 on, `stereo_declare_x11_format()` from version 1.
- **Xlib errors** go to the program's own error handler; a flushed request is not proof that the server accepted it.
- **Under Xwayland** a declared child window becomes a surface of its own in Sparky Stereo OS's Xwayland, so the 3D area of an X11 program works the same way as on Wayland.

## Toolkits

**Qt 6 on X11** (QtGui, Xlib):

```cpp
#include <QGuiApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <stereo-declare.h>

void declareStereo(QWindow &window)
{
    window.create();
    if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        stereo_declare_x11(native->display(), window.winId(), STEREO_SBS_FULL, STEREO_CLASS_GAME, STEREO_GAME_3D);
    }
}
```

**Qt 6 on Wayland** (reaching a window's surface needs QtGuiPrivate today):

```cpp
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformwindow_p.h>

// Once, on the GUI thread; Qt's event loop dispatches the registry afterwards:
auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
stereo_declare_wayland_init(app->display(), nullptr);

// After QWindow::create(), once support has been announced:
auto *native = window.nativeInterface<QNativeInterface::Private::QWaylandWindow>();
if (native->surface() && stereo_supported_wayland()) {
    stereo_declare_wayland(native->surface(), STEREO_SBS_FULL, STEREO_CLASS_GAME, STEREO_GAME_3D);
}
// Qt commits the surface on its next frame.
```

Tie the declaration to the native surface's lifetime: on `QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed` remove it while the surface is valid, declare again after the surface is created, and call `stereo_declare_wayland_finish()` before Qt closes its display.
The library itself has no Qt dependency.

**Wine and Windows programs:** a Windows DLL cannot call Xlib or libwayland with an HWND.
A launcher on the Linux side finds the program's X11 window (by `_NET_WM_PID` or `WM_CLASS`), and calls `stereo_declare_x11()` with the Xwayland display and that window, following windows as they are created and replaced.
For Wine's Wayland driver, the call belongs in the native backend that owns the `wl_display` and `wl_surface`: a surface pointer cannot cross to another process.

## Building against it

- **pkg-config:** `pkg-config --cflags --libs stereo-declare` (it brings Xlib's flags, since the header includes `<X11/Xlib.h>`).
- **CMake:** `find_package(StereoDeclare 1.1 CONFIG REQUIRED)`, then link `StereoDeclare::stereo-declare`.
- **Meson:** `dependency('stereo-declare', version: '>=1.1')`.
- **Optional use:** programs that must also build without it test for the package at configure time and keep their 2D path, as the edition's mpv (Meson) and VLC (autoconf) do.

## Specifications

- `kde-stereo-content-v1`: [the protocol file](https://github.com/Sparky-OS/plasma-wayland-protocols/blob/stereo3d/src/protocols/kde-stereo-content-v1.xml), values none 0 and side_by_side_full 3.
- `wp_content_type_v1`: [wayland-protocols, staging/content-type](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/tree/main/staging/content-type).
- `wp_viewporter`: [wayland-protocols, stable/viewporter](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/tree/main/stable/viewporter).
- The packing: HDMI Specification 1.4b, the 3D_Structure field of the HDMI Vendor-Specific InfoFrame, "Side-by-Side (Full)".
