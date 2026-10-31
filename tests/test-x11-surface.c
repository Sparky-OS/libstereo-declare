/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

/* An ordinary X server must keep the legacy route, without creating a child. */
int main(void)
{
    Display *d = XOpenDisplay(NULL);
    assert(d);
    Window parent = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 64, 32, 0, 0, 0);
    XSync(d, False);
    struct stereo_x11_surface *s = (void *)(uintptr_t)1;
    assert(stereo_x11_surface_create(DisplayString(d), parent, &s) == -ENOTSUP);
    assert(!s);
    Window root, ancestor, *children = NULL;
    unsigned count = 99;
    assert(XQueryTree(d, parent, &root, &ancestor, &children, &count));
    assert(count == 0);
    if (children) XFree(children);
    assert(stereo_x11_surface_create(DisplayString(d), None, &s) == -EINVAL);
    assert(stereo_x11_surface_poll(NULL, NULL, NULL) == -EINVAL);
    stereo_x11_surface_hide(NULL);
    stereo_x11_surface_close(NULL);
    XDestroyWindow(d, parent);
    XCloseDisplay(d);
    puts("unqualified X server keeps the legacy route without a native child");
    return 0;
}
