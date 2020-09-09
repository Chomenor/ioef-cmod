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

#ifdef CMOD_COMMON_SERVER_INFOSTRING_HOOKS
char *sv_get_serverinfo_string(qboolean status_query) {
	char *info = Cvar_InfoString(CVAR_SERVERINFO);
#ifdef CMOD_SERVER_INFOSTRING_OVERRIDE
	if(*sv_override_client_map->string) {
		Info_SetValueForKey(info, "mapname", sv_override_client_map->string); }
#endif
	return info; }

char *sv_get_systeminfo_string(void) {
	char *info = Cvar_InfoString_Big(CVAR_SYSTEMINFO);
#ifdef CMOD_SERVER_INFOSTRING_OVERRIDE
	if(*sv_override_client_mod->string) {
		Info_SetValueForKey_Big(info, "fs_game", sv_override_client_mod->string); }
#endif
	return info; }
#endif

#ifdef CMOD_DOWNLOAD_PROTOCOL_FIXES
void write_download_dummy_snapshot(client_t *client, msg_t *msg) {
	// Legacy protocol requires a snapshot even in download messages, so send a minimal snapshot
	MSG_WriteByte(msg, svc_snapshot);
	MSG_WriteLong(msg, client->lastClientCommand);

	if(client->oldServerTime) MSG_WriteLong(msg, client->oldServerTime);
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

#ifdef CMOD_GAMESTATE_OVERFLOW_FIX
void sv_calculate_max_baselines(client_t *client, msg_t msg) {
	// Determines amount of baselines that can be written to gamestate message without causing overflow
	// and sets client->baseline_cutoff
	byte		msgBuffer[MAX_MSGLEN];
	int			start;
	entityState_t	*base, nullstate;
	int valid_baselines = 0;
	int total_baselines = 0;
	int highest_valid_baseline = -1;

	msg.data = msgBuffer;	// Just to be safe

	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( start = 0 ; start < MAX_GENTITIES; start++ ) {
		base = &sv.svEntities[start].baseline;
		if ( !base->number ) {
			continue;
		}
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, base, qtrue );

		++total_baselines;
		if(msg.cursize + 24 < msg.maxsize) {
			++valid_baselines;
			highest_valid_baseline = start; }
	}

	if(valid_baselines != total_baselines) {
		client->baseline_cutoff = highest_valid_baseline + 1;
		cmLog(LOG_SERVER, LOGFLAG_COM_PRINTF, "Skipping baselines for client %i to avoid gamestate overflow - "
				"writing %i of %i baselines", (int)(client-svs.clients), valid_baselines, total_baselines); }
	else {
		client->baseline_cutoff = -1; } }
#endif
