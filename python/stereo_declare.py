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
import ctypes
from pathlib import Path

__all__ = ['StereoUnavailable', 'Declaration', 'descendants', 'window_state', 'SBS_FULL', 'CLASS_GAME',
           'GAME_GL', 'GAME_3D']

# stereo_layout, stereo_class and stereo_subclass of stereo-declare.h
SBS_FULL = 3
CLASS_GAME = 3
GAME_3D = 2
GAME_GL = 3

SONAME = 'libstereo-declare.so.1'
MINIMUM_VERSION = 2


class StereoUnavailable(RuntimeError):
    """The desktop cannot take a declaration, or the library or the display is missing."""


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
    """

    def __init__(self, layout=SBS_FULL, content_class=CLASS_GAME, subclass=GAME_GL, on_declare=None):
        try:
            from Xlib import display
            self.x = display.Display()
        except ImportError as exc:
            raise StereoUnavailable('python3-xlib is missing') from exc
        except Exception as exc:
            raise StereoUnavailable(f'Cannot open X11 display: {exc}') from exc
        self.layout, self.content_class, self.subclass = layout, content_class, subclass
        self.on_declare = on_declare
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

    def update(self, wanted):
        for wid in self.active - wanted:
            self.lib.stereo_remove_x11(self.native, wid)
        self.xlib.XSync(self.native, 0)
        self.active.intersection_update(wanted)
        for wid in wanted - self.active:
            self.error = 0
            result = self.lib.stereo_declare_x11(self.native, wid, self.layout, self.content_class, self.subclass)
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
