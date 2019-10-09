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

// For misc cmod server stuff, included at the end of server.h

#ifdef CMOD_DL_PROTOCOL_FIXES
void write_download_dummy_snapshot(client_t *client, msg_t *msg);
#endif

#ifdef CMOD_RECORD
void record_initialize(void);
void record_process_usercmd(int clientNum, usercmd_t *usercmd);
void record_process_configstring_change(int index, const char *value);
void record_process_servercmd(int clientNum, const char *value);
void record_process_map_loaded(void);
void record_process_snapshot(void);
void record_game_shutdown(void);
void record_verify_visibility_check(int clientNum, int numSnapshotEntities, int *snapshotEntities, int areabytes, byte *areabits);
qboolean record_process_connection(netadr_t *address, const char *userinfo, qboolean compat);
qboolean record_process_packet_event(netadr_t *address, msg_t *msg, int qport);
#endif

#ifdef CMOD_SERVER_CMD_TOOLS
void cmod_sv_cmd_tools_init(void);
#endif

#ifdef CMOD_VOTING
qboolean cmod_voting_handle_command(client_t *client, const char *cmd_string);
void cmod_voting_handle_map_restart(void);
void cmod_voting_handle_map_change(void);
void cmod_voting_frame(void);
#endif

#ifdef CMOD_MAPTABLE
typedef struct {
	char *key;
	char *value;
} cmod_maptable_entry_t;

typedef struct {
	cmod_maptable_entry_t *entries;		// may be null if no entries
	unsigned int entry_count;
	qboolean maptable_loaded;	// true if any maptable files (including empty files) were found for given map
} cmod_maptable_t;

cmod_maptable_t cmod_maptable_load(const char *map_name, qboolean verbose);
void cmod_maptable_free(cmod_maptable_t *maptable);
const char *cmod_maptable_get_value(cmod_maptable_t *maptable, const char *key);
void cmod_maptable_init(void);
#endif
