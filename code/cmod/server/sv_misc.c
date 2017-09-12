/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2017 Noah Metzger (chomenor@gmail.com)

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

#ifdef CMOD_DL_PROTOCOL_FIXES
void write_download_dummy_snapshot(client_t *client, msg_t *msg) {
	// Legacy protocol requires a snapshot even in download messages, so send a minimal snapshot
	MSG_WriteByte(msg, svc_snapshot);
	MSG_WriteLong(msg, client->lastClientCommand);

	if(client->oldServerTime) MSG_WriteLong(msg, sv.time + client->oldServerTime);
	else MSG_WriteLong(msg, sv.time);

	// Delta frame, snapflags, areabits
	MSG_WriteByte(msg, 0);
	MSG_WriteByte(msg, 0);
	MSG_WriteByte(msg, 0);

	// Playerstate
	MSG_WriteBits(msg, 0, 32);
	MSG_WriteBits(msg, 0, 20);

	// Entities
	MSG_WriteBits(msg, (MAX_GENTITIES-1), GENTITYNUM_BITS); }
#endif
