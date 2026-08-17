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

#endif /* #ifndef _M59DBG_H */
