# stereo-declare

A small MIT C library for KWin's stereo declaration. Full side by side,
left eye first, is the common case. Each half contains one complete eye
at the chosen render resolution; KWin selects the view for each output.

```c
#include <stereo-declare.h>

/* X11/Xwayland: display and window belong to this application. */
int result = stereo_declare_x11(display, window, STEREO_SBS_FULL,
                                STEREO_CLASS_GAME, STEREO_GAME_3D);
/* Check result == 0. Removal: stereo_remove_x11(display, window). */
```

The layout values are stable: 0 none and 3 SBS full, left eye first. The helper
declares the format only. Programs that know their content kind set it through
the existing `wp_content_type_v1` object with
`stereo_declare_wayland_use_content_type()`.

## Wayland

```c
/* Initialize once on the display's dispatch thread. */
int result = stereo_declare_wayland_init(display, NULL);
/* In a standalone client, dispatch the initial registry announcements: */
wl_display_roundtrip(display);
if (result == 0 && stereo_supported_wayland() >= 1) {
    result = stereo_declare_wayland(surface, STEREO_SBS_FULL,
                                    STEREO_CLASS_GAME, STEREO_GAME_3D);
    wl_surface_commit(surface); /* or the application's next buffer commit */
}
/* Before destroying surface: */
stereo_remove_wayland(surface);
wl_surface_commit(surface);
/* Before disconnecting display, after removing all declared surfaces: */
stereo_declare_wayland_finish();
```

Include `<wayland-client.h>` for the Wayland calls in that example.
Initialization binds globals asynchronously. In a toolkit, let its event
loop dispatch the registry instead of adding a blocking roundtrip. A custom
queue may be supplied; the application must dispatch it on the same thread
as the helper calls. The helper supports one caller-owned Wayland display
per thread. Every surface passed on that thread must belong to that display.
The display cannot be recovered from a `wl_surface` through libwayland's
public API, which is why explicit initialization is required.

The helper does not commit, dispatch or disconnect your Wayland connection.
Changes, including removal, take effect with your next surface commit.
Remove before destroying/replacing a surface; finish before disconnecting.
Repeated declarations update the same object. Declaring layout `STEREO_NONE`
clears the format; removal destroys the declaration.

Use the viewporter protocol to give a packed buffer the logical size of one
eye. The render resolution may exceed the logical size for supersampling.
Subsurfaces with ordinary controls should remain undeclared.

## Qt 6 native handles

X11 (QtGui, Xlib, stereo-declare):

```cpp
#include <QGuiApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <stereo-declare.h>

void declareStereo(QWindow &window)
{
    window.create();
    if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        stereo_declare_x11(native->display(), window.winId(), STEREO_SBS_FULL,
                           STEREO_CLASS_GAME, STEREO_GAME_3D);
    }
}
```

Wayland (Qt 6.10; getting a window's surface currently uses QtGuiPrivate):

```cpp
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformwindow_p.h>

// Once, on the GUI thread; let Qt dispatch registry events afterward:
auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
stereo_declare_wayland_init(app->display(), nullptr);

// After QWindow::create(), and after support has been announced:
auto *native = window.nativeInterface<QNativeInterface::Private::QWaylandWindow>();
if (native->surface() && stereo_supported_wayland()) {
    stereo_declare_wayland(native->surface(), STEREO_SBS_FULL,
                           STEREO_CLASS_GAME, STEREO_GAME_3D);
}
// Qt commits the surface on its next presentation.
```

Keep the declaration tied to the native surface's lifetime. Handle
`QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed` by removing it while the
surface is valid, and declare again after surface creation. Call finish before
Qt tears down its display. Qt private native interfaces require matching Qt
headers; the library itself has no Qt dependency. Check return values in
application code; the snippets omit application error reporting.

## Wine / Windows

A Windows DLL cannot call libX11 or libwayland with an HWND. The wiz3D brief's
Linux-side route is a Steam launch wrapper (`wiz3d-run %command%`): follow
the Wine process tree, identify its double-width client X11 window by
`_NET_WM_PID` or the game's `WM_CLASS`, then call `stereo_declare_x11` with
that Xwayland display and XID, and `STEREO_SBS_FULL`. Track window
creation/destruction and redeclare replacements.
Window identification belongs to the wrapper, not this library. Do not rely
on arbitrary Wine `SetProp` names being mirrored to X11, or on `wine start
/unix` launching an ELF helper; the brief specifically excludes those
assumptions. An in-process Wine integration can instead resolve its HWND to
the native client XID and call through a Linux-side bridge.

For Wine's Wayland driver, call from the native backend that owns its
`wl_display` and `wl_surface`; a surface pointer is process-local and cannot
be sent over IPC to a separate helper process. This library supplies the
shared declaration calls, not the Wine backend or window-discovery wrapper.

## Build and contract

Build this directory independently of the protocol package:

```sh
cmake -S stereo-declare -B helper-build -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build helper-build -j4
cmake --install helper-build
pkg-config --cflags --libs stereo-declare
```

Dependencies: Xlib, libwayland-client, wayland-scanner, wayland-protocols
(content-type-v1), and the adjacent `kde-stereo-content-v1.xml`.
CMake consumers can use `find_package(StereoDeclare 1 CONFIG REQUIRED)` and
`StereoDeclare::stereo-declare`. The shared-library ABI has SONAME 1.

Calls return 0 or negative errno values. Xlib errors use the application's
X error handler; enqueueing a request does not prove the server accepted it.
X11 requests are flushed. `stereo_supported_x11(display)` returns the root's
support version. The helper requires version 2. `_KDE_NET_WM_STEREO_CONTENT`
is CARDINAL/32 with one layout value. `_KDE_NET_WM_STEREO_CONTENT_CLASS`
is CARDINAL/32 with exactly two values, class then sub-class. Removing deletes
both properties. The class arguments preserve the existing ABI; Wayland clients
use the existing `wp_content_type_v1` separately when they need to identify a
content kind.
X11 updates are separate requests, not an atomic transaction.
`stereo_supported_wayland()` returns 1 when the version-1 global is present,
otherwise 0. No automatic display-mode switch is requested by these calls.

KWin's optional `testStereoDeclareHelper` links this installed package and
checks declaration/readback/change/removal in its private headless KWin and
Xwayland. Build KWin with this prefix on `CMAKE_PREFIX_PATH` to enable it.
