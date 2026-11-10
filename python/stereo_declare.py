# SPDX-License-Identifier: MIT
"""Declare full side by side windows to Stereo KWin from Python, on X11 and Xwayland.

This is the Python side of libstereo-declare (stereo-declare.h): it calls the
same library, so a program that cannot link C, such as a launcher or a game
manager, makes the declaration the library's C callers make.

A Declaration finds the game's window and declares it: the window reported by
the wrapper in its state file, or, when there is no state file, the window of
the game's own processes.  The caller drives it from its own loop:

    declaration = Declaration()
    state = window_state(path)
    declaration.update(declaration.candidates(state, descendants(pid, known)))
    ...
    declaration.close()
"""
import collections
import ctypes
import errno
from pathlib import Path

__all__ = ['StereoUnavailable', 'Declaration', 'descendants', 'window_state', 'SBS_FULL', 'CLASS_GAME',
           'GAME_GL', 'GAME_3D', 'DepthLimits', 'DepthLimitsNotOffered', 'DepthLimitsNotDelivered',
           'x11_depth_limits', 'x11_depth_limits_event', 'Wayland']

# stereo_layout, stereo_class and stereo_subclass of stereo-declare.h
SBS_FULL = 3
CLASS_GAME = 3
GAME_3D = 2
GAME_GL = 3

SONAME = 'libstereo-declare.so.1'
MINIMUM_VERSION = 2
# the support version that takes the size one view is shown at
ONE_VIEW_SIZE_VERSION = 6


# the first library version with the depth limits calls
DEPTH_LIMITS_VERSION = (1 << 16) | (10 << 8)


class StereoUnavailable(RuntimeError):
    """The desktop cannot take a declaration, or the library or the display is missing."""


class DepthLimitsNotOffered(StereoUnavailable):
    """The compositor does not send the person's depth limits (Wayland protocol under 5, X11 support under 10),
    or the library is older than 1.10.0. Draw flat or say so; never invent numbers."""


class DepthLimitsNotDelivered(RuntimeError):
    """The compositor offers the limits but has not delivered them yet: Wayland before its first event,
    X11 while the window does not declare stereo content."""


class DepthLimits(collections.namedtuple('DepthLimits', 'sunk popped')):
    """The person's limits on the output a window or surface mainly sits on, as Stereo KWin resolves them:
    how far behind (sunk) and in front of (popped) the screen anything may go, magnitudes in pixels of a view
    1920 wide; scale them to the view's width (limit * width / 1920)."""
    __slots__ = ()


class _Limits(ctypes.Structure):
    _fields_ = [('sunk', ctypes.c_double), ('popped', ctypes.c_double)]


def _limits_error(result):
    """The Python error for a negative errno result of the depth limits calls."""
    if result == -errno.ENOTSUP:
        return DepthLimitsNotOffered('the compositor does not send the depth limits')
    if result == -errno.ENODATA:
        return DepthLimitsNotDelivered('the compositor has not delivered the depth limits yet')
    return OSError(-result, 'depth limits')


def _library(lib):
    """The loaded library with the depth limits prototypes, or DepthLimitsNotOffered when it is too old."""
    if lib is None:
        try:
            lib = ctypes.CDLL(SONAME)
        except OSError as exc:
            raise StereoUnavailable(f'{SONAME} is missing') from exc
    if not hasattr(lib, 'stereo_depth_x11_get_limits') or lib.stereo_declare_version() < DEPTH_LIMITS_VERSION:
        raise DepthLimitsNotOffered('libstereo-declare 1.10.0 or later is required for the depth limits')
    if getattr(lib, '_depth_limits_ready', False):
        return lib
    lib.stereo_declare_version.restype = ctypes.c_uint
    lib.stereo_depth_limits_supported_x11.argtypes = [ctypes.c_void_p]
    lib.stereo_depth_limits_supported_x11.restype = ctypes.c_uint
    lib.stereo_depth_x11_get_limits.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(_Limits)]
    lib.stereo_depth_x11_limits_event.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.POINTER(_Limits)]
    lib.stereo_declare_wayland_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.stereo_depth_limits_supported_wayland.restype = ctypes.c_uint
    lib.stereo_depth_wayland_get_limits.argtypes = [ctypes.c_void_p, ctypes.POINTER(_Limits)]
    lib.stereo_depth_wayland_watch_limits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    lib.stereo_depth_wayland_unwatch.argtypes = [ctypes.c_void_p]
    lib._depth_limits_ready = True
    return lib


def x11_depth_limits(display, window, lib=None):
    """The person's depth limits of an X11 or Xwayland window that declares stereo content, as DepthLimits.

    display is the native Display pointer (an integer or ctypes pointer, as Declaration.native); lib is the
    loaded libstereo-declare, or None to load it. Raises DepthLimitsNotOffered (support version under 10),
    DepthLimitsNotDelivered (the window does not declare yet) or OSError."""
    lib = _library(lib)
    limits = _Limits()
    result = lib.stereo_depth_x11_get_limits(display, window, ctypes.byref(limits))
    if result:
        raise _limits_error(result)
    return DepthLimits(limits.sunk, limits.popped)


def x11_depth_limits_event(display, window, event, lib=None):
    """Inspect a PropertyNotify (a pointer to the XEvent, as the caller's own Xlib binding gives it, or None to
    read the current value): DepthLimits for a change of the window's limits, None for an unrelated event.
    Raises DepthLimitsNotDelivered when the property was deleted (the window no longer declares)."""
    lib = _library(lib)
    limits = _Limits()
    result = lib.stereo_depth_x11_limits_event(display, window, event, ctypes.byref(limits))
    if result == 1 or (result == 0 and event is None):
        return DepthLimits(limits.sunk, limits.popped)
    if result == 0:
        return None
    raise _limits_error(result)


class Wayland:
    """The person's depth limits of Wayland surfaces: libstereo-declare's Wayland side on a display the caller owns.

    display and queue are the native wl_display and wl_event_queue pointers (an integer, or None for the default
    queue); surfaces are native wl_surface pointers. The caller dispatches the queue: initialisation is
    asynchronous, the limits arrive with the compositor's events.

        wayland = Wayland(display)
        ...dispatch...
        limits = wayland.depth_limits(surface)   # DepthLimits, or an error that says why not
    """

    def __init__(self, display, queue=None, lib=None):
        self.lib = _library(lib)
        self.callbacks = {}
        result = self.lib.stereo_declare_wayland_init(display, queue)
        if result:
            raise OSError(-result, 'stereo_declare_wayland_init')
        self.open = True

    @property
    def offered(self):
        """True once the compositor's protocol is at version 5 or later; dispatch before asking."""
        return bool(self.lib.stereo_depth_limits_supported_wayland())

    def depth_limits(self, surface):
        """The limits of the output the surface mainly sits on. The first call follows the surface and raises
        DepthLimitsNotDelivered until the compositor's event is dispatched; DepthLimitsNotOffered below protocol 5."""
        limits = _Limits()
        result = self.lib.stereo_depth_wayland_get_limits(surface, ctypes.byref(limits))
        if result:
            raise _limits_error(result)
        return DepthLimits(limits.sunk, limits.popped)

    def watch(self, surface, callback):
        """Call callback(surface, DepthLimits) for each delivery of the surface's limits, while the queue is dispatched."""
        listener = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(_Limits))

        def deliver(data, wl_surface, limits):
            callback(wl_surface, DepthLimits(limits.contents.sunk, limits.contents.popped))
        entry = listener(deliver)
        result = self.lib.stereo_depth_wayland_watch_limits(surface, ctypes.cast(entry, ctypes.c_void_p), None)
        if result:
            raise _limits_error(result)
        self.callbacks[surface] = entry

    def unwatch(self, surface):
        """Stop following the surface (its limits and its depth state), before it is destroyed."""
        result = self.lib.stereo_depth_wayland_unwatch(surface)
        self.callbacks.pop(surface, None)
        if result:
            raise OSError(-result, 'stereo_depth_wayland_unwatch')

    def close(self):
        """Destroy the library's objects; call before the display is disconnected."""
        if self.open:
            self.open = False
            self.lib.stereo_declare_wayland_finish()
            self.callbacks.clear()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def descendants(root, known):
    """The process tree below root, kept in known as Wine reparents its children."""
    parents = {}
    for entry in Path('/proc').iterdir():
        if entry.name.isdigit():
            try:
                text = (entry / 'stat').read_text()
                parents[int(entry.name)] = int(text[text.rfind(')') + 2:].split()[1])
            except (OSError, ValueError, IndexError):
                pass
    # Retain known Wine children after wineserver reparents them.
    known.intersection_update(parents)
    known.add(root)
    while True:
        added = {pid for pid, parent in parents.items() if parent in known} - known
        if not added:
            return known
        known.update(added)


def window_state(path):
    """The window wiz3D reports in its state file as (width, height, executable, xid), or None."""
    try:
        lines = Path(path).read_text(encoding='utf-8').splitlines()
        magic, w, h, xid = lines[0].split()
        w, h, xid = int(w), int(h), int(xid)
        if magic == 'wiz3d-full-sbs-v1' and w > 0 and w % 2 == 0 and h > 0 and 0 <= xid <= 0xffffffff and len(lines) == 2:
            return w, h, lines[1].casefold(), xid
    except (OSError, ValueError, IndexError, UnicodeError):
        pass
    return None


class Declaration:
    """Declares windows on the default X display.

    on_declare, when given, is called with the window id each time a window is declared.
    layout, content_class and subclass are the values of stereo-declare.h; the default
    is full side by side, left eye first, of a game that wraps OpenGL or Direct3D.
    one_view_size, when given as (width, height), is the size one view is shown at, in
    display pixels, for windows whose views have another resolution
    (stereo_declare_x11_one_view_size()); it needs support version 6.
    """

    def __init__(self, layout=SBS_FULL, content_class=CLASS_GAME, subclass=GAME_GL, on_declare=None, one_view_size=None):
        try:
            from Xlib import display
            self.x = display.Display()
        except ImportError as exc:
            raise StereoUnavailable('python3-xlib is missing') from exc
        except Exception as exc:
            raise StereoUnavailable(f'Cannot open X11 display: {exc}') from exc
        self.layout, self.content_class, self.subclass = layout, content_class, subclass
        self.on_declare = on_declare
        self.one_view_size = one_view_size
        self.xlib = ctypes.CDLL('libX11.so.6')
        self.xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.xlib.XOpenDisplay.restype = ctypes.c_void_p
        self.xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.xlib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        # A client can disappear between discovery and the declaration request.
        self.error = 0
        handler_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
        def on_error(display, event):
            self.error = 1
            return 0
        self.handler = handler_type(on_error)
        self.xlib.XSetErrorHandler.argtypes = [ctypes.c_void_p]
        self.xlib.XSetErrorHandler.restype = ctypes.c_void_p
        self.previous_handler = self.xlib.XSetErrorHandler(ctypes.cast(self.handler, ctypes.c_void_p))
        self.native = self.xlib.XOpenDisplay(None)
        if not self.native:
            self.xlib.XSetErrorHandler(self.previous_handler)
            self.x.close()
            raise StereoUnavailable('Cannot open X11 display')
        try:
            self.lib = ctypes.CDLL(SONAME)
        except OSError as exc:
            self.close()
            raise StereoUnavailable(f'{SONAME} is missing') from exc
        self.lib.stereo_supported_x11.argtypes = [ctypes.c_void_p]
        self.lib.stereo_declare_x11.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
        self.lib.stereo_remove_x11.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.version = self.lib.stereo_supported_x11(self.native)
        if self.version < MINIMUM_VERSION:
            self.close()
            raise StereoUnavailable(f'KWin stereo declaration support version {MINIMUM_VERSION} is required')
        if one_view_size is not None:
            if self.version < ONE_VIEW_SIZE_VERSION:
                self.close()
                raise StereoUnavailable(f'KWin stereo declaration support version {ONE_VIEW_SIZE_VERSION} is required for the size one view is shown at')
            self.lib.stereo_declare_x11_one_view_size.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
        self.pid_atom = self.x.intern_atom('_NET_WM_PID')
        self.active = set()
        self.declared_any = False

    def candidates(self, state, pids, steam_class=None):
        from Xlib import X, Xatom, error
        if state is None:
            return set()
        width, height, exe, xid = state
        result = set()
        stack = [self.x.create_resource_object('window', xid)] if xid else [self.x.screen().root]
        while stack:
            window = stack.pop()
            try:
                if not xid:
                    stack.extend(window.query_tree().children)
                geom = window.get_geometry()
                if (not xid and (geom.width, geom.height) != (width, height)) or window.get_attributes().map_state != X.IsViewable:
                    continue
                classes = {s.casefold() for s in (window.get_wm_class() or ())}
                pid = window.get_full_property(self.pid_atom, Xatom.CARDINAL)
                child = pid is not None and pid.format == 32 and len(pid.value) == 1 and int(pid.value[0]) in pids
                steam = bool(steam_class and steam_class.casefold() in classes)
                identity = window.id == xid if xid else (child or steam)
                if identity and (exe in classes or steam):
                    result.add(window.id)
            except error.XError:
                continue
        # Without a native XID, ambiguous same-game windows stay undeclared.
        return result if len(result) == 1 else set()

    def windows_of(self, pids):
        """The one viewable top-level window of the given processes, as a set; several count as none.

        For a game that draws full side by side itself, so there is no state file to say which window it is."""
        from Xlib import X, Xatom, error
        result = set()
        for window in self.x.screen().root.query_tree().children:
            try:
                if window.get_attributes().map_state != X.IsViewable:
                    continue
                pid = window.get_full_property(self.pid_atom, Xatom.CARDINAL)
                if pid is not None and pid.format == 32 and len(pid.value) == 1 and int(pid.value[0]) in pids:
                    result.add(window.id)
            except error.XError:
                continue
        return result if len(result) == 1 else set()

    def depth_limits(self, window):
        """The person's depth limits of a declared window (x11_depth_limits()); raises DepthLimitsNotOffered,
        DepthLimitsNotDelivered or OSError."""
        return x11_depth_limits(self.native, window, self.lib)

    def update(self, wanted):
        for wid in self.active - wanted:
            self.lib.stereo_remove_x11(self.native, wid)
        self.xlib.XSync(self.native, 0)
        self.active.intersection_update(wanted)
        for wid in wanted - self.active:
            self.error = 0
            result = self.lib.stereo_declare_x11(self.native, wid, self.layout, self.content_class, self.subclass)
            if result == 0 and self.one_view_size is not None:
                result = self.lib.stereo_declare_x11_one_view_size(self.native, wid, *self.one_view_size)
            self.xlib.XSync(self.native, 0)
            if result == 0 and not self.error:
                self.active.add(wid)
                self.declared_any = True
                if self.on_declare:
                    self.on_declare(wid)
        self.xlib.XSync(self.native, 0)

    def close(self):
        if getattr(self, 'active', None) is not None and getattr(self, 'lib', None) is not None:
            self.update(set())
        self.xlib.XCloseDisplay(self.native)
        self.xlib.XSetErrorHandler(self.previous_handler)
        self.x.close()
