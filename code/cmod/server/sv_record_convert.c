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
// Record Demo Writer
/* ******************************************************************************** */

typedef struct {
	fileHandle_t demofile;
	qboolean legacy_protocol;
	record_entityset_t baselines;

	qboolean have_delta;
	record_entityset_t delta_entities;
	record_visibility_state_t delta_visibility;
	playerState_t delta_playerstate;

	char pending_commands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	int pending_command_count;

	int baseline_cutoff;
	int message_sequence;
	int server_command_sequence;
	int snapflags;
} record_demo_writer_t;

static qboolean initialize_demo_writer(record_demo_writer_t *rdw, const char *path, qboolean legacy_protocol) {
	// Returns qtrue on success, qfalse otherwise
	// In the event of qtrue, stream needs to be freed by close_demo_writer
	Com_Memset(rdw, 0, sizeof(*rdw));

	rdw->demofile = FS_FOpenFileWrite(path);
	if(!rdw->demofile) {
		record_printf(RP_ALL, "initialize_demo_writer: failed to open file\n");
		return qfalse; }

	rdw->legacy_protocol = legacy_protocol;
	rdw->message_sequence = 1;
	return qtrue; }

static void close_demo_writer(record_demo_writer_t *rdw) {
	FS_FCloseFile(rdw->demofile); }

static void finish_demo_message(msg_t *msg, record_demo_writer_t *rdw) {
	// From sv_net_chan->SV_Netchan_Transmit
	if(!rdw->legacy_protocol) MSG_WriteByte(msg, svc_EOF);

	FS_Write(&rdw->message_sequence, 4, rdw->demofile);
	++rdw->message_sequence;

	FS_Write(&msg->cursize, 4, rdw->demofile);
	FS_Write(msg->data, msg->cursize, rdw->demofile); }

static void write_demo_gamestate(record_entityset_t *baselines, char **configstrings, int clientNum, record_demo_writer_t *rdw) {
	// Based on cl_main.c->CL_Record_f
	byte buffer[MAX_MSGLEN];
	msg_t msg;

	// Delta from baselines for next snapshot
	rdw->have_delta = qfalse;
	rdw->baselines = *baselines;

	if(rdw->legacy_protocol) {
		MSG_InitOOB(&msg, buffer, sizeof(buffer));
		msg.compat = qtrue; }
	else {
		MSG_Init(&msg, buffer, sizeof(buffer));
		// lastClientCommand; always 0 for demo file
		MSG_WriteLong(&msg, 0); }

	record_write_gamestate_message(baselines, configstrings, clientNum, rdw->server_command_sequence, &msg, &rdw->baseline_cutoff);

	finish_demo_message(&msg, rdw); }

static void write_demo_svcmd(char *command, record_demo_writer_t *rdw) {
	if(rdw->pending_command_count >= ARRAY_LEN(rdw->pending_commands)) {
		record_printf(RP_ALL, "write_demo_svcmd: pending command overflow\n");
		return; }

	Q_strncpyz(rdw->pending_commands[rdw->pending_command_count], command, sizeof(*rdw->pending_commands));
	++rdw->pending_command_count; }

static void write_demo_snapshot(record_entityset_t *entities, record_visibility_state_t *visibility, playerState_t *ps,
			int sv_time, record_demo_writer_t *rdw) {
	// Based on sv.snapshot.c->SV_SendClientSnapshot
	int i;
	byte buffer[MAX_MSGLEN];
	msg_t msg;

	if(rdw->legacy_protocol) {
		MSG_InitOOB(&msg, buffer, sizeof(buffer));
		msg.compat = qtrue; }
	else {
		MSG_Init(&msg, buffer, sizeof(buffer));
		// lastClientCommand; always 0 for demo file
		MSG_WriteLong(&msg, 0); }

	// send any reliable server commands
	for(i=0; i<rdw->pending_command_count; ++i) {
		MSG_WriteByte(&msg, svc_serverCommand);
		MSG_WriteLong(&msg, ++rdw->server_command_sequence);
		MSG_WriteString(&msg, rdw->pending_commands[i]); }
	rdw->pending_command_count = 0;

	// Write the snapshot
	if(rdw->have_delta) {
		record_write_snapshot_message(entities, visibility, ps, &rdw->delta_entities, &rdw->delta_visibility,
				&rdw->delta_playerstate, &rdw->baselines, rdw->baseline_cutoff, 0, 1, rdw->snapflags, sv_time, &msg); }
	else {
		record_write_snapshot_message(entities, visibility, ps, 0, 0, 0, &rdw->baselines, rdw->baseline_cutoff, 0, 0,
				rdw->snapflags, sv_time, &msg); }

	// Store delta for next frame
	rdw->delta_entities = *entities;
	rdw->delta_visibility = *visibility;
	rdw->delta_playerstate = *ps;
	rdw->have_delta = qtrue;

	finish_demo_message(&msg, rdw); }

static void write_demo_map_restart(record_demo_writer_t *rdw) {
	rdw->snapflags ^= SNAPFLAG_SERVERCOUNT; }

/* ******************************************************************************** */
// Record Stream Reader
/* ******************************************************************************** */

typedef struct {
	record_data_stream_t stream;
	record_state_t *rs;

	record_command_t command;
	int time;
	int clientNum;
} record_stream_reader_t;

static qboolean load_record_file_into_stream(fileHandle_t fp, record_data_stream_t *stream) {
	// Returns qtrue on success, qfalse otherwise
	// In the event of qtrue, call free on stream->data
	FS_Seek(fp, 0, FS_SEEK_END);
	stream->size = FS_FTell(fp);
	if(!stream->size) return qfalse;
	stream->data = record_calloc(stream->size);

	FS_Seek(fp, 0, FS_SEEK_SET);
	FS_Read(stream->data, stream->size, fp);
	stream->position = 0;
	return qtrue; }

static qboolean initialize_record_stream_reader(record_stream_reader_t *rsr, const char *path) {
	// Returns qtrue on success, qfalse otherwise
	// In the event of qtrue, stream needs to be freed by close_record_stream_reader
	fileHandle_t fp = 0;
	int protocol;
	int max_clients;

	Com_Memset(rsr, 0, sizeof(*rsr));

	FS_SV_FOpenFileRead(path, &fp);
	if(!fp) {
		record_printf(RP_ALL, "initialize_record_stream_reader: failed to open source file\n");
		return qfalse; }

	if(!load_record_file_into_stream(fp, &rsr->stream)) {
		record_printf(RP_ALL, "initialize_record_stream_reader: failed to read source file\n");
		FS_FCloseFile(fp);
		return qfalse; }
	FS_FCloseFile(fp);

	if(rsr->stream.size < 8) {
		record_printf(RP_ALL, "initialize_record_stream_reader: invalid source file length\n");
		record_free(rsr->stream.data);
		return qfalse; }

	protocol = *(int *)record_stream_read_static(4, &rsr->stream);
	if(protocol != RECORD_PROTOCOL) {
		record_printf(RP_ALL, "initialize_record_stream_reader: record stream has wrong protocol (got %i, expected %i)\n",
				protocol, RECORD_PROTOCOL);
		record_free(rsr->stream.data);
		return qfalse; }

	max_clients = *(int *)record_stream_read_static(4, &rsr->stream);
	if(max_clients < 1 || max_clients > 256) {
		record_printf(RP_ALL, "initialize_record_stream_reader: bad max_clients\n");
		record_free(rsr->stream.data);
		return qfalse; }

	rsr->rs = allocate_record_state(max_clients);
	record_printf(RP_DEBUG, "stream reader initialized with %i max_clients\n", max_clients);
	return qtrue; }

static void close_record_stream_reader(record_stream_reader_t *rsr) {
	record_free(rsr->stream.data);
	free_record_state(rsr->rs); }

static void stream_reader_set_clientnum(record_stream_reader_t *rsr, int clientNum) {
	if(clientNum < 0 || clientNum >= rsr->rs->max_clients) {
		record_stream_error(&rsr->stream, "stream_reader_set_clientnum: invalid clientnum"); }
	rsr->clientNum = clientNum; }

static qboolean advance_stream_reader(record_stream_reader_t *rsr) {
	// Returns qtrue on success, qfalse on error or end of stream
	if(rsr->stream.position >= rsr->stream.size) return qfalse;
	rsr->command = *(unsigned char *)record_stream_read_static(1, &rsr->stream);

	switch(rsr->command) {
		case RC_STATE_ENTITY_SET:
			record_decode_entityset(&rsr->rs->entities, &rsr->stream);
			break;
		case RC_STATE_PLAYERSTATE:
			stream_reader_set_clientnum(rsr, *(unsigned char *)record_stream_read_static(1, &rsr->stream));
			record_decode_playerstate(&rsr->rs->clients[rsr->clientNum].playerstate, &rsr->stream);
			break;
		case RC_STATE_VISIBILITY:
			stream_reader_set_clientnum(rsr, *(unsigned char *)record_stream_read_static(1, &rsr->stream));
			record_decode_visibility_state(&rsr->rs->clients[rsr->clientNum].visibility, &rsr->stream);
			break;
		case RC_STATE_USERCMD:
			stream_reader_set_clientnum(rsr, *(unsigned char *)record_stream_read_static(1, &rsr->stream));
			record_decode_usercmd(&rsr->rs->clients[rsr->clientNum].usercmd, &rsr->stream);
			break;
		case RC_STATE_CONFIGSTRING: {
			int index = *(unsigned short *)record_stream_read_static(2, &rsr->stream);
			char *string = record_decode_string(&rsr->stream, 0);
			Z_Free(rsr->rs->configstrings[index]);
			rsr->rs->configstrings[index] = CopyString(string);
			break; }
		case RC_STATE_CURRENT_SERVERCMD: {
			char *string = record_decode_string(&rsr->stream, 0);
			Z_Free(rsr->rs->current_servercmd);
			rsr->rs->current_servercmd = CopyString(string);
			break; }

		case RC_EVENT_SNAPSHOT:
			rsr->time = *(int *)record_stream_read_static(4, &rsr->stream);
			break;
		case RC_EVENT_SERVERCMD:
		case RC_EVENT_CLIENT_ENTER_WORLD:
		case RC_EVENT_CLIENT_DISCONNECT:
			stream_reader_set_clientnum(rsr, *(unsigned char *)record_stream_read_static(1, &rsr->stream));
			break;
		case RC_EVENT_BASELINES:
		case RC_EVENT_MAP_RESTART:
			break;

		default:
			record_printf(RP_ALL, "advance_stream_reader: unknown command %i\n", rsr->command);
			return qfalse; }

	return qtrue; }

/* ******************************************************************************** */
// Record Conversion
/* ******************************************************************************** */

typedef enum {
	CSTATE_NOT_STARTED,		// Gamestate not written yet
	CSTATE_CONVERTING,		// Gamestate written, write snapshots
	CSTATE_FINISHED			// Finished, don't write anything more
} record_conversion_state_t;

typedef struct {
	int clientNum;
	int instance_wait;
	int firing_time;	// For weapon timing
	record_conversion_state_t state;
	record_entityset_t baselines;
	record_stream_reader_t rsr;
	record_demo_writer_t rdw;
	int frame_count;
} record_conversion_handler_t;

static void process_stream_conversion(record_conversion_handler_t *rch) {
	rch->rsr.stream.abort_set = qtrue;
	if(setjmp(rch->rsr.stream.abort)) return;

	while(advance_stream_reader(&rch->rsr)) {
		switch(rch->rsr.command) {
			case RC_EVENT_BASELINES:
				rch->baselines = rch->rsr.rs->entities;
				break;

			case RC_EVENT_SNAPSHOT:
				if(rch->state == CSTATE_CONVERTING) {
					playerState_t ps = rch->rsr.rs->clients[rch->clientNum].playerstate;
					if(record_convert_simulate_follow->integer) playerstate_set_follow_mode(&ps);
					write_demo_snapshot(&rch->rsr.rs->entities, &rch->rsr.rs->clients[rch->clientNum].visibility,
							&ps, rch->rsr.time, &rch->rdw);
					++rch->frame_count; }
				break;

			case RC_EVENT_SERVERCMD:
				if(rch->state == CSTATE_CONVERTING && rch->rsr.clientNum == rch->clientNum) {
					write_demo_svcmd(rch->rsr.rs->current_servercmd, &rch->rdw); }
				break;

			case RC_STATE_USERCMD:
				if(rch->state == CSTATE_CONVERTING && rch->rsr.clientNum == rch->clientNum &&
						record_convert_weptiming->integer) {
					usercmd_t usercmd;
					record_convert_record_usercmd_to_usercmd(&rch->rsr.rs->clients[rch->clientNum].usercmd, &usercmd);
					if(usercmd_is_firing_weapon(&usercmd)) {
						if(!rch->firing_time) {
							write_demo_svcmd("print \"Firing\n\"", &rch->rdw);
							rch->firing_time = usercmd.serverTime; } }
					else {
						if(rch->firing_time) {
							char buffer[128];
							Com_sprintf(buffer, sizeof(buffer), "print \"Ceased %i\n\"",
									usercmd.serverTime - rch->firing_time);
							write_demo_svcmd(buffer, &rch->rdw);
							rch->firing_time = 0; } } }
				break;

			case RC_EVENT_MAP_RESTART:
				if(rch->state == CSTATE_CONVERTING) write_demo_map_restart(&rch->rdw);
				break;

			case RC_EVENT_CLIENT_ENTER_WORLD:
				if(rch->state == CSTATE_NOT_STARTED && rch->rsr.clientNum == rch->clientNum) {
					if(rch->instance_wait) --rch->instance_wait;
					else {
						// Start encoding
						write_demo_gamestate(&rch->baselines, rch->rsr.rs->configstrings, rch->clientNum, &rch->rdw);
						rch->state = CSTATE_CONVERTING; } }
				break;

			case RC_EVENT_CLIENT_DISCONNECT:
				if(rch->state == CSTATE_CONVERTING && rch->rsr.clientNum == rch->clientNum) {
					// Stop encoding
					rch->state = CSTATE_FINISHED; }
				break;
			default:
				break; } }

	rch->rsr.stream.abort_set = qfalse; }

static void run_conversion(const char *path, int clientNum, int instance) {
	const char *output_path = record_convert_legacy_protocol->integer ?
			"demos/output.efdemo" : "demos/output.dm_26";
	record_conversion_handler_t *rch;

	rch = record_calloc(sizeof(*rch));
	rch->clientNum = clientNum;
	rch->instance_wait = instance;

	if(!initialize_record_stream_reader(&rch->rsr, path)) {
		record_free(rch);
		return; }

	if(!initialize_demo_writer(&rch->rdw, output_path, record_convert_legacy_protocol->integer ? qtrue : qfalse)) {
		close_record_stream_reader(&rch->rsr);
		record_free(rch);
		return; }

	process_stream_conversion(rch);

	if(rch->state == CSTATE_NOT_STARTED) {
		record_printf(RP_ALL, "failed to locate session; check client and instance parameters\n"
				"use record_scan command to show available client and instance options\n"); }
	else {
		if(rch->state == CSTATE_CONVERTING) {
			record_printf(RP_ALL, "failed to reach disconnect marker; demo may be incomplete\n"); }
		record_printf(RP_ALL, "%i frames written to %s\n", rch->frame_count, output_path); }

	close_demo_writer(&rch->rdw);
	close_record_stream_reader(&rch->rsr);
	record_free(rch); }

void record_convert_cmd(void) {
	char path[128];

	if(Cmd_Argc() < 2) {
		record_printf(RP_ALL, "Usage: record_convert <path within 'records' directory> <client> <instance>\n"
			"Example: record_convert source.rec 0 0\n");
		return; }

	Com_sprintf(path, sizeof(path), "records/%s", Cmd_Argv(1));
	COM_DefaultExtension(path, sizeof(path), ".rec");
	if(strstr(path, "..")) {
		record_printf(RP_ALL, "Invalid path\n");
		return; }

	run_conversion(path, atoi(Cmd_Argv(2)), atoi(Cmd_Argv(3))); }

/* ******************************************************************************** */
// Record Scanning
/* ******************************************************************************** */

static void process_stream_scan(record_stream_reader_t *rsr) {
	int instance_counts[256];

	rsr->stream.abort_set = qtrue;
	if(setjmp(rsr->stream.abort)) return;

	Com_Memset(instance_counts, 0, sizeof(instance_counts));

	while(advance_stream_reader(rsr)) {
		switch(rsr->command) {
			case RC_EVENT_CLIENT_ENTER_WORLD:
				record_printf(RP_ALL, "client(%i) instance(%i)\n", rsr->clientNum,
						instance_counts[rsr->clientNum]);
				++instance_counts[rsr->clientNum];
				break;
			default:
				break; } }

	rsr->stream.abort_set = qfalse; }

static void run_scan(const char *path) {
	record_stream_reader_t *rsr = record_calloc(sizeof(*rsr));

	if(!initialize_record_stream_reader(rsr, path)) {
		record_free(rsr);
		return; }

	process_stream_scan(rsr);

	close_record_stream_reader(rsr);
	record_free(rsr); }

void record_scan_cmd(void) {
	char path[128];

	if(Cmd_Argc() < 2) {
		record_printf(RP_ALL, "Usage: record_scan <path within 'records' directory>\n"
			"Example: record_scan source.rec\n");
		return; }

	Com_sprintf(path, sizeof(path), "records/%s", Cmd_Argv(1));
	COM_DefaultExtension(path, sizeof(path), ".rec");
	if(strstr(path, "..")) {
		record_printf(RP_ALL, "Invalid path\n");
		return; }

	run_scan(path); }

#endif
