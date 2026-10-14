#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The Python module on a plain X server (Xvfb): the support check, window discovery, the properties, read back."""
import os
from pathlib import Path
import sys
import tempfile
from Xlib import X, Xatom, display

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import stereo_declare

x = display.Display()
root = x.screen().root
support = x.intern_atom('_KDE_NET_WM_STEREO_CONTENT_SUPPORTED')
layout = x.intern_atom('_KDE_NET_WM_STEREO_CONTENT')
kind = x.intern_atom('_KDE_NET_WM_STEREO_CONTENT_CLASS')
failures = 0


def check(condition, text):
    global failures
    if condition:
        print('PASS:', text)
    else:
        print('FAIL:', text)
        failures += 1


def read(w, atom):
    p = w.get_full_property(atom, X.AnyPropertyType)
    if p is None:
        return None
    return list(p.value) if p.property_type == Xatom.CARDINAL and p.format == 32 else ['wrong type']


def window(width=1600, pid=os.getpid(), name='test.exe'):
    w = root.create_window(0, 0, width, 600, 0, x.screen().root_depth, X.InputOutput, X.CopyFromParent)
    w.set_wm_class(name, name)
    w.change_property(x.intern_atom('_NET_WM_PID'), Xatom.CARDINAL, 32, [pid])
    w.map()
    x.sync()
    return w


root.delete_property(support)
x.sync()
try:
    stereo_declare.Declaration()
except stereo_declare.StereoUnavailable:
    check(True, 'a compositor without declaration support is rejected')
else:
    check(False, 'a compositor without declaration support is rejected')
root.change_property(support, Xatom.CARDINAL, 32, [1])
x.sync()
try:
    stereo_declare.Declaration()
except stereo_declare.StereoUnavailable:
    check(True, 'support version 1 is rejected')
else:
    check(False, 'support version 1 is rejected')
for version in (2, 3):
    root.change_property(support, Xatom.CARDINAL, 32, [version])
    x.sync()
    d = stereo_declare.Declaration()
    check(d.version == version, f'support version {version} read from the root')
    d.close()

declared = []
d = stereo_declare.Declaration(on_declare=declared.append)
valid = window()
wrong_size = window(width=800)
wrong_pid = window(pid=2147483647)
wrong_class = window(name='other.exe')
state = (1600, 600, 'test.exe', 0)
check(d.candidates(state, {os.getpid()}) == {valid.id}, 'class, process and geometry pick the one game window')
check(d.candidates(None, {os.getpid()}) == set(), 'no state, no window')
d.update({valid.id})
check(read(valid, layout) == [3] and read(valid, kind) == [3, 3], 'full side by side, game, GL wrapper written to the window')
check(all(read(w, layout) is None for w in (wrong_size, wrong_pid, wrong_class)), 'no other window declared')
check(declared == [valid.id] and d.declared_any, 'the caller is told about the declaration once')
d.update({valid.id})
check(declared == [valid.id], 'declaring the same window again is not announced again')
valid.destroy()
x.sync()
replacement = window()
d.update(d.candidates((1600, 600, 'test.exe', replacement.id), set()))
check(read(replacement, layout) == [3], 'a replacement window is declared')
replacement.configure(width=1200)
x.sync()
check(d.candidates((1600, 600, 'test.exe', replacement.id), set()) == {replacement.id}, 'a known window keeps its declaration when resized')
d.update(set())
check(read(replacement, layout) is None and read(replacement, kind) is None, 'the declaration is removed')
d.close()

d = stereo_declare.Declaration(layout=3, content_class=3, subclass=stereo_declare.GAME_3D)
d.update({replacement.id})
check(read(replacement, kind) == [3, 2], 'the sub-class is the caller\'s')
d.close()
check(read(replacement, layout) is None, 'close removes what is declared')

pids = {os.getpid()}
check(os.getpid() in stereo_declare.descendants(os.getpid(), pids), 'the process tree includes its root')

with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / 'state'
    path.write_text('wiz3d-full-sbs-v1 1600 600 0\nTest.exe\n')
    check(stereo_declare.window_state(path) == state, 'state file parsed')
    path.write_text('wiz3d-full-sbs-v1 1601 600 0\ntest.exe\n')
    check(stereo_declare.window_state(path) is None, 'an odd width is refused')
    path.write_text('wiz3d-full-sbs-v1 1600 600 4294967296\ntest.exe\n')
    check(stereo_declare.window_state(path) is None, 'a window id over 32 bits is refused')
    path.write_text('partial')
    check(stereo_declare.window_state(path) is None, 'a partial file is refused')
    check(stereo_declare.window_state(Path(temp) / 'absent') is None, 'a missing file is no window')
x.close()
sys.exit(1 if failures else 0)
