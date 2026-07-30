// Meridian 59, Copyright 1994-2026 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * gamepad.c:  Play with an Xbox-style controller.
 *
 * This is written for the Steam Deck, whose built-in controls Steam Input
 * presents to the game as an Xbox 360 pad, but nothing here is Deck specific
 * and any XInput controller works.
 *
 * The pad produces the same actions the keyboard does, so everything below
 * ends at PerformAction and the existing movement code in move.c.  That keeps
 * collision, run speed, server position updates and module hooks identical
 * between the two input paths; only the way an action is produced differs:
 *
 *   - The left stick picks one of the eight movement actions from its
 *     direction, and the run variant from how far it is pushed.  Repeating
 *     that every poll is exactly what holding a movement key does, and
 *     UserMovePlayer scales each step by elapsed time.
 *   - The right stick turns and tilts the view continuously, like mouselook.
 *     Rates are per second and integrated over the time since the last poll,
 *     with the sub-unit remainder carried forward so that small deflections
 *     still turn.
 *   - Buttons fire on the press edge, except for actions the keyboard also
 *     auto-repeats (looking up and down, map zoom), which fire while held.
 *
 * XInput is loaded dynamically.  The client has no import on it, so a machine
 * without the runtime just runs without a pad, and under Proton we pick up
 * whichever xinput DLL that build of Wine provides.
 */

#include "client.h"

#include <xinput.h>

// Milliseconds between reads of a connected controller.  Polling is cheap and
// MainIdle can spin faster than this in some client states.
#define GAMEPAD_POLL_INTERVAL 4

// Milliseconds between scans for a newly connected controller.  Reading an
// empty XInput slot is slow, so this is deliberately infrequent.
#define GAMEPAD_SCAN_INTERVAL 2000

// Longest time step we'll integrate the look stick over.  Without this, a
// hitch (a level load, a stall in the driver) would snap the view around.
#define GAMEPAD_MAX_LOOK_MS 100

// Synthetic button bits for the analog triggers, chosen above the XInput
// button bits, which stop at 0x8000.
#define GAMEPAD_LEFT_TRIGGER  0x00010000
#define GAMEPAD_RIGHT_TRIGGER 0x00020000

// Keyboard turning covers TURNDEGREES every TURN_DELAY (100) milliseconds, and
// holding the run key triples it; see UserTurnPlayer in move.c.  A stick at
// full deflection matches that run rate at sensitivity 10.
#define GAMEPAD_TURN_UNITS_PER_SEC (3.0f * TURNDEGREES * 10.0f)

// The view tilts over a range of 3 * CLASSIC_HEIGHT units end to end (see
// HEIGHT_MAX_OFFSET in move.c).  At sensitivity 10 a fully deflected stick
// sweeps that range in 1.2 seconds.
#define GAMEPAD_PITCH_UNITS_PER_SEC (3.0f * CLASSIC_HEIGHT / 1.2f)

// How far the movement stick must be pushed before the player runs.
#define GAMEPAD_RUN_THRESHOLD 0.7f

typedef DWORD (WINAPI *XInputGetStateProc)(DWORD dwUserIndex, XINPUT_STATE *pState);

static HMODULE            xinput_lib = NULL;
static XInputGetStateProc pXInputGetState = NULL;

static int   pad_index = -1;      // XInput slot we're reading, -1 when none
static DWORD last_poll_time = 0;
static DWORD last_scan_time = 0;
static DWORD held_buttons = 0;    // Buttons that were down at the last poll

// Carried-over fractions of a turn / tilt unit, so that a stick held just off
// centre still moves the view instead of truncating to zero every poll.
static float turn_remainder = 0.0f;
static float pitch_remainder = 0.0f;

/* Settings, from the [config] section of config.ini */
static bool  gamepad_enabled    = true;
static bool  gamepad_always_run = false;
static bool  gamepad_invert_y   = false;
static int   look_x_scale       = 15;
static int   look_y_scale       = 10;
static float move_deadzone      = 0.18f;
static float look_deadzone      = 0.15f;
static float trigger_threshold  = 0.30f;

/* Actions that can be put on a button in the [gamepad] section of config.ini */
typedef struct {
   const char *name;
   int         action;
   const void *data;
} GamepadAction;

static const GamepadAction gamepad_actions[] = {
   { "none",           A_NOACTION,       NULL },
   { "attack",         A_ATTACKCLOSEST,  NULL },
   { "mousetarget",    A_ATTACK,         NULL },
   { "open",           A_GO,             NULL },
   { "pickup",         A_PICKUP,         NULL },
   { "look",           A_LOOK,           NULL },
   { "lookinside",     A_LOOKINSIDE,     NULL },
   { "apply",          A_APPLY,          NULL },
   { "use",            A_USE,            NULL },
   { "unuse",          A_UNUSE,          NULL },
   { "offer",          A_OFFER,          NULL },
   { "buy",            A_BUY,            NULL },
   { "deposit",        A_DEPOSIT,        NULL },
   { "withdraw",       A_WITHDRAW,       NULL },
   { "targetnext",     A_TARGETNEXT,     NULL },
   { "targetprevious", A_TARGETPREVIOUS, NULL },
   { "targetclear",    A_TARGETCLEAR,    NULL },
   { "targetself",     A_TARGETSELF,     NULL },
   { "flip",           A_FLIP,           NULL },
   { "lookup",         A_LOOKUP,         NULL },
   { "lookdown",       A_LOOKDOWN,       NULL },
   { "lookstraight",   A_LOOKSTRAIGHT,   NULL },
   { "map",            A_MAP,            NULL },
   { "mapzoomin",      A_MAPZOOMIN,      NULL },
   { "mapzoomout",     A_MAPZOOMOUT,     NULL },
   { "chat",           A_GOTOSAY,        NULL },
   { "who",            A_WHO,            NULL },
   { "fullscreen",     A_FULLSCREEN,     NULL },
   { "say",            A_TEXTINSERT,     "say " },
   { "tell",           A_TEXTINSERT,     "tell " },
   { "yell",           A_TEXTINSERT,     "yell " },
   { "emote",          A_TEXTINSERT,     "emote " },
};

static const int NUM_GAMEPAD_ACTIONS =
   (int) (sizeof(gamepad_actions) / sizeof(gamepad_actions[0]));

/* Buttons, with the defaults documented in install/steamdeck/README.md */
typedef struct {
   const char *name;      // Key used in the [gamepad] section of config.ini
   DWORD       mask;      // XINPUT_GAMEPAD_* bit, or one of the trigger bits
   int         action;
   const void *data;
} GamepadBinding;

static GamepadBinding gamepad_bindings[] = {
   { "a",             XINPUT_GAMEPAD_A,              A_GO,             NULL },
   { "b",             XINPUT_GAMEPAD_B,              A_TARGETCLEAR,    NULL },
   { "x",             XINPUT_GAMEPAD_X,              A_PICKUP,         NULL },
   { "y",             XINPUT_GAMEPAD_Y,              A_LOOK,           NULL },
   { "leftshoulder",  XINPUT_GAMEPAD_LEFT_SHOULDER,  A_TARGETPREVIOUS, NULL },
   { "rightshoulder", XINPUT_GAMEPAD_RIGHT_SHOULDER, A_TARGETNEXT,     NULL },
   { "lefttrigger",   GAMEPAD_LEFT_TRIGGER,          A_TARGETSELF,     NULL },
   { "righttrigger",  GAMEPAD_RIGHT_TRIGGER,         A_ATTACKCLOSEST,  NULL },
   { "dpadup",        XINPUT_GAMEPAD_DPAD_UP,        A_LOOKUP,         NULL },
   { "dpaddown",      XINPUT_GAMEPAD_DPAD_DOWN,      A_LOOKDOWN,       NULL },
   { "dpadleft",      XINPUT_GAMEPAD_DPAD_LEFT,      A_MAPZOOMOUT,     NULL },
   { "dpadright",     XINPUT_GAMEPAD_DPAD_RIGHT,     A_MAPZOOMIN,      NULL },
   { "leftstick",     XINPUT_GAMEPAD_LEFT_THUMB,     A_FLIP,           NULL },
   { "rightstick",    XINPUT_GAMEPAD_RIGHT_THUMB,    A_LOOKSTRAIGHT,   NULL },
   { "view",          XINPUT_GAMEPAD_BACK,           A_MAP,            NULL },
   { "menu",          XINPUT_GAMEPAD_START,          A_FULLSCREEN,     NULL },
};

static const int NUM_GAMEPAD_BINDINGS =
   (int) (sizeof(gamepad_bindings) / sizeof(gamepad_bindings[0]));

/* Directions of the movement stick, clockwise from straight ahead */
static const int walk_actions[8] = {
   A_FORWARD,   A_SLIDERIGHTFORWARD,  A_SLIDERIGHT, A_SLIDERIGHTBACKWARD,
   A_BACKWARD,  A_SLIDELEFTBACKWARD,  A_SLIDELEFT,  A_SLIDELEFTFORWARD,
};

static const int run_actions[8] = {
   A_FORWARDFAST,  A_SLIDERIGHTFORWARDFAST, A_SLIDERIGHTFAST, A_SLIDERIGHTBACKWARDFAST,
   A_BACKWARDFAST, A_SLIDELEFTBACKWARDFAST, A_SLIDELEFTFAST,  A_SLIDELEFTFORWARDFAST,
};

/************************************************************************/
/*
 * GamepadReadBool:  Read a true/false setting from config.ini.
 */
static bool GamepadReadBool(const char *key, bool default_value)
{
   char value[32];

   GetPrivateProfileString("config", key, default_value ? "true" : "false",
                           value, sizeof(value), ConfigGetConfigIni());
   strlwr(value);

   return (0 == strcmp(value, "true"));
}
/************************************************************************/
/*
 * GamepadReadScale:  Read a sensitivity setting from config.ini, clamped to
 *   the same 1 to 30 range the mouselook settings use.
 */
static int GamepadReadScale(const char *key, int default_value)
{
   char value[32];
   int  scale;

   GetPrivateProfileString("config", key, "", value, sizeof(value), ConfigGetConfigIni());
   if (value[0] == 0)
      return default_value;

   scale = atoi(value);
   return std::min(30, std::max(1, scale));
}
/************************************************************************/
/*
 * GamepadReadPercent:  Read a 0 to 100 setting from config.ini and return it
 *   as a fraction.
 */
static float GamepadReadPercent(const char *key, int default_percent)
{
   char value[32];
   int  percent;

   GetPrivateProfileString("config", key, "", value, sizeof(value), ConfigGetConfigIni());
   percent = (value[0] == 0) ? default_percent : atoi(value);

   return std::min(90, std::max(0, percent)) / 100.0f;
}
/************************************************************************/
/*
 * GamepadLoadBindings:  Let the [gamepad] section of config.ini override the
 *   default button assignments.  An unrecognized action name leaves the
 *   default in place; that's friendlier than silently unbinding a button.
 */
static void GamepadLoadBindings(void)
{
   char value[64];

   for (int i = 0; i < NUM_GAMEPAD_BINDINGS; i++)
   {
      GetPrivateProfileString("gamepad", gamepad_bindings[i].name, "",
                              value, sizeof(value), ConfigGetConfigIni());
      if (value[0] == 0)
         continue;

      strlwr(value);

      bool found = false;
      for (int j = 0; j < NUM_GAMEPAD_ACTIONS; j++)
      {
         if (0 != strcmp(value, gamepad_actions[j].name))
            continue;

         gamepad_bindings[i].action = gamepad_actions[j].action;
         gamepad_bindings[i].data   = gamepad_actions[j].data;
         found = true;
         break;
      }

      if (!found)
         debug(("Unknown gamepad action \"%s\" for button \"%s\"\n",
                value, gamepad_bindings[i].name));
   }
}
/************************************************************************/
/*
 * GamepadInit:  Load the XInput runtime and the controller settings.
 */
void GamepadInit(void)
{
   // Newest first.  Wine and Proton provide 1_4 and 1_3; 9_1_0 ships with
   // Windows itself and is the last resort.
   static const char *libraries[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };

   gamepad_enabled = GamepadReadBool("gamepad", true);
   if (!gamepad_enabled)
   {
      debug(("Gamepad support disabled in config.ini\n"));
      return;
   }

   gamepad_always_run = GamepadReadBool("gamepadalwaysrun", false);
   gamepad_invert_y   = GamepadReadBool("gamepadinverty", false);
   look_x_scale       = GamepadReadScale("gamepadlookxscale", 15);
   look_y_scale       = GamepadReadScale("gamepadlookyscale", 10);
   move_deadzone      = GamepadReadPercent("gamepadmovedeadzone", 18);
   look_deadzone      = GamepadReadPercent("gamepadlookdeadzone", 15);
   trigger_threshold  = GamepadReadPercent("gamepadtriggerthreshold", 30);

   GamepadLoadBindings();

   for (int i = 0; i < (int) (sizeof(libraries) / sizeof(libraries[0])); i++)
   {
      xinput_lib = LoadLibrary(libraries[i]);
      if (xinput_lib == NULL)
         continue;

      pXInputGetState = (XInputGetStateProc) GetProcAddress(xinput_lib, "XInputGetState");
      if (pXInputGetState != NULL)
      {
         debug(("Gamepad support using %s\n", libraries[i]));
         return;
      }

      FreeLibrary(xinput_lib);
      xinput_lib = NULL;
   }

   debug(("No XInput runtime found; gamepad support unavailable\n"));
}
/************************************************************************/
/*
 * GamepadClose:  Release the XInput runtime.
 */
void GamepadClose(void)
{
   pXInputGetState = NULL;
   pad_index = -1;
   held_buttons = 0;

   if (xinput_lib != NULL)
   {
      FreeLibrary(xinput_lib);
      xinput_lib = NULL;
   }
}
/************************************************************************/
bool GamepadIsConnected(void)
{
   return pad_index >= 0;
}
/************************************************************************/
void GamepadReleaseAll(void)
{
   held_buttons = 0;
   turn_remainder = 0.0f;
   pitch_remainder = 0.0f;
}
/************************************************************************/
/*
 * GamepadStick:  Turn a raw stick reading into a direction and a magnitude in
 *   [0, 1], with a radial dead zone rescaled so that the usable range still
 *   starts at zero.  Returns false when the stick is centered.
 */
static bool GamepadStick(SHORT raw_x, SHORT raw_y, float deadzone,
                         float *out_x, float *out_y, float *out_magnitude)
{
   // The negative end of the range is one larger than the positive end.
   float x = std::max(-1.0f, raw_x / 32767.0f);
   float y = std::max(-1.0f, raw_y / 32767.0f);

   float length = sqrtf(x * x + y * y);

   *out_x = 0.0f;
   *out_y = 0.0f;
   *out_magnitude = 0.0f;

   if (length <= deadzone)
      return false;

   float magnitude = (std::min(1.0f, length) - deadzone) / (1.0f - deadzone);

   *out_x = x / length * magnitude;
   *out_y = y / length * magnitude;
   *out_magnitude = magnitude;

   return true;
}
/************************************************************************/
/*
 * GamepadCurve:  Shape a stick axis so that the middle of its travel is for
 *   fine aiming and the last of it is for spinning around quickly.
 */
static float GamepadCurve(float v)
{
   float magnitude = fabsf(v);
   float shaped = magnitude * (0.35f + 0.65f * magnitude * magnitude);

   return (v < 0.0f) ? -shaped : shaped;
}
/************************************************************************/
/*
 * GamepadMove:  Move the player in the direction the left stick is pushed.
 */
static void GamepadMove(const XINPUT_GAMEPAD *pad)
{
   float x, y, magnitude;

   if (!GamepadStick(pad->sThumbLX, pad->sThumbLY, move_deadzone, &x, &y, &magnitude))
      return;

   // Angle clockwise from straight ahead, snapped to the nearest eighth of a
   // turn: those are the eight directions the movement actions cover.
   float angle = atan2f(x, y);
   if (angle < 0.0f)
      angle += 2.0f * PI;

   int direction = ((int) ((angle + PI / 8.0f) / (PI / 4.0f))) & 7;

   bool run = gamepad_always_run || (magnitude >= GAMEPAD_RUN_THRESHOLD);

   PerformAction(run ? run_actions[direction] : walk_actions[direction], NULL);
}
/************************************************************************/
/*
 * GamepadLook:  Turn and tilt the view according to the right stick.
 *   dt is the number of milliseconds since the last poll.
 */
static void GamepadLook(const XINPUT_GAMEPAD *pad, DWORD dt)
{
   float x, y, magnitude;

   if (!GamepadStick(pad->sThumbRX, pad->sThumbRY, look_deadzone, &x, &y, &magnitude))
   {
      turn_remainder = 0.0f;
      pitch_remainder = 0.0f;
      return;
   }

   float seconds = dt / 1000.0f;

   float turn = GamepadCurve(x) * GAMEPAD_TURN_UNITS_PER_SEC * look_x_scale / 10.0f * seconds
      + turn_remainder;
   int turn_units = (int) turn;
   turn_remainder = turn - turn_units;

   // Invert the axis, not the result: negating after the remainder is added
   // would flip the sign of what the last poll carried over.
   float pitch_axis = gamepad_invert_y ? -y : y;

   float pitch = GamepadCurve(pitch_axis) * GAMEPAD_PITCH_UNITS_PER_SEC * look_y_scale / 10.0f * seconds
      + pitch_remainder;
   int pitch_units = (int) pitch;
   pitch_remainder = pitch - pitch_units;

   if (pitch_units != 0)
      PlayerChangeHeightMouse(pitch_units);

   if (turn_units != 0)
      // Redraws and updates the server with our new facing.
      UserTurnPlayerMouse(turn_units);
   else if (pitch_units != 0)
      RedrawAll();
}
/************************************************************************/
/*
 * GamepadButtons:  Perform the actions bound to the buttons that are down.
 *   Actions the keyboard auto-repeats repeat here too; everything else fires
 *   once per press.
 */
static void GamepadButtons(DWORD buttons)
{
   DWORD pressed = buttons & ~held_buttons;

   for (int i = 0; i < NUM_GAMEPAD_BINDINGS; i++)
   {
      int action = gamepad_bindings[i].action;

      if (action == A_NOACTION || action == A_FULLSCREEN)
         continue;   // Full screen is handled outside of the game states

      DWORD mask = gamepad_bindings[i].mask;

      if (RepeatAction(action) ? ((buttons & mask) != 0) : ((pressed & mask) != 0))
         PerformAction(action, gamepad_bindings[i].data);
   }
}
/************************************************************************/
/*
 * GamepadPoll:  Read the controller and act on it.  Called from MainIdle.
 */
void GamepadPoll(void)
{
   // Not "state": that name belongs to the client's own STATE_GAME variable.
   XINPUT_STATE pad_state;
   DWORD now, dt;

   if (pXInputGetState == NULL)
      return;

   now = timeGetTime();
   dt = now - last_poll_time;
   if (dt < GAMEPAD_POLL_INTERVAL)
      return;
   last_poll_time = now;

   if (pad_index >= 0 && pXInputGetState(pad_index, &pad_state) != ERROR_SUCCESS)
   {
      debug(("Gamepad %d disconnected\n", pad_index));
      pad_index = -1;
      GamepadReleaseAll();
   }

   if (pad_index < 0)
   {
      // Reading an unoccupied XInput slot is expensive, so look for a newly
      // connected controller only every so often.
      if (now - last_scan_time < GAMEPAD_SCAN_INTERVAL)
         return;
      last_scan_time = now;

      for (DWORD i = 0; i < (DWORD) XUSER_MAX_COUNT; i++)
      {
         if (pXInputGetState(i, &pad_state) != ERROR_SUCCESS)
            continue;

         pad_index = (int) i;
         debug(("Gamepad %d connected\n", pad_index));
         break;
      }

      if (pad_index < 0)
         return;
   }

   DWORD buttons = pad_state.Gamepad.wButtons;
   if (pad_state.Gamepad.bLeftTrigger / 255.0f > trigger_threshold)
      buttons |= GAMEPAD_LEFT_TRIGGER;
   if (pad_state.Gamepad.bRightTrigger / 255.0f > trigger_threshold)
      buttons |= GAMEPAD_RIGHT_TRIGGER;

   // Don't let a controller sitting on a table drive a client in the
   // background, and don't act on buttons that went down while we were there.
   if (!is_foreground)
   {
      held_buttons = buttons;
      turn_remainder = 0.0f;
      pitch_remainder = 0.0f;
      return;
   }

   // Toggling full screen is a window command rather than a game action, so
   // it works in every client state: the offline screen, login, and play.
   DWORD pressed = buttons & ~held_buttons;
   for (int i = 0; i < NUM_GAMEPAD_BINDINGS; i++)
      if (gamepad_bindings[i].action == A_FULLSCREEN
          && (pressed & gamepad_bindings[i].mask) != 0)
      {
         FullScreenToggle();
         break;
      }

   // The rest is gameplay, and follows the same rules HandleKeys does: only
   // while playing, and only when the main window has the focus rather than
   // the text input box or a dialog.
   if (state != STATE_GAME || GameGetState() != GAME_PLAY || GetFocus() != hMain)
   {
      held_buttons = buttons;
      turn_remainder = 0.0f;
      pitch_remainder = 0.0f;
      return;
   }

   if (buttons != 0 || pad_state.Gamepad.sThumbLX != 0 || pad_state.Gamepad.sThumbLY != 0
       || pad_state.Gamepad.sThumbRX != 0 || pad_state.Gamepad.sThumbRY != 0)
      UserDidSomething();   // Keep the inactivity logoff timer at bay

   GamepadMove(&pad_state.Gamepad);
   GamepadLook(&pad_state.Gamepad, std::min((DWORD) GAMEPAD_MAX_LOOK_MS, dt));
   GamepadButtons(buttons);

   held_buttons = buttons;
}
