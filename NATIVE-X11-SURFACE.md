# Native X11 child presentation

The additive `stereo_x11_surface_*` API uses the existing child placement and version 1 presentation receipt contracts.
It does not change their wire layouts, enable Xwayland discovery, or change the existing library entry points or SONAME.
The build requires Xpresent, Xext (Shape), Xcomposite and threads in addition to the existing dependencies.

Create a surface on the GUI thread using the display name and the live toolkit top-level XID.
The library opens a private X connection and never consumes toolkit events.
The parent and that connection must remain live until close; close before destroying the toolkit parent when possible.
Parent destruction is detected and retires the outstanding tag; loss of the X server connection retains ordinary Xlib I/O error semantics.
Calls for one surface must be serialized by its owner.
The process-wide X error trap is serialized, delegates foreign-display errors, and does not install an I/O error handler.

Show takes opaque RGB32 pixels, left view followed by right view at native sample resolution, an even packed width, a byte stride and a nonzero caller tag.
It copies the native pixels without resampling.
Child placement is expressed in the parent's X pixel coordinates, independently of native sample dimensions; toolkit owners convert their logical placement using their actual scale.
The compositor applies the declared one-view placement.
The input shape is empty, so pointer events continue to reach the original widget.

A newly declared child is redirected asynchronously by KWin.
Show retains one pending frame until Composite can name its redirected backing pixmap, then sends Present Copy with the exact receipt serial.
That observation qualifies submission only; it does not complete a frame.
Two server pixmaps are reused only after Present Idle, and a single pending native frame is coalesced to the latest show under pressure.
A subsequent show cancels and acknowledges the old request without reporting failure for a still-current acquisition.
The owner must retire genuinely replaced acquisition state independently of this callback supersession.

Poll returns a tag and terminal outcome only from the validated receipt mailbox, or a discard when the child/backend is lost.
A timer may call poll but cannot manufacture completion.
After a presented receipt the helper continues watching for backend loss until replacement, hide or close.
Hide cancels the request and unmaps; the caller retires its immutable acquisition in its existing lifecycle owner.
No input readiness or ACTIVE transition follows merely from show, Present progress, buffer release or elapsed time.

Create fails when stereo declaration, receipt discovery or the required X extensions are absent.
The frontend then keeps its ordinary left-view route.
Neither the helper nor its callers enable external SPICE input admission.
