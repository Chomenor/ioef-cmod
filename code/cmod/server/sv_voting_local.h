/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2019 Noah Metzger (chomenor@gmail.com)

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../../server/server.h"

#ifdef CMOD_LOGGING
#define VOTE_LOG(...) cmLog(LOG_VOTING, 0, __VA_ARGS__)
#define VOTE_LOG_FLUSH(...) cmLog(LOG_VOTING, LOGFLAG_FLUSH, __VA_ARGS__)
#else
#define VOTE_LOG(...) Com_Printf(__VA_ARGS__)
#define VOTE_LOG_FLUSH(...) Com_Printf(__VA_ARGS__)
#endif

typedef struct {
	char info_string[256];
	char pass_command[1024];
} vote_action_t;

qboolean voteaction_process_callvote(client_t *client, qboolean vote_in_progress, vote_action_t *action_output);
