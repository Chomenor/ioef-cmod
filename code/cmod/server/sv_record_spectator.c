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
	playerState_t ps;
	int frame_entities_position;
	record_visibility_state_t visibility;
} spectator_frame_t;

typedef struct {
	client_t cl;
	int target_client;	// Client currently being spectated
	spectator_frame_t frames[PACKET_BACKUP];
	int last_snapshot_sv_time;
	int baseline_cutoff;
	int target_firing_time;

	// Client settings
	qboolean weptiming;
	qboolean cycleall;
} spectator_t;

#define FRAME_ENTITY_COUNT (PACKET_BACKUP * 2)

typedef struct {
	record_entityset_t current_baselines;
	spectator_t *spectators;
	int max_spectators;
	int frame_entities_position;
	record_entityset_t frame_entities[FRAME_ENTITY_COUNT];
} spectator_system_t;

spectator_system_t *sps;

/* ******************************************************************************** */
// Command / configstring update handling
/* ******************************************************************************** */

static void spectator_add_server_command(client_t *cl, const char *cmd) {
	// Based on sv_main.c->SV_AddServerCommand
	int index;
	++cl->reliableSequence;
	if(cl->reliableSequence - cl->reliableAcknowledge >= MAX_RELIABLE_COMMANDS + 1) {
		record_printf(RP_DEBUG, "spectator_add_server_command: command overflow\n");
		return; }
	index = cl->reliableSequence & (MAX_RELIABLE_COMMANDS - 1);
	Q_strncpyz(cl->reliableCommands[index], cmd, sizeof(cl->reliableCommands[index])); }

static void QDECL spectator_add_server_command_fmt(client_t *cl, const char *fmt, ...)
		Q_PRINTF_FUNC(2, 3);

static void QDECL spectator_add_server_command_fmt(client_t *cl, const char *fmt, ...) {
	va_list argptr;
	char message[MAX_STRING_CHARS];

	va_start(argptr, fmt);
	Q_vsnprintf(message, sizeof(message), fmt, argptr);
	va_end(argptr);

	spectator_add_server_command(cl, message); }

static void spectator_send_configstring(client_t *cl, int index, const char *value) {
	// Based on sv_init.c->SV_SendConfigstring
	int maxChunkSize = MAX_STRING_CHARS - 24;
	int len = strlen(value);

	if( len >= maxChunkSize ) {
		int		sent = 0;
		int		remaining = len;
		char	*cmd;
		char	buf[MAX_STRING_CHARS];

		while (remaining > 0 ) {
			if ( sent == 0 ) {
				cmd = "bcs0";
			}
			else if( remaining < maxChunkSize ) {
				cmd = "bcs2";
			}
			else {
				cmd = "bcs1";
			}
			Q_strncpyz( buf, &value[sent],
				maxChunkSize );

			spectator_add_server_command_fmt(cl, "%s %i \"%s\"\n", cmd, index, buf);

			sent += (maxChunkSize - 1);
			remaining -= (maxChunkSize - 1);
		}
	} else {
		// standard cs, just send it
		spectator_add_server_command_fmt(cl, "cs %i \"%s\"\n", index, value);
	}
}

/* ******************************************************************************** */
// Target Selection
/* ******************************************************************************** */

static qboolean target_client_valid(int clientnum) {
	if(sv.state != SS_GAME) return qfalse;
	if(clientnum < 0 || clientnum > sv_maxclients->integer) return qfalse;
	if(svs.clients[clientnum].state != CS_ACTIVE) return qfalse;
	return qtrue; }

static int select_target_client(int start_index, qboolean cycleall) {
	// Returns clientnum if valid client selected, -1 otherwise
	int i;
	if(start_index < 0 || start_index >= sv_maxclients->integer) start_index = 0;

	for(i=start_index; i<start_index+sv_maxclients->integer; ++i) {
		int clientnum = i % sv_maxclients->integer;
		if(!target_client_valid(clientnum)) continue;
		if(!cycleall) {
			if(svs.clients[clientnum].netchan.remoteAddress.type == NA_BOT) continue;
			if(playerstate_is_spectator(SV_GameClientNum(clientnum))) continue; }
		return clientnum; }

	if(!cycleall) return select_target_client(start_index, qtrue);
	return -1; }

static void advance_target_client(spectator_t *spectator) {
	// Advances to next target client
	// Sets target_client to -1 if no valid target available
	int original_target = spectator->target_client;
	spectator->target_client = select_target_client(spectator->target_client + 1, spectator->cycleall);
	if(spectator->target_client >= 0 && spectator->target_client != original_target) {
		const char *suffix = "";
		if(playerstate_is_spectator(SV_GameClientNum(spectator->target_client))) suffix = " [SPECT]";
		if(svs.clients[spectator->target_client].netchan.remoteAddress.type == NA_BOT) suffix = " [BOT]";

		spectator_add_server_command_fmt(&spectator->cl, "print \"Client(%i) Name(%s^7)%s\n\"",
				spectator->target_client, svs.clients[spectator->target_client].name, suffix); } }

static void validate_target_client(spectator_t *spectator) {
	// Advances target client if current one is invalid
	// Sets target_client to -1 if no valid target available
	if(!target_client_valid(spectator->target_client)) advance_target_client(spectator); }

/* ******************************************************************************** */
// Outgoing message (gamestate/snapshot) handling
/* ******************************************************************************** */

static void initialize_spectator_message(client_t *cl, msg_t *msg, byte *buffer, int buffer_size) {
	// Initializes a base message common to both gamestate and snapshot
#ifdef ELITEFORCE
	if(cl->compat) {
		MSG_InitOOB(msg, buffer, buffer_size);
		msg->compat = qtrue; }
	else
#endif
	MSG_Init(msg, buffer, buffer_size);

	// let the client know which reliable clientCommands we have received
#ifdef ELITEFORCE
	if(!cl->compat)
#endif
	MSG_WriteLong(msg, cl->lastClientCommand);

	// Update server commands to client
	// The standard non-spectator function *should* be safe to use here
	SV_UpdateServerCommandsToClient(cl, msg); }

static void send_spectator_gamestate(spectator_t *spectator) {
	// Based on sv_client.c->SV_SendClientGameState
	client_t *cl = &spectator->cl;
	msg_t msg;
	byte msg_buf[MAX_MSGLEN];

	cl->state = CS_PRIMED;

	// Note the message number to avoid further attempts to send the gamestate
	// until the client acknowledges a higher message number
	cl->gamestateMessageNum = cl->netchan.outgoingSequence;

	// Initialize message
	initialize_spectator_message(cl, &msg, msg_buf, sizeof(msg_buf));

	// Write gamestate message
	record_write_gamestate_message(&sps->current_baselines, sv.configstrings, 0, cl->reliableSequence, &msg,
			&spectator->baseline_cutoff);

	// Send to client
	SV_SendMessageToClient(&msg, cl); }

static void send_spectator_snapshot(spectator_t *spectator) {
	// Based on sv_snapshot.c->SV_SendClientSnapshot
	client_t *cl = &spectator->cl;
	msg_t msg;
	byte msg_buf[MAX_MSGLEN];
	spectator_frame_t *current_frame = &spectator->frames[cl->netchan.outgoingSequence % PACKET_BACKUP];
	spectator_frame_t *delta_frame = 0;
	int delta_frame_offset = 0;
	int snapFlags = svs.snapFlagServerBit;

	// Advance target client if current one is invalid
	validate_target_client(spectator);
	if(spectator->target_client < 0) return;

	// Store snapshot time in case it is needed to set oldServerTime on a map change
	spectator->last_snapshot_sv_time = sv.time + cl->oldServerTime;

	// Determine snapFlags
	if(cl->state != CS_ACTIVE) snapFlags |= SNAPFLAG_NOT_ACTIVE;

	// Set up current frame
	current_frame->frame_entities_position = sps->frame_entities_position;
	current_frame->ps = *SV_GameClientNum(spectator->target_client);
	record_get_current_visibility(spectator->target_client, &current_frame->visibility);

	// Tweak playerstate to indicate spectator mode
	playerstate_set_follow_mode(&current_frame->ps);

	// Determine delta frame
	if(cl->state == CS_ACTIVE && cl->deltaMessage > 0) {
		delta_frame_offset = cl->netchan.outgoingSequence - cl->deltaMessage;
		if(delta_frame_offset > 0 && delta_frame_offset < PACKET_BACKUP - 3) {
			delta_frame = &spectator->frames[cl->deltaMessage % PACKET_BACKUP];
			// Make sure delta frame references valid frame entities
			// If this client skipped enough frames, the frame entities could have been overwritten
			if(sps->frame_entities_position - delta_frame->frame_entities_position >= FRAME_ENTITY_COUNT) {
				delta_frame = 0; } } }

	// Initialize message
	initialize_spectator_message(cl, &msg, msg_buf, sizeof(msg_buf));

	// Write snapshot message
	record_write_snapshot_message(&sps->frame_entities[current_frame->frame_entities_position % FRAME_ENTITY_COUNT],
			&current_frame->visibility, &current_frame->ps,
			delta_frame ? &sps->frame_entities[delta_frame->frame_entities_position % FRAME_ENTITY_COUNT] : 0,
			delta_frame ? &delta_frame->visibility : 0, delta_frame ? &delta_frame->ps : 0,
			&sps->current_baselines, spectator->baseline_cutoff, cl->lastClientCommand,
			delta_frame ? delta_frame_offset : 0, snapFlags, spectator->last_snapshot_sv_time, &msg);

	// Send to client
	SV_SendMessageToClient(&msg, cl); }

/* ******************************************************************************** */
// Drop spectator
/* ******************************************************************************** */

static void drop_spectator(spectator_t *spectator, const char *message) {
	client_t *cl = &spectator->cl;
	if(cl->state == CS_FREE) return;

	if(message) {
#ifdef ELITEFORCE
		if(cl->compat) spectator_add_server_command_fmt(cl, "disconnect %s", message);
		else
#endif
		spectator_add_server_command_fmt(cl, "disconnect \"%s\"", message);

		send_spectator_snapshot(spectator);
		while(cl->netchan.unsentFragments || cl->netchan_start_queue) {
			SV_Netchan_TransmitNextFragment(cl); } }

	SV_Netchan_FreeQueue(cl);
	cl->state = CS_FREE; }

/* ******************************************************************************** */
// Incoming message handling
/* ******************************************************************************** */

static void spectator_process_userinfo(spectator_t *spectator, const char *userinfo) {
	// Based on sv_client.c->SV_UserinfoChanged
	// Currently just sets rate
	spectator->cl.rate = atoi(Info_ValueForKey(userinfo, "rate"));
	if(spectator->cl.rate <= 0) spectator->cl.rate = 90000;
	else if(spectator->cl.rate < 5000) spectator->cl.rate = 5000;
	else if(spectator->cl.rate > 90000) spectator->cl.rate = 90000; }

static void spectator_enter_world(spectator_t *spectator) {
	// Based on sv_client.c->SV_ClientEnterWorld
	// Spectators don't really enter the world, but they do need some configuration
	// to go to CS_ACTIVE after loading the map
	client_t *cl = &spectator->cl;
	int i;

	cl->state = CS_ACTIVE;

	// Based on sv_init.c->SV_UpdateConfigstrings
	for(i=0; i<MAX_CONFIGSTRINGS; ++i) {
		if(cl->csUpdated[i]) {
			spectator_send_configstring(cl, i, sv.configstrings[i]);
			cl->csUpdated[i] = qfalse; } }

	cl->deltaMessage = -1;
	cl->lastSnapshotTime = 0; }

static void spectator_think(spectator_t *spectator, usercmd_t *cmd) {
	client_t *cl = &spectator->cl;
	if(usercmd_is_firing_weapon(cmd) && !usercmd_is_firing_weapon(&cl->lastUsercmd)) advance_target_client(spectator); }

static void process_spectator_move(spectator_t *spectator, msg_t *msg, qboolean delta) {
	// Based on sv_client.c->SV_UserMove
	client_t *cl = &spectator->cl;
	int i;
	int cmdCount;
	usercmd_t nullcmd;
	usercmd_t cmds[MAX_PACKET_USERCMDS];
	usercmd_t *cmd, *oldcmd;

	if(delta) cl->deltaMessage = cl->messageAcknowledge;
	else cl->deltaMessage = -1;

	cmdCount = MSG_ReadByte(msg);
	if(cmdCount < 1 || cmdCount > MAX_PACKET_USERCMDS) {
		record_printf(RP_DEBUG, "process_spectator_move: invalid spectator cmdCount\n");
		return; }

	Com_Memset(&nullcmd, 0, sizeof(nullcmd));
	oldcmd = &nullcmd;
	for(i = 0; i < cmdCount; i++) {
		cmd = &cmds[i];
#ifdef ELITEFORCE
		MSG_ReadDeltaUsercmd(msg, oldcmd, cmd);
#else
		MSG_ReadDeltaUsercmdKey(msg, key, oldcmd, cmd);
#endif
		oldcmd = cmd; }

	if(cl->state == CS_PRIMED) spectator_enter_world(spectator);

	// Handle sv.time reset on map restart etc.
	if(cl->lastUsercmd.serverTime > sv.time) cl->lastUsercmd.serverTime = 0;

	for(i=0; i<cmdCount; ++i) {
		if(cmds[i].serverTime > cmds[cmdCount-1].serverTime) continue;
		if(cmds[i].serverTime <= cl->lastUsercmd.serverTime) continue;
		spectator_think(spectator, &cmds[i]);
		cl->lastUsercmd = cmds[i]; } }

static void process_boolean_setting(spectator_t *spectator, const char *setting_name, qboolean *target) {
	if(!Q_stricmp(Cmd_Argv(1), "0")) {
		spectator_add_server_command_fmt(&spectator->cl, "print \"%s disabled\n\"", setting_name);
		*target = qfalse; }
	else if(!Q_stricmp(Cmd_Argv(1), "1")) {
		spectator_add_server_command_fmt(&spectator->cl, "print \"%s enabled\n\"", setting_name);
		*target = qtrue; }
	else spectator_add_server_command_fmt(&spectator->cl, "print \"Usage: '%s 0' or '%s 1'\n\"",
				setting_name, setting_name); }

static void process_spectator_command(spectator_t *spectator, msg_t *msg) {
	// Based on sv_client.c->SV_ClientCommand
	client_t *cl = &spectator->cl;
	int seq = MSG_ReadLong(msg);
	const char *cmd = MSG_ReadString(msg);

	if(cl->lastClientCommand >= seq) {
		// Command already executed
		return; }

	if(seq > cl->lastClientCommand + 1) {
		// Command lost error
		record_printf(RP_ALL, "Spectator %i lost client commands\n", (int)(spectator-sps->spectators));
		drop_spectator(spectator, "Lost reliable commands");
		return; }

	record_printf(RP_DEBUG, "Have spectator command: %s\n", cmd);
	cl->lastClientCommand = seq;
	Q_strncpyz(cl->lastClientCommandString, cmd, sizeof(cl->lastClientCommandString));

	Cmd_TokenizeString(cmd);
	if(!Q_stricmp(Cmd_Argv(0), "disconnect")) {
		record_printf(RP_ALL, "Spectator %i disconnected\n", (int)(spectator-sps->spectators));
		drop_spectator(spectator, "disconnected");
		return; }
	else if(!Q_stricmp(Cmd_Argv(0), "weptiming")) {
		process_boolean_setting(spectator, "weptiming", &spectator->weptiming); }
	else if(!Q_stricmp(Cmd_Argv(0), "cycleall")) {
		process_boolean_setting(spectator, "cycleall", &spectator->cycleall); }
	else if(!Q_stricmp(Cmd_Argv(0), "help")) {
		spectator_add_server_command(cl, "print \"Commands:\nweptiming - Enables or disables"
			" weapon firing prints\ncycleall - Enables or disables selecting bot and spectator"
			" target clients\n\""); }
	else if(!Q_stricmp(Cmd_Argv(0), "userinfo")) {
		spectator_process_userinfo(spectator, Cmd_Argv(1)); } }

static void process_spectator_message(spectator_t *spectator, msg_t *msg) {
	// Based on sv_client.c->SV_ExecuteClientMessage
	client_t *cl = &spectator->cl;
	int serverId;
	int cmd;

#ifdef ELITEFORCE
	if(!msg->compat)
#endif
	MSG_Bitstream(msg);

	serverId = MSG_ReadLong(msg);
	cl->messageAcknowledge = MSG_ReadLong(msg);
	if(cl->messageAcknowledge < 0) return;

	cl->reliableAcknowledge = MSG_ReadLong(msg);
	if(cl->reliableAcknowledge < cl->reliableSequence - MAX_RELIABLE_COMMANDS) {
		cl->reliableAcknowledge = cl->reliableSequence;
		return; }

	if(serverId < sv.restartedServerId || serverId > sv.serverId) {
		// Pre map change serverID, or invalid high serverID
		if(cl->messageAcknowledge > cl->gamestateMessageNum) {
			// No previous gamestate waiting to be acknowledged - send new one
			send_spectator_gamestate(spectator); }
		return; }

	// No need to send old servertime once an up-to-date gamestate is acknowledged
	cl->oldServerTime = 0;

	// Read optional client command strings
	while(1) {
		cmd = MSG_ReadByte( msg );

#ifdef ELITEFORCE
		if(msg->compat && cmd == -1) return;
#endif
		if(cmd == clc_EOF) return;
		if(cmd != clc_clientCommand) break;
		process_spectator_command(spectator, msg);

		// In case command resulted in error/disconnection
		if(cl->state < CS_CONNECTED) return; }

	// Process move commands
	if(cmd == clc_move) process_spectator_move(spectator, msg, qtrue);
	else if(cmd == clc_moveNoDelta) process_spectator_move(spectator, msg, qfalse);
	else record_printf(RP_DEBUG, "process_spectator_message: invalid spectator command byte\n"); }

/* ******************************************************************************** */
// Spectator system initialization/allocation
/* ******************************************************************************** */

static void initialize_spectator_system(int max_spectators) {
	sps = record_calloc(sizeof(*sps));
	sps->spectators = record_calloc(sizeof(*sps->spectators) * max_spectators);
	sps->max_spectators = max_spectators;
	get_current_baselines(&sps->current_baselines); }

static void free_spectator_system(void) {
	record_free(sps->spectators);
	record_free(sps);
	sps = 0; }

static spectator_t *allocate_spectator(netadr_t *address, int qport) {
	// Returns either reused or new spectator on success, or null if all slots in use
	// Allocated structure will not have zeroed memory
	int i;
	spectator_t *avail = 0;
	if(!sps) initialize_spectator_system(admin_spectator_slots->integer);
	for(i=0; i<sps->max_spectators; ++i) {
		if(sps->spectators[i].cl.state == CS_FREE) {
			if(!avail) avail = &sps->spectators[i]; }
		else if ( NET_CompareBaseAdr( *address, sps->spectators[i].cl.netchan.remoteAddress )
				&& ( sps->spectators[i].cl.netchan.qport == qport
				|| address->port == sps->spectators[i].cl.netchan.remoteAddress.port ) ) {
			drop_spectator(&sps->spectators[i], 0);
			return &sps->spectators[i]; } }
	return avail; }

/* ******************************************************************************** */
// Exported functions
/* ******************************************************************************** */

void record_spectator_status(void) {
	int i;
	if(!sps) {
		record_printf(RP_ALL, "No spectators; spectator system not running\n");
		return; }

	for(i=0; i<sps->max_spectators; ++i) {
		client_t *cl = &sps->spectators[i].cl;
		const char *state = "unknown";
		if(cl->state == CS_FREE) continue;

		if(cl->state == CS_CONNECTED) state = "connected";
		else if(cl->state == CS_PRIMED) state = "primed";
		else if(cl->state == CS_ACTIVE) state = "active";

		record_printf(RP_ALL, "num(%i) address(%s) state(%s) lastmsg(%i) rate(%i)\n", i,
				NET_AdrToString(cl->netchan.remoteAddress), state, svs.time - cl->lastPacketTime, cl->rate); } }

void record_spectator_process_snapshot(void) {
	int i;
	qboolean active = qfalse;
	if(!sps) return;

	// Add current entities to entity buffer
	get_current_entities(&sps->frame_entities[++sps->frame_entities_position % FRAME_ENTITY_COUNT]);

	// Based on sv_snapshot.c->SV_SendClientMessages
	for(i=0; i<sps->max_spectators; ++i) {
		client_t *cl = &sps->spectators[i].cl;
		if(cl->state == CS_FREE) continue;
		active = qtrue;

		if(cl->lastPacketTime > svs.time) cl->lastPacketTime = svs.time;
		if(svs.time - cl->lastPacketTime > 60000) {
			record_printf(RP_ALL, "Spectator %i timed out\n", i);
			drop_spectator(&sps->spectators[i], "timed out");
			continue; }

		if(cl->netchan.unsentFragments || cl->netchan_start_queue) {
			SV_Netchan_TransmitNextFragment(cl);
			cl->rateDelayed = qtrue;
			continue; }

		// SV_RateMsec appears safe to call
		if(SV_RateMsec(cl) > 0) {
			cl->rateDelayed = qtrue;
			continue; }

		send_spectator_snapshot(&sps->spectators[i]);
		cl->lastSnapshotTime = svs.time;
		cl->rateDelayed = qfalse; }

	if(!active) {
		// No active spectators; free spectator system to save memory
		free_spectator_system(); } }

qboolean record_spectator_process_connection(netadr_t *address, const char *userinfo, qboolean compat) {
	// Returns qtrue to suppress normal handling of connection, qfalse otherwise
	spectator_t *spectator;
	const char *password = Info_ValueForKey(userinfo, "password");
	if(Q_stricmpn(password, "spect_", 6)) return qfalse;

	if(!*admin_spectator_password->string) {
		NET_OutOfBandPrint(NS_SERVER, *address, "print\nSpectator mode not enabled on this server.\n");
		return qtrue; }

	if(strcmp(password + 6, admin_spectator_password->string)) {
		NET_OutOfBandPrint(NS_SERVER, *address, "print\nIncorrect spectator password.\n");
		return qtrue; }

	spectator = allocate_spectator(address, atoi(Info_ValueForKey(userinfo, "qport")));
	if(!spectator) {
		record_printf(RP_ALL, "Failed to allocate spectator slot.\n");
		NET_OutOfBandPrint(NS_SERVER, *address, "print\nSpectator slots full.\n");
		return qtrue; }

	// Perform initializations from sv_client.c->SV_DirectConnect
	Com_Memset(spectator, 0, sizeof(*spectator));
	spectator->target_client = -1;
	spectator->cl.challenge = atoi(Info_ValueForKey(userinfo, "challenge"));
	spectator->cl.compat = compat;
	Netchan_Setup(NS_SERVER, &spectator->cl.netchan, *address,
			atoi(Info_ValueForKey(userinfo, "qport")), spectator->cl.challenge, compat);
	spectator->cl.netchan_end_queue = &spectator->cl.netchan_start_queue;
	NET_OutOfBandPrint(NS_SERVER, *address, "connectResponse %d", spectator->cl.challenge);
	spectator->cl.lastPacketTime = svs.time;
	spectator->cl.gamestateMessageNum = -1;
	spectator->cl.state = CS_CONNECTED;
	spectator_process_userinfo(spectator, userinfo);

	spectator_add_server_command(&spectator->cl, "print \"Spectator mode enabled - type /help for options\n\"");
	record_printf(RP_ALL, "Spectator %i connected from %s\n", (int)(spectator-sps->spectators), NET_AdrToString(*address));

	return qtrue; }

qboolean record_spectator_process_packet_event(netadr_t *address, msg_t *msg, int qport) {
	// Returns qtrue to suppress normal handling of packet, qfalse otherwise
	int i;
	if(!sps) return qfalse;

	// Based on sv_main.c->SV_PacketEvent
	for(i=0; i<sps->max_spectators; ++i) {
		client_t *cl = &sps->spectators[i].cl;
		if(cl->state == CS_FREE) continue;
		if(!NET_CompareBaseAdr(*address, cl->netchan.remoteAddress)) continue;
		if(cl->netchan.qport != qport) continue;

		cl->netchan.remoteAddress.port = address->port;
		msg->compat = cl->compat;
		if(SV_Netchan_Process(cl, msg)) {
			if(cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = svs.time;	// don't timeout
				process_spectator_message(&sps->spectators[i], msg); } }
		return qtrue; }

	return qfalse; }

void record_spectator_process_map_loaded(void) {
	int i;
	if(!sps) return;

	// Update current baselines
	get_current_baselines(&sps->current_baselines);

	for(i=0; i<sps->max_spectators; ++i) {
		client_t *cl = &sps->spectators[i].cl;
		if(cl->state >= CS_CONNECTED) {
			cl->state = CS_CONNECTED;
			cl->oldServerTime = sps->spectators[i].last_snapshot_sv_time; } } }

void record_spectator_process_configstring_change(int index, const char *value) {
	int i;
	if(!sps) return;

	// Based on sv_init.c->SV_SetConfigstring
	if(sv.state == SS_GAME || sv.restarting) {
		for(i=0; i<sps->max_spectators; ++i) {
			client_t *cl = &sps->spectators[i].cl;
			if(cl->state == CS_ACTIVE) spectator_send_configstring(cl, index, value);
			else cl->csUpdated[index] = qtrue; } } }

void record_spectator_process_servercmd(int clientNum, const char *value) {
	int i;
	if(!sps) return;

	if(!Q_stricmpn(value, "cs ", 3) || !Q_stricmpn(value, "bcs0 ", 5) || !Q_stricmpn(value, "bcs1 ", 5) ||
			!Q_stricmpn(value, "bcs2 ", 5) || !Q_stricmpn(value, "disconnect ", 11)) {
		// Skip configstring updates because they are handled separately
		// Also don't cause the spectator to disconnect when the followed client gets a disconnect command
		return; }

	for(i=0; i<sps->max_spectators; ++i) {
		client_t *cl = &sps->spectators[i].cl;
		if(cl->state != CS_ACTIVE) continue;
		if(sps->spectators[i].target_client == clientNum) {
			spectator_add_server_command(cl, value); } } }

void record_spectator_process_usercmd(int clientNum, usercmd_t *usercmd) {
	int i;
	if(!sps) return;

	for(i=0; i<sps->max_spectators; ++i) {
		// Send firing/ceased messages to spectators following this client with weptiming enabled
		client_t *cl = &sps->spectators[i].cl;
		if(cl->state != CS_ACTIVE) continue;
		if(sps->spectators[i].target_client != clientNum) continue;

		if(usercmd_is_firing_weapon(usercmd)) {
			if(!sps->spectators[i].target_firing_time) {
				if(sps->spectators[i].weptiming) spectator_add_server_command(cl, "print \"Firing\n\"");
				sps->spectators[i].target_firing_time = usercmd->serverTime; } }
		else {
			if(sps->spectators[i].target_firing_time) {
				if(sps->spectators[i].weptiming) spectator_add_server_command_fmt(cl, "print \"Ceased %i\n\"",
						usercmd->serverTime - sps->spectators[i].target_firing_time);
				sps->spectators[i].target_firing_time = 0; } } } }

#endif
