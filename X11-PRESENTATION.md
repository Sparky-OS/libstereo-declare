# X11 presentation receipts, version 1

This is the implementation contract for the additive libstereo-declare 1.9 API.
It is not advertised by the current 1.8 library or by an unqualified Xwayland.
The library keeps SONAME 1 and every existing entry point and property meaning.

## What a receipt means

A successful receipt says that the Wayland compositor reported a particular declared X11 window's surface commit as presented.
It identifies the Present pixmap submission that supplied that commit, with its request identity, presentation timestamp, refresh interval and presentation sequence.
It does not mean the pixmap is idle, input is authorized, the window is still visible, or that a physical display was measured.
The client must also match the receipt to its still-current acquisition, target, connection and effective visibility.

Ordinary Present completion, MSC progression, timer fallback and buffer release keep their existing behavior, including for declared windows.
They are not receipts under this contract.
In particular, filtering ordinary Present completion to Flip mode does not establish compositor presentation.
A client that never requests this extension receives no receipt and incurs no per-submission feedback allocation.

## Discovery and ownership

Xwayland owns `_KDE_NET_WM_STEREO_PRESENTATION_SUPPORTED` on each root window.
It is CARDINAL, format 32, exactly eight words:

| Word | Value |
|---|---|
| 0 | Receipt contract version, 1 |
| 1 | The clock identifier announced by `wp_presentation.clock_id`, as a 32-bit value |
| 2 | Maximum outstanding receipt transactions per native window, 1 |
| 3 | Receipt record length in words, 16 |
| 4 | Request record length in words, 12 |
| 5–7 | Reserved, zero |

Xwayland publishes this only after binding `wp_presentation` and receiving its clock identifier.
An absent, malformed or unsupported record means the receipt API is unavailable.
Loss of the presentation global removes the property and retires pending receipts.
The existing compositor-owned `_KDE_NET_WM_STEREO_CONTENT_SUPPORTED` is not changed by this extension.
Clients still check its layout and child-placement support separately.

A request targets a live native X11 window that declares `STEREO_SBS_FULL` itself.
For a listed child, it must have its own redirected surface and a valid current parent association.
A request for a parent that merely lists stereo children cannot certify those children.
There is no whole-parent or most-recent-child fallback.
The receipt state belongs to the X11 window's lifetime, not its numeric XID alone.

This is a coordination protocol on an already trusted X connection, not authentication against other clients on that X server.
As with the existing stereo properties, another trusted X client can modify properties.
It must not be presented as an isolation boundary against an untrusted local X client.

## Request mailbox

The client owns `_KDE_NET_WM_STEREO_PRESENTATION_REQUEST` on the native content window.
It uses CARDINAL, format 32, exactly twelve words:

| Word | Value |
|---|---|
| 0 | Version, 1 |
| 1 | Operation: ARM 1, CANCEL 2, ACK 3 |
| 2–3 | Nonzero 64-bit client session identity, high word first |
| 4–5 | Nonzero 64-bit request number, high word first |
| 6 | The 32-bit Present pixmap serial, including zero when the caller uses it |
| 7–11 | Reserved, zero |

The session identity must not be reused for another binding of this window in the same live X connection.
Request numbers increase within that session and never wrap; start a fresh session on exhaustion.
The caller serializes this mailbox with its Present submissions on the same X connection.
It selects PropertyChangeMask without replacing any event masks its toolkit already owns.
The library does not read another toolkit's event queue or install a global X error handler.

ARM precedes the matching PresentPixmap request in X request order.
The server validates and snapshots ARM immediately when the property changes.
At admission of the next matching PresentPixmap for that window and serial, it binds this snapshot to that internal Present event.
It never attaches a new request to an earlier queued event, looks up the latest mailbox when rendering, or matches a serial from another window.
PresentNotifyMSC cannot claim an ARM.
An unrelated PresentPixmap before the matching serial leaves ARM pending; after binding, any submission that replaces the bound content must preserve its identity or retire it.

There is one transaction slot per window, including its terminal receipt until acknowledged.
Repeating an identical ARM is idempotent.
A different ARM while occupied cannot overwrite either the existing request or its receipt.
The library reports busy locally; a client that violates this serialization must cancel or acknowledge the existing transaction before retrying.
Ordinary rendering continues during this pressure; the extension must not stall legacy Present scheduling or buffer release.

CANCEL for the exact session, request and serial terminates a nonterminal transaction as discarded.
It cannot change a terminal receipt, and stale or unrelated cancellation is ignored.
ACK consumes only the exact terminal transaction, deletes its receipt property and frees the slot.
ACK before a terminal result, an old session's ACK and an unrelated serial's ACK do nothing.
The client reads and validates the terminal record before sending ACK.
ACK followed by the next ARM on the same connection is processed in that order.

Deleting the request property explicitly closes the local binding and invalidates all its pending callbacks and records.
Before closing, the client retires its own acquisition readiness.
A new session cannot inherit success from the deleted binding.
Window destruction or X disconnection closes the binding without requiring an event to a dead recipient.

## Receipt mailbox

Xwayland owns `_KDE_NET_WM_STEREO_PRESENTATION_RECEIPT` on the same window.
It is CARDINAL, format 32, exactly sixteen words, replaced atomically once per terminal transaction:

| Word | Value |
|---|---|
| 0 | Version, 1 |
| 1 | Outcome: presented 1, discarded 2, rejected 3 |
| 2–3 | Client session identity, high word first |
| 4–5 | Request number, high word first |
| 6 | Present pixmap serial |
| 7 | Reason, listed below |
| 8–9 | Presentation seconds from `wp_presentation_feedback.presented`, high word first |
| 10 | Presentation nanoseconds, less than 1,000,000,000 |
| 11 | Refresh interval in nanoseconds, zero when the compositor reports unknown |
| 12–13 | Presentation sequence from the compositor, high word first |
| 14 | The compositor's presentation kind flags, passed through |
| 15 | Reserved, zero |

Reason 0 accompanies presented.
Discarded reasons are compositor discard 1, superseded before commit 2, explicit cancel 3, unmap 4, reparent or parent-association change 5, surface replacement 6, declaration removal 7, and presentation-backend loss 8.
Rejected reasons are malformed request 9, ineligible content window 10, unsupported commit path 11, and allocation failure 12.
For discarded or rejected outcomes, words 8–14 are zero.
A malformed request is rejected only if the server can validate its complete identity prefix; otherwise it is ignored without mutating another transaction.
An invalid reserved word or record length must never generate presented.

The complete receipt remains until its exact ACK or binding close, so PropertyNotify coalescing does not lose the terminal result.
PropertyNotify is a wakeup, not the receipt itself; the client reads the property and validates type, format, length, version, reserved fields and the full identity.
An unrelated, stale or duplicate event cannot qualify a different acquisition.
The library may also poll the record after installing its event mask to avoid a lost initial notification.

## Commit and callback lifetime

The server requests `wp_presentation_feedback` immediately before the exact `wl_surface.commit` that carries the bound pixmap contents.
It retains a separate receipt object until the feedback terminates or lifecycle retirement occurs.
This object must not point into a Present vblank allocation that ordinary completion or idle processing can free.
It retains the copied identity, native-window incarnation, surface incarnation and exact commit association.

Only the compositor's `presented` callback may produce presented.
Ordinary timer, frame, sync, completion and buffer-release callbacks cannot call that transition.
Compositor discard produces discarded.
A pending child that is unmapped, reparented, loses its declaration or surface, or loses its presentation backend is retired before its resources are freed.
An already terminal receipt is immutable; the client still retires readiness on a later visibility or target change.

For a copy path, the association must follow the actual damaged window pixmap into the surface buffer commit.
If damage is overwritten or coalesced before that commit, the superseded request is discarded; it cannot claim the replacement's feedback.
Partial updates are eligible only when the implementation proves which complete buffer and child the commit represents.
An implementation may reject an unqualified receipt path while continuing to render it through the existing route.
It may not silently certify that path, drop it, or substitute CPU transport for a GL acquisition.

On every terminal transition, feedback destruction and receipt publication occur at most once.
Late callbacks from a retired surface or session cannot touch a reused XID or a new transaction.
Serial reuse in Present does not revive an old receipt, because the session and request number remain part of the binding.
A client must not convert a timeout into presented.
For an obscured surface with no feedback yet, it may cancel and retire locally; a lack of callbacks is not presentation.

## Additive library surface

The 1.9 implementation provides a caller-owned presentation context bound to a Display, native window and client session identity.
It exposes discovery, ARM, terminal record parsing, CANCEL, ACK and close operations matching the state machine above.
It returns unsupported on an old Xwayland before writing requests, busy for an occupied local slot, and protocol error for malformed receipt data.
It does not reinterpret a successful X request send as server acceptance.
The C API is available since 1.9.0:

| Function | Result |
|---|---|
| `stereo_x11_presentation_supported(display, window, &clock_id)` | 1 supported, 0 absent, negative errno malformed/error |
| `stereo_x11_presentation_create(display, window, session, &context)` | 0 created, `-ENOTSUP` absent, `-EBUSY` existing mailbox |
| `stereo_x11_presentation_arm(context, request, serial)` | 0 armed/idempotent, `-EAGAIN` occupied, `-EINVAL` non-increasing identity |
| `stereo_x11_presentation_read(context, event, &receipt)` | 1 matching terminal record, 0 none/unrelated, negative errno malformed/error |
| `stereo_x11_presentation_cancel(context)` | 0 cancellation sent or already terminal |
| `stereo_x11_presentation_ack(context)` | 0 terminal slot consumed locally, `-EINVAL` before reading its terminal result |
| `stereo_x11_presentation_close(context)` | Deletes both local mailbox properties and frees the context |

Pass a null event to poll. Otherwise pass the caller's PropertyNotify event without removing events from another owner's queue.
Use exactly one serialized context per window; do not share it between threads.
Close it before destroying its window or closing its Display.
The caller owns X error handling and retires readiness on every disconnect, target, visibility or surface change.
Deleting REQUEST invalidates the server binding immediately; deleting RECEIPT removes its old property bytes.
The server also clears stale receipt bytes on a fresh ARM, and never lets a new session inherit them.
Existing 1.8 callers, SONAME 1, Python declarations and all layout/place/membership APIs remain unchanged.

GTK and Qt event-loop integrations must preserve this ownership and pass immutable acquisition data to their completion callbacks.
Canceling feedback for a repeated redraw of the same acquisition does not by itself retire that still-valid acquisition; actual discard, hide, target replacement and teardown do.
Input remains subject to the frontend's existing target and agent-applied snapshot validation.

## Qualification before advertising

1. Stop the private compositor before submitting a new pixmap.
   Ordinary Present may still complete, but this receipt must not report presented until the compositor resumes and presents that exact commit.
2. Cover both flip and copy paths, same-acquisition repeated redraw, distinct replacement, queued/coalesced damage, two independent children and both orders of feedback.
3. Exercise hidden/unmapped children, reparent, declaration removal, context or surface loss, teardown, reconnect and XID/Present-serial reuse.
   No stale callback may qualify a new transaction.
4. Exercise malformed length/type/version/reserved fields, duplicate ARM/CANCEL/ACK, early ACK, stale session, slot pressure and allocation failure.
5. Prove old clients on the new server retain their ordinary Present events and pixels, and a new library on an old server makes no extension writes.
   Run existing 1.8 declaration, placement and membership tests and compare its exported ABI.
6. Qualify the real GTK and Karton CPU, GL and encoded acquisition owners, native samples, placement, left-view fallback and unchanged Wayland behavior before using receipts for readiness.

References: [Present protocol](https://sources.debian.org/src/xorgproto/2025.1-1/presentproto.txt/), [Wayland presentation-time](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/presentation-time/presentation-time.xml), and [child placement](CHILD-PLACE.md).
