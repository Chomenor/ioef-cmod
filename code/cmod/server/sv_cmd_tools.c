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

#ifdef CMOD_SERVER_CMD_TOOLS
#include "../../server/server.h"

/* ******************************************************************************** */
// Utility functions
/* ******************************************************************************** */

#ifdef CMOD_CVAR_HANDLING
#define CVAR_SET(cvar_name, value) Cvar_CommandSet(cvar_name, value, 0, CMD_NORMAL, qfalse, qtrue)
#else
#define CVAR_SET(cvar_name, value) Cvar_Set(cvar_name, value)
#endif

static const char *cmdtools_process_parameter(const char *value) {
	// Handles special keywords and cvar dereferencing based on leading asterisks
	// Example: input "abc" just returns string "abc", input "*abc" returns value of
	//   cvar "abc", input "**abc" returns value of cvar named by value of cvar "abc"
	int ref_count = 0;

	if(!Q_stricmp(value, "&none")) return "";
	if(!Q_stricmp(value, "&space")) return " ";
	if(!Q_stricmp(value, "&semi")) return ";";
	if(!Q_stricmp(value, "&asterisk")) return "*";

	while(*value == '*') {
		++ref_count;
		++value; }

	while(ref_count) {
		value = Cvar_VariableString(value);
		--ref_count; }

	return value; }

static qboolean cmdtools_str_to_bool(const char *value) {
	if(!Q_stricmp(value, "true")) return qtrue;
	if(!Q_stricmp(value, "yes")) return qtrue;
	if(!Q_stricmp(value, "on")) return qtrue;
	if(!Q_stricmp(value, "enable")) return qtrue;
	if(!Q_stricmp(value, "enabled")) return qtrue;
	if(atoi(value) > 0) return qtrue;
	return qfalse; }

static void cmdtools_advance_token(const char **ptr, const char *delim, char *buffer, int buffer_size) {
	// Copies next token to buffer and advances ptr
	int delim_len = delim ? strlen(delim) : 0;
	int input_size;
	int output_size;
	const char *next;

	if(!delim_len) {
		// Null delimiter - read next character
		next = **ptr ? *ptr + 1 : 0; }
	else {
		// Read next token
		next = Q_stristr(*ptr, delim); }

	if(next) {
		input_size = next - *ptr + delim_len;
		output_size = next - *ptr + 1; }
	else {
		input_size = strlen(*ptr);
		output_size = input_size + 1; }
	if(output_size > buffer_size) output_size = buffer_size;

	if(buffer_size) {
		Q_strncpyz(buffer, *ptr, output_size); }
	*ptr += input_size; }

typedef struct {
	const char *search_term;
	const char *replace_term;
	int search_term_length;
} replace_pair_t;

static void cmdtools_replace_multi(const char *source, char *buffer, unsigned int buffer_size,
		replace_pair_t *pairs, int pair_count) {
	// Performs single-pass string replacement, with support for multiple search-replace terms
	int i;
	cmod_stream_t stream = {buffer, 0, buffer_size, qfalse};

	while(1) {
		const char *next_position = 0;
		replace_pair_t *next_pair = 0;

		for(i=0; i<pair_count; ++i) {
			const char *result = Q_stristr(source, pairs[i].search_term);
			if(result && (!next_position || result < next_position)) {
				next_position = result;
				next_pair = &pairs[i]; } }

		if(next_position) {
			unsigned int length = next_position - source;
			cmod_stream_append_data(&stream, source, length);
			cmod_stream_append_string(&stream, next_pair->replace_term);
			source += length + next_pair->search_term_length; }
		else {
			cmod_stream_append_string(&stream, source);
			break; } } }

/* ******************************************************************************** */
// If command
/* ******************************************************************************** */

static void cmd_if(void) {
	// Execute trailing command conditionally depending on result of if statement
	const char *operation = Cmd_Argv(2);
	int arg_start = 4;
	qboolean result = qfalse;

	// Case-insensitive string comparison (example: if *somecvar s= abc vstr something)
	if(!Q_stricmp(operation, "s=") || !Q_stricmp(operation, "s!=")) {
		result = !Q_stricmp(cmdtools_process_parameter(Cmd_Argv(1)), cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Case-sensitive "exact" string comparison (example: if *somecvar e= abc vstr something)
	else if(!Q_stricmp(operation, "e=") || !Q_stricmp(operation, "e!=")) {
		result = !strcmp(cmdtools_process_parameter(Cmd_Argv(1)), cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Boolean comparison (example: if *somecvar b= true vstr something)
	else if(!Q_stricmp(operation, "b=") || !Q_stricmp(operation, "b!=")) {
		result = cmdtools_str_to_bool(cmdtools_process_parameter(Cmd_Argv(1))) ==
				cmdtools_str_to_bool(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Integer comparison (example: if *somecvar i!= 10 vstr something)
	else if(!Q_stricmp(operation, "i=") || !Q_stricmp(operation, "i!=")) {
		result = atoi(cmdtools_process_parameter(Cmd_Argv(1))) == atoi(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i>") || !Q_stricmp(operation, "i<=")) {
		result = atoi(cmdtools_process_parameter(Cmd_Argv(1))) > atoi(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i>=") || !Q_stricmp(operation, "i<")) {
		result = atoi(cmdtools_process_parameter(Cmd_Argv(1))) >= atoi(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Float comparison (example: if *somecvar f> 0.5 vstr something)
	else if(!Q_stricmp(operation, "f=") || !Q_stricmp(operation, "f!=")) {
		result = atof(cmdtools_process_parameter(Cmd_Argv(1))) == atof(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f>") || !Q_stricmp(operation, "f<=")) {
		result = atof(cmdtools_process_parameter(Cmd_Argv(1))) > atof(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f>=") || !Q_stricmp(operation, "f<")) {
		result = atof(cmdtools_process_parameter(Cmd_Argv(1))) >= atof(cmdtools_process_parameter(Cmd_Argv(3))) ? qtrue : qfalse; }
	else {
		Com_Printf("WARNING: Invalid if syntax\n");
		return; }

	if(strchr(operation, '!') || strchr(operation, '<')) {
		// Check for any inverse operations and invert result if needed
		result = result ? qfalse : qtrue; }

	if(result) {
		// Execute trailing command
		int i;
		int count = Cmd_Argc();
		char buffer[65536];
		cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};

		stream.data[0] = 0;
		for(i=arg_start; i<count; ++i) {
			cmod_stream_append_string_separated(&stream, Cmd_Argv(i), " "); }

		Cbuf_ExecuteText(EXEC_INSERT, stream.data); } }

/* ******************************************************************************** */
// Setop command
/* ******************************************************************************** */

static const char *setop_argv(int arg) {
	return cmdtools_process_parameter(Cmd_Argv(arg + 3)); }

static int setop_argc(void) {
	int args = Cmd_Argc() - 3;
	return args > 0 ? args : 0; }

static void setop_copy(const char *target_cvar) {
	CVAR_SET(target_cvar, setop_argv(0)); }

static void setop_join(const char *target_cvar) {
	int i;
	int args = setop_argc();
	const char *sep = setop_argv(0);
	char buffer[65536];
	cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};

	for(i=1; i<args; ++i) {
		const char *token = setop_argv(i);
		if(stream.position && *token) cmod_stream_append_string(&stream, sep);
		cmod_stream_append_string(&stream, token); }

	CVAR_SET(target_cvar, buffer); }

#define MAX_REPLACE_PAIRS 64

static void setop_replace(const char *target_cvar) {
	char buffer[65536];
	int i;
	int args = setop_argc();

	replace_pair_t replace_pairs[MAX_REPLACE_PAIRS];
	int replace_pair_count;

	for(i=1, replace_pair_count=0; i<args && replace_pair_count<MAX_REPLACE_PAIRS; i+=2) {
		replace_pair_t *pair = &replace_pairs[replace_pair_count];
		pair->search_term = setop_argv(i);
		pair->replace_term = setop_argv(i+1);
		pair->search_term_length = strlen(pair->search_term);
		if(*pair->search_term) ++replace_pair_count; }

	cmdtools_replace_multi(setop_argv(0), buffer, sizeof(buffer), replace_pairs, replace_pair_count);
	CVAR_SET(target_cvar, buffer); }

static void setop_str_contains_str(const char *target_cvar) {
	const char *search_term = setop_argv(1);
	CVAR_SET(target_cvar, (*search_term && Q_stristr(setop_argv(0), search_term)) ? "true" : "false"); }

static void setop_str_contains_term(const char *target_cvar) {
	const char *source_string = setop_argv(0);
	const char *search_term = setop_argv(1);
	const char *delim = setop_argc() >= 3 ? setop_argv(2) : " ";
	char buffer[65536];

	while(*source_string) {
		cmdtools_advance_token(&source_string, delim, buffer, sizeof(buffer));
		if(!Q_stricmp(buffer, search_term)) {
			CVAR_SET(target_cvar, "true");
			return; } }

	CVAR_SET(target_cvar, "false"); }

static int setop_count_tokens(const char *input, const char *delim) {
	int count = 0;
	while(*input) {
		cmdtools_advance_token(&input, delim, 0, 0);
		++count; }
	return count; }

static void setop_token_range(const char *target_cvar, const char *input, const char *delim,
		int start_index, int end_index) {
	// Writes tokens in range [start, end)
	int i;
	char buffer_out[65536];
	cmod_stream_t stream = {buffer_out, 0, sizeof(buffer_out), qfalse};
	char token[65536];

	if(start_index < 0 || end_index < 0) {
		// Handle negative indices
		int count = setop_count_tokens(input, delim);
		if(start_index < 0) start_index += count;
		if(end_index < 0) end_index += count; }

	for(i=0; *input && i<end_index; ++i) {
		if(i >= start_index) {
			cmdtools_advance_token(&input, delim, token, sizeof(token));
			if(stream.position) cmod_stream_append_string(&stream, delim);
			cmod_stream_append_string(&stream, token); }
		else {
			cmdtools_advance_token(&input, delim, 0, 0); } }

	cmod_stream_append_string(&stream, "");		// null terminate
	CVAR_SET(target_cvar, buffer_out); }

static void setop_token_at(const char *target_cvar) {
	int index = atoi(setop_argv(1));
	const char *delim = setop_argc() >= 3 ? setop_argv(2) : " ";
	setop_token_range(target_cvar, setop_argv(0), delim, index, index == -1 ? 65536 : index + 1); }

static void setop_tokens_from(const char *target_cvar) {
	int index = atoi(setop_argv(1));
	const char *delim = setop_argc() >= 3 ? setop_argv(2) : " ";
	setop_token_range(target_cvar, setop_argv(0), delim, index, 65536); }

static void setop_tokens_until(const char *target_cvar) {
	int index = atoi(setop_argv(1));
	const char *delim = setop_argc() >= 3 ? setop_argv(2) : " ";
	setop_token_range(target_cvar, setop_argv(0), delim, 0, index); }

static void setop_char_at(const char *target_cvar) {
	int index = (unsigned int)atoi(setop_argv(1));
	setop_token_range(target_cvar, setop_argv(0), "", index, index == -1 ? 65536 : index + 1); }

static void setop_chars_from(const char *target_cvar) {
	int index = (unsigned int)atoi(setop_argv(1));
	setop_token_range(target_cvar, setop_argv(0), "", index, 65536); }

static void setop_chars_until(const char *target_cvar) {
	int index = (unsigned int)atoi(setop_argv(1));
	setop_token_range(target_cvar, setop_argv(0), "", 0, index); }

static void setop_add(const char *target_cvar) {
	int i;
	int args = setop_argc();
	double result = 0.0;

	for(i=0; i<args; ++i) {
		result += atof(setop_argv(i)); }

	CVAR_SET(target_cvar, va("%g", result)); }

static void setop_subtract(const char *target_cvar) {
	double result = atof(setop_argv(0)) - atof(setop_argv(1));
	CVAR_SET(target_cvar, va("%g", result)); }

static void setop_multiply(const char *target_cvar) {
	double result = atof(setop_argv(0)) * atof(setop_argv(1));
	CVAR_SET(target_cvar, va("%g", result)); }

static void setop_divide(const char *target_cvar) {
	double result = atof(setop_argv(0)) / atof(setop_argv(1));
	CVAR_SET(target_cvar, va("%g", result)); }

static void setop_rand(const char *target_cvar) {
	int start = atoi(setop_argv(0));
	int end = atoi(setop_argv(1));
	int range = end - start;
	if(range < 0) {
		Com_Printf("setop-rand: Invalid range\n");
		return; }
	CVAR_SET(target_cvar, va("%i", rand() % (range + 1) + start)); }

static void setop_randf(const char *target_cvar) {
	float start = atof(setop_argv(0));
	float end = atof(setop_argv(1));
	float range = end - start;
	if(range < 0.0) {
		Com_Printf("setop-randf: Invalid range\n");
		return; }
	CVAR_SET(target_cvar, va("%g", ((float)rand() / (float)(RAND_MAX)) * range + start)); }

static void setop_file_exists(const char *target_cvar) {
	const char *path = setop_argv(0);
	CVAR_SET(target_cvar, (*path && FS_ReadFile(path, NULL) > 0) ? "true" : "false"); }

typedef struct {
	const char *cmd_name;
	int min_parameters;
	int max_parameters;
	void (*fn)(const char *target_cvar);
	const char *parameter_info;
} setop_command_t;

// "Usage: setop <target_cvar> token_at <index> <delimiter> <input_string>\n"
// TODO: Add more info messages

setop_command_t setop_commands[] = {
	{"copy", 1, 1, setop_copy, "<source_value>"},
	{"join", 2, 256, setop_join, "<separator> <token1> <...>"},
	{"replace", 1, 256, setop_replace, "<input> <search_term> <replace_term> <...>"},
	{"str_contains_str", 2, 2, setop_str_contains_str, "<string> <search_term>"},
	{"str_contains_term", 2, 3, setop_str_contains_term, "<string> <search_term> <delimiter>"},
	{"token_at", 2, 3, setop_token_at, "<string> <index> <delimiter>"},
	{"tokens_from", 2, 3, setop_tokens_from, "<string> <index> <delimiter>"},
	{"tokens_until", 2, 3, setop_tokens_until, "<string> <index> <delimiter>"},
	{"char_at", 2, 2, setop_char_at, "<index> <input_string>"},
	{"chars_from", 2, 2, setop_chars_from, "<index> <input_string>"},
	{"chars_until", 2, 2, setop_chars_until, "<index> <input_string>"},
	{"add", 2, 256, setop_add, "<value> <value> <...>"},
	{"subtract", 2, 2, setop_subtract, "<value> <value>"},
	{"multiply", 2, 2, setop_multiply, "<value> <value>"},
	{"divide", 2, 2, setop_divide, "<value> <value>"},
	{"rand", 2, 2, setop_rand, "<start value> <end value>"},
	{"randf", 2, 2, setop_randf, "<start value> <end value>"},
	{"file_exists", 1, 1, setop_file_exists, "<path>"},
};

static void cmd_setop(void) {
	int args = Cmd_Argc();
	if(args < 3) {
		Com_Printf("Usage: setop <target_cvar> <command> <...>\n"); }
	else {
		int i;
		int parameters = args - 3;
		const char *cmd_str = Cmd_Argv(2);
		setop_command_t *cmd = 0;

		for(i=0; i<ARRAY_LEN(setop_commands); ++i) {
			if(!Q_stricmp(cmd_str, setop_commands[i].cmd_name)) {
				cmd = &setop_commands[i];
				break; } }

		if(!cmd) {
			Com_Printf("setop: Invalid command '%s'\n", cmd_str);
			return; }

		if(parameters < cmd->min_parameters || parameters > cmd->max_parameters) {
			Com_Printf("setop: Invalid number of parameters for command '%s'\n", cmd->cmd_name);
			return; }
		cmd->fn(cmdtools_process_parameter(Cmd_Argv(1))); } }

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

static void cmd_servercmd(void) {
	int clientnum = atoi(cmdtools_process_parameter(Cmd_Argv(1)));
	const char *cmd = cmdtools_process_parameter(Cmd_Argv(2));

	if(!*cmd) {
		Com_Printf("Usage: servercmd <clientnum> <cmd>\n");
		return; }
	if(!com_sv_running->integer) {
		Com_Printf("servercmd: Server not running.\n");
		return; }
	if(clientnum < -1 || clientnum > sv_maxclients->integer) {
		Com_Printf("servercmd: Invalid client number.\n");
		return; }
	if(clientnum >= 0 && svs.clients[clientnum].state < CS_PRIMED) {
		Com_Printf("servercmd: Client %i is not active.\n", clientnum);
		return; }

	// Convert "\n", "\q", and "\\" inputs
	char buffer[1020];
	cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};
	while(*cmd) {
		char next = *(cmd++);
		if(next == '\\') {
			next = *(cmd++);
			if(next == 'n') next = '\n';
			if(next == 'q') next = '\"'; }
		cmod_stream_append_data(&stream, &next, 1); }
	cmod_stream_append_string(&stream, "");		// null terminate
	if(stream.overflowed) {
		Com_Printf("servercmd: Command length overflow.\n");
		return; }

	SV_SendServerCommand(clientnum >= 0 ? &svs.clients[clientnum] : 0, "%s", stream.data); }

#ifdef CMOD_SERVER_CMD_TRIGGERS
/* ******************************************************************************** */
// Triggers
/* ******************************************************************************** */

#define MAX_TRIGGERS 256

/* *** Time functions *** */

#include <sys/timeb.h>

static uint64_t trigger_curtime_ms(void) {
	// https://stackoverflow.com/a/44616416
#if defined(_WIN32) || defined(_WIN64)
	struct _timeb timebuffer;
	_ftime(&timebuffer);
	return (uint64_t)(((timebuffer.time * 1000) + timebuffer.millitm));
#else
	struct timeb timebuffer;
	ftime(&timebuffer);
	return (uint64_t)(((timebuffer.time * 1000) + timebuffer.millitm));
#endif
}

/* *** Trigger defs *** */

static const char *trigger_type_to_string(cmd_trigger_type_t type) {
	switch(type) {
		default: return "none";
		case TRIGGER_TIMER: return "timer";
		case TRIGGER_REPEAT: return "repeat";
		case TRIGGER_MAP_CHANGE: return "map_change";
		case TRIGGER_MAP_RESTART: return "map_restart";
		case TRIGGER_INTERMISSION_START: return "intermission_start";
		case TRIGGER_CLIENT_CONNECT: return "client_connect";
		case TRIGGER_CLIENT_DISCONNECT: return "client_disconnect";
		case TRIGGER_CLIENT_ENTERWORLD: return "client_enterworld"; } }

static cmd_trigger_type_t string_to_trigger_type(const char *string) {
	if(!Q_stricmp(string, "timer")) return TRIGGER_TIMER;
	if(!Q_stricmp(string, "repeat")) return TRIGGER_REPEAT;
	if(!Q_stricmp(string, "map_change")) return TRIGGER_MAP_CHANGE;
	if(!Q_stricmp(string, "map_restart")) return TRIGGER_MAP_RESTART;
	if(!Q_stricmp(string, "intermission_start")) return TRIGGER_INTERMISSION_START;
	if(!Q_stricmp(string, "client_connect")) return TRIGGER_CLIENT_CONNECT;
	if(!Q_stricmp(string, "client_disconnect")) return TRIGGER_CLIENT_DISCONNECT;
	if(!Q_stricmp(string, "client_enterworld")) return TRIGGER_CLIENT_ENTERWORLD;
	return TRIGGER_NONE; }

typedef struct {
	cmd_trigger_type_t type;
	char *tag;
	char *cmd;
	uint64_t trigger_time;
	unsigned int duration;
} cmd_trigger_t;

/* *** Implementation *** */

static cmd_trigger_t triggers[MAX_TRIGGERS];
static qboolean triggers_enabled = qfalse;

static cmd_trigger_t *get_trigger_by_tag(const char *tag) {
	// Returns matching trigger if found, null otherwise
	int i;
	for(i=0; i<MAX_TRIGGERS; ++i) {
		if(triggers[i].type == TRIGGER_NONE) continue;
		if(!Q_stricmp(tag, triggers[i].tag)) return &triggers[i]; }
	return 0; }

static cmd_trigger_t *get_free_trigger(void) {
	// Returns free trigger slot if available, null otherwise
	int i;
	for(i=0; i<MAX_TRIGGERS; ++i) {
		if(triggers[i].type == TRIGGER_NONE) return &triggers[i]; }
	return 0; }

static void trigger_delete(cmd_trigger_t *trigger) {
	// Deallocate trigger
	if(trigger->type != TRIGGER_NONE) {
		Z_Free(trigger->tag);
		Z_Free(trigger->cmd); }
	trigger->type = TRIGGER_NONE; }

static void cmd_trigger_set(void) {
	const char *arg_type = cmdtools_process_parameter(Cmd_Argv(1));
	const char *arg_tag = cmdtools_process_parameter(Cmd_Argv(2));
	const char *arg_cmd = cmdtools_process_parameter(Cmd_Argv(3));
	if(!*arg_type || !*arg_tag || !*arg_cmd) {
		Com_Printf("Usage: trigger_set <type> <tag> <command> <...>\n");
		return; }

	// Get type
	cmd_trigger_type_t type = string_to_trigger_type(arg_type);
	if(type == TRIGGER_NONE) {
		Com_Printf("trigger_set: Invalid trigger type '%s'\n", Cmd_Argv(1));
		return; }

	// If a trigger already exists with tag, free it
	cmd_trigger_t *trigger = get_trigger_by_tag(arg_tag);
	if(trigger) trigger_delete(trigger);

	// Create new trigger (don't necessarily use the slot from get_trigger_by_tag for ordering reasons)
	trigger = get_free_trigger();
	if(!trigger) {
		Com_Printf("trigger_set: No trigger slots available\n");
		return; }
	trigger->type = type;
	trigger->tag = CopyString(arg_tag);
	trigger->cmd = CopyString(arg_cmd);

	// Set the time for time-based triggers
	if(type == TRIGGER_TIMER || type == TRIGGER_REPEAT) {
		trigger->duration = (unsigned int)atoi(cmdtools_process_parameter(Cmd_Argv(4)));
		trigger->trigger_time = trigger_curtime_ms() + trigger->duration; }

	triggers_enabled = qtrue; }

static void cmd_trigger_clear(void) {
	// Remove triggers matching filter
	const char *filter = cmdtools_process_parameter(Cmd_Argv(1));
	int i;
	for(i=0; i<MAX_TRIGGERS; ++i) {
		if(triggers[i].type == TRIGGER_NONE) continue;
		if(Com_Filter((char *)filter, triggers[i].tag, 0)) trigger_delete(&triggers[i]); } }

static void cmd_trigger_status(void) {
	// Debug command to show trigger info
	qboolean have_trigger = qfalse;
	uint64_t curtime = trigger_curtime_ms();
	int i;
	char buffer[65536];
	cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};
	#define ADD_TEXT(s) cmod_stream_append_string(&stream, s);

	for(i=0; i<MAX_TRIGGERS; ++i) {
		if(triggers[i].type == TRIGGER_NONE) continue;
		have_trigger = qtrue;

		stream.position = 0;
		ADD_TEXT(va("trigger %i: type(%s) tag(%s) cmd(%s)", i, trigger_type_to_string(triggers[i].type),
				triggers[i].tag, triggers[i].cmd));

		if(triggers[i].type == TRIGGER_TIMER || triggers[i].type == TRIGGER_REPEAT) {
			uint64_t remaining = triggers[i].trigger_time > curtime ? triggers[i].trigger_time - curtime : 0;
			unsigned int msec = remaining % 1000;
			remaining /= 1000;
			unsigned int sec = remaining % 1000;
			remaining /= 60;
			unsigned int min = remaining % 60;
			remaining /= 60;
			unsigned int hour = (unsigned int)remaining;

			ADD_TEXT(" remaining(");
			if(hour) ADD_TEXT(va("%uh ", hour));
			if(hour || min) ADD_TEXT(va("%um ", min));
			if(hour || min || sec) ADD_TEXT(va("%us ", sec));
			ADD_TEXT(va("%ums)", msec)); }

		if(triggers[i].type == TRIGGER_REPEAT) {
			ADD_TEXT(va(" interval(%u)", triggers[i].duration)); }

		Com_Printf("%s\n", stream.data); }

	if(!have_trigger) Com_Printf("No triggers active.\n"); }

// cmod_cmd.c / cmd.c
qboolean Cbuf_IsEmpty(void);

static void trigger_exec(const char *cmd, const char *tag) {
	// Execute the command action for trigger
	qboolean empty = Cbuf_IsEmpty();
	Cbuf_ExecuteText(EXEC_APPEND, "\nset cmod_in_trigger 1\n");
	Cbuf_ExecuteText(EXEC_APPEND, cmd);
	Cbuf_ExecuteText(EXEC_APPEND, "\nset cmod_in_trigger 0\n");

	if(cmod_trigger_debug->integer) {
		Com_Printf("Running trigger '%s'\n", tag); }

	// Only exec now if there were no previous commands in command buffer
	if(empty) {
		Cbuf_Execute(); }
	else {
		Com_Printf("note: trigger '%s' deferred due to nonempty command buffer\n", tag); } }

void trigger_exec_type(cmd_trigger_type_t type) {
	// Execute all triggers for given type
	if(!triggers_enabled) return;

	uint64_t curtime = 0;
	if(type == TRIGGER_TIMER || type == TRIGGER_REPEAT) {
		curtime = trigger_curtime_ms(); }

	int i;
	for(i=0; i<MAX_TRIGGERS; ++i) {
		cmd_trigger_t *trigger = &triggers[i];
		if(trigger->type != type) continue;

		// Only fire timer triggers when time has elapsed
		if(type == TRIGGER_TIMER || type == TRIGGER_REPEAT) {
			if(trigger->duration && curtime < trigger->trigger_time) continue; }

		// Save for exec later
		char *cmd = CopyString(trigger->cmd);
		char *tag = CopyString(trigger->tag);

		if(type == TRIGGER_TIMER) {
			// Timer triggers only fire once, so delete it
			trigger_delete(trigger); }

		if(type == TRIGGER_REPEAT) {
			// Update next fire time
			trigger->trigger_time += trigger->duration;
			if(trigger->trigger_time < curtime) {
				// Shouldn't normally happen...
				trigger->trigger_time = curtime + trigger->duration; } }

		// Execute the trigger
		trigger_exec(cmd, tag);
		Z_Free(cmd);
		Z_Free(tag); } }
#endif

/* ******************************************************************************** */
// Init
/* ******************************************************************************** */

void cmod_sv_cmd_tools_init(void) {
	Cmd_AddCommand("if", cmd_if);
	Cmd_AddCommand("setop", cmd_setop);
	Cmd_AddCommand("servercmd", cmd_servercmd);
#ifdef CMOD_SERVER_CMD_TRIGGERS
	Cmd_AddCommand("trigger_set", cmd_trigger_set);
	Cmd_AddCommand("trigger_clear", cmd_trigger_clear);
	Cmd_AddCommand("trigger_status", cmd_trigger_status);
#endif
}

#endif
