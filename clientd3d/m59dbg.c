// Meridian 59 — harness debug overlay.  NOT PART OF THE STOCK CLIENT.
//
// This file exists because every geometric argument in the m59-harness repository has
// been conducted in numbers — "68 regions", "17,402 pockets", "walkable(4,2) === false" —
// and several of those numbers were confidently wrong for weeks.  Three separate
// diagnoses of one hang were wrong before somebody logged in and said "it looks like a
// normal corridor to me", which settled it in one sentence.  A person looking at the
// screen has repeatedly beaten hours of telemetry, and the expensive part was always
// getting the model's claim and the human's eye into the same picture.
//
// So this draws WHAT THE HARNESS BELIEVES, on the client's own automap, in the client's
// own coordinate space.  Three things, each deliberately small:
//
//   1. A per-square overlay read from a file the harness writes.
//   2. The player's position — room, coarse square and fine coordinates — in the title
//      bar, which is otherwise never set at all.
//   3. A hotkey that sends one UDP datagram to a listener on loopback, so a person
//      standing in the world can say "now" without alt-tabbing.
//
// THE OVERLAY IS READ, NEVER COMPUTED.  The client has the BSP in memory and could
// derive most of this itself — and that is exactly the trap.  The whole point is to see
// what the ROUTER thinks, and a second implementation here would be a third map, so a
// disagreement between the picture and the bot would prove nothing.  If the file is
// absent, wrong or stale, this draws nothing and says so in the title bar.
//
// IT IS INERT UNLESS ASKED FOR.  M59_OVERLAY_DIR unset means no file is ever opened.
// M59_SIGNAL_PORT unset or 0 means no socket is ever created.  A stock player running
// this binary sees an ordinary client with a more informative caption.
//
// EVERYTHING HERE FAILS QUIET.  It is a debugging aid attached to a program that holds
// live game sessions; a malformed overlay file must cost a picture, never a session.

// Winsock comes from client.h, and it is WINSOCK 1 — the whole client is built on it
// (com.c:62 starts up MAKEWORD(1,1)).  Including winsock2.h here instead does not
// upgrade anything; it collides with the winsock.h that windows.h has already pulled
// in and produces a hundred errors inside ws2tcpip.h.  Everything this file needs —
// socket, sendto, sockaddr_in, htons, htonl, closesocket — is in version 1.
#include "client.h"

// Declared per-file throughout this client rather than in extern.h; map.c:67 and
// annotate.c:22 do the same, and this file is drawn from the same globals they are.
extern player_info player;
extern room_type   current_room;

#define DBG_MAX_COLORS   32
#define DBG_MAX_MARKS    64
#define DBG_MAX_LABEL    64
#define DBG_TITLE_MS     200        // caption repaints are cheap but not free; MainIdle
                                    // spins far faster than the eye can read
#define DBG_STAT_MS      1000       // how often to re-stat the overlay file

typedef struct {
   char     ch;
   COLORREF color;
   int      style;                  // 0 solid, 1 hatch, 2 cross, 3 diagonal
   HBRUSH   brush;
   char     label[DBG_MAX_LABEL];
} DbgColor;

typedef struct {
   int      row, col;
   COLORREF color;
   char     label[DBG_MAX_LABEL];
} DbgMark;

static bool     dbg_enabled     = false;   // an overlay directory was configured
static char     dbg_dir[MAX_PATH];
static int      dbg_port        = 0;       // 0 = no signalling
static char     dbg_map_url[MAX_PATH];    // base URL of the harness room-view server
static bool     dbg_recording   = false;  // F9 toggle; the caption says so while it is on
static bool     dbg_title       = true;

static int      dbg_room        = 0;       // room the loaded overlay is for, 0 = none
static char     dbg_room_name[128];
static int      dbg_rows        = 0, dbg_cols = 0;
static int      dbg_shift       = 0;       // grid index -> client square index
// UNSIGNED, AND THAT IS NOT A DETAIL. A .roo security number routinely exceeds INT_MAX
// — West Merchant Way's is 3283785701 — while `room_type.security` is a signed int, so
// the client holds it as a negative number and `atoi` on the decimal text overflows.
// Compared naively the two never match and every overlay in the world is refused as
// WRONG ROOM. Both sides are cast to unsigned, where the bit patterns agree.
static unsigned int dbg_security = 0;      // the .roo this overlay was baked from, 0 = unstated
static int      dbg_num         = 0;       // the harness's own room number, for the caption
static unsigned char *dbg_grid  = NULL;    // dbg_rows * dbg_cols chars, '.' = nothing
static DbgColor dbg_colors[DBG_MAX_COLORS];
static int      dbg_num_colors  = 0;
static DbgMark  dbg_marks[DBG_MAX_MARKS];
static int      dbg_num_marks   = 0;
static char     dbg_status[96];            // what the title bar says about the overlay
static FILETIME dbg_mtime;                 // of the file we loaded, to notice a rewrite
static bool     dbg_have_mtime  = false;

static SOCKET   dbg_sock        = INVALID_SOCKET;
static struct sockaddr_in dbg_addr;
static bool     dbg_once        = false;   // DebugInit has run; see the note there

// ---- object-keyed annotations (see DebugAnnotationFor in m59dbg.h) ---------
// Bounded like every other table here: a hostile or buggy sender must cost a
// picture, never memory. Sixteen is far more than a conversation ever points
// at once, and the soonest-expiring is replaced when it overflows.
#define DBG_MAX_ANNOTATIONS 16
typedef struct {
   int      id;        // Meridian runtime object id
   COLORREF colour;
   DWORD    expires;   // GetTickCount() deadline; 0 = free slot
} DbgAnnotation;
static DbgAnnotation dbg_notes[DBG_MAX_ANNOTATIONS];
static int      dbg_note_port  = 0;               // 0 = annotations switched off
static SOCKET   dbg_note_sock  = INVALID_SOCKET;  // inbound, non-blocking, bound

/*****************************************************************************/
/*
 * OverlayPath:  Full path of the overlay file for a room.  Returns false if
 *   overlays are switched off or the name would not fit.
 */
static bool OverlayPath(int room_id, char *out, size_t len)
{
   int n;

   if (!dbg_enabled)
      return false;

   n = _snprintf_s(out, len, _TRUNCATE, "%s\\%d.ovl", dbg_dir, room_id);
   return n > 0;
}
/*****************************************************************************/
/*
 * DebugRoomURL:  The harness's page for the room we are standing in, if we can name it.
 *
 *   THE ROOM NUMBER IS NOT player.room_id.  The client knows the room by its server
 *   OBJECT id -- room 108 is object 751 -- and the harness keys everything by the kod
 *   room number.  The two are not derivable from one another here, and guessing would
 *   open a confidently wrong map, which is worse than opening none.
 *
 *   `dbg_num` is the harness's own number, read out of the overlay file's header when the
 *   overlay was loaded, so it is available exactly when the harness has something to say
 *   about this room.  When it is absent -- no overlay for this room -- this opens the
 *   server's index instead, which lists every room it can draw.  That is an honest answer
 *   and one click from the right page.
 */
bool DebugRoomURL(char *out, size_t len)
{
   if (dbg_map_url[0] == 0)
      return false;

   if (dbg_num > 0)
      return _snprintf_s(out, len, _TRUNCATE, "%s/%d", dbg_map_url, dbg_num) > 0;

   return _snprintf_s(out, len, _TRUNCATE, "%s/", dbg_map_url) > 0;
}
/*****************************************************************************/
/*
 * DebugForget:  Drop whatever overlay is loaded, freeing its GDI objects.
 *   Safe to call when nothing is loaded.
 */
static void DebugForget(void)
{
   int i;

   for (i = 0; i < dbg_num_colors; i++)
      if (dbg_colors[i].brush != NULL)
      {
         DeleteObject(dbg_colors[i].brush);
         dbg_colors[i].brush = NULL;
      }
   dbg_num_colors = 0;
   dbg_num_marks = 0;

   if (dbg_grid != NULL)
   {
      SafeFree(dbg_grid);
      dbg_grid = NULL;
   }
   dbg_rows = dbg_cols = 0;
   dbg_shift = 0;
   dbg_security = 0;
   dbg_num = 0;
   dbg_room = 0;
   dbg_room_name[0] = 0;
   dbg_have_mtime = false;
}
/*****************************************************************************/
/*
 * DbgBrushFor:  Make the brush for one palette entry.  PALETTERGB rather than RGB,
 *   because the map surface is an 8-bit palettized DIB and the stock map colours all
 *   go through the same macro — using RGB here would colour-match differently from
 *   every other thing drawn on this surface.
 */
static HBRUSH DbgBrushFor(COLORREF color, int style)
{
   switch (style)
   {
   case 1:  return CreateHatchBrush(HS_FDIAGONAL, color);
   case 2:  return CreateHatchBrush(HS_DIAGCROSS, color);
   case 3:  return CreateHatchBrush(HS_BDIAGONAL, color);
   default: return CreateSolidBrush(color);
   }
}
/*****************************************************************************/
/*
 * DbgStyle:  Style name to code.  An unrecognised name is SOLID rather than an
 *   error: a debugging overlay that refuses to draw because of a typo in a style
 *   is worse than one that draws it in the wrong pattern.
 */
static int DbgStyle(const char *name)
{
   if (name == NULL)              return 0;
   if (!_stricmp(name, "hatch"))  return 1;
   if (!_stricmp(name, "cross"))  return 2;
   if (!_stricmp(name, "diag"))   return 3;
   return 0;
}
/*****************************************************************************/
/*
 * DbgParseColor:  "RRGGBB" -> COLORREF.  Returns false on anything else.
 */
static bool DbgParseColor(const char *hex, COLORREF *out)
{
   unsigned int v = 0;
   int i;

   if (hex == NULL)
      return false;
   if (*hex == '#')
      hex++;
   for (i = 0; i < 6; i++)
   {
      char c = hex[i];
      v <<= 4;
      if      (c >= '0' && c <= '9') v |= (unsigned int) (c - '0');
      else if (c >= 'a' && c <= 'f') v |= (unsigned int) (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (unsigned int) (c - 'A' + 10);
      else return false;
   }
   if (hex[6] != 0 && hex[6] != '\n' && hex[6] != '\r' && hex[6] != ' ')
      return false;

   *out = PALETTERGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
   return true;
}
/*****************************************************************************/
/*
 * DebugLoadOverlay:  Read <dir>\<room>.ovl.  Returns true if something was drawn
 *   from it.  On any malformation the partial load is discarded whole — a half-read
 *   grid would be a confident picture of the wrong thing, which is the failure this
 *   whole file exists to prevent.
 */
static bool DebugLoadOverlay(int room_id)
{
   char path[MAX_PATH];
   FILE *f = NULL;
   char line[512];
   int  grid_row = 0;
   bool in_grid = false;
   bool ok = false;
   HANDLE h;

   DebugForget();

   if (!OverlayPath(room_id, path, sizeof(path)))
      return false;

   if (fopen_s(&f, path, "r") != 0 || f == NULL)
   {
      _snprintf_s(dbg_status, sizeof(dbg_status), _TRUNCATE, "no overlay");
      return false;
   }

   // Remember the file time so DebugIdle can notice the harness rewriting it.
   h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   if (h != INVALID_HANDLE_VALUE)
   {
      dbg_have_mtime = GetFileTime(h, NULL, NULL, &dbg_mtime) ? true : false;
      CloseHandle(h);
   }

   while (fgets(line, sizeof(line), f) != NULL)
   {
      char *nl = strpbrk(line, "\r\n");
      if (nl != NULL)
         *nl = 0;

      if (in_grid)
      {
         int c;
         if (grid_row >= dbg_rows)
         {
            in_grid = false;                 // extra lines after the grid are directives
         }
         else
         {
            for (c = 0; c < dbg_cols; c++)
               dbg_grid[grid_row * dbg_cols + c] = (line[c] != 0) ? (unsigned char) line[c]
                                                                  : (unsigned char) '.';
            grid_row++;
            continue;
         }
      }

      if (line[0] == 0 || line[0] == '#')
         continue;

      if (!strncmp(line, "rows ", 5))
         dbg_rows = atoi(line + 5);
      else if (!strncmp(line, "cols ", 5))
         dbg_cols = atoi(line + 5);
      else if (!strncmp(line, "shift ", 6))
         dbg_shift = atoi(line + 6);
      else if (!strncmp(line, "security ", 9))
         dbg_security = (unsigned int) strtoul(line + 9, NULL, 10);
      else if (!strncmp(line, "room ", 5))
         dbg_num = atoi(line + 5);          // the HARNESS's room number, not player.room_id
      else if (!strncmp(line, "name ", 5))
         strncpy_s(dbg_room_name, sizeof(dbg_room_name), line + 5, _TRUNCATE);
      else if (!strncmp(line, "color ", 6))
      {
         char ch = 0, hex[16] = {0}, style[16] = {0}, label[DBG_MAX_LABEL] = {0};
         COLORREF color;
         int got = sscanf_s(line + 6, "%c %15s %15s %63[^\n]",
                            &ch, (unsigned) sizeof(ch),
                            hex, (unsigned) sizeof(hex),
                            style, (unsigned) sizeof(style),
                            label, (unsigned) sizeof(label));
         if (got >= 2 && dbg_num_colors < DBG_MAX_COLORS && DbgParseColor(hex, &color))
         {
            DbgColor *e = &dbg_colors[dbg_num_colors];
            e->ch = ch;
            e->color = color;
            e->style = (got >= 3) ? DbgStyle(style) : 0;
            e->brush = DbgBrushFor(color, e->style);
            strncpy_s(e->label, sizeof(e->label), (got >= 4) ? label : "", _TRUNCATE);
            if (e->brush != NULL)
               dbg_num_colors++;
         }
      }
      else if (!strncmp(line, "mark ", 5))
      {
         int r = 0, c = 0;
         char hex[16] = {0}, label[DBG_MAX_LABEL] = {0};
         COLORREF color;
         int got = sscanf_s(line + 5, "%d %d %15s %63[^\n]",
                            &r, &c,
                            hex, (unsigned) sizeof(hex),
                            label, (unsigned) sizeof(label));
         if (got >= 3 && dbg_num_marks < DBG_MAX_MARKS && DbgParseColor(hex, &color))
         {
            DbgMark *m = &dbg_marks[dbg_num_marks++];
            m->row = r;
            m->col = c;
            m->color = color;
            strncpy_s(m->label, sizeof(m->label), (got >= 4) ? label : "", _TRUNCATE);
         }
      }
      else if (!strcmp(line, "grid"))
      {
         if (dbg_rows > 0 && dbg_cols > 0 && dbg_rows < 4096 && dbg_cols < 4096)
         {
            dbg_grid = (unsigned char *) SafeMalloc((size_t) dbg_rows * (size_t) dbg_cols);
            memset(dbg_grid, '.', (size_t) dbg_rows * (size_t) dbg_cols);
            in_grid = true;
            grid_row = 0;
         }
         else
         {
            break;                           // a grid with no dimensions is unreadable
         }
      }
   }

   fclose(f);

   // THE SECURITY NUMBER IS THE ONE CHECK WORTH FAILING ON.
   //
   // Room OBJECT ids are reissued whenever the server garbage-collects, which it does
   // on every save — so the id this file is named after can, in principle, come to mean
   // a different room. `room_type.security` is unique per .roo and is exactly the value
   // the client already uses to prove it loaded the right geometry. An overlay that
   // does not match it is not a near miss; it is a confident map of somewhere else,
   // which is worse than no map at all.
   if (dbg_security != 0 && current_room.security != 0
       && dbg_security != (unsigned int) current_room.security)
   {
      // BOTH NUMBERS IN THE MESSAGE. A bare "WRONG ROOM" is a verdict with no way to
      // tell a genuinely stale bake from a parsing mistake, and the first version of
      // this check was the second of those.
      unsigned int mine = dbg_security, theirs = (unsigned int) current_room.security;
      DebugForget();
      _snprintf_s(dbg_status, sizeof(dbg_status), _TRUNCATE,
                  "ovl WRONG ROOM %u!=%u", mine, theirs);
      return false;
   }

   ok = (dbg_grid != NULL && grid_row == dbg_rows) || dbg_num_marks > 0;
   if (ok)
   {
      dbg_room = room_id;
      if (dbg_num)
         _snprintf_s(dbg_status, sizeof(dbg_status), _TRUNCATE,
                     "ovl r%d %dx%d", dbg_num, dbg_rows, dbg_cols);
      else
         _snprintf_s(dbg_status, sizeof(dbg_status), _TRUNCATE,
                     "ovl %dx%d", dbg_rows, dbg_cols);
   }
   else
   {
      DebugForget();
      _snprintf_s(dbg_status, sizeof(dbg_status), _TRUNCATE, "overlay UNREADABLE");
   }
   return ok;
}
/*****************************************************************************/
/*
 * DebugInit:  Read the environment and set up.  Called once, from the same place
 *   the map's own GDI objects are made.
 */
void DebugInit(void)
{
   char buf[MAX_PATH];
   size_t got = 0;

   // ONCE PER PROCESS, BECAUSE THE HOOK IS NOT ONCE PER PROCESS.  DebugInit hangs off
   // MapInitialize, which hangs off InitGraphics — and that runs on every entry to the
   // game view, not at startup.  Without this guard each entry started Winsock again
   // and leaked another socket.
   if (dbg_once)
      return;
   dbg_once = true;

   dbg_status[0] = 0;
   dbg_room_name[0] = 0;
   memset(dbg_colors, 0, sizeof(dbg_colors));

   if (getenv_s(&got, buf, sizeof(buf), "M59_OVERLAY_DIR") == 0 && got > 1)
   {
      strncpy_s(dbg_dir, sizeof(dbg_dir), buf, _TRUNCATE);
      dbg_enabled = true;
      _snprintf_s(dbg_status, sizeof(dbg_status), _TRUNCATE, "no overlay");
   }

   if (getenv_s(&got, buf, sizeof(buf), "M59_SIGNAL_PORT") == 0 && got > 1)
      dbg_port = atoi(buf);

   if (getenv_s(&got, buf, sizeof(buf), "M59_DEBUG_TITLE") == 0 && got > 1)
      dbg_title = (atoi(buf) != 0);

   /* WHERE THE MAP OF THIS ROOM LIVES.  Same shape as the settings above: an environment
    * variable, absent by default, so a stock run of this client is unchanged.  Set it and
    * the help key stops opening the public guides and starts opening what the HARNESS
    * believes about the room you are standing in -- which is the question anybody running
    * this build actually has.  See StartHelp in winmsg.c. */
   if (getenv_s(&got, buf, sizeof(buf), "M59_MAP_URL") == 0 && got > 1)
      strncpy_s(dbg_map_url, sizeof(dbg_map_url), buf, _TRUNCATE);

   if (dbg_port > 0)
   {
      WSADATA wsa;
      // STARTED AND NEVER CLEANED UP, AND THAT ASYMMETRY IS THE WHOLE POINT.
      //
      // WSAStartup/WSACleanup are refcounted PER PROCESS, so a WSACleanup here is not a
      // local act — it is a decrement of the count com.c's game socket is living on.
      // The first version of this file called it from DebugClose, which hangs off
      // MapClose, which hangs off CloseGraphics — and that runs on every teardown of
      // the game view, not once at exit.  The count reached zero underneath a live
      // connection, `connection` stopped being CON_SOCKET, and the client died on
      // `assert(0)` at com.c:303 with no hint that the map code was involved.
      //
      // Leaking one Winsock reference for the life of the process costs nothing and
      // the process exit reclaims it.  Tearing one down under the game socket costs
      // the session.  There is no version of this worth balancing.
      //
      // MAKEWORD(1,1) matches com.c:62; the whole client is built on Winsock 1.
      if (WSAStartup(MAKEWORD(1, 1), &wsa) == 0)
      {
         dbg_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
         if (dbg_sock != INVALID_SOCKET)
         {
            memset(&dbg_addr, 0, sizeof(dbg_addr));
            dbg_addr.sin_family = AF_INET;
            dbg_addr.sin_port = htons((unsigned short) dbg_port);
            dbg_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
         }
      }
   }

   /* The inbound half.  Winsock may already be started above for the outbound
    * signal socket, but annotations must work when M59_SIGNAL_PORT is unset, so
    * start it here too -- WSAStartup is refcounted, and the long note above
    * explains why nothing in this file ever calls WSACleanup. */
   if (getenv_s(&got, buf, sizeof(buf), "M59_ANNOTATE_PORT") == 0 && got > 1)
      dbg_note_port = atoi(buf);

   if (dbg_note_port > 0)
   {
      WSADATA wsa_in;
      if (WSAStartup(MAKEWORD(1, 1), &wsa_in) == 0)
      {
         dbg_note_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
         if (dbg_note_sock != INVALID_SOCKET)
         {
            struct sockaddr_in bind_addr;
            unsigned long nonblocking = 1;
            memset(&bind_addr, 0, sizeof(bind_addr));
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_port = htons((unsigned short) dbg_note_port);
            bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            /* NON-BLOCKING BEFORE ANYTHING ELSE.  This is drained from MainIdle;
             * a blocking recvfrom there would freeze the client whenever nothing
             * was sending, which is exactly the cost a debugging aid must never
             * impose on a live session. */
            if (ioctlsocket(dbg_note_sock, FIONBIO, &nonblocking) != 0 ||
                bind(dbg_note_sock, (struct sockaddr *) &bind_addr,
                     sizeof(bind_addr)) != 0)
            {
               closesocket(dbg_note_sock);
               dbg_note_sock = INVALID_SOCKET;
               dbg_note_port = 0;
            }
         }
      }
   }

   if (dbg_enabled || dbg_port > 0 || dbg_note_port > 0)
      debug(("m59dbg: overlay dir '%s', signal port %d, annotate port %d\n",
             dbg_enabled ? dbg_dir : "(off)", dbg_port, dbg_note_port));
}
/*****************************************************************************/
/*
 * DebugClose:  Give back the overlay's own memory and brushes, and NOTHING ELSE.
 *
 * It is called from MapClose, which is called from CloseGraphics, which runs on every
 * teardown of the game view — so anything released here is released repeatedly and at
 * times that have nothing to do with the process ending.  The socket deliberately
 * outlives this; see the note in DebugInit about what happened when it did not.
 */
void DebugClose(void)
{
   DebugForget();
}
/*****************************************************************************/
void DebugEnterRoom(int room_id)
{
   if (!dbg_enabled)
      return;
   DebugLoadOverlay(room_id);
   DebugSignal("room", NULL);
}
/*****************************************************************************/
/*
 * DebugDrawOverlay:  Paint the loaded overlay in room coordinates.
 *
 * THE SQUARE CONVENTION LIVES IN THE FILE, NOT HERE.  The harness counts squares
 * from 1 and the client's own fine coordinates put square N at [(N-1)*FINENESS,
 * N*FINENESS) — an off-by-one that is invisible until it is nine squares wrong at
 * the far side of a room.  `shift` in the file is what reconciles them, so a
 * convention change is a rewrite of a text file rather than a rebuild of a client.
 */
void DebugDrawOverlay(HDC hdc, int x, int y, float scale, room_type *room)
{
   int r, c, i;
   HBRUSH hOldBrush;
   HPEN hOldPen;

   if (dbg_room == 0 || dbg_room != player.room_id)
      return;
   if (room == NULL)
      return;

   hOldBrush = (HBRUSH) GetCurrentObject(hdc, OBJ_BRUSH);
   hOldPen = (HPEN) GetCurrentObject(hdc, OBJ_PEN);

   if (dbg_grid != NULL)
   {
      for (r = 0; r < dbg_rows; r++)
      {
         for (c = 0; c < dbg_cols; c++)
         {
            unsigned char ch = dbg_grid[r * dbg_cols + c];
            RECT rc;
            int k;

            if (ch == '.' || ch == ' ')
               continue;

            for (k = 0; k < dbg_num_colors; k++)
               if ((unsigned char) dbg_colors[k].ch == ch)
                  break;
            if (k == dbg_num_colors)
               continue;                     // a character with no colour draws nothing

            rc.left   = x + (int) ((float) ((c + dbg_shift)     * FINENESS) * scale);
            rc.top    = y + (int) ((float) ((r + dbg_shift)     * FINENESS) * scale);
            rc.right  = x + (int) ((float) ((c + dbg_shift + 1) * FINENESS) * scale);
            rc.bottom = y + (int) ((float) ((r + dbg_shift + 1) * FINENESS) * scale);

            // At low zoom a square can round to nothing; one pixel is the floor,
            // because a hazard that vanishes when you zoom out is worse than a
            // slightly fat one.
            if (rc.right <= rc.left)
               rc.right = rc.left + 1;
            if (rc.bottom <= rc.top)
               rc.bottom = rc.top + 1;

            FillRect(hdc, &rc, dbg_colors[k].brush);
         }
      }
   }

   // Marks are drawn after the squares and are deliberately loud: they are the
   // "look HERE" channel, and there are at most a few dozen of them.
   if (dbg_num_marks > 0)
   {
      SelectObject(hdc, GetStockObject(NULL_BRUSH));
      for (i = 0; i < dbg_num_marks; i++)
      {
         HPEN pen = CreatePen(PS_SOLID, 2, dbg_marks[i].color);
         HPEN old;
         int cx, cy, rad;

         if (pen == NULL)
            continue;
         old = (HPEN) SelectObject(hdc, pen);

         cx = x + (int) ((float) ((dbg_marks[i].col + dbg_shift) * FINENESS
                                  + FINENESS / 2) * scale);
         cy = y + (int) ((float) ((dbg_marks[i].row + dbg_shift) * FINENESS
                                  + FINENESS / 2) * scale);
         rad = (int) ((float) FINENESS * scale);
         if (rad < 4)
            rad = 4;

         Ellipse(hdc, cx - rad, cy - rad, cx + rad, cy + rad);
         MoveToEx(hdc, cx - rad, cy, NULL);
         LineTo(hdc, cx + rad, cy);
         MoveToEx(hdc, cx, cy - rad, NULL);
         LineTo(hdc, cx, cy + rad);

         SelectObject(hdc, old);
         DeleteObject(pen);
      }
   }

   SelectObject(hdc, hOldBrush);
   SelectObject(hdc, hOldPen);
}
/*****************************************************************************/
/*
 * DebugSignal:  One UDP datagram to the listener, or nothing at all.
 *
 * UDP because it cannot block the render loop, needs no connect, needs no listener
 * to exist, and cannot half-write.  A dropped datagram costs one notification; a
 * blocking socket in here would cost the frame.
 */
void DebugSignal(const char *kind, const char *detail)
{
   char msg[512];
   int n;

   if (dbg_sock == INVALID_SOCKET)
      return;

   n = _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                   "{\"kind\":\"%s\",\"detail\":\"%s\",\"account\":\"%s\",\"room\":%d,"
                   "\"row\":%d,\"col\":%d,\"fine_x\":%d,\"fine_y\":%d,\"angle\":%d,"
                   "\"ms\":%lu}\n",
                   kind != NULL ? kind : "?",
                   detail != NULL ? detail : "",
                   config.username,
                   (int) player.room_id,
                   (player.y >> LOG_FINENESS) + 1,
                   (player.x >> LOG_FINENESS) + 1,
                   player.x, player.y,
                   (int) player.angle,
                   (unsigned long) timeGetTime());
   if (n > 0)
      sendto(dbg_sock, msg, n, 0, (struct sockaddr *) &dbg_addr, sizeof(dbg_addr));
}
/*****************************************************************************/
/*
 * DebugKey:  F5..F9, ahead of the modules.  F9 toggles recording.
 *
 * AHEAD OF THE MODULES ON PURPOSE.  merintr binds every function key to
 * A_TEXTCOMMAND so that user aliases can claim them, so a key handled after the
 * modules is a key that sometimes works.  These four are taken from the alias
 * space deliberately, and only while signalling is switched on.
 */
bool DebugKey(UINT vk, BOOL fDown, UINT flags)
{
   if (dbg_sock == INVALID_SOCKET)
      return false;
   if (!fDown || (flags & 0x4000))            // 0x4000 is auto-repeat
      return false;

   switch (vk)
   {
   /* F9 IS THE RECORD TOGGLE, AND IT IS A TOGGLE RATHER THAN A HOLD ON PURPOSE.
    *
    * The thing worth capturing is usually several seconds long and involves both hands on
    * the movement keys -- a jump, a stuck corner, a doorway that refuses.  Holding a key
    * through it is not something a person can do while also doing the thing.
    *
    * What goes out is a start and a stop, each stamped with where the character was when
    * it was pressed.  The harness has the timestamps and every ledger it writes; this only
    * has to say WHEN, and where the human was looking when they said it.  That is the whole
    * division of labour: the person marks the moment, the machine keeps the evidence. */
   case VK_F9:
      dbg_recording = !dbg_recording;
      {
         char detail[128];
         _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%s room=%d sq=%d,%d fine=%d,%d",
                     dbg_recording ? "start" : "stop",
                     dbg_num > 0 ? dbg_num : (int) player.room_id,
                     (player.y >> LOG_FINENESS) + 1,
                     (player.x >> LOG_FINENESS) + 1,
                     player.x, player.y);
         DebugSignal("record", detail);
      }
      return true;
   case VK_F5:  DebugSignal("key", "F5");  return true;
   case VK_F6:  DebugSignal("key", "F6");  return true;
   case VK_F7:  DebugSignal("key", "F7");  return true;
   case VK_F8:  DebugSignal("key", "F8");  return true;
   }
   return false;
}
/*****************************************************************************/
/*
 * DebugAnnotationFor:  Is this object highlighted, and in what colour?
 *
 * Called from the per-object draw, so it must be cheap and must never allocate,
 * block or touch a file.  Expiry is checked here rather than swept, so a mark
 * whose deadline has passed simply stops drawing on the next frame.
 */
bool DebugAnnotationFor(int object_id, COLORREF *out)
{
   int i;
   DWORD now;

   if (dbg_note_port <= 0 || object_id == 0)
      return false;

   now = GetTickCount();
   for (i = 0; i < DBG_MAX_ANNOTATIONS; i++)
   {
      if (dbg_notes[i].expires == 0 || dbg_notes[i].id != object_id)
         continue;
      /* A tick count wraps about every 49 days; comparing the difference rather
       * than the values means a wrap costs one stale frame, not a stuck mark. */
      if ((int) (now - dbg_notes[i].expires) >= 0)
      {
         dbg_notes[i].expires = 0;
         return false;
      }
      if (out != NULL)
         *out = dbg_notes[i].colour;
      return true;
   }
   return false;
}
/*****************************************************************************/
/*
 * DebugAnnotationRemember:  Record one, replacing any already held for that object.
 */
static void DebugAnnotationRemember(int object_id, COLORREF colour, int ttl_ms)
{
   int i, soonest = 0;
   DWORD now = GetTickCount();

   if (ttl_ms < 1000)   ttl_ms = 1000;
   if (ttl_ms > 600000) ttl_ms = 600000;

   for (i = 0; i < DBG_MAX_ANNOTATIONS; i++)
      if (dbg_notes[i].expires != 0 && dbg_notes[i].id == object_id)
      {
         dbg_notes[i].colour = colour;
         dbg_notes[i].expires = now + ttl_ms;
         return;
      }
   for (i = 0; i < DBG_MAX_ANNOTATIONS; i++)
      if (dbg_notes[i].expires == 0)
      {
         dbg_notes[i].id = object_id;
         dbg_notes[i].colour = colour;
         dbg_notes[i].expires = now + ttl_ms;
         return;
      }
   for (i = 1; i < DBG_MAX_ANNOTATIONS; i++)
      if ((int) (dbg_notes[i].expires - dbg_notes[soonest].expires) < 0)
         soonest = i;
   dbg_notes[soonest].id = object_id;
   dbg_notes[soonest].colour = colour;
   dbg_notes[soonest].expires = now + ttl_ms;
}
/*****************************************************************************/
/*
 * DebugAnnotationDrain:  Read whatever arrived, without ever waiting.
 *
 * The datagram is one line of key=value pairs rather than JSON, because this is
 * C inside a game loop and a parser here would be a liability:
 *
 *    id=<object id> colour=<RRGGBB> ttl=<milliseconds>
 *
 * Anything unparsed is dropped in silence.  A malformed datagram must cost a
 * picture and never a session -- the same rule the overlay file follows.
 */
static void DebugAnnotationDrain(void)
{
   char packet[512];
   struct sockaddr_in from;
   int fromlen, got, id, ttl;
   unsigned int colour;
   const char *at;

   if (dbg_note_sock == INVALID_SOCKET)
      return;

   for (;;)
   {
      fromlen = sizeof(from);
      got = recvfrom(dbg_note_sock, packet, sizeof(packet) - 1, 0,
                     (struct sockaddr *) &from, &fromlen);
      if (got <= 0)
         return;                    /* WSAEWOULDBLOCK, or nothing waiting */
      packet[got] = 0;

      /* turn=<0..4095>: face this server angle, as if the player had turned there.
       *
       * The harness's cameraman (m59-harness tools/m59-cameraman.mjs) keeps a parked
       * character's view on a fight for video.  It used to do this by forging BP_TURN
       * into the client's stream through a proxy, and the client died on the first
       * forged packet.  Doing it here instead goes through the ordinary path: set our
       * own angle, and MoveUpdateServer sends the BP_REQ_TURN itself (statgame.c calls
       * it every tick, so a turn throttled by MOVE_INTERVAL still reaches the server
       * a moment later).  Loopback only, like the rest of this socket, and inert
       * outside play. */
      at = strstr(packet, "turn=");
      if (at != NULL)
      {
         int angle = atoi(at + 5);
         if (angle >= 0 && angle < MAX_ANGLE &&
             state == STATE_GAME && GameGetState() == GAME_PLAY)
         {
            player.angle = ANGLE_STOC(angle) % NUMDEGREES;
            MoveUpdateServer();
            RedrawAll();
         }
         continue;
      }

      ttl = 20000;
      colour = 0x00FF0000;          /* red, the attack colour */

      at = strstr(packet, "id=");
      if (at == NULL)
         continue;
      id = atoi(at + 3);
      if (id == 0)
         continue;

      at = strstr(packet, "colour=");
      if (at != NULL)
         colour = (unsigned int) strtoul(at + 7, NULL, 16);

      at = strstr(packet, "ttl=");
      if (at != NULL)
         ttl = atoi(at + 4);

      /* The wire carries RRGGBB; PALETTERGB matches the game palette the way
       * every other colour in this file does. */
      DebugAnnotationRemember(id,
                              PALETTERGB((colour >> 16) & 0xff,
                                         (colour >> 8) & 0xff,
                                         colour & 0xff),
                              ttl);
   }
}
/*****************************************************************************/
/*
 * DebugIdle:  Keep the caption current and notice the overlay file changing.
 *
 * THE CAPTION IS THE CHEAPEST OF THE THREE FEATURES AND THE MOST USED.  The stock
 * client never calls SetWindowText on the main window at all, so twenty-one fleet
 * members are twenty-one windows called "Meridian 59" — indistinguishable in the
 * task bar, in Alt-Tab, and to anything that goes looking for one by name.
 */
void DebugIdle(void)
{
   DebugAnnotationDrain();

   static DWORD last_title = 0, last_stat = 0;
   static int   last_room = -1;
   DWORD now = timeGetTime();

   if (dbg_enabled && player.room_id != last_room)
   {
      // A room change reaches MapEnterRoom, but a client that starts up already in a
      // room, or one whose overlay arrived late, would otherwise never look.
      last_room = player.room_id;
      DebugLoadOverlay(player.room_id);
   }

   if (dbg_enabled && now - last_stat >= DBG_STAT_MS)
   {
      char path[MAX_PATH];
      last_stat = now;

      // NOTHING LOADED IS A STATE TO RETRY FROM, NOT A VERDICT.
      //
      // The overlay for the room you are standing in is routinely written WHILE you
      // are standing in it — that is the working loop: look, regenerate, look again.
      // Watching only files we already loaded meant the first attempt in a room
      // decided the answer for as long as you stayed there, so a freshly generated
      // overlay appeared to do nothing and the tool looked broken.
      if (dbg_room == 0)
      {
         if (player.room_id != 0)
            DebugLoadOverlay((int) player.room_id);
      }
      else if (dbg_have_mtime && OverlayPath(dbg_room, path, sizeof(path)))
      {
         HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
         if (h != INVALID_HANDLE_VALUE)
         {
            FILETIME ft;
            bool changed = false;
            if (GetFileTime(h, NULL, NULL, &ft))
               changed = (CompareFileTime(&ft, &dbg_mtime) != 0);
            CloseHandle(h);
            if (changed)
               DebugLoadOverlay(dbg_room);
         }
      }
   }

   if (!dbg_title || hMain == NULL)
      return;
   if (now - last_title < DBG_TITLE_MS)
      return;
   last_title = now;

   {
      char title[256];
      // The room's own name resource rather than the overlay's `name` line: it is
      // always there, it is what the server said, and merintr's InterfaceNewRoom is
      // already using it for the caption this one replaces five times a second.
      const char *where = (player.room_name_res != 0) ? LookupNameRsc(player.room_name_res)
                                                      : dbg_room_name;
      if (where == NULL)
         where = "";

      // Squares are stated in the HARNESS's convention — 1-based, row first — because
      // that is the convention every tool, every log line and every bug report in the
      // harness uses.  The client's own 0-based index is a different number for the
      // same place, and a person reading one and typing the other is a whole evening.
      // RECORDING IS SAID AT THE END, IN WORDS, because the whole point of the toggle is
      // that a person can glance up and know whether the thing they are about to do is
      // being captured.  A silent recorder is worse than none: it is a person doing the
      // interesting thing twice, once outside the window.
      _snprintf_s(title, sizeof(title), _TRUNCATE,
                  "M59%s%s | %d %s | sq %d,%d | fine %d,%d%s%s%s",
                  config.username[0] ? " " : "",
                  config.username[0] ? config.username : "",
                  (int) player.room_id,
                  where,
                  (player.y >> LOG_FINENESS) + 1,
                  (player.x >> LOG_FINENESS) + 1,
                  player.x, player.y,
                  dbg_status[0] ? " | " : "",
                  dbg_status[0] ? dbg_status : "",
                  dbg_recording ? "  [* RECORDING *]" : "");
      SetWindowText(hMain, title);
   }
}
