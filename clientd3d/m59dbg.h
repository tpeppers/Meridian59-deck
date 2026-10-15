// Meridian 59 — harness debug overlay.
//
// Not part of the stock client. See m59dbg.c for what this is and why it exists.

#ifndef _M59DBG_H
#define _M59DBG_H

void DebugInit(void);
void DebugClose(void);

// Called when the client enters a room, from MapEnterRoom.  Loads
// <M59_OVERLAY_DIR>\<room>.ovl if it exists; forgets the previous room's if not.
void DebugEnterRoom(int room_id);

// Paint the loaded overlay.  Called from MapDraw with the same (x, y, scale) the
// wall drawing uses, BEFORE the walls, so walls stay legible on top.
void DebugDrawOverlay(HDC hdc, int x, int y, float scale, room_type *room);

// Called from MainIdle.  Refreshes the title bar and re-reads the overlay file
// when it changes on disk.  Cheap and self-throttling.
void DebugIdle(void);

// Called from MainKey ahead of the modules.  Returns true if it consumed the key.
bool DebugKey(UINT vk, BOOL fDown, UINT flags);

// Send one event to whatever is listening on the signal port.  Never blocks and
// never fails loudly: this is a debugging aid, not a feature of the game.
void DebugSignal(const char *kind, const char *detail);
// The harness's page for the room we are in, when M59_MAP_URL is set and the room is
// one the harness can name. False means "no opinion" and the caller should do whatever
// it did before. See StartHelp in winmsg.c.
bool DebugRoomURL(char *out, size_t len);

// PHASE 2 SCAFFOLDING -- the object-keyed annotation channel.
//
// The overlay above answers "what does the harness believe about this ROOM".
// This answers "which OBJECT is being discussed", which the overlay cannot: an
// .ovl mark names a square, is drawn where a creature stood when somebody
// pointed, and does not follow it. A room routinely holds four battered
// skeletons, and pointing at one of them is the whole reason this exists.
//
// Annotations arrive as one UDP datagram per mark on M59_ANNOTATE_PORT, in the
// same fire-and-forget shape DebugSignal sends: unset the variable and no
// socket is created and this is inert. Each carries an object id, a colour and
// a lifetime, and expires on its own so a stale highlight cannot outlive the
// conversation that produced it.
//
// True when this object should be drawn highlighted, with its colour in *out.
// Returns false immediately when annotations are switched off, so the renderer
// call sites collapse to stock behaviour with no configuration.
bool DebugAnnotationFor(int object_id, COLORREF *out);

#endif /* #ifndef _M59DBG_H */
