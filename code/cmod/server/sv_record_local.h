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
#include <setjmp.h>

/* ******************************************************************************** */
// Definitions
/* ******************************************************************************** */

typedef struct {
	char *data;
	unsigned int position;
	unsigned int size;

	// Overflow abort
	qboolean abort_set;
	jmp_buf abort;
} record_data_stream_t;

typedef enum {
	RP_ALL,
	RP_DEBUG
} record_print_mode_t;

typedef struct {
	int active_flags[(MAX_GENTITIES+31)/32];
	entityState_t entities[MAX_GENTITIES];
} record_entityset_t;

typedef struct {
	int ent_visibility[(MAX_GENTITIES+31)/32];
	int area_visibility[8];
	int area_visibility_size;
} record_visibility_state_t;

typedef struct {
	int serverTime;
	int angles[3];
	byte buttons;
	char forwardmove, rightmove, upmove;
	byte weapon;
	byte padding[3];
} record_usercmd_t;

typedef struct {
	playerState_t playerstate;
	record_visibility_state_t visibility;
	record_usercmd_t usercmd;
} record_state_client_t;

typedef struct {
	// Holds current data state of the record stream for both recording and playback
	record_entityset_t entities;
	record_state_client_t *clients;
	int max_clients;
	char *configstrings[MAX_CONFIGSTRINGS];
	char *current_servercmd;
} record_state_t;

#define RECORD_PROTOCOL 6

typedef enum {
	// State
	RC_STATE_ENTITY_SET = 32,
	RC_STATE_PLAYERSTATE,
	RC_STATE_VISIBILITY,
	RC_STATE_USERCMD,
	RC_STATE_CONFIGSTRING,
	RC_STATE_CURRENT_SERVERCMD,

	// Events
	RC_EVENT_BASELINES,
	RC_EVENT_SNAPSHOT,
	RC_EVENT_SERVERCMD,
	RC_EVENT_CLIENT_ENTER_WORLD,
	RC_EVENT_CLIENT_DISCONNECT,
	RC_EVENT_MAP_RESTART
} record_command_t;

/* ******************************************************************************** */
// Main
/* ******************************************************************************** */

extern cvar_t *admin_spectator_enabled;
extern cvar_t *admin_spectator_password;
extern cvar_t *admin_spectator_slots;

extern cvar_t *record_auto_recording;
extern cvar_t *record_full_bot_data;
extern cvar_t *record_full_usercmd_data;

extern cvar_t *record_convert_legacy_protocol;
extern cvar_t *record_convert_weptiming;
extern cvar_t *record_convert_simulate_follow;

extern cvar_t *record_verify_data;
extern cvar_t *record_debug_prints;

/* ******************************************************************************** */
// Writer
/* ******************************************************************************** */

void record_write_usercmd(usercmd_t *usercmd, int clientNum);
void record_write_configstring_change(int index, const char *value);
void record_write_servercmd(int clientNum, const char *value);
void record_write_snapshot(void);
void record_write_stop(void);
void record_start_cmd(void);
void record_stop_cmd(void);

/* ******************************************************************************** */
// Convert
/* ******************************************************************************** */

void record_convert_cmd(void);
void record_scan_cmd(void);

/* ******************************************************************************** */
// Spectator
/* ******************************************************************************** */

void record_spectator_status(void);
void record_spectator_process_snapshot(void);
qboolean record_spectator_process_connection(netadr_t *address, const char *userinfo, qboolean compat);
qboolean record_spectator_process_packet_event(netadr_t *address, msg_t *msg, int qport);
void record_spectator_process_map_loaded(void);
void record_spectator_process_configstring_change(int index, const char *value);
void record_spectator_process_servercmd(int clientNum, const char *value);
void record_spectator_process_usercmd(int clientNum, usercmd_t *usercmd);

/* ******************************************************************************** */
// Common
/* ******************************************************************************** */

// ***** Data Stream *****

void record_stream_error(record_data_stream_t *stream, const char *message);
char *record_stream_write_allocate(int size, record_data_stream_t *stream);
void record_stream_write(void *data, int size, record_data_stream_t *stream);
void record_stream_write_value(int value, int size, record_data_stream_t *stream);
char *record_stream_read_static(int size, record_data_stream_t *stream);
void record_stream_read_buffer(void *output, int size, record_data_stream_t *stream);
void dump_stream_to_file(record_data_stream_t *stream, fileHandle_t file);

// ***** Memory Allocation *****

void *record_calloc(unsigned int size);
void record_free(void *ptr);

// ***** Bit Operations *****

void record_bit_set(int *target, int position);
void record_bit_unset(int *target, int position);
int record_bit_get(int *source, int position);

// ***** Flag Operations *****

qboolean usercmd_is_firing_weapon(const usercmd_t *cmd);
qboolean playerstate_is_spectator(const playerState_t *ps);
void playerstate_set_follow_mode(playerState_t *ps);

// ***** Message Printing *****

void QDECL record_printf(record_print_mode_t mode, const char *fmt, ...) Q_PRINTF_FUNC(2, 3);

// ***** Record State *****

record_state_t *allocate_record_state(int max_clients);
void free_record_state(record_state_t *rs);

// ***** Structure Encoding/Decoding Functions *****

void record_encode_string(char *string, record_data_stream_t *stream);
char *record_decode_string(record_data_stream_t *stream, int *length_out);
void record_encode_playerstate(playerState_t *state, playerState_t *source, record_data_stream_t *stream);
void record_decode_playerstate(playerState_t *state, record_data_stream_t *stream);
void record_encode_entitystate(entityState_t *state, entityState_t *source, record_data_stream_t *stream);
void record_decode_entitystate(entityState_t *state, record_data_stream_t *stream);
void record_encode_entityset(record_entityset_t *state, record_entityset_t *source, record_data_stream_t *stream);
void record_decode_entityset(record_entityset_t *state, record_data_stream_t *stream);
void record_encode_visibility_state(record_visibility_state_t *state, record_visibility_state_t *source,
		record_data_stream_t *stream);
void record_decode_visibility_state(record_visibility_state_t *state, record_data_stream_t *stream);
void record_encode_usercmd(record_usercmd_t *state, record_usercmd_t *source, record_data_stream_t *stream);
void record_decode_usercmd(record_usercmd_t *state, record_data_stream_t *stream);

// ***** Usercmd Conversion *****

void record_convert_usercmd_to_record_usercmd(usercmd_t *source, record_usercmd_t *target);
void record_convert_record_usercmd_to_usercmd(record_usercmd_t *source, usercmd_t *target);

// ***** Entity Set Building *****

void get_current_entities(record_entityset_t *target);
void get_current_baselines(record_entityset_t *target);

// ***** Visibility Building *****

void record_get_current_visibility(int clientNum, record_visibility_state_t *target);
void record_tweak_inactive_visibility(record_entityset_t *entityset, record_visibility_state_t *delta,
			record_visibility_state_t *source, record_visibility_state_t *target);

// ***** Message Building *****

void record_write_gamestate_message(record_entityset_t *baselines, char **configstrings, int clientNum,
		int serverCommandSequence, msg_t *msg, int *baseline_cutoff_out);
void record_write_snapshot_message(record_entityset_t *entities, record_visibility_state_t *visibility, playerState_t *ps,
		record_entityset_t *delta_entities, record_visibility_state_t *delta_visibility, playerState_t *delta_ps,
		record_entityset_t *baselines, int baseline_cutoff, int lastClientCommand, int deltaFrame, int snapFlags,
		int sv_time, msg_t *msg);
