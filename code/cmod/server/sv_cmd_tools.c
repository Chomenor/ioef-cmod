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

#ifdef CMOD_CVAR_HANDLING
#define CVAR_SET(cvar_name, value) cvar_command_set(cvar_name, value, 0, CMD_NORMAL, qfalse, qfalse)
#else
#define CVAR_SET(cvar_name, value) Cvar_Set(cvar_name, value)
#endif

static qboolean cmd_str_to_bool(const char *value) {
	if(!Q_stricmp(value, "true")) return qtrue;
	if(!Q_stricmp(value, "yes")) return qtrue;
	if(!Q_stricmp(value, "on")) return qtrue;
	if(!Q_stricmp(value, "enable")) return qtrue;
	if(!Q_stricmp(value, "enabled")) return qtrue;
	if(atoi(value)) return qtrue;
	return qfalse; }

static const char *cvar_dereference(const char *value) {
	// Performs cvar deferencing based on number of leading asterisks
	// Example: input "abc" just returns string "abc", input "*abc" returns value of
	//   cvar "abc", input "**abc" returns value of cvar named by value of cvar "abc"
	int ref_count = 0;

	// Support extra shortcuts for some commonly used symbols, so config files don't have
	//   to worry about them being defined in cvars
	if(!Q_stricmp(value, "&space")) return " ";
	if(!Q_stricmp(value, "&semi")) return ";";
	if(!Q_stricmp(value, "&asterisk")) return "*";
	if(!Q_stricmp(value, "&null")) return "";

	while(*value == '*') {
		++ref_count;
		++value; }

	while(ref_count) {
		value = Cvar_VariableString(value);
		--ref_count; }

	return value; }

static void cmd_if(void) {
	// Execute trailing command conditionally depending on result of if statement
	const char *operation = Cmd_Argv(2);
	int arg_start = 4;
	qboolean result = qfalse;

	// Check if space-separated list contains term
	// Example: if "abc def" contains_term abc vstr something
	if(!Q_stricmp(operation, "contains_term") || !Q_stricmp(operation, "!contains_term")) {
		const char *source_terms = cvar_dereference(Cmd_Argv(1));
		const char *test_term = cvar_dereference(Cmd_Argv(3));
		char buffer[256];
		while(cmod_read_token_ws(&source_terms, buffer, sizeof(buffer))) {
			if(!Q_stricmp(test_term, buffer)) {
				result = qtrue;
				break; } } }

	// Check if string contains substring
	// Example: if "test" contains_str "es" vstr something
	else if(!Q_stricmp(operation, "contains_str") || !Q_stricmp(operation, "!contains_str")) {
		if(Q_stristr(cvar_dereference(Cmd_Argv(1)), cvar_dereference(Cmd_Argv(3)))) {
			result = qtrue; } }

	// Case-insensitive string comparison (example: if *somecvar s= abc vstr something)
	else if(!Q_stricmp(operation, "s=") || !Q_stricmp(operation, "s!=")) {
		result = !Q_stricmp(cvar_dereference(Cmd_Argv(1)), cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Case-sensitive "exact" string comparison (example: if *somecvar e= abc vstr something)
	else if(!Q_stricmp(operation, "e=") || !Q_stricmp(operation, "e!=")) {
		result = !strcmp(cvar_dereference(Cmd_Argv(1)), cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Boolean comparison (example: if *somecvar b= true vstr something)
	else if(!Q_stricmp(operation, "b=") || !Q_stricmp(operation, "b!=")) {
		result = cmd_str_to_bool(cvar_dereference(Cmd_Argv(1))) ==
				cmd_str_to_bool(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Integer comparison (example: if *somecvar i!= 10 vstr something)
	else if(!Q_stricmp(operation, "i=") || !Q_stricmp(operation, "i!=")) {
		result = atoi(cvar_dereference(Cmd_Argv(1))) == atoi(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i>") || !Q_stricmp(operation, "i<=")) {
		result = atoi(cvar_dereference(Cmd_Argv(1))) > atoi(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i>=") || !Q_stricmp(operation, "i<")) {
		result = atoi(cvar_dereference(Cmd_Argv(1))) >= atoi(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }

	// Float comparison (example: if *somecvar f> 0.5 vstr something)
	else if(!Q_stricmp(operation, "f=") || !Q_stricmp(operation, "f!=")) {
		result = atof(cvar_dereference(Cmd_Argv(1))) == atof(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f>") || !Q_stricmp(operation, "f<=")) {
		result = atof(cvar_dereference(Cmd_Argv(1))) > atof(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f>=") || !Q_stricmp(operation, "f<")) {
		result = atof(cvar_dereference(Cmd_Argv(1))) >= atof(cvar_dereference(Cmd_Argv(3))) ? qtrue : qfalse; }
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

static void cmd_setx(void) {
	// Concatenates one or more strings or cvar references into target cvar (with no separators)
	// Example: setx target *src1 &space some text &space *src2
	// If cvar "src1" = "abc" and cvar "src2" = "def", cvar "target" is set to "abc sometext def"
	int count = Cmd_Argc();
	if(count < 3) {
		Com_Printf("Usage: setx <target> <source1> ...\n"); }
	else {
		int i;
		char buffer[65536];
		cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};

		for(i=2; i<count; ++i) {
			cmod_stream_append_string(&stream, cvar_dereference(Cmd_Argv(i))); }

		CVAR_SET(cvar_dereference(Cmd_Argv(1)), stream.data); } }

static void cmd_add(void) {
	// Increments value in target cvar by given amount
	// Example: add target 0.5
	if(Cmd_Argc() < 3) {
		Com_Printf("Usage: add <target> <amount>\n"); }
	else {
		const char *target_cvar = cvar_dereference(Cmd_Argv(1));
		float target_value = Cvar_VariableValue(target_cvar) + atof(cvar_dereference(Cmd_Argv(2)));
		CVAR_SET(target_cvar, va("%g", target_value)); } }

static void cmd_mult(void) {
	// Multiplies value in target cvar by given amount
	// Example: mult target 0.5
	if(Cmd_Argc() < 3) {
		Com_Printf("Usage: mult <target> <amount>\n"); }
	else {
		const char *target_cvar = cvar_dereference(Cmd_Argv(1));
		float target_value = Cvar_VariableValue(target_cvar) * atof(cvar_dereference(Cmd_Argv(2)));
		CVAR_SET(target_cvar, va("%g", target_value)); } }

static void cmd_rand(void) {
	// Sets target cvar to a random integer value in the given range (inclusive)
	// Example: rand target 1 3
	if(Cmd_Argc() < 4) {
		Com_Printf("Usage: rand <target> <start value> <end value>\n"); }
	else {
		int start = atoi(cvar_dereference(Cmd_Argv(2)));
		int end = atoi(cvar_dereference(Cmd_Argv(3)));
		int range = end - start;
		int result;
		if(range < 0) {
			Com_Printf("rand: Invalid range\n");
			return; }
		result = rand() % (range + 1) + start;
		CVAR_SET(cvar_dereference(Cmd_Argv(1)), va("%i", result)); } }

static void cmd_randf(void) {
	// Sets target cvar to a random floating point value in the given range
	// Example: randf target 1 3
	if(Cmd_Argc() < 4) {
		Com_Printf("Usage: randf <target> <start value> <end value>\n"); }
	else {
		float start = atof(cvar_dereference(Cmd_Argv(2)));
		float end = atof(cvar_dereference(Cmd_Argv(3)));
		float range = end - start;
		float result;
		if(range < 0.0) {
			Com_Printf("randf: Invalid range\n");
			return; }
		result = ((float)rand() / (float)(RAND_MAX)) * range + start;
		CVAR_SET(cvar_dereference(Cmd_Argv(1)), va("%g", result)); } }

static void cmd_split_token(void) {
	// Removes leading token from source cvar and places it in target cvar
	// Target will be set to empty if no tokens remain
	if(Cmd_Argc() < 3) {
		Com_Printf("Usage: split_token <source_cvar> <target_cvar> <optional delimiter char>\n"); }
	else {
		const char *source = Cmd_Argv(1);
		const char *target = Cmd_Argv(2);
		char delimiter = *Cmd_Argv(3);	// Can be null to use whitespace delimiter (see cmod_read_token)

		const char *string_ptr = Cvar_VariableString(source);
		char token_buffer[512];

		cmod_read_token(&string_ptr, token_buffer, sizeof(token_buffer), delimiter);
		CVAR_SET(source, string_ptr);
		CVAR_SET(target, token_buffer); } }

static void cmd_split_character(void) {
	// Removes leading character from source cvar and places it in target cvar
	// Target will be set to empty if no characters remain
	if(Cmd_Argc() < 3) {
		Com_Printf("Usage: split_character <source_cvar> <target_cvar>\n"); }
	else {
		const char *source = Cmd_Argv(1);
		const char *target = Cmd_Argv(2);
		char *string_ptr = Cvar_VariableString(source);
		char char_string[2] = {*string_ptr, 0};

		CVAR_SET(source, *string_ptr ? string_ptr + 1 : "");
		CVAR_SET(target, char_string); } }

static void cmd_check_file_exists(void) {
	if(Cmd_Argc() < 3) {
		Com_Printf("Usage: check_file_exists <path> <target_cvar>\n"); }
	else {
		const char *source = cvar_dereference(Cmd_Argv(1));
		const char *result = (*source && FS_ReadFile(source, NULL) > 0) ? "true" : "false";
		CVAR_SET(Cmd_Argv(2), result); } }

void cmod_sv_cmd_tools_init(void) {
	Cmd_AddCommand("if", cmd_if);
	Cmd_AddCommand("setx", cmd_setx);
	Cmd_AddCommand("add", cmd_add);
	Cmd_AddCommand("mult", cmd_mult);
	Cmd_AddCommand("rand", cmd_rand);
	Cmd_AddCommand("randf", cmd_randf);
	Cmd_AddCommand("split_token", cmd_split_token);
	Cmd_AddCommand("split_character", cmd_split_character);
	Cmd_AddCommand("check_file_exists", cmd_check_file_exists); }

#endif
