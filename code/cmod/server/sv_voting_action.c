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
#include <setjmp.h>

#define VOTE_MAX_OPTIONS 128
#define VOTE_MAX_CONFIG_ENTRIES 512
#define VOTE_OPTION_NAME_LENGTH 128
#define VOTE_PARAMETER_LENGTH 128

/* ******************************************************************************** */
// Misc support functions
/* ******************************************************************************** */

static qboolean vote_str_to_bool(const char *value) {
	if(!Q_stricmp(value, "true")) return qtrue;
	if(!Q_stricmp(value, "yes")) return qtrue;
	if(!Q_stricmp(value, "on")) return qtrue;
	if(!Q_stricmp(value, "enable")) return qtrue;
	if(!Q_stricmp(value, "enabled")) return qtrue;
	if(atoi(value)) return qtrue;
	return qfalse; }

static qboolean vote_string_in_list(const char *list, const char *string) {
	// Returns qtrue if string is found in space-separated list, qfalse otherwise
	char buffer[256];
	while(cmod_read_token_ws(&list, buffer, sizeof(buffer))) {
		if(!Q_stricmp(buffer, string)) return qtrue; }
	return qfalse; }

static qboolean vote_list_overlap(const char *list1, const char *list2) {
	// Returns qtrue if space-separated lists contain common item, qfalse otherwise
	char buffer[256];
	while(cmod_read_token_ws(&list1, buffer, sizeof(buffer))) {
		if(vote_string_in_list(list2, buffer)) return qtrue; }
	return qfalse; }

static qboolean vote_verify_numeral(const char *string, qboolean allow_decimal, qboolean allow_negative) {
	// Verify input string is a valid decimal format number to help catch vote command syntax errors
	if(*string == '-' && allow_negative) ++string;
	if(!*string) return qfalse;
	while(*string) {
		if(*string == '.' && allow_decimal) return vote_verify_numeral(string+1, qfalse, qfalse);
		if(!(*string >= '0' && *string <= '9')) return qfalse;
		++string; }
	return qtrue; }

static void vote_filter_chars(const char *input, const char *filter, qboolean inclusive, char *output,
		unsigned int output_length) {
	// Inclusive mode - copies input to output, with only characters in filter included
	// Exclusive mode - copies input to output, with characters in filter excluded
	char filter_set[256];
	Com_Memset(filter_set, inclusive ? 0 : 1, sizeof(filter_set));
	while(*filter) {
		filter_set[*(unsigned char *)filter] = inclusive ? 1 : 0;
		++filter; }
	while(*input) {
		if(output_length > 1 && filter_set[*(unsigned char *)input]) {
			*(output++) = *input;
			--output_length; }
		++input; }
	*output = 0; }

// ***** Vote option iterator *****

typedef struct {
	const char *feed;
	char option_name[VOTE_OPTION_NAME_LENGTH];
	int option_index;
} vote_option_iterator_t;

static vote_option_iterator_t get_vote_option_iterator(void) {
	vote_option_iterator_t it;
	Com_Memset(&it, 0, sizeof(it));
	it.feed = cmod_sv_voting_option_list->string;
	it.option_index = -1;
	return it; }

static qboolean advance_vote_option_iterator(vote_option_iterator_t *it) {
	// Returns qtrue if valid option retrieved, qfalse otherwise
	if(it->option_index < VOTE_MAX_OPTIONS &&
			cmod_read_token_ws(&it->feed, it->option_name, sizeof(it->option_name))) {
		++it->option_index;
		return qtrue; }
	else {
		return qfalse; } }

/* ******************************************************************************** */
// Error handling
/* ******************************************************************************** */

static client_t *vote_abort_current_client;
static jmp_buf vote_abort_jump_buf;

static void execute_vote_abort_jump(const char *msg) {
	if(!vote_abort_current_client) {
		// Assume jump configuration is broken and error is called from the wrong place
		Com_Error(ERR_FATAL, "vote error with vote_abort_current_client not set: %s", msg); }
	longjmp(vote_abort_jump_buf, 1); }

static void vote_unexpected_error(const char *msg) {
	// Print generic message to client and abort vote
	// For problems with server configuration, not normal errors with the client command
	if(vote_abort_current_client) {
		SV_SendServerCommand(vote_abort_current_client, "print \"An error occurred processing the vote command.\n\""); }
	VOTE_LOG("!WARNING: vote_unexpected_error: %s", msg);
	execute_vote_abort_jump(msg); }

static void vote_standard_error(const char *msg) {
	// Prints message to client and aborts vote
	if(vote_abort_current_client) {
		SV_SendServerCommand(vote_abort_current_client, "print \"%s\n\"", msg); }
	VOTE_LOG("vote_standard_error: %s", msg);
	execute_vote_abort_jump(msg); }

/* ******************************************************************************** */
// Vote process definition and support functions
/* ******************************************************************************** */

typedef struct {
	char *key;
	char *value;
} vote_config_entry_t;

typedef struct {
	// Vote configuration values
	// vote_pass_command - command to be executed on the server if the vote passes
	// vote_info_string - sent to clients to be displayed on the screen while the vote is in progress
	// option1_cmdname - (example) user-supplied command that enabled vote option 1
	// option1_parameter1 - (example) user-supplied first parameter string for vote option 1
	unsigned int config_entry_count;
	vote_config_entry_t config_entries[VOTE_MAX_CONFIG_ENTRIES];

	// Active vote options
	qboolean vote_options_active[VOTE_MAX_OPTIONS];
} vote_process_t;

static vote_config_entry_t *vote_get_config_entry(vote_process_t *process, const char *key) {
	int i;
	for(i=0; i<process->config_entry_count; ++i) {
		if(!Q_stricmp(process->config_entries[i].key, key)) return &process->config_entries[i]; }
	return 0; }

static void vote_setconfig(vote_process_t *process, const char *key, const char *value, const char *debug_context) {
	vote_config_entry_t *config_entry = vote_get_config_entry(process, key);
	if(config_entry) {
		// Updating existing entry
		Z_Free(config_entry->value); }
	else {
		// Create new entry
		if(process->config_entry_count >= VOTE_MAX_CONFIG_ENTRIES) vote_unexpected_error("config key overflow");
		config_entry = &process->config_entries[process->config_entry_count++];
		config_entry->key = CopyString(key); }

	config_entry->value = CopyString(value);
	if(cmod_sv_voting_debug->integer) {
		VOTE_LOG("> %s: setting config key '%s' to '%s'", debug_context, key, value); } }

static const char *vote_getconfig(vote_process_t *process, const char *key) {
	vote_config_entry_t *config_key = vote_get_config_entry(process, key);
	if(config_key) return config_key->value;
	return ""; }

static void vote_process_free(vote_process_t *process) {
	// Free memory allocations in vote process structure
	int i;
	for(i=0; i<process->config_entry_count; ++i) {
		Z_Free(process->config_entries[i].key);
		Z_Free(process->config_entries[i].value); } }

/* ******************************************************************************** */
// Vote Finalization
/* ******************************************************************************** */

static void vote_finalize(vote_process_t *process, vote_action_t *action_output) {
	// Generate vote action output
	if(!action_output) vote_unexpected_error("vote_finalize: null action_output");

	const char *pass_command = vote_getconfig(process, "vote_pass_command");
	if(!*pass_command) vote_unexpected_error("vote_finalize: no pass command set");
	if(strlen(pass_command) >= sizeof(action_output->pass_command)) {
		vote_unexpected_error("vote_finalize: pass command overflowed"); }
	Q_strncpyz(action_output->pass_command, pass_command, sizeof(action_output->pass_command));
	VOTE_LOG("Pass command set to '%s'", pass_command);

	const char *info_string = vote_getconfig(process, "vote_info_string");
	if(!*info_string) vote_unexpected_error("vote_finalize: no info string set");
	if(strlen(info_string) >= sizeof(action_output->info_string)) {
		vote_unexpected_error("vote_finalize: info string overflowed"); }
	Q_strncpyz(action_output->info_string, info_string, sizeof(action_output->info_string));
	VOTE_LOG("Info string set to '%s'", info_string); }

/* ******************************************************************************** */
// Server command processing
/* ******************************************************************************** */

static void get_option_for_tag(vote_process_t *process, const char *tag, char *output, int output_length) {
	// Writes the name of a single active option that matches given tag, or null if no match
	// If multiple options match the given tag only one will be written
	vote_option_iterator_t it = get_vote_option_iterator();
	while(advance_vote_option_iterator(&it)) {
		if(process->vote_options_active[it.option_index]) {
			const char *tags = Cvar_VariableString(va("voteoption_%s_tags", it.option_name));
			if(vote_string_in_list(tags, tag)) {
				Q_strncpyz(output, it.option_name, output_length);
				return; } } }
	Q_strncpyz(output, "", output_length); }

static void vote_process_value(vote_process_t *process, const char **tokens, char *output, unsigned int output_size,
			qboolean sequence, const char *debug_context) {
	// Reads a token value from input and writes to output, processing special commands as needed
	qboolean skip_delimiter = qfalse;
	char delimiter[32] = "$condspace";
	char buffer[8192];
	const char *output_string = "";
	cmod_stream_t output_stream = {output, 0, output_size, qfalse};
	*output = 0;

	while(1) {
		cmod_read_token_ws(tokens, buffer, sizeof(buffer));
		if(!*buffer) break;

		if(*buffer == '&') {
			// Handle special commands
			if(!Q_stricmp(buffer, "&<")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qtrue, debug_context);
				output_string = buffer; }
			else if(!Q_stricmp(buffer, "&>")) {
				break; }
			else if(!Q_stricmp(buffer, "&_")) {
				skip_delimiter = qtrue;
				continue; }
			else if(!Q_stricmp(buffer, "&sep")) {
				vote_process_value(process, tokens, delimiter, sizeof(delimiter), qfalse, debug_context);
				continue; }
			else if(!Q_stricmp(buffer, "&null")) {
				output_string = ""; }
			else if(!Q_stricmp(buffer, "&space")) {
				output_string = " "; }
			else if(!Q_stricmp(buffer, "&semi")) {
				output_string = ";"; }
			else if(!Q_stricmp(buffer, "&newline")) {
				output_string = "\n"; }
			else if(!Q_stricmp(buffer, "&cvar")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = Cvar_VariableString(buffer); }
			else if(!Q_stricmp(buffer, "&cfg")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = vote_getconfig(process, buffer); }
			else if(!Q_stricmp(buffer, "&lowercase")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = Q_strlwr(buffer); }
			else if(!Q_stricmp(buffer, "&uppercase")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = Q_strupr(buffer); }
			else if(!Q_stricmp(buffer, "&filterchars") || !Q_stricmp(buffer, "&subtractchars")) {
				qboolean inclusive = !Q_stricmp(buffer, "&filterchars") ? qtrue : qfalse;
				char source[8192];
				char filter[1024];
				vote_process_value(process, tokens, source, sizeof(source), qfalse, debug_context);
				vote_process_value(process, tokens, filter, sizeof(filter), qfalse, debug_context);
				vote_filter_chars(source, filter, inclusive, buffer, sizeof(buffer));
				output_string = buffer; }
			else if(!Q_stricmp(buffer, "&firstchar")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				buffer[1] = 0;
				output_string = buffer; }
			else if(!Q_stricmp(buffer, "&listcontains")) {
				char term[1024];
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				vote_process_value(process, tokens, term, sizeof(term), qfalse, debug_context);
				output_string = vote_string_in_list(buffer, term) ? "true" : "false"; }
			else if(!Q_stricmp(buffer, "&strlen")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = va("%u", (unsigned int)strlen(buffer)); }
			else if(!Q_stricmp(buffer, "&validnum")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = vote_verify_numeral(buffer, qtrue, qtrue) ? "true" : "false"; }
			else if(!Q_stricmp(buffer, "&roundinterval")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				float interval = atof(buffer);
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				float value = atof(buffer);
				if(interval > 0.0f) value = floor(value / interval) * interval;
				output_string = va("%g", value); }
			else if(!Q_stricmp(buffer, "&tagactive")) {
				char tagname[1024];
				vote_process_value(process, tokens, tagname, sizeof(tagname), qfalse, debug_context);
				get_option_for_tag(process, tagname, buffer, sizeof(buffer));
				output_string = *buffer ? "true" : "false"; }
			else if(!Q_stricmp(buffer, "&findtag")) {
				char tagname[1024];
				vote_process_value(process, tokens, tagname, sizeof(tagname), qfalse, debug_context);
				get_option_for_tag(process, tagname, buffer, sizeof(buffer));
				output_string = buffer; }
			else if(!Q_stricmp(buffer, "&fileexists")) {
				vote_process_value(process, tokens, buffer, sizeof(buffer), qfalse, debug_context);
				output_string = (*buffer && FS_ReadFile(buffer, NULL) > 0) ? "true" : "false"; }
			else vote_unexpected_error(va("%s: unknown value specifier '%s'", debug_context, buffer)); }
		else if(*buffer == '#') {
			output_string = buffer + 1; }
		else {
			// Regular string
			output_string = buffer; }

		// Write delimiter
		if(!Q_stricmp(delimiter, "$condspace")) {
			// Write space delimiter only if it won't precede/follow a newline or another space
			if(output_stream.position && output[output_stream.position-1] != ' ' && output[output_stream.position-1] != '\n'
					&& *output_string && *output_string != ' ' && *output_string != '\n' && !skip_delimiter) {
				cmod_stream_append_string(&output_stream, " "); } }
		else {
			if(output_stream.position && *output_string && !skip_delimiter) {
				cmod_stream_append_string(&output_stream, delimiter); } }
		skip_delimiter = qfalse;

		// Write string
		cmod_stream_append_string(&output_stream, output_string);

		// Stop after first token unless in sequence mode
		if(!sequence) break; }

	if(output_stream.overflowed) vote_unexpected_error(va("%s: value stream overflow", debug_context)); }

static qboolean vote_process_if_command(vote_process_t *process, const char **tokens, const char *debug_context) {
	char value1[8192];
	char operation[16];
	char value2[8192];
	qboolean result;

	vote_process_value(process, tokens, value1, sizeof(value1), qfalse, debug_context);
	cmod_read_token_ws(tokens, operation, sizeof(operation));
	vote_process_value(process, tokens, value2, sizeof(value2), qfalse, debug_context);

	if(!Q_stricmp(operation, "b=") || !Q_stricmp(operation, "b!=")) {
		result = vote_str_to_bool(value1) == vote_str_to_bool(value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "s=") || !Q_stricmp(operation, "s!=")) {
		result = !Q_stricmp(value1, value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "e=") || !Q_stricmp(operation, "e!=")) {
		result = !strcmp(value1, value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i=") || !Q_stricmp(operation, "i!=")) {
		result = atoi(value1) == atoi(value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i>") || !Q_stricmp(operation, "i<=")) {
		result = atoi(value1) > atoi(value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "i>=") || !Q_stricmp(operation, "i<")) {
		result = atoi(value1) >= atoi(value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f=") || !Q_stricmp(operation, "f!=")) {
		result = atof(value1) == atof(value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f>") || !Q_stricmp(operation, "f<=")) {
		result = atof(value1) > atof(value2) ? qtrue : qfalse; }
	else if(!Q_stricmp(operation, "f>=") || !Q_stricmp(operation, "f<")) {
		result = atof(value1) >= atof(value2) ? qtrue : qfalse; }
	else {
		vote_unexpected_error(va("%s: unknown if comparison operation '%s'", debug_context, operation)); }

	if(strchr(operation, '!') || strchr(operation, '<')) result = result ? qfalse : qtrue;
	return result; }

static void vote_process_server_command(vote_process_t *process, const char *command, const char *debug_context) {
	char buffer[8192];
	int runcmd_call_number = 0;		// For tracking runcmd execution path in debug prints

	while(1) {
		cmod_read_token_ws(&command, buffer, sizeof(buffer));
		if(!*buffer) return;

		if(!Q_stricmp(buffer, "if")) {
			// Usage: if <value> <comparison operation> <value> <... full-line command string>
			if(!vote_process_if_command(process, &command, debug_context)) return; }

		else if(!Q_stricmp(buffer, "error")) {
			// Usage: error <error string>
			vote_process_value(process, &command, buffer, sizeof(buffer), qfalse, debug_context);
			if(!*buffer) vote_unexpected_error(va("%s: invalid 'error' message", debug_context));
			if(cmod_sv_voting_debug->integer) {
				VOTE_LOG("> %s: processing error command", debug_context); }
			vote_standard_error(buffer); }

		else if(!Q_stricmp(buffer, "exception")) {
			// Usage: exception <unexpected error string>
			vote_process_value(process, &command, buffer, sizeof(buffer), qfalse, debug_context);
			if(!*buffer) vote_unexpected_error(va("%s: invalid 'exception' message", debug_context));
			if(cmod_sv_voting_debug->integer) {
				VOTE_LOG("> %s: processing exception command", debug_context); }
			vote_unexpected_error(buffer); }

		else if(!Q_stricmp(buffer, "runcmd")) {
			// Usage: runcmd <command string>
			char new_debug_context[256];
			if(strlen(debug_context) > 200) {
				// Assume too many runcmds are stacked; abort now before stack overflow crash
				vote_unexpected_error(va("%s: runcmd recursive overflow", debug_context)); }
			vote_process_value(process, &command, buffer, sizeof(buffer), qfalse, debug_context);

			++runcmd_call_number;
			Com_sprintf(new_debug_context, sizeof(new_debug_context), "%s-runcmd%i", debug_context, runcmd_call_number);
			vote_process_server_command(process, buffer, new_debug_context); }

		else if(!Q_stricmp(buffer, "runcmdseq")) {
			// Usage: runcmd <base cvar name> <cmd count>
			int i;
			char cvar_base[256];
			char new_debug_context[256];
			if(strlen(debug_context) > 200) {
				// Assume too many runcmds are stacked; abort now before stack overflow crash
				vote_unexpected_error(va("%s: runcmdseq recursive overflow", debug_context)); }
			vote_process_value(process, &command, cvar_base, sizeof(cvar_base), qfalse, debug_context);
			if(!*cvar_base) vote_unexpected_error(va("%s: runcmdseq invalid cvar base", debug_context));
			vote_process_value(process, &command, buffer, sizeof(buffer), qfalse, debug_context);
			int count = atoi(buffer);
			if(count < 1 || count > 256) vote_unexpected_error(va("%s: runcmdseq invalid count", debug_context));

			++runcmd_call_number;
			for(i=1; i<=count; ++i) {
				Com_sprintf(new_debug_context, sizeof(new_debug_context), "%s-runcmd%i.%i", debug_context, runcmd_call_number, i);
				Q_strncpyz(buffer, Cvar_VariableString(va("%s%i", cvar_base, i)), sizeof(buffer));
				vote_process_server_command(process, buffer, new_debug_context); } }

		else if(!Q_stricmp(buffer, "setcfg")) {
			// Usage: setcfg <target cfg key> <value>
			char key[256];
			vote_process_value(process, &command, key, sizeof(key), qfalse, debug_context);
			if(!*key) vote_unexpected_error(va("%s: missing 'setcfg' config name", debug_context));
			vote_process_value(process, &command, buffer, sizeof(buffer), qfalse, debug_context);
			vote_setconfig(process, key, buffer, debug_context); }

		else if(!Q_stricmp(buffer, "appendcfg")) {
			// Usage: appendcfg <target cfg key> <delimiter> <value>
			char key[256];
			char delimiter[32];
			vote_process_value(process, &command, key, sizeof(key), qfalse, debug_context);
			if(!*key) vote_unexpected_error(va("%s: missing 'appendcfg' config name", debug_context));
			const char *existing_value = vote_getconfig(process, key);
			vote_process_value(process, &command, delimiter, sizeof(delimiter), qfalse, debug_context);
			vote_process_value(process, &command, buffer, sizeof(buffer), qfalse, debug_context);
			const char *delimiter_active = *existing_value && *buffer ? delimiter : "";
			vote_setconfig(process, key, va("%s%s%s", existing_value, delimiter_active, buffer), debug_context); }

		else vote_unexpected_error(va("%s: invalid command '%s'", debug_context, buffer)); } }

static void vote_process_server_commands(vote_process_t *process) {
	char debug_context[32];
	vote_process_server_command(process, cmod_sv_voting_preoption_script->string, "preoption.script");

	vote_option_iterator_t it = get_vote_option_iterator();
	while(advance_vote_option_iterator(&it)) {
		if(process->vote_options_active[it.option_index]) {
			char *command = Cvar_VariableString(va("voteoption_%s_cmd", it.option_name));
			if(*command) {
				Com_sprintf(debug_context, sizeof(debug_context), "option[%s].script", it.option_name);
				vote_process_server_command(process, command, debug_context); } } }

	vote_process_server_command(process, cmod_sv_voting_postoption_script->string, "postoption.script"); }

/* ******************************************************************************** */
// Vote command processing
/* ******************************************************************************** */

static void vote_check_nocombo_tags(vote_process_t *process) {
	// Make sure no two active vote options have conflicting nocombo tags
	vote_option_iterator_t it1 = get_vote_option_iterator();
	while(advance_vote_option_iterator(&it1)) {
		if(!process->vote_options_active[it1.option_index]) continue;
		vote_option_iterator_t it2 = get_vote_option_iterator();
		while(advance_vote_option_iterator(&it2)) {
			if(!process->vote_options_active[it2.option_index]) continue;
			if(it1.option_index == it2.option_index) continue;

			const char *option1_tags = Cvar_VariableString(va("voteoption_%s_tags", it1.option_name));
			const char *option2_nocombo_tags = Cvar_VariableString(va("voteoption_%s_nocombo_tags", it2.option_name));
			if(vote_list_overlap(option1_tags, option2_nocombo_tags)) {
				vote_standard_error(va("Can't combine commands: %s, %s",
						vote_getconfig(process, va("option_%s_cmdname", it1.option_name)),
						vote_getconfig(process, va("option_%s_cmdname", it2.option_name)))); } } } }

static int process_user_command(vote_process_t *process, const char *cmd, int arg_position) {
	// Returns number of additional arguments used by command
	vote_option_iterator_t it = get_vote_option_iterator();
	while(advance_vote_option_iterator(&it)) {
		if(!vote_str_to_bool(Cvar_VariableString(va("voteoption_%s_enabled", it.option_name)))) continue;
		const char *cmdnames = Cvar_VariableString(va("voteoption_%s_cmdnames", it.option_name));
		if(!vote_string_in_list(cmdnames, cmd)) continue;
		const char *type = Cvar_VariableString(va("voteoption_%s_type", it.option_name));

		if(!*type || !Q_stricmp(type, "general")) {
			// Can't have two commands match the same option
			if(process->vote_options_active[it.option_index]) {
				vote_standard_error(va("Can't combine commands: %s, %s", cmd,
						vote_getconfig(process, va("option_%s_cmdname", it.option_name)))); }

			process->vote_options_active[it.option_index] = qtrue;
			vote_setconfig(process, va("option_%s_cmdname", it.option_name), cmd, va("option[%s].usercmd", it.option_name));

			int parameter;
			int parameter_count = Cvar_VariableIntegerValue(va("voteoption_%s_parameter_count", it.option_name));
			if(parameter_count < 0 || parameter_count > 10) {
				vote_unexpected_error(va("vote option '%s': invalid parameter count\n", it.option_name)); }
			for(parameter=1; parameter<=parameter_count; ++parameter) {
				char buffer[VOTE_PARAMETER_LENGTH];
				const char *arg = Cmd_Argv(arg_position + parameter);
				Q_strncpyz(buffer, arg, sizeof(buffer));
				vote_setconfig(process, va("option_%s_parameter%i", it.option_name, parameter), buffer,
						va("option[%s].usercmd", it.option_name)); }

			return parameter_count; }

		else {
			vote_unexpected_error(va("vote option '%s': unrecognized option type '%s'\n", it.option_name, type)); } }

	vote_standard_error(va("Invalid vote command: %s", cmd));
	return 0; }

static void vote_process_user_commands(vote_process_t *process) {
	int arg_position = 1;
	while(1) {
		const char *arg = Cmd_Argv(arg_position);
		if(!*arg) break;
		arg_position += process_user_command(process, arg, arg_position) + 1; } }

/* ******************************************************************************** */
// Interface Functions
/* ******************************************************************************** */

qboolean voteaction_process_callvote(client_t *client, qboolean vote_in_progress, vote_action_t *action_output) {
	// Returns qtrue if action output generated successfully, qfalse otherwise
	// If vote_in_progress set, action output won't be generated, but errors/instructions can still be printed
	vote_process_t process;

	// Set up error jump handler
	vote_abort_current_client = client;
	if(setjmp(vote_abort_jump_buf)) {
		vote_process_free(&process);
		vote_abort_current_client = 0;
		return qfalse; }

	// Process vote
	Com_Memset(&process, 0, sizeof(process));
	vote_setconfig(&process, "vote_in_progress", vote_in_progress ? "true" : "false", "init");
	vote_setconfig(&process, "vote_arg_count", va("%i", Cmd_Argc()-1), "init");
	vote_process_user_commands(&process);
	vote_check_nocombo_tags(&process);
	vote_process_server_commands(&process);
	if(vote_in_progress) vote_standard_error("Vote already in progress.");
	vote_finalize(&process, action_output);

	vote_process_free(&process);
	vote_abort_current_client = 0;
	return qtrue; }

#endif
