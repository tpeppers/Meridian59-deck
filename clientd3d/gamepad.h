// Meridian 59, Copyright 1994-2026 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * gamepad.h:  Header file for gamepad.c
 */

#ifndef _GAMEPAD_H
#define _GAMEPAD_H

void GamepadInit(void);
void GamepadClose(void);

// Read the controller and turn its state into game actions.  Called from
// MainIdle, so it runs in every client state, not just during play.
void GamepadPoll(void);

// Forget which buttons were held.  Called when the window loses the focus, so
// that a button released while we were in the background doesn't register as
// a fresh press when we come back.
void GamepadReleaseAll(void);

bool GamepadIsConnected(void);

#endif /* #ifndef _GAMEPAD_H */
