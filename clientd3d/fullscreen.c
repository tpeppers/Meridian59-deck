// Meridian 59, Copyright 1994-2026 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * fullscreen.c:  Borderless full screen mode for the main window.
 *
 * The renderer draws into a fixed size back buffer that is presented into the
 * client area, so going full screen is purely a window operation: drop the
 * frame and the menu bar, then size the window to the monitor it is on.  No
 * device reset is needed, and the WM_SIZE that falls out of the resize runs
 * the same relayout path that maximizing already uses.
 *
 * This is deliberately borderless-windowed rather than an exclusive mode
 * switch.  Alt+Tab stays instant, the Steam overlay keeps compositing, and on
 * Steam Deck gamescope scales the window to the panel without a mode change.
 */

#include "client.h"

static bool fullscreen = false;

// Window state saved on the way in, restored on the way out.
static LONG_PTR saved_style   = 0;
static LONG_PTR saved_exstyle = 0;
static WINDOWPLACEMENT saved_placement = { sizeof(WINDOWPLACEMENT) };
static HMENU saved_menu = NULL;

/************************************************************************/
/*
 * FullScreenIsActive:  Return true iff the main window is currently
 *   borderless full screen.
 */
bool FullScreenIsActive(void)
{
   return fullscreen;
}
/************************************************************************/
/*
 * FullScreenGetPlacement:  Return the placement the window should be
 *   restored to.  While full screen the window's own placement describes the
 *   borderless geometry, which is not what we want to write to the INI file.
 */
void FullScreenGetPlacement(WINDOWPLACEMENT *w)
{
   if (fullscreen)
   {
      *w = saved_placement;
      return;
   }

   w->length = sizeof(WINDOWPLACEMENT);
   GetWindowPlacement(hMain, w);
}
/************************************************************************/
/*
 * FullScreenGetMenu:  Return the main window's menu, whether or not it is
 *   currently attached to the window.
 */
HMENU FullScreenGetMenu(void)
{
   HMENU menu = GetMenu(hMain);

   return (menu != NULL) ? menu : saved_menu;
}
/************************************************************************/
/*
 * FullScreenUpdateMenu:  Reflect the current mode in the Options menu.  The
 *   check sticks while the menu is detached and shows up when it comes back.
 */
static void FullScreenUpdateMenu(void)
{
   HMENU menu = FullScreenGetMenu();

   if (menu != NULL)
      CheckMenuItem(menu, ID_OPTIONS_FULLSCREEN, fullscreen ? MF_CHECKED : MF_UNCHECKED);
}
/************************************************************************/
/*
 * FullScreenSet:  Enter or leave borderless full screen mode.
 */
void FullScreenSet(bool enable)
{
   if (hMain == NULL || enable == fullscreen)
      return;

   if (enable)
   {
      MONITORINFO mi;
      HMONITOR monitor;

      // Full screen means the monitor the window is on, not the primary one.
      monitor = MonitorFromWindow(hMain, MONITOR_DEFAULTTONEAREST);
      mi.cbSize = sizeof(mi);
      if (!GetMonitorInfo(monitor, &mi))
      {
         debug(("FullScreenSet couldn't find the window's monitor\n"));
         return;
      }

      saved_placement.length = sizeof(WINDOWPLACEMENT);
      GetWindowPlacement(hMain, &saved_placement);
      saved_style   = GetWindowLongPtr(hMain, GWL_STYLE);
      saved_exstyle = GetWindowLongPtr(hMain, GWL_EXSTYLE);

      // Detaching the menu doesn't destroy it; we reattach the same handle
      // on the way out, so winmenu.c's cached HMENU stays valid.
      saved_menu = GetMenu(hMain);
      SetMenu(hMain, NULL);

      SetWindowLongPtr(hMain, GWL_STYLE,
         (saved_style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX
                          | WS_MAXIMIZEBOX | WS_SYSMENU)) | WS_POPUP);
      SetWindowLongPtr(hMain, GWL_EXSTYLE,
         saved_exstyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE
                           | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

      // Set before the resize so that MainMinMaxInfo stops clamping us to the
      // maximized window size, which is smaller than the monitor.
      fullscreen = true;

      SetWindowPos(hMain, HWND_TOP,
         mi.rcMonitor.left, mi.rcMonitor.top,
         mi.rcMonitor.right - mi.rcMonitor.left,
         mi.rcMonitor.bottom - mi.rcMonitor.top,
         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
   }
   else
   {
      fullscreen = false;

      SetWindowLongPtr(hMain, GWL_STYLE, saved_style);
      SetWindowLongPtr(hMain, GWL_EXSTYLE, saved_exstyle);

      if (saved_menu != NULL)
      {
         SetMenu(hMain, saved_menu);
         MenuBarApply(saved_menu);
         DrawMenuBar(hMain);
         saved_menu = NULL;
      }

      // SetWindowPlacement won't recompute the non-client area on its own.
      SetWindowPos(hMain, NULL, 0, 0, 0, 0,
         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      SetWindowPlacement(hMain, &saved_placement);
   }

   config.fullscreen = fullscreen;

   FullScreenUpdateMenu();

   // The style change can leave stale frame pixels behind on the first paint.
   RedrawWindow(hMain, NULL, NULL,
      RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
/************************************************************************/
/*
 * FullScreenToggle:  Switch between borderless full screen and windowed.
 */
void FullScreenToggle(void)
{
   FullScreenSet(!fullscreen);
}
/************************************************************************/
/*
 * FullScreenRestoreSetting:  Go full screen at startup if that's how the
 *   client was last left.  Called after the main window has been shown, so
 *   that the windowed placement we save is the one the INI file describes.
 */
void FullScreenRestoreSetting(void)
{
   if (config.fullscreen)
      FullScreenSet(true);
   else
      FullScreenUpdateMenu();
}
