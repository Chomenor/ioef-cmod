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

#ifdef CMOD_VOTING
#include "sv_voting_local.h"

/* ******************************************************************************** */
// Declarations
/* ******************************************************************************** */

#define	EF_CS_WARMUP			5
#define EF_CS_VOTE_TIME			8
#define EF_CS_VOTE_STRING		9
#define	EF_CS_VOTE_YES			10
#define	EF_CS_VOTE_NO			11

typedef struct {
	netadr_t address;
	qboolean voted;
} voter_t;

typedef struct {
	qboolean vote_active;
	vote_action_t vote_action;

	int vote_end_time;	// In terms of sv.time
	int intermission_suspend_time;	// In terms of time remaining

	netadr_t caller_address;	// For fail counting
	voter_t voters[128];
	int voter_count;
	int yes_votes;
	int no_votes;
} vote_state_t;

#define MAX_VOTE_FAILS 32

typedef struct {
	netadr_t address;
	int time;	// svs.time of failure
} vote_fail_t;

static vote_state_t vote_state;
static vote_fail_t vote_fails[MAX_VOTE_FAILS];
int vote_last_pass_time;

/* ******************************************************************************** */
// Fail Counting
/* ******************************************************************************** */

static void register_vote_fail(netadr_t *address) {
	// Records vote fail from specified address
	int i;
	int oldest_fail_time = 0;
	vote_fail_t *oldest_fail = 0;

	// Find the oldest fail entry to overwrite
	for(i=0; i<MAX_VOTE_FAILS; ++i) {
		int elapsed = svs.time - vote_fails[i].time;
		if(elapsed < 0) {
			oldest_fail = &vote_fails[i];
			break; }
		if(!oldest_fail || elapsed > oldest_fail_time) {
			oldest_fail = &vote_fails[i];
			oldest_fail_time = elapsed; } }

	// Write the entry
	oldest_fail->address = *address;
	oldest_fail->time = svs.time; }

static int qsort_int(const void *element1, const void *element2) {
	return *(int *)element1 - *(int *)element2; }

static int check_vote_fails(netadr_t *address) {
	// Returns number of seconds until specified address is allowed to vote again
	int i;
	int fail_times[MAX_VOTE_FAILS];		// ms since failed vote
	int count = 0;
	int wait = 0;

	// Get list of fail times (milliseconds since failed vote) sorted from soonest to latest
	for(i=0; i<MAX_VOTE_FAILS; ++i) {
		int elapsed = svs.time - vote_fails[i].time;
		if(elapsed >= 0 && vote_fails[i].address.type &&
				NET_CompareBaseAdr(vote_fails[i].address, *address)) {
			fail_times[count++] = elapsed; } }
	qsort(fail_times, count, sizeof(*fail_times), qsort_int);

	// Print log message
	if(count) {
		char buffer[256];
		cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};
		cmod_stream_append_string(&stream, "Have fail times for IP:");
		for(i=0; i<count; ++i) cmod_stream_append_string(&stream, va(" %i", fail_times[i]));
		if(!count) cmod_stream_append_string(&stream, " <None>");
		VOTE_LOG("%s", buffer); }

	// Require 20 second delay after each failed vote, to prevent spam and give other players
	// a chance to call votes
	if(count >= 1 && fail_times[0] < 20000) {
		wait = (20000 - fail_times[0]) / 1000; }

	// Allow maximum 3 failed votes per 5 minute period
	if(count >= 3 && fail_times[2] < 300000) {
		int x_wait = (300000 - fail_times[2]) / 1000;
		if(x_wait > wait) wait = x_wait; }

	return wait; }

/* ******************************************************************************** */
// Vote Tally Handling
/* ******************************************************************************** */

static void register_voter(client_t *client) {
	int i;
	int match_count = 0;
	if(vote_state.voter_count >= ARRAY_LEN(vote_state.voters)) return;
	for(i=0; i<vote_state.voter_count; ++i) {
		if(NET_CompareBaseAdr(client->netchan.remoteAddress, vote_state.voters[i].address)) ++match_count; }
	if(match_count >= cmod_sv_voting_max_voters_per_ip->integer) return;

	// Add the voter
	vote_state.voters[vote_state.voter_count++].address = client->netchan.remoteAddress; }

static void initialize_eligible_voters(void) {
	// Assumes relevant vote_state structures (voters and voter_count) are zeroed
	int i;
	for(i=0; i<sv_maxclients->integer; ++i) {
		if(svs.clients[i].state < CS_CONNECTED) continue;
		if(svs.clients[i].netchan.remoteAddress.type == NA_BOT) continue;
		register_voter(&svs.clients[i]); } }

static int check_tally(qboolean countdown_ended) {
	// Returns 0 for no result, 1 for pass, 2 for fail
	if(vote_state.yes_votes > vote_state.voter_count / 2) return 1;				// Pass by absolute majority
	if(vote_state.no_votes >= (vote_state.voter_count + 1) / 2) return 2;		// Certain fail
	if(!countdown_ended) return 0;												// Timer still running
	if(cmod_sv_voting_mode->integer != 1 && vote_state.yes_votes >
			(vote_state.yes_votes + vote_state.no_votes) * 2 / 3) return 1;		// Pass by final preference
	return 2; }

static qboolean register_vote(client_t *client, qboolean yes_vote) {
	// Returns qtrue if vote successfully registered, qfalse otherwise
	int i;
	voter_t *voter = 0;

	// Try to get exact address match
	for(i=0; i<vote_state.voter_count; ++i) {
		voter_t *candidate = &vote_state.voters[i];
		if(NET_CompareAdr(client->netchan.remoteAddress, candidate->address)) {
			voter = candidate;
			break; } }

	// Try to find a spare slot with base address match
	if(!voter) {
		for(i=0; i<vote_state.voter_count; ++i) {
			voter_t *candidate = &vote_state.voters[i];
			if(!candidate->voted && NET_CompareBaseAdr(client->netchan.remoteAddress, candidate->address)) {
				voter = candidate;
				break; } } }

	if(!voter || voter->voted) return qfalse;

	voter->voted = qtrue;
	voter->address = client->netchan.remoteAddress;
	if(yes_vote) ++vote_state.yes_votes;
	else ++vote_state.no_votes;
	return qtrue; }

/* ******************************************************************************** */
// Rendering
/* ******************************************************************************** */

static void blank_configstring(int index) {
	// Kludge to make sure SV_SetConfigstring retransmits the configstring
	Z_Free( sv.configstrings[index] );
	sv.configstrings[index] = CopyString(""); }

static void render_yes_votes(void) {
	blank_configstring(EF_CS_VOTE_YES);
	SV_SetConfigstring(EF_CS_VOTE_YES, va("%i", vote_state.yes_votes)); }

static void render_no_votes(void) {
	int count = vote_state.no_votes;
	if(cmod_sv_voting_mode->integer) count = vote_state.voter_count - vote_state.yes_votes;
	blank_configstring(EF_CS_VOTE_NO);
	SV_SetConfigstring(EF_CS_VOTE_NO, va("%i", count)); }

static void render_vote(void) {
	blank_configstring(EF_CS_VOTE_STRING);
	SV_SetConfigstring(EF_CS_VOTE_STRING, vote_state.vote_action.info_string);
	blank_configstring(EF_CS_VOTE_TIME);
	SV_SetConfigstring(EF_CS_VOTE_TIME, va("%i", vote_state.vote_end_time - 30000));
	render_yes_votes();
	render_no_votes(); }

static void render_voting_inactive(void) {
	blank_configstring(EF_CS_VOTE_TIME);
	SV_SetConfigstring(EF_CS_VOTE_TIME, "0"); }

/* ******************************************************************************** */
// Callvote Handling
/* ******************************************************************************** */

static void execute_vote_pass_command(void) {
	Cbuf_ExecuteText(EXEC_APPEND, va("%s\n", vote_state.vote_action.pass_command)); }

static int get_voting_game_status(void) {
	// Returns 0 if there are no active players, 1 if in intermission, and 2 if game is active
	int i;
	for(i=0; i<sv_maxclients->integer; ++i) {
		if(svs.clients[i].state < CS_ACTIVE) continue;
		if(SV_GameClientNum(i)->pm_type == 5) return 1;
		return 2; }
	return 0; }

static void process_callvote(client_t *client, const char *cmd_string) {
	int fail_wait_time;
	VOTE_LOG("# ## # ## # ## # ## # ## # ## # ## # ## # ## # ## # ## #");
	VOTE_LOG_FLUSH("Have callvote command: client(%i) ip(%s) name(%s) cmd(%s)",
			(int)(client-svs.clients), NET_AdrToStringwPort(client->netchan.remoteAddress), client->name, cmd_string);

	// Check for existing vote in progress
	if(vote_state.vote_active) {
		VOTE_LOG("Processing vote despite vote already in progress (info message printing only)");
		voteaction_process_callvote(client, qtrue, 0);
		return; }

	// Check for timing issues that could be prone to causing bugs
	if(svs.time >= vote_last_pass_time && svs.time - vote_last_pass_time < 100) {
		VOTE_LOG("Skipping vote due to recent vote pass (within 100 ms)");
		return; }
	if(sv.time < 5000) {
		VOTE_LOG("Skipping vote due to recent map change (within 5000 ms)");
		return; }

	// Clear vote state
	Com_Memset(&vote_state, 0, sizeof(vote_state));

	// Process the command
	if(!voteaction_process_callvote(client, qfalse, &vote_state.vote_action)) return;

	// Initialize voters
	initialize_eligible_voters();

	// Attempt to register automatic yes vote for caller
	register_vote(client, qtrue);

	// Check for immediate pass or fail
	int check_value = check_tally(qfalse);
	if(check_value == 1) {
		VOTE_LOG("Immediate pass due to no other players available to vote. (yes:%i no:%i total:%i)",
				vote_state.yes_votes, vote_state.no_votes, vote_state.voter_count);
		SV_SendServerCommand(0, "print \"Vote passed.\n\"");
		execute_vote_pass_command();
		return; }
	else if(check_value == 2) {
		VOTE_LOG("WARNING: Failed to start vote due to check_tally value. (yes:%i no:%i total:%i)",
				vote_state.yes_votes, vote_state.no_votes, vote_state.voter_count);
		SV_SendServerCommand(0, "print \"There was an error starting the vote.\n\"");
		return; }

	// Don't start votes during intermission
	if(get_voting_game_status() < 2) {
		VOTE_LOG("Skipping vote due to intermission.");
		SV_SendServerCommand(client, "print \"Can't vote during intermission.\n\"");
		return; }

	// Check if player is blocked from calling votes by fail time limits
	fail_wait_time = check_vote_fails(&client->netchan.remoteAddress);
	if(fail_wait_time > 0) {
		VOTE_LOG("Skipping vote due to fail wait time (%i seconds).", fail_wait_time);
		SV_SendServerCommand(client, "print \"Wait %i seconds to vote again.\n\"", fail_wait_time);
		return; }

	// Initiate the vote
	VOTE_LOG("Vote initiated with %i available voters.", vote_state.voter_count);
	SV_SendServerCommand(0, "print \"%s^7 called a vote.\n\"", client->name);
	vote_state.vote_active = qtrue;
	vote_state.caller_address = client->netchan.remoteAddress;
	vote_state.vote_end_time = sv.time + 20000;
	vote_state.intermission_suspend_time = 0;
	render_vote(); }

/* ******************************************************************************** */
// Yes/No Vote Handling
/* ******************************************************************************** */

static void process_vote(client_t *client, const char *cmd_string) {
	qboolean yes_vote;
	char *input = Cmd_Argv(1);

	if(!vote_state.vote_active) {
		SV_SendServerCommand(client, "print \"No vote in progress.\n\"");
		return; }
	if(vote_state.intermission_suspend_time) {
		SV_SendServerCommand(client, "print \"Can't vote during intermission.\n\"");
		return; }

	// Determine whether vote is yes or no
	if(*input == 'y' || *input == 'Y' || *input == '1') yes_vote = qtrue;
	else if(*input == 'n' || *input == 'N' || *input == '0') yes_vote = qfalse;
	else {
		SV_SendServerCommand(client, "print \"Invalid vote command. Acceptable commands are 'vote yes' and 'vote no'.\n\"");
		return; }

	// Register the vote
	if((cmod_sv_voting_mode->integer == 1 && !yes_vote) || !register_vote(client, yes_vote)) {
		SV_SendServerCommand(client, "print \"Vote already cast.\n\"");
		return; }

	// Record to logs
	VOTE_LOG("Client %i (%s) voted %s.", (int)(client-svs.clients), client->name, yes_vote ? "yes" : "no");
	SV_SendServerCommand(client, "print \"Vote cast.\n\"");

	// Update render
	if(yes_vote) render_yes_votes();
	if(!yes_vote || cmod_sv_voting_mode->integer == 1) render_no_votes(); }

/* ******************************************************************************** */
// Interface Functions
/* ******************************************************************************** */

qboolean cmod_voting_handle_command(client_t *client, const char *cmd_string) {
	// Returns qtrue to suppress normal handling of command, qfalse otherwise
	// Assumes command has already been tokenized
	if(!cmod_sv_voting_enabled || !cmod_sv_voting_enabled->integer) return qfalse;
	if(!Q_stricmp(Cmd_Argv(0), "callvote") || !Q_stricmp(Cmd_Argv(0), "cv")) {
		process_callvote(client, cmd_string);
		return qtrue; }
	if(!Q_stricmp(Cmd_Argv(0), "vote")) {
		process_vote(client, cmd_string);
		return qtrue; }
	return qfalse; }

void cmod_voting_handle_map_restart(void) {
	if(!vote_state.vote_active) return;

	if(vote_state.intermission_suspend_time) {
		// Resume the vote at previous time
		VOTE_LOG("Resuming vote due to intermission end.");
		vote_state.vote_end_time = sv.time + vote_state.intermission_suspend_time;
		vote_state.intermission_suspend_time = 0; }

	// Resend vote configstrings because map restart command clears vote message on client
	render_vote(); }

void cmod_voting_handle_map_change(void) {
	if(vote_state.vote_active) VOTE_LOG("Aborting vote due to map change.");
	vote_state.vote_active = 0; }

void cmod_voting_frame(void) {
	if(!vote_state.vote_active) return;

	int game_status = get_voting_game_status();
	if(!game_status) {
		VOTE_LOG("Dropping vote due to no active clients.");
		vote_state.vote_active = qfalse;
		render_voting_inactive();
		return; }

	if(vote_state.intermission_suspend_time) return;

	if(game_status == 1) {
		// Set the suspend time
		VOTE_LOG("Suspending vote due to intermission.");
		vote_state.intermission_suspend_time = vote_state.vote_end_time - sv.time;
		return; }

	if(sv.time + 100000 < vote_state.vote_end_time) {
		// Shouldn't happen
		VOTE_LOG("!WARNING: Dropping vote due to invalid end time: %i", vote_state.vote_end_time - sv.time);
		vote_state.vote_active = qfalse;
		render_voting_inactive();
		return; }

	qboolean countdown_ended = sv.time >= vote_state.vote_end_time ? qtrue : qfalse;
	int tally_result = check_tally(countdown_ended);
	if(tally_result == 1) {
		VOTE_LOG("Vote passed - executing pass command. (yes:%i no:%i total:%i)",
				vote_state.yes_votes, vote_state.no_votes, vote_state.voter_count);
		SV_SendServerCommand(0, "print \"Vote passed.\n\"");
		vote_state.vote_active = qfalse;
		render_voting_inactive();
		vote_last_pass_time = svs.time;
		execute_vote_pass_command(); }
	else if(tally_result == 2 || countdown_ended) {
		VOTE_LOG("Vote failed. (yes:%i no:%i total:%i)",
				vote_state.yes_votes, vote_state.no_votes, vote_state.voter_count);
		SV_SendServerCommand(0, "print \"Vote failed.\n\"");
		vote_state.vote_active = qfalse;
		render_voting_inactive();
		register_vote_fail(&vote_state.caller_address); } }

#endif
