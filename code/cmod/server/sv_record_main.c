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

static qboolean record_initialized = qfalse;

cvar_t *admin_spectator_password;
cvar_t *admin_spectator_slots;

cvar_t *record_auto_recording;
cvar_t *record_full_bot_data;
cvar_t *record_full_usercmd_data;

cvar_t *record_convert_legacy_protocol;
cvar_t *record_convert_weptiming;
cvar_t *record_convert_simulate_follow;

cvar_t *record_debug_prints;
cvar_t *record_verify_data;

/* ******************************************************************************** */
// Server Calls
/* ******************************************************************************** */

void record_process_usercmd(int clientNum, usercmd_t *usercmd) {
	if(!record_initialized) return;
	record_spectator_process_usercmd(clientNum, usercmd);
	record_write_usercmd(usercmd, clientNum); }

void record_process_configstring_change(int index, const char *value) {
	if(!record_initialized) return;
	record_spectator_process_configstring_change(index, value);
	record_write_configstring_change(index, value); }

void record_process_servercmd(int clientNum, const char *value) {
	if(!record_initialized) return;
	record_spectator_process_servercmd(clientNum, value);
	record_write_servercmd(clientNum, value); }

void record_process_map_loaded(void) {
	if(!record_initialized) return;
	record_spectator_process_map_loaded(); }

void record_process_snapshot(void) {
	if(!record_initialized) return;
	record_spectator_process_snapshot();
	record_write_snapshot(); }

void record_game_shutdown(void) {
	if(!record_initialized) return;
	record_write_stop(); }

qboolean record_process_connection(netadr_t *address, const char *userinfo, qboolean compat) {
	// Returns qtrue to suppress normal handling of connection, qfalse otherwise
	if(!record_initialized) return qfalse;
	return record_spectator_process_connection(address, userinfo, compat); }

qboolean record_process_packet_event(netadr_t *address, msg_t *msg, int qport) {
	// Returns qtrue to suppress normal handling of packet, qfalse otherwise
	if(!record_initialized) return qfalse;
	return record_spectator_process_packet_event(address, msg, qport); }

/* ******************************************************************************** */
// Initialization
/* ******************************************************************************** */

void record_initialize(void) {
	admin_spectator_password = Cvar_Get("admin_spectator_password", "", 0);
	admin_spectator_slots = Cvar_Get("admin_spectator_slots", "32", 0);
	Cvar_CheckRange(admin_spectator_slots, 1, 1024, qtrue);

	record_auto_recording = Cvar_Get("record_auto_recording", "0", 0);
	record_full_bot_data = Cvar_Get("record_full_bot_data", "0", 0);
	record_full_usercmd_data = Cvar_Get("record_full_usercmd_data", "0", 0);

	record_convert_legacy_protocol = Cvar_Get("record_convert_legacy_protocol", "1", 0);
	record_convert_weptiming = Cvar_Get("record_convert_weptiming", "0", 0);
	record_convert_simulate_follow = Cvar_Get("record_convert_simulate_follow", "1", 0);

	record_verify_data = Cvar_Get("record_verify_data", "0", 0);
	record_debug_prints = Cvar_Get("record_debug_prints", "0", 0);

	Cmd_AddCommand("record_start", record_start_cmd);
	Cmd_AddCommand("record_stop", record_stop_cmd);
	Cmd_AddCommand("record_convert", record_convert_cmd);
	Cmd_AddCommand("record_scan", record_scan_cmd);
	Cmd_AddCommand("spect_status", record_spectator_status);

	record_initialized = qtrue; }

#endif
