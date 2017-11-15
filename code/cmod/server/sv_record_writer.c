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

#ifdef CMOD_RECORD
#include "sv_record_local.h"

/* ******************************************************************************** */
// Definitions
/* ******************************************************************************** */

typedef struct {
	qboolean auto_started;

	record_state_t *rs;
	char active_players[256];
	int last_snapflags;

	char *target_directory;
	char *target_filename;

	fileHandle_t recordfile;
	record_data_stream_t stream;
	char stream_buffer[131072];
} record_writer_state_t;

record_writer_state_t *rws;

/* ******************************************************************************** */
// State-Updating Operations
/* ******************************************************************************** */

static qboolean compare_entity_states(record_entityset_t *state1, record_entityset_t *state2, qboolean verbose) {
	// Returns qtrue on discrepancy, qfalse otherwise
	qboolean discrepancy = qfalse;
	int i;
	for(i=0; i<MAX_GENTITIES; ++i) {
		if(record_bit_get(state1->active_flags, i) != record_bit_get(state2->active_flags, i)) {
			if(verbose) record_printf(RP_ALL, "Entity %i active discrepancy\n", i);
			discrepancy = qtrue;
			continue; }
		if(!record_bit_get(state1->active_flags, i)) continue;
		if(memcmp(&state1->entities[i], &state2->entities[i], sizeof(*state1->entities))) {
			if(verbose) {
				record_printf(RP_ALL, "Entity %i content discrepancy\n", i); }
			discrepancy = qtrue;
			continue; } }
	return discrepancy; }

static void record_update_entityset(record_entityset_t *entities) {
	record_data_stream_t verify_stream;
	record_entityset_t *verify_entities = 0;

	record_stream_write_value(RC_STATE_ENTITY_SET, 1, &rws->stream);

	if(record_verify_data->integer) {
		verify_stream = rws->stream;
		verify_entities = Z_Malloc(sizeof(*verify_entities));
		*verify_entities = rws->rs->entities; }

	record_encode_entityset(&rws->rs->entities, entities, &rws->stream);

	if(record_verify_data->integer) {
		record_decode_entityset(verify_entities, &verify_stream);
		if(verify_stream.position != rws->stream.position) {
			record_printf(RP_ALL, "record_update_entityset: verify stream in different position\n"); }
		else if(compare_entity_states(entities, verify_entities, qtrue)) {
			record_printf(RP_ALL, "record_update_entityset: verify discrepancy\n"); }
		Z_Free(verify_entities); } }

static void record_update_playerstate(playerState_t *ps, int clientNum) {
	record_data_stream_t verify_stream;
	playerState_t verify_ps;

	if(!memcmp(ps, &rws->rs->clients[clientNum].playerstate, sizeof(*ps))) return;

	record_stream_write_value(RC_STATE_PLAYERSTATE, 1, &rws->stream);

	// We can't rely on ps->clientNum because it can be wrong due to spectating and such
	record_stream_write_value(clientNum, 1, &rws->stream);

	if(record_verify_data->integer) {
		verify_stream = rws->stream;
		verify_ps = rws->rs->clients[clientNum].playerstate; }

	record_encode_playerstate(&rws->rs->clients[clientNum].playerstate, ps, &rws->stream);

	if(record_verify_data->integer) {
		record_decode_playerstate(&verify_ps, &verify_stream);
		if(verify_stream.position != rws->stream.position) {
			record_printf(RP_ALL, "record_update_playerstate: verify stream in different position\n"); }
		else if(memcmp(ps, &verify_ps, sizeof(*ps))) {
			record_printf(RP_ALL, "record_update_playerstate: verify discrepancy\n"); } } }

static void record_update_visibility_state(record_visibility_state_t *vs, int clientNum) {
	record_data_stream_t verify_stream;
	record_visibility_state_t verify_vs;

	if(!memcmp(vs, &rws->rs->clients[clientNum].visibility, sizeof(*vs))) return;

	record_stream_write_value(RC_STATE_VISIBILITY, 1, &rws->stream);
	record_stream_write_value(clientNum, 1, &rws->stream);

	if(record_verify_data->integer) {
		verify_stream = rws->stream;
		verify_vs = rws->rs->clients[clientNum].visibility; }

	record_encode_visibility_state(&rws->rs->clients[clientNum].visibility, vs, &rws->stream);

	if(record_verify_data->integer) {
		record_decode_visibility_state(&verify_vs, &verify_stream);
		if(verify_stream.position != rws->stream.position) {
			record_printf(RP_ALL, "record_update_visibility_state: verify stream in different position\n"); }
		else if(memcmp(vs, &verify_vs, sizeof(*vs))) {
			record_printf(RP_ALL, "record_update_visibility_state: verify discrepancy\n"); } } }

static void record_update_visibility_state_client(int clientNum) {
	record_visibility_state_t vs;
	record_visibility_state_t vs_tweaked;
	record_get_current_visibility(clientNum, &vs);
	record_tweak_inactive_visibility(&rws->rs->entities, &rws->rs->clients[clientNum].visibility, &vs, &vs_tweaked);
	record_update_visibility_state(&vs_tweaked, clientNum); }

static void record_update_usercmd(usercmd_t *usercmd, int clientNum) {
	record_usercmd_t record_usercmd;
	record_data_stream_t verify_stream;
	record_usercmd_t verify_usercmd;

	record_convert_usercmd_to_record_usercmd(usercmd, &record_usercmd);

	record_stream_write_value(RC_STATE_USERCMD, 1, &rws->stream);
	record_stream_write_value(clientNum, 1, &rws->stream);

	if(record_verify_data->integer) {
		verify_stream = rws->stream;
		verify_usercmd = rws->rs->clients[clientNum].usercmd; }

	record_encode_usercmd(&rws->rs->clients[clientNum].usercmd, &record_usercmd, &rws->stream);

	if(record_verify_data->integer) {
		record_decode_usercmd(&verify_usercmd, &verify_stream);
		if(verify_stream.position != rws->stream.position) {
			record_printf(RP_ALL, "record_update_usercmd: verify stream in different position\n"); }
		else if(memcmp(&record_usercmd, &verify_usercmd, sizeof(record_usercmd))) {
			record_printf(RP_ALL, "record_update_usercmd: verify discrepancy\n"); } } }

static void record_update_configstring(int index, char *value) {
	if(index < 0 || index >= MAX_CONFIGSTRINGS) {
		record_printf(RP_ALL, "record_update_configstring: invalid configstring index\n");
		return; }

	if(!strcmp(rws->rs->configstrings[index], value)) return;

	record_stream_write_value(RC_STATE_CONFIGSTRING, 1, &rws->stream);
	record_stream_write_value(index, 2, &rws->stream);
	record_encode_string(value, &rws->stream);

	Z_Free(rws->rs->configstrings[index]);
	rws->rs->configstrings[index] = CopyString(value); }

static void record_update_current_servercmd(char *value) {
	if(!strcmp(rws->rs->current_servercmd, value)) return;

	record_stream_write_value(RC_STATE_CURRENT_SERVERCMD, 1, &rws->stream);
	record_encode_string(value, &rws->stream);

	Z_Free(rws->rs->current_servercmd);
	rws->rs->current_servercmd = CopyString(value); }

/* ******************************************************************************** */
// Recording Start/Stop Functions
/* ******************************************************************************** */

static void deallocate_record_writer(void) {
	if(!rws) return;
	if(rws->rs) free_record_state(rws->rs);
	if(rws->target_directory) Z_Free(rws->target_directory);
	if(rws->target_filename) Z_Free(rws->target_filename);
	record_free(rws);
	rws = 0; }

static void close_record_writer(void) {
	if(!rws) {
		// Not supposed to happen
		record_printf(RP_ALL, "close_record_writer called with record writer not initialized\n");
		return; }

	// Flush stream to file and close temp file
	dump_stream_to_file(&rws->stream, rws->recordfile);
	FS_FCloseFile(rws->recordfile);

	// Attempt to move the temp file to final destination
	FS_SV_Rename("records/current.rec", va("records/%s/%s.rec", rws->target_directory, rws->target_filename), qfalse);

	deallocate_record_writer(); }

static void initialize_record_writer(int max_clients, qboolean auto_started) {
	if(rws) {
		// Not supposed to happen
		record_printf(RP_ALL, "initialize_record_writer called with record writer already initialized\n");
		return; }

	// Allocate the structure
	rws = record_calloc(sizeof(*rws));

	// Make sure records folder exists
	Sys_Mkdir(va("%s/records", Cvar_VariableString("fs_homepath")));

	// Rename any existing output file that might have been left over from a crash
	FS_SV_Rename("records/current.rec", va("records/orphan_%u.rec", rand()), qfalse);

	// Determine move location (target_directory and target_filename) for when recording is complete
	{	time_t rawtime;
		struct tm *timeinfo;

		time(&rawtime);
		timeinfo = localtime(&rawtime);
		if(!timeinfo) {
			record_printf(RP_ALL, "initialize_record_writer: failed to get timeinfo\n");
			deallocate_record_writer();
			return; }

		rws->target_directory = CopyString(va("%i-%02i-%02i", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday));
		rws->target_filename = CopyString(va("%02i-%02i-%02i", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec)); }

	// Open the temp output file
	rws->recordfile = FS_SV_FOpenFileWrite("records/current.rec");
	if(!rws->recordfile) {
		record_printf(RP_ALL, "initialize_record_writer: failed to open output file\n");
		deallocate_record_writer();
		return; }

	// Set up the stream
	rws->stream.data = rws->stream_buffer;
	rws->stream.size = sizeof(rws->stream_buffer);

	// Set up the record state
	rws->rs = allocate_record_state(max_clients);
	rws->auto_started = auto_started;
	rws->last_snapflags = svs.snapFlagServerBit; }

static void record_write_client_enter_world(int clientNum) {
	if(!rws) return;
	rws->active_players[clientNum] = 1;
	record_stream_write_value(RC_EVENT_CLIENT_ENTER_WORLD, 1, &rws->stream);
	record_stream_write_value(clientNum, 1, &rws->stream); }

static void record_write_client_disconnect(int clientNum) {
	if(!rws || !rws->active_players[clientNum]) return;
	rws->active_players[clientNum] = 0;
	record_stream_write_value(RC_EVENT_CLIENT_DISCONNECT, 1, &rws->stream);
	record_stream_write_value(clientNum, 1, &rws->stream); }

static void check_record_connections(void) {
	// Handles connecting / disconnecting clients from record state
	int i;
	for(i=0; i<rws->rs->max_clients; ++i) {
		if(sv.state == SS_GAME && i < sv_maxclients->integer && svs.clients[i].state == CS_ACTIVE &&
				(svs.clients[i].netchan.remoteAddress.type != NA_BOT || record_full_bot_data->integer)) {
			if(!rws->active_players[i]) record_write_client_enter_world(i); }
		else {
			if(rws->active_players[i]) record_write_client_disconnect(i); } } }

static void record_write_start(int max_clients, qboolean auto_started) {
	int i;
	if(rws) return;

	if(max_clients < 1 || max_clients > 256) {
		record_printf(RP_ALL, "record_write_start: invalid max_clients");
		max_clients = 256; }

	initialize_record_writer(max_clients, auto_started);
	if(!rws) return;

	// Write the protocol
	record_stream_write_value(RECORD_PROTOCOL, 4, &rws->stream);

	// Write max clients
	record_stream_write_value(max_clients, 4, &rws->stream);

	// Write the configstrings
	for(i=0; i<MAX_CONFIGSTRINGS; ++i) {
		if(!sv.configstrings[i]) {
			record_printf(RP_ALL, "record_write_start: null configstring\n");
			continue; }
		if(!*sv.configstrings[i]) continue;
		record_update_configstring(i, sv.configstrings[i]); }

	// Write the baselines
	{	record_entityset_t baselines;
		get_current_baselines(&baselines);
		record_update_entityset(&baselines); }
	record_stream_write_value(RC_EVENT_BASELINES, 1, &rws->stream);

	dump_stream_to_file(&rws->stream, rws->recordfile);

	record_printf(RP_ALL, "Recording to %s/%s.rec\n", rws->target_directory, rws->target_filename); }

void record_write_stop(void) {
	if(!rws) return;
	close_record_writer();
	record_printf(RP_ALL, "Recording stopped.\n"); }

static qboolean have_recordable_players(qboolean include_bots) {
	int i;
	if(sv.state != SS_GAME) return qfalse;
	for(i=0; i<sv_maxclients->integer; ++i) {
		if(svs.clients[i].state == CS_ACTIVE && ((include_bots && record_full_bot_data->integer) ||
				svs.clients[i].netchan.remoteAddress.type != NA_BOT)) return qtrue; }
	return qfalse; }

void record_start_cmd(void) {
	if(rws) {
		record_printf(RP_ALL, "Already recording.\n");
		return; }
	if(!have_recordable_players(record_full_bot_data->integer)) {
		record_printf(RP_ALL, "No players to record.\n");
		return; }
	record_write_start(sv_maxclients->integer, qfalse); }

void record_stop_cmd(void) {
	if(!rws) {
		record_printf(RP_ALL, "Not currently recording.\n");
		return; }
	if(record_auto_recording->integer) {
		record_printf(RP_ALL, "NOTE: To permanently stop recording, set record_auto_recording to 0.\n"); }
	record_write_stop(); }

/* ******************************************************************************** */
// Event Handling Functions
/* ******************************************************************************** */

void record_write_usercmd(usercmd_t *usercmd, int clientNum) {
	if(!rws || !rws->active_players[clientNum]) return;

	if(!record_full_usercmd_data->integer) {
		// Don't write a new usercmd if most of the fields are the same
		usercmd_t old_usercmd;
		record_convert_record_usercmd_to_usercmd(&rws->rs->clients[clientNum].usercmd, &old_usercmd);
		if(usercmd->buttons == old_usercmd.buttons && usercmd->weapon == old_usercmd.weapon
				&& usercmd->forwardmove == old_usercmd.forwardmove && usercmd->rightmove == old_usercmd.rightmove
				&& usercmd->upmove == old_usercmd.upmove) return; }

	record_update_usercmd(usercmd, clientNum); }

void record_write_configstring_change(int index, const char *value) {
	if(!rws) return;
	record_update_configstring(index, (char *)value); }

void record_write_servercmd(int clientNum, const char *value) {
	if(!rws || !rws->active_players[clientNum]) return;
	record_update_current_servercmd((char *)value);
	record_stream_write_value(RC_EVENT_SERVERCMD, 1, &rws->stream);
	record_stream_write_value(clientNum, 1, &rws->stream); }

void record_write_snapshot(void) {
	// Check record connections; auto start and stop recording if needed
	if(!rws && record_auto_recording->integer && have_recordable_players(qfalse)) {
		record_write_start(sv_maxclients->integer, qtrue); }
	if(rws) check_record_connections();
	if(rws && !have_recordable_players(record_full_bot_data->integer && !rws->auto_started)) {
		record_write_stop(); }
	if(!rws) return;

	// Check for map restart
	if((rws->last_snapflags & SNAPFLAG_SERVERCOUNT) != (svs.snapFlagServerBit & SNAPFLAG_SERVERCOUNT)) {
		record_printf(RP_DEBUG, "record_write_snapshot: recording map restart\n");
		record_stream_write_value(RC_EVENT_MAP_RESTART, 1, &rws->stream); }
	rws->last_snapflags = svs.snapFlagServerBit;

	{	record_entityset_t entities;
		get_current_entities(&entities);
		record_update_entityset(&entities); }

	{	int i;
		for(i=0; i<sv_maxclients->integer && i<rws->rs->max_clients; ++i) {
			if(svs.clients[i].state < CS_ACTIVE) continue;
			if(!rws->active_players[i]) continue;
			record_update_playerstate(SV_GameClientNum(i), i);
			record_update_visibility_state_client(i); } }

	record_stream_write_value(RC_EVENT_SNAPSHOT, 1, &rws->stream);
	record_stream_write_value(sv.time, 4, &rws->stream);

	dump_stream_to_file(&rws->stream, rws->recordfile); }

#endif
