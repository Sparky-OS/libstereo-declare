# stereo-declare

**For programmers:** tell the desktop, with one call, that your window shows a 3D picture, and it reaches every kind of 3D screen.

Your program draws both eyes side by side at full size, the left eye first, and declares the window once.
Stereo KWin, the compositor of Sparky Stereo OS, then shows each eye to its eye: on a 3D television's own 3D modes, as anaglyph on any monitor with red-cyan glasses, on interleaved and frame-sequential displays, or in a headset.
The program never needs to know which screen it is on.

It is a small C library (MIT) for X11 and Wayland, used by the edition's mpv, VLC, Qt Multimedia, MpvQt, Kdenlive, Marble, QEMU, SPICE and others.

**Status:** version 1.1.0, a stable C API with semantic versioning; it works with Stereo KWin in Sparky Stereo OS, Daniel Ramos's edition of SparkyLinux (by Paweł "pavroo" Pijanowski). On other desktops the calls return `-ENOTSUP`, nothing is sent, and the window stays an ordinary 2D window.

## Start here

- **The API, with one example per platform:** [API.md](API.md).
- **Complete programs:** [examples/declare-wayland.c](examples/declare-wayland.c) and [examples/declare-x11.c](examples/declare-x11.c).
- **Build and install:**

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build
cmake --install build
```

Then `pkg-config --cflags --libs stereo-declare`, or in CMake `find_package(StereoDeclare 1.1 CONFIG REQUIRED)` and `StereoDeclare::stereo-declare`.
It needs Xlib, libwayland-client, wayland-scanner, wayland-protocols (for `content-type-v1`) and the `kde-stereo-content-v1.xml` protocol, which a release carries in `protocol/`.

## Where it comes from

Written by Daniel Campos Ramos with AI assistance, for Sparky Stereo OS.
The protocol it speaks, `kde-stereo-content-v1`, lives in the edition's fork of KDE's [plasma-wayland-protocols](https://invent.kde.org/libraries/plasma-wayland-protocols); the kind of content goes through `wp_content_type_v1` from [wayland-protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols) (by Emmanuel Gil Peyrot and Xaver Hugl).
The one-value shape of the declaration follows the review of Emmanuel Gil Peyrot (linkmauve) on [wayland-protocols !533](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/merge_requests/533).

Licence: MIT, see [LICENSE](LICENSE).
