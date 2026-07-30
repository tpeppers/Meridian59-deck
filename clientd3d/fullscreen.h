// Meridian 59, Copyright 1994-2026 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * fullscreen.h:  Header file for fullscreen.c
 */

#ifndef _FULLSCREEN_H
#define _FULLSCREEN_H

M59EXPORT void FullScreenToggle(void);
void FullScreenSet(bool enable);
M59EXPORT bool FullScreenIsActive(void);

// Fill w with the placement the window should be restored to.  While full
// screen this is the windowed placement saved at the time we went full
// screen, not the borderless geometry the window currently has.
void FullScreenGetPlacement(WINDOWPLACEMENT *w);

// The main window's menu.  The menu is detached from the window while full
// screen, so GetMenu returns NULL; callers that need the menu itself rather
// than the window's copy of it should use this.
HMENU FullScreenGetMenu(void);

// Apply the saved full screen preference.  Called once at startup, after the
// main window has been shown.
void FullScreenRestoreSetting(void);

#endif /* #ifndef _FULLSCREEN_H */
