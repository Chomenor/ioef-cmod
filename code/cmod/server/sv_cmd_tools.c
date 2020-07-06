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
#define CVAR_SET(cvar_name, value) cvar_command_set(cvar_name, value, 0, CMD_NORMAL, qfalse, qfalse)
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
	int delim_len = strlen(delim);
	int input_size;
	int output_size;
	const char *next = delim_len ? Q_stristr(*ptr, delim) : 0;

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
	const char *delim = setop_argv(2);
	char buffer[1024];

	if(!*delim) delim = " ";

	while(*source_string) {
		cmdtools_advance_token(&source_string, delim, buffer, sizeof(buffer));
		if(!Q_stricmp(buffer, search_term)) {
			CVAR_SET(target_cvar, "true");
			return; } }

	CVAR_SET(target_cvar, "false"); }

static void setop_token_cmn(const char *target_cvar, const char *mode) {
	int index = atoi(setop_argv(0));
	const char *delim = setop_argv(1);
	const char *input = setop_argv(2);

	while(index-- > 0 && *input) {
		cmdtools_advance_token(&input, delim, 0, 0); }

	if(!Q_stricmp(mode, "token_at")) {
		char buffer[65536];
		cmdtools_advance_token(&input, delim, buffer, sizeof(buffer));
		CVAR_SET(target_cvar, buffer); }
	else {
		CVAR_SET(target_cvar, input); } }

static void setop_token_at(const char *target_cvar) {
	setop_token_cmn(target_cvar, "token_at"); }

static void setop_tokens_from(const char *target_cvar) {
	setop_token_cmn(target_cvar, "tokens_from"); }

static void setop_char_at(const char *target_cvar) {
	int index = atoi(setop_argv(0));
	const char *input = setop_argv(1);
	char output[2] = {0, 0};
	if(index >= 0 && index < strlen(input)) output[0] = input[index];
	CVAR_SET(target_cvar, output); }

static void setop_chars_from(const char *target_cvar) {
	int index = atoi(setop_argv(0));
	const char *input = setop_argv(1);
	const char *output = "";
	if(index >= 0 && index < strlen(input)) output = input + index;
	CVAR_SET(target_cvar, output); }

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
	const void (*fn)(const char *target_cvar);
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
	{"token_at", 3, 3, setop_token_at, "<index> <delimiter> <input_string>"},
	{"tokens_from", 3, 3, setop_tokens_from, "<index> <delimiter> <input_string>"},
	{"char_at", 2, 2, setop_char_at, "<index> <input_string>"},
	{"chars_from", 2, 2, setop_chars_from, "<index> <input_string>"},
	{"add", 1, 256, setop_add, "<value> <...>"},
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
// Init
/* ******************************************************************************** */

void cmod_sv_cmd_tools_init(void) {
	Cmd_AddCommand("if", cmd_if);
	Cmd_AddCommand("setop", cmd_setop); }

#endif
