// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * admin.h
 *
 */

#ifndef _ADMIN_H
#define _ADMIN_H

/* MOD: raised from 120. The admin command line is the control plane for an
   external process driving the world, and 120 bytes is a hard cap on every
   command including its prefix — `send object <room> SomeoneSaidRoom what
   OBJECT <id> type INT 1 string Q ` alone is 68 characters, leaving 50 for the
   actual sentence, silently truncated with the remainder discarded.

   4096 is comfortably below LEN_TEMP_STRING (6000, blakserv/bstring.h), which
   is what the string ultimately lands in, so nothing downstream needs changing.
   This is a fixed-size field in admin_data, so the cost is 4KB per admin
   session. */
#define MAX_ADMIN_COMMAND 4096

typedef struct
{
   char command[MAX_ADMIN_COMMAND];
} admin_data;

#endif
