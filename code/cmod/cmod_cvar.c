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

#ifdef CMOD_CVAR_HANDLING
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

int cvar_modifiedFlags;		// Publicly shared

/* ******************************************************************************** */
// Definitions
/* ******************************************************************************** */

#define CVAR_CREATED_FLAGS (CVAR_USER_CREATED|CVAR_VM_CREATED|CVAR_IMPORT_CREATED|CVAR_SERVER_CREATED)

typedef struct localCvar_s {
	// Components shared with the rest of the game, and defined in q_shared.h
	cvar_t s;

	// System values - set by system code (e.g. Cvar_Get), represent system defaults
	char *system_default;
	int system_flags;

	// Main values - set by console commands and system code (e.g. Cvar_Set)
	char *main_value;
	int main_flags;

	// Protected values - set by VM, systeminfo, and protected console commands
	// Cleared when session ends, unless CVAR_PROTECTED_ARCHIVABLE is present
	char *protected_default;
	char *protected_value;
	int protected_flags;

	// Validations
	qboolean validate;
	qboolean integral;
	float min;
	float max;

	// Misc
	int vm_handle;	// For sharing with VMs (the int gets casted to a vm_handle_index_t)
	int category;	// To make settings file more readable
	char *description;

	// Iteration
	struct localCvar_s *next;
	struct localCvar_s *prev;

	// Hashtable
	struct localCvar_s *hash_next;
	struct localCvar_s *hash_prev;
	int hash;
} localCvar_t;

/* ******************************************************************************** */
// Misc support functions
/* ******************************************************************************** */

static int cvar_hash_value( const char *name ) {
	int i;
	int hash;
	char letter;

	hash = 0;
	i = 0;
	while (name[i] != '\0') {
		letter = tolower(name[i]);
		hash+=(long)(letter)*(i+119);
		i++; }

	return hash; }

typedef struct {
	char *data;
	int position;
	int size;
} cvar_stream_t;

void cvar_stream_append_string(cvar_stream_t *stream, const char *string) {
	// If stream runs out of space, output is truncated.
	// Stream data will always be null terminated.
	while(*string && stream->position < stream->size-1) {
		stream->data[stream->position++] = *(string++); }
	if(stream->position < stream->size) stream->data[stream->position] = 0; }

// Some stream macros
#define ADD_TEXT(string) cvar_stream_append_string(&stream, string)
#define ADD_TEXT2(string) cvar_stream_append_string(stream, string)

/* ******************************************************************************** */
// Memory management
/* ******************************************************************************** */

static const char cvar_mstatic[][2] = { {0,0}, {'0',0}, {'1',0} };
//int alloc_count = 0;

static void *cvar_malloc(int size) {
	int *countptr = S_Malloc(size + sizeof(int));
	*countptr = 1;
	//++alloc_count;
	return countptr + 1; }

static void cvar_mfree(void *data) {
	int *countptr;
	if(!data || ((char *)data >= *cvar_mstatic && (char *)data < *cvar_mstatic + sizeof(cvar_mstatic))) return;
	countptr = (int *)data - 1;
	if(--*countptr <= 0) {
		//--alloc_count;
		Z_Free(countptr); } }

static void cvar_mrealloc(void *data) {
	int *countptr;
	if(!data || ((char *)data >= *cvar_mstatic && (char *)data < *cvar_mstatic + sizeof(cvar_mstatic))) return;
	countptr = (int *)data - 1;
	++*countptr; }

static void cvar_copystring(const char *source, char **target) {
	int len = strlen(source);
	cvar_mfree(*target);
	if(len == 0) *target = (char *)cvar_mstatic[0];
	else if(len == 1 && source[0] == '0') *target = (char *)cvar_mstatic[1];
	else if(len == 1 && source[0] == '1') *target = (char *)cvar_mstatic[2];
	else {
		*target = cvar_malloc(len + 1);
		memcpy(*target, source, len);
		(*target)[len] = 0; } }

static void cvar_linkstring(char *source, char **target) {
	cvar_mfree(*target);
	cvar_mrealloc(source);
	*target = source; }

static void cvar_clearstring(char **target) {
	cvar_mfree(*target);
	*target = 0; }

/* ******************************************************************************** */
// Validation functions
/* ******************************************************************************** */

static qboolean cvar_valid_name(const char *s) {
	if(!s || !*s) return qfalse;
	if(strlen(s) > 1000) return qfalse;
	if(strchr(s, '\"')) return qfalse;
	if(strchr(s, '\\')) return qfalse;
	if(strchr(s, ';')) return qfalse;
	if(strchr(s, '\n')) return qfalse;
	if(strchr(s, '\r')) return qfalse;
	return qtrue; }

static void cvar_check_range(localCvar_t *cvar, char **string, qboolean warn) {
	qboolean changed = qfalse;
	float valuef;

	if(!cvar->validate) return;
	valuef = atof(*string);

	if(!Q_isanumber(*string)) {
		if(warn) Com_Printf( "WARNING: cvar '%s' must be numeric\n", cvar->s.name );
		if(cvar->s.resetString) valuef = atof(cvar->s.resetString);
		else valuef = 0;
		changed = qtrue; }

	if(valuef < cvar->min) {
		if(warn) Com_Printf( "WARNING: cvar '%s' out of range (min %f)\n", cvar->s.name, cvar->min );
		valuef = cvar->min;
		changed = qtrue; }

	if(valuef > cvar->max) {
		if(warn) Com_Printf( "WARNING: cvar '%s' out of range (max %f)\n", cvar->s.name, cvar->max );
		valuef = cvar->max;
		changed = qtrue; }

	if(cvar->integral && !Q_isintegral(valuef)) {
		if(warn) Com_Printf( "WARNING: cvar '%s' must be integral\n", cvar->s.name );
		valuef = (int)valuef;
		changed = qtrue; }

	if(changed) {
		char temp[20];
		if(Q_isintegral(valuef)) Com_sprintf(temp, sizeof(temp), "%i", (int)valuef );
		else Com_sprintf(temp, sizeof(temp), "%f", valuef );
		cvar_copystring(temp, string); } }

/* ******************************************************************************** */
// Cvar Storage
/* ******************************************************************************** */

cvar_t *sv_cheats;

#define CVAR_TABLE_SIZE 128
localCvar_t *cvar_table[CVAR_TABLE_SIZE];

localCvar_t *cvar_first;
localCvar_t *cvar_last;

static localCvar_t *get_cvar(const char *name, qboolean create) {
	// Returns cvar on success, 0 on false.
	// May return 0 even if create is specified, if name fails to validate.

	int hash = cvar_hash_value(name);
	int hash_index = hash % CVAR_TABLE_SIZE;
	localCvar_t *current = cvar_table[hash_index];
	while(current) {
		if(current->hash == hash && !Q_stricmp(name, current->s.name)) return current;
		current = current->hash_next; }

	if(!create) return 0;
	if(!cvar_valid_name(name)) {
		Com_Printf("invalid cvar name string: %s\n", name);
		return 0; }

	current = S_Malloc(sizeof(*current));
	Com_Memset(current, 0, sizeof(*current));
	cvar_copystring(name, &current->s.name);
	cvar_copystring("", &current->s.string);

	current->hash = hash;
	current->hash_next = cvar_table[hash_index];
	if(cvar_table[hash_index]) cvar_table[hash_index]->hash_prev = current;
	cvar_table[hash_index] = current;

	current->prev = cvar_last;
	if(cvar_last) cvar_last->next = current;
	cvar_last = current;
	if(!cvar_first) cvar_first = current;

	return current; }

/* ******************************************************************************** */
// Primary Cvar Modifiers
/* ******************************************************************************** */

// If value is being withheld due to active latch, it will go in cvar->s.latchedString
// If it is a session-based latch, CVAR_LATCH will be set in system flags
// If it is a VM-based latch CVAR_LATCH will be set in protected flags

static void cvar_finalize(localCvar_t *cvar, qboolean unlatch) {
	qboolean protect = qfalse;
	char *old_string = 0;
	char *old_latch = 0;

	// Save old values
	if(cvar->s.string) cvar_linkstring(cvar->s.string, &old_string);
	if(cvar->s.latchedString) cvar_linkstring(cvar->s.latchedString, &old_latch);

	// Determine flags
	int old_flags = cvar->s.flags;
	cvar->s.flags = cvar->protected_flags | cvar->main_flags | cvar->system_flags;
	cvar_modifiedFlags |= cvar->s.flags ^ old_flags;

	// Update the reset string
	if(cvar->system_default) cvar_linkstring(cvar->system_default, &cvar->s.resetString);
	else if(cvar->protected_default) cvar_linkstring(cvar->protected_default, &cvar->s.resetString);
	else cvar_copystring("", &cvar->s.resetString);
	cvar_check_range(cvar, &cvar->s.resetString, qtrue);

	// Update the latch string
	if(cvar->protected_value) {
		cvar_linkstring(cvar->protected_value, &cvar->s.latchedString);
		protect = qtrue; }
	else if(cvar->main_value) {
		cvar_linkstring(cvar->main_value, &cvar->s.latchedString); }
	else if(cvar->protected_default) {
		cvar_linkstring(cvar->protected_default, &cvar->s.latchedString);
		protect = qtrue; }
	else if(cvar->system_default) {
		cvar_linkstring(cvar->system_default, &cvar->s.latchedString); }
	else {
		cvar_copystring("", &cvar->s.latchedString); }
	cvar_check_range(cvar, &cvar->s.latchedString, qtrue);

	// Decide if we want to unlatch it right away
	if(unlatch || !(cvar->s.flags & CVAR_LATCH) || !cvar->s.string ||
			cvar->s.latchedString == cvar->s.string || !strcmp(cvar->s.latchedString, cvar->s.string)) {
		// Perform the unlatch
		cvar_linkstring(cvar->s.latchedString, &cvar->s.string);
		cvar_clearstring(&cvar->s.latchedString);
		cvar->s.protect = protect; }

	// Check if new values are different from the old values
	if(!old_string || strcmp(old_string, cvar->s.string)) {
		// Update modification trackers
		++cvar->s.modificationCount;
		cvar->s.modified = qtrue;
		cvar_modifiedFlags |= cvar->s.flags;

		// Update values
		cvar->s.value = atof(cvar->s.string);
		cvar->s.integer = atoi(cvar->s.string); }

	else if(cvar->s.latchedString && (!old_latch || strcmp(old_latch, cvar->s.latchedString))) {
		// "dedicated" cvar check in Com_Frame depends on modified being set due to latch change
		++cvar->s.modificationCount;
		cvar->s.modified = qtrue; }

	cvar_mfree(old_string);
	cvar_mfree(old_latch); }

static int get_protected_permissions(localCvar_t *cvar) {
	// Returns 0 for nonmodifiable, 1 for modifiable, 2 for archivable
	if(!(cvar->system_flags & CVAR_SYSTEM_REGISTERED)) return 2;
	if(cvar->system_flags & CVAR_PROTECTED_ARCHIVABLE) return 2;
	if(cvar->system_flags & (CVAR_PROTECTED_MODIFIABLE | CVAR_SYSTEMINFO)) return 1;
	return 0; }

static localCvar_t *cvar_system_register(const char *name, const char *value, int flags) {
	localCvar_t *cvar = get_cvar(name, qtrue);
	if(!cvar) return 0;

	//Com_Printf("cvar_system_register %s to %s\n", name, value);

	// Set values
	// NOTE: The first-value-has precedence behavior is used to allow the special cvar
	// defines to override other defaults; does it cause issues anywhere else?
	if(value && !cvar->system_default) cvar_copystring(value, &cvar->system_default);

	// Set flags
	cvar->system_flags |= flags | CVAR_SYSTEM_REGISTERED;
	if(cvar->system_flags & CVAR_LATCH) cvar->protected_flags &= ~CVAR_LATCH;

	// If setting CVAR_ROM, wipe other values
	if(flags & CVAR_ROM) {
		cvar_clearstring(&cvar->main_value);
		cvar_clearstring(&cvar->protected_value);
		cvar_clearstring(&cvar->protected_default); }

	// If cvar is no longer protected modifiable, wipe protected values
	if(!get_protected_permissions(cvar)) {
		cvar_clearstring(&cvar->protected_value);
		cvar_clearstring(&cvar->protected_default);
		cvar->protected_flags = 0; }

	// If cvar was set under import mode and CVAR_IMPORT_ALLOWED is not present, clear values
	if(!(cvar->system_flags & CVAR_IMPORT_ALLOWED)) {
		if(cvar->protected_flags & CVAR_IMPORT_CREATED) {
			cvar_clearstring(&cvar->protected_value);
			cvar->protected_flags = 0; }
		if(cvar->main_flags & CVAR_IMPORT_CREATED) {
			cvar_clearstring(&cvar->main_value);
			cvar->main_flags = 0; } }

	cvar_finalize(cvar, qtrue);
	return cvar; }

static void cvar_system_set(const char *name, const char *value) {
	localCvar_t *cvar = get_cvar(name, qtrue);
	if(!cvar) return;

	// Set main value
	cvar_copystring(value, &cvar->main_value);

	// Wipe protected value
	cvar_clearstring(&cvar->protected_value);

	cvar_finalize(cvar, qtrue); }

static qboolean check_command_permissions(localCvar_t *cvar, qboolean init, qboolean verbose) {
	// Returns qtrue if modifiable by commands, qfalse otherwise
	if(cvar->s.flags & CVAR_SERVER_CREATED) {
		if(verbose) Com_Printf("%s is set by remote server.\n", cvar->s.name);
		return qfalse; }
	if(cvar->s.flags & CVAR_ROM) {
		if(verbose) Com_Printf("%s is read only.\n", cvar->s.name);
		return qfalse; }
	if(cvar->s.flags & CVAR_INIT && !init) {
		if(verbose) Com_Printf("%s can only be set as a command line parameter.\n", cvar->s.name);
		return qfalse; }
	if((cvar->s.flags & CVAR_CHEAT) && !sv_cheats->integer) {
		if(verbose) Com_Printf("%s is cheat protected.\n", cvar->s.name);
		return qfalse; }
	return qtrue; }

void cvar_command_set(const char *name, const char *value, int flags, cmd_mode_t mode, qboolean init, qboolean verbose) {
	localCvar_t *cvar = get_cvar(name, qtrue);
	if(!cvar) return;

	//Com_Printf("cvar_command_set %s to %s\n", name, value);

	// Check for blocking conditions
	if(!check_command_permissions(cvar, init, verbose)) return;

	// Check for settings import/safe autoexec.cfg mode
	if(mode & CMD_SETTINGS_IMPORT) {
		// If cvar is already system registered and not import allowed, abort here
		if((cvar->system_flags & CVAR_SYSTEM_REGISTERED) && !(cvar->system_flags & CVAR_IMPORT_ALLOWED)) {
			return; }
		// Set import flag, so import check can be performed if cvar is system registered in the future
		flags |= CVAR_IMPORT_CREATED; }

	// Set value and flags in appropriate location
	if(mode & CMD_PROTECTED) {
		if(!get_protected_permissions(cvar) || (cvar->main_flags & CVAR_PROTECTED)) {
			if(verbose) Com_Printf("%s cannot be set in protected mode.\n", name);
			return; }
		if(value) {
			cvar_copystring(value, &cvar->protected_value);
			cvar->protected_flags &= ~CVAR_CREATED_FLAGS;
			cvar->protected_flags |= CVAR_USER_CREATED; }
		cvar->protected_flags |= flags; }
	else {
		if(value) {
			cvar_copystring(value, &cvar->main_value);
			cvar->main_flags &= ~CVAR_CREATED_FLAGS;
			cvar->main_flags |= CVAR_USER_CREATED;
			cvar_clearstring(&cvar->protected_value); }
		cvar->main_flags |= flags; }

	cvar_finalize(cvar, qfalse);

	// Print message latch is blocking the new value from being activated
	if(value && verbose && cvar->s.latchedString) Com_Printf("%s will be changed upon restarting.\n", name); }

static void cvar_command_reset(localCvar_t *cvar, qboolean clear_flags) {
	// User-invoked cvar reset
	if(!check_command_permissions(cvar, qfalse, qtrue)) return;
	cvar_clearstring(&cvar->main_value);
	if(clear_flags) cvar->main_flags = 0;
	cvar_clearstring(&cvar->protected_value);
	if(!(cvar->s.flags & CVAR_CHEAT)) cvar_clearstring(&cvar->protected_default);
	if(clear_flags) cvar->protected_flags &= CVAR_CHEAT;
	cvar_finalize(cvar, qfalse); }

static localCvar_t *cvar_vm_register(const char *name, const char *value, int flags) {
	localCvar_t *cvar = get_cvar(name, qtrue);
	int permissions;
	if(!cvar) return 0;
	permissions = get_protected_permissions(cvar);
	if(!permissions) return cvar;

	// Set values
	if(!(cvar->system_flags & CVAR_IGNORE_VM_DEFAULT)) {
		cvar_copystring(value, &cvar->protected_default); }

	// Set flags
	cvar->protected_flags |= flags & (CVAR_USERINFO | CVAR_SERVERINFO | CVAR_SYSTEMINFO |
			CVAR_LATCH | CVAR_ROM | CVAR_CHEAT | CVAR_NORESTART);
	if(permissions == 2) cvar->protected_flags |= flags & CVAR_ARCHIVE;
	if(cvar->system_flags & CVAR_LATCH) cvar->protected_flags &= ~CVAR_LATCH;

	// If setting CVAR_ROM, override user value
	if((cvar->protected_flags & CVAR_ROM) && !(flags & CVAR_INIT) && cvar->protected_default &&
		((cvar->protected_value ? cvar->protected_flags : cvar->main_flags) & CVAR_USER_CREATED)) {
		cvar->protected_flags &= ~CVAR_CREATED_FLAGS;
		cvar->protected_flags |= CVAR_VM_CREATED;
		cvar_linkstring(cvar->protected_default, &cvar->protected_value); }

	cvar_finalize(cvar, cvar->protected_flags & CVAR_LATCH);
	return cvar; }

static localCvar_t *cvar_protected_set(const char *name, const char *value, int flags, int created_flag) {
	localCvar_t *cvar = get_cvar(name, qtrue);
	int permissions;
	if(!cvar) return 0;
	permissions = get_protected_permissions(cvar);
	if(!permissions) return cvar;

	// Set values
	if(!(cvar->main_flags & (CVAR_PROTECTED|CVAR_ROM)) || (cvar->protected_flags & (CVAR_ROM|CVAR_CHEAT))) {
		cvar_copystring(value, &cvar->protected_value);
		cvar->protected_flags &= ~CVAR_CREATED_FLAGS;
		cvar->protected_flags |= created_flag; }

	// Set flags
	cvar->protected_flags |= flags & (CVAR_USERINFO | CVAR_SERVERINFO | CVAR_SYSTEMINFO |
			CVAR_LATCH | CVAR_ROM | CVAR_CHEAT | CVAR_NORESTART);
	if(permissions == 2) cvar->protected_flags |= flags & CVAR_ARCHIVE;
	if(cvar->system_flags & CVAR_LATCH) cvar->protected_flags &= ~CVAR_LATCH;

	cvar_finalize(cvar, cvar->protected_flags & CVAR_LATCH);
	return cvar; }

void Cvar_CheckRange( cvar_t *var, float min, float max, qboolean integral ) {
	localCvar_t *cvar = (localCvar_t *)var;

	// Record the range parameters
	cvar->validate = qtrue;
	cvar->min = min;
	cvar->max = max;
	cvar->integral = integral;

	cvar_finalize(cvar, qtrue); }

void Cvar_SetCheatState(void) {
	// Set default value on cheat cvars
	localCvar_t *cvar;
	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if((cvar->s.flags & CVAR_CHEAT) && !(cvar->protected_flags & CVAR_VM_CREATED)) {
			cvar->protected_flags &= ~CVAR_CREATED_FLAGS;
			cvar_linkstring(cvar->s.resetString, &cvar->protected_value);
			cvar_finalize(cvar, qfalse); } } }

void Cvar_SetDescription(cvar_t *var, const char *var_description) {
	localCvar_t *cvar = (localCvar_t *)var;
	cvar_copystring(var_description, &cvar->description); }

void cvar_end_session(void) {
	// Reset non-archivable protected values when disconnecting from remote server
	localCvar_t *cvar;
	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if(cvar->protected_flags || cvar->protected_value || cvar->protected_default) {
			if((cvar->protected_flags & CVAR_SERVER_CREATED) || get_protected_permissions(cvar) < 2) {
				// Clear value and all flags
				cvar->protected_flags = 0;
				cvar_clearstring(&cvar->protected_value);
				cvar_clearstring(&cvar->protected_default); }

			else {
				// Have archive permission; just clear most of the flags
				cvar->protected_flags &= CVAR_ARCHIVE; }

			cvar_finalize(cvar, qfalse); } } }

/* ******************************************************************************** */
// Additional Cvar Modifiers
/* ******************************************************************************** */

cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags ) {
	// Called by system code. VM calls should use Cvar_Register instead
	return &cvar_system_register(var_name, var_value, flags)->s; }

cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force ) {
	// Called in a single place in system code
	cvar_system_set(var_name, value);
	return 0; }

void Cvar_StartupSet(const char *var_name, const char *value) {
	// Used for loading startup variables from the command line
	cvar_command_set(var_name, value, 0, CMD_NORMAL, qtrue, qtrue); }

void Cvar_SystemInfoSet( const char *var_name, const char *value ) {
	cvar_protected_set(var_name, value, CVAR_ROM, CVAR_SERVER_CREATED); }

void Cvar_Set( const char *var_name, const char *value) {
	// Called by system code
	cvar_system_set(var_name, value); }

void Cvar_SetValue( const char *var_name, float value) {
	// Called by system code.
	char	val[32];

	if ( value == (int)value ) {
		Com_sprintf (val, sizeof(val), "%i",(int)value);
	} else {
		Com_sprintf (val, sizeof(val), "%f",value);
	}
	Cvar_Set (var_name, val); }

void Cvar_SetLatched( const char *var_name, const char *value) {
	// Called in a single place in system code
	cvar_system_set(var_name, value); }

void Cvar_SetSafe( const char *var_name, const char *value ) {
	// Called by VMs
	cvar_protected_set(var_name, value, 0, CVAR_VM_CREATED); }

void Cvar_SetValueSafe( const char *var_name, float value ) {
	// Called by VMs
	char val[32];

	if( Q_isintegral( value ) )
		Com_sprintf( val, sizeof(val), "%i", (int)value );
	else
		Com_sprintf( val, sizeof(val), "%f", value );
	Cvar_SetSafe( var_name, val ); }

void Cvar_Reset( const char *var_name ) {
	// Called by by a UI VM call
	localCvar_t *cvar = get_cvar(var_name, qfalse);
	if(!cvar) return;
	if(get_protected_permissions(cvar)) {
		cvar_linkstring(cvar->s.resetString, &cvar->protected_value); }
	cvar_finalize(cvar, qfalse); }

void Cvar_ForceReset(const char *var_name) {
	// Called in a couple places in system code
	localCvar_t *cvar = get_cvar(var_name, qfalse);
	if(!cvar) return;
	cvar_clearstring(&cvar->main_value);
	cvar->main_flags = 0;
	cvar_clearstring(&cvar->protected_value);
	cvar_clearstring(&cvar->protected_default);
	cvar->protected_flags = 0;
	cvar_finalize(cvar, qtrue); }

void Cvar_Restart(qboolean unsetVM) {
	// This is currently only called from Com_GameRestart
	localCvar_t *cvar;
	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if((cvar->main_flags & CVAR_USER_CREATED) || (unsetVM && (cvar->main_flags & CVAR_VM_CREATED))) {
			cvar_clearstring(&cvar->main_value);
			cvar->main_flags = 0; }
		if((cvar->protected_flags & CVAR_USER_CREATED) || (unsetVM && (cvar->protected_flags & CVAR_VM_CREATED))) {
			cvar_clearstring(&cvar->protected_value);
			cvar_clearstring(&cvar->protected_default);
			cvar->protected_flags = 0; }
		cvar_finalize(cvar, qtrue); } }

/* ******************************************************************************** */
// Cvar Accessors
/* ******************************************************************************** */

float Cvar_VariableValue( const char *var_name ) {
	localCvar_t *cvar = get_cvar(var_name, qfalse);
	if(!cvar) return 0;
	return cvar->s.value; }

int Cvar_VariableIntegerValue( const char *var_name ) {
	localCvar_t *cvar = get_cvar(var_name, qfalse);
	if(!cvar) return 0;
	return cvar->s.integer; }

char *Cvar_VariableString( const char *var_name ) {
	// Do not modify or free result
	localCvar_t *cvar = get_cvar(var_name, qfalse);
	if(!cvar) return "";
	return cvar->s.string; }

void Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	Q_strncpyz( buffer, Cvar_VariableString(var_name), bufsize ); }

int Cvar_Flags( const char *var_name ) {
	localCvar_t *cvar = get_cvar(var_name, qfalse);
	if(!cvar) return CVAR_NONEXISTENT;
	if(cvar->s.modified) return cvar->s.flags | CVAR_MODIFIED;
	return cvar->s.flags; }

void Cvar_CommandCompletion(void (*callback)(const char *s)) {
	localCvar_t *cvar;
	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		callback(cvar->s.name); } }

const char *cvar_getstring(const char *name) {
	localCvar_t *cvar = get_cvar(name, qfalse);
	if(!cvar) return "";
	return cvar->s.string; }

char *Cvar_InfoString(int bit) {
	static char	info[MAX_INFO_STRING];
	localCvar_t *cvar;

	info[0] = 0;

	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if(cvar->s.name && (cvar->s.flags & bit))
			Info_SetValueForKey (info, cvar->s.name, cvar->s.string); }

	return info; }

char *Cvar_InfoString_Big(int bit) {
	static char	info[BIG_INFO_STRING];
	localCvar_t *cvar;

	info[0] = 0;

	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if(cvar->s.name && (cvar->s.flags & bit))
			Info_SetValueForKey_Big (info, cvar->s.name, cvar->s.string); }
	return info; }

void Cvar_InfoStringBuffer( int bit, char* buff, int buffsize ) {
	Q_strncpyz(buff,Cvar_InfoString(bit),buffsize); }

void Cvar_Print(localCvar_t *cvar) {
	char data[MAXPRINTMSG];
	cvar_stream_t stream = {data, 0, sizeof(data)};

	ADD_TEXT("\""); ADD_TEXT(cvar->s.name); ADD_TEXT("\" is:\""); ADD_TEXT(cvar->s.string); ADD_TEXT("^7\"");
	if(*cvar->s.resetString) {
		if(!Q_stricmp(cvar->s.string, cvar->s.resetString)) {
			ADD_TEXT(", the default\n"); }
		else {
			ADD_TEXT(" default:\""); ADD_TEXT(cvar->s.resetString); ADD_TEXT("\"\n"); } }
	else ADD_TEXT("\n");

	if(cvar->s.latchedString) {
		ADD_TEXT("latched: \""); ADD_TEXT(cvar->s.latchedString); ADD_TEXT("\"\n"); }

	if(cvar->description) {
		ADD_TEXT(cvar->description); ADD_TEXT("\n"); }

	Com_Printf("%s", stream.data); }

/* ******************************************************************************** */
// VM Access
/* ******************************************************************************** */

#define CVAR_VM_HANDLE_COUNT 1024
localCvar_t *vm_handles[CVAR_VM_HANDLE_COUNT];

typedef struct {
	short reset_count;
	short index;
} vm_handle_index_t;

vm_handle_index_t current_index = {1, 0};

/*
static void cvar_clear_vm_handles(void) {
	++current_index.reset_count;
	current_index.index = 0; }
*/

void Cvar_Update(vmCvar_t *vmCvar) {
	vm_handle_index_t index = *(vm_handle_index_t *)&vmCvar->handle;
	localCvar_t *cvar;

	if(index.index < 0 || index.index >= current_index.index || index.reset_count != current_index.reset_count) {
		Com_Error(ERR_DROP, "Cvar_Update on invalid handle"); }
	cvar = vm_handles[index.index];

#ifdef CMOD_CROSSHAIR
	if(crosshair_cvar_update(cvar->s.name, vmCvar)) return;
#endif

	if(cvar->s.modificationCount == vmCvar->modificationCount) return;

	vmCvar->modificationCount = cvar->s.modificationCount;
	Q_strncpyz(vmCvar->string, cvar->s.string, MAX_CVAR_VALUE_STRING);
	vmCvar->value = cvar->s.value;
	vmCvar->integer = cvar->s.integer; }

void Cvar_Register(vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags) {
	localCvar_t *cvar = cvar_vm_register(varName, defaultValue, flags);
	if(!cvar || !vmCvar) return;

	if(!cvar->vm_handle) {
		if(current_index.index >= CVAR_VM_HANDLE_COUNT) {
			Com_Error(ERR_FATAL, "CVAR_VM_HANDLE_COUNT hit"); }
		else {
			vm_handles[current_index.index] = cvar;
			cvar->vm_handle = *(int *)&current_index;
			++current_index.index; } }

	vmCvar->handle = cvar->vm_handle;
	vmCvar->modificationCount = -1;		// Immediately update in Cvar_Update
	Cvar_Update(vmCvar); }

/* ******************************************************************************** */
// Commands
/* ******************************************************************************** */

void Cvar_Print_f(void) {
	char *name;
	localCvar_t *cvar;
	
	if(Cmd_Argc() != 2)
	{
		Com_Printf ("usage: print <variable>\n");
		return;
	}

	name = Cmd_Argv(1);

	cvar = get_cvar(name, qfalse);
	
	if(cvar)
		Cvar_Print(cvar);
	else
		Com_Printf ("Cvar %s does not exist.\n", name); }

qboolean cvar_command(cmd_mode_t mode) {
	localCvar_t *cvar = get_cvar(Cmd_Argv(0), qfalse);
	if(!cvar) return qfalse;

	// perform a variable print or set
	if ( Cmd_Argc() == 1 ) {
		Cvar_Print(cvar);
		return qtrue; }

	// perform the same set as in Cvar_Set_Command
	cvar_command_set(cvar->s.name, Cmd_Args(), 0, mode, qfalse, qtrue);
	return qtrue; }

void cvar_vstr(cmd_mode_t mode) {
	localCvar_t *cvar;
	if(Cmd_Argc () != 2) {
		Com_Printf ("vstr <variablename> : execute a variable command\n");
		return; }

	cvar = get_cvar(Cmd_Argv(1), qfalse);
	if(!cvar) return;

	mode &= ~CMD_PROTECTED;
	if(cvar->protected_value) mode |= CMD_PROTECTED;
	else if(!Q_stricmp(cvar->s.name, "fs_game")) mode |= CMD_PROTECTED;

	Cbuf_ExecuteTextByMode(EXEC_INSERT, cvar->s.string, mode); }

void Cvar_CompleteCvarName( char *args, int argNum ) {
	if( argNum == 2 ) {
		// Skip "<cmd> "
		char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteCommand( p, qfalse, qtrue ); } }

static void cvar_flag_set_command(cmd_mode_t mode, const char *cvar_name, const char *value, char *flag_string) {
	int flags = 0;
	while(*flag_string) {
		char flag = tolower(*flag_string);
		if(flag == 'a') flags |= CVAR_ARCHIVE;
		if(flag == 'u') flags |= CVAR_USERINFO;
		if(flag == 's') flags |= CVAR_SERVERINFO;
		if(flag == 'r') flags |= CVAR_ROM;
		if(flag == 'v') flags |= CVAR_PROTECTED;
		if(flag == 'n') flags |= CVAR_NORESTART;
		if(flag == 'p') mode |= CMD_PROTECTED;
		++flag_string; }
	cvar_command_set(cvar_name, value, flags, mode, qfalse, qtrue); }

static void cvar_cmd_setf(void) {
	int c = Cmd_Argc();
	char *cmd = Cmd_Argv(0);

	if(c < 3) {
		Com_Printf("usage: %s <variable> <flags>\n", cmd);
		return; }

	cvar_flag_set_command(CMD_NORMAL, Cmd_Argv(1), 0, Cmd_Argv(2)); }

static void Cvar_Set_f(cmd_mode_t mode) {
	int c = Cmd_Argc();
	char *cmd = Cmd_Argv(0);

	if(c < 2) {
		Com_Printf("usage: %s <variable> <value>\n", cmd);
		return; }
	if(c == 2) {
		Cvar_Print_f();
		return; }

	if(!cmd[0] || !cmd[1] || !cmd[2]) return;	// Shouldn't happen

	cvar_flag_set_command(mode, Cmd_Argv(1), Cmd_ArgsFrom(2), cmd + 3); }

static void Cvar_List_f( void ) {
	localCvar_t *cvar;
	int cvar_count = 0;
	char *match;

	if ( Cmd_Argc() > 1 ) {
		match = Cmd_Argv( 1 );
	} else {
		match = NULL;
	}

	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if(match && !Com_Filter(match, cvar->s.name, qfalse)) continue;

		if (cvar->s.flags & CVAR_SERVERINFO) {
			Com_Printf("S");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_SYSTEMINFO) {
			Com_Printf("s");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_USERINFO) {
			Com_Printf("U");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_ROM) {
			Com_Printf("R");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_INIT) {
			Com_Printf("I");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_ARCHIVE) {
			Com_Printf("A");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_LATCH) {
			Com_Printf("L");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_CHEAT) {
			Com_Printf("C");
		} else {
			Com_Printf(" ");
		}
		if (cvar->s.flags & CVAR_USER_CREATED) {
			Com_Printf("?");
		} else {
			Com_Printf(" ");
		}

		Com_Printf (" %s \"%s\"\n", cvar->s.name, cvar->s.string);
	}

	Com_Printf ("\n%i total cvars\n", cvar_count);
	Com_Printf ("%i VM indexes\n", current_index.index); }

static void Cvar_Toggle_f(cmd_mode_t mode) {
	int		i, c = Cmd_Argc();
	char		*curval;

	if(c < 2) {
		Com_Printf("usage: toggle <variable> [value1, value2, ...]\n");
		return;
	}

	if(c == 2) {
		cvar_command_set(Cmd_Argv(1), va("%d", 
			!Cvar_VariableValue(Cmd_Argv(1))), 0, mode, qfalse, qtrue);
		return;
	}

	if(c == 3) {
		Com_Printf("toggle: nothing to toggle to\n");
		return;
	}

	curval = Cvar_VariableString(Cmd_Argv(1));

	// don't bother checking the last arg for a match since the desired
	// behaviour is the same as no match (set to the first argument)
	for(i = 2; i + 1 < c; i++) {
		if(strcmp(curval, Cmd_Argv(i)) == 0) {
			cvar_command_set(Cmd_Argv(1), Cmd_Argv(i + 1), 0, mode, qfalse, qtrue);
			return;
		}
	}

	// fallback
	cvar_command_set(Cmd_Argv(1), Cmd_Argv(2), 0, mode, qfalse, qtrue); }

static void Cvar_Reset_f( void ) {
	localCvar_t *cvar;
	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: reset <variable>\n");
		return; }

	cvar = get_cvar(Cmd_Argv(1), qfalse);
	if(cvar) cvar_command_reset(cvar, qfalse); }

static void Cvar_Unset_f(void) {
	localCvar_t *cvar;
	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: unset <variable>\n");
		return; }

	cvar = get_cvar(Cmd_Argv(1), qfalse);
	if(cvar) cvar_command_reset(cvar, qtrue); }

static void Cvar_Restart_f(void) {
	localCvar_t *cvar;
	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		if(cvar->s.flags & CVAR_NORESTART) continue;
		cvar_command_reset(cvar, qtrue); } }

static void cvar_flags_to_stream(int flags, cvar_stream_t *stream) {
	int have_flag = 0;
	#define RUN_FLAG(flag) if(flags&flag) {if(have_flag) ADD_TEXT2(", "); else have_flag = 1; ADD_TEXT2(#flag);}
	RUN_FLAG(CVAR_ARCHIVE);
	RUN_FLAG(CVAR_USERINFO);
	RUN_FLAG(CVAR_SERVERINFO);
	RUN_FLAG(CVAR_SYSTEMINFO);
	RUN_FLAG(CVAR_INIT);
	RUN_FLAG(CVAR_LATCH);
	RUN_FLAG(CVAR_ROM);
	RUN_FLAG(CVAR_USER_CREATED);
	RUN_FLAG(CVAR_TEMP);
	RUN_FLAG(CVAR_CHEAT);
	RUN_FLAG(CVAR_NORESTART);
	RUN_FLAG(CVAR_SERVER_CREATED);
	RUN_FLAG(CVAR_VM_CREATED);
	RUN_FLAG(CVAR_PROTECTED);
	RUN_FLAG(CVAR_SYSTEM_REGISTERED);
	RUN_FLAG(CVAR_PROTECTED_MODIFIABLE);
	RUN_FLAG(CVAR_PROTECTED_ARCHIVABLE);
	RUN_FLAG(CVAR_IMPORT_ALLOWED);
	RUN_FLAG(CVAR_IMPORT_CREATED);
	RUN_FLAG(CVAR_IGNORE_VM_DEFAULT);
	RUN_FLAG(CVAR_NOARCHIVE);
	RUN_FLAG(CVAR_NUMERIC);
	if(!have_flag) ADD_TEXT2("<None>"); }

static void cvar_cmd_var(void) {
	localCvar_t *cvar = get_cvar(Cmd_Argv(1), qfalse);
	char data[1000];
	cvar_stream_t stream = {data, 0, sizeof(data)};

	if(!cvar) {
		Com_Printf("Variable not found.\n");
		return; }

	ADD_TEXT("variable name: "); ADD_TEXT(cvar->s.name); ADD_TEXT("\n");
	ADD_TEXT("working value: "); ADD_TEXT(cvar->s.string); ADD_TEXT("\n");
	ADD_TEXT("working flags: "); cvar_flags_to_stream(cvar->s.flags, &stream); ADD_TEXT("\n");
	ADD_TEXT("latch value: "); ADD_TEXT(cvar->s.latchedString ? cvar->s.latchedString : "<None>"); ADD_TEXT("\n\n");

	ADD_TEXT("system default: "); ADD_TEXT(cvar->system_default ? cvar->system_default : "<None>"); ADD_TEXT("\n");
	ADD_TEXT("system flags: "); cvar_flags_to_stream(cvar->system_flags, &stream); ADD_TEXT("\n");
	ADD_TEXT("main value: "); ADD_TEXT(cvar->main_value ? cvar->main_value : "<None>"); ADD_TEXT("\n");
	ADD_TEXT("main flags: "); cvar_flags_to_stream(cvar->main_flags, &stream); ADD_TEXT("\n");
	ADD_TEXT("protected default: "); ADD_TEXT(cvar->protected_default ? cvar->protected_default : "<None>"); ADD_TEXT("\n");
	ADD_TEXT("protected value: "); ADD_TEXT(cvar->protected_value ? cvar->protected_value : "<None>"); ADD_TEXT("\n");
	ADD_TEXT("protected flags: "); cvar_flags_to_stream(cvar->protected_flags, &stream); ADD_TEXT("\n");

	Com_Printf("%s", stream.data); }

/* ******************************************************************************** */
// Special Cvars
/* ******************************************************************************** */

typedef enum {
	CVARTYPE_NONE,
	CVARTYPE_PREFERENCES,
	CVARTYPE_GRAPHICS,
	CVARTYPE_SOUND,
	CVARTYPE_NETWORK,
	CVARTYPE_MENU,
	CVARTYPE_LAST
} cvar_category_t;

typedef struct {
	char *cvar_name;
	char *default_value;
	cvar_category_t category;
	int flags;
} special_cvar_t;

/*
// Known unlisted VM sets
cl_freelook
color
r_allowExtensions
r_colorbits
r_depthbits
r_glDriver		// deprecated in renderer
r_intensity		// not actually set
r_lowEndVideo	// deprecated in renderer
r_stencilbits
r_texturebits
r_vertexLight	// not actually set
session*
*/

special_cvar_t specials[] = {
	// Special cvars and defaults
	{"ui_cdkeychecked", "-1", CVARTYPE_NONE, 0},
	{"cl_motd", "0", CVARTYPE_NONE, 0},
#ifdef USE_RENDERER_DLOPEN
	{"cl_renderer", "opengl1", CVARTYPE_NONE, 0},
#endif
	{"com_hunkmegs", "256", CVARTYPE_NONE, 0},
	{"com_soundMegs", "32", CVARTYPE_NONE, 0},
	{"s_sdlSpeed", "44100", CVARTYPE_NONE, 0},
	{"com_altivec", "0", CVARTYPE_NONE, 0},
	{"sv_master1", "master.stvef.org", CVARTYPE_NONE, 0},
	{"sv_master2", "efmaster.tjps.eu", CVARTYPE_NONE, 0},
	{"sv_master3", "master.stef1.daggolin.de", CVARTYPE_NONE, 0},
	{"sv_master4", "master.stef1.ravensoft.com", CVARTYPE_NONE, 0},
	{"sv_master5", "", CVARTYPE_NONE, 0},
#ifdef DEDICATED
	{"dedicated", "1", CVARTYPE_NONE, CVAR_NOARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
#else
	{"dedicated", "0", CVARTYPE_NONE, CVAR_NOARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
#endif
	{"fs_game", "", CVARTYPE_NONE, CVAR_NOARCHIVE},
	{"sv_killserver", "", CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawCrosshair", 0, CVARTYPE_NONE, CVAR_PROTECTED_ARCHIVABLE},
	{"cmod_crosshair_enable", "1", CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"sv_hostname", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cl_yawspeed", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cl_pitchspeed", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cl_run", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},

	// Server settings
	{"nextmap", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"sv_pure", "0", CVARTYPE_NONE, CVAR_SERVERINFO|CVAR_PROTECTED_MODIFIABLE},
	{"sv_minRate", "25000", CVARTYPE_NONE, 0},
	{"sv_fps", "30", CVARTYPE_NONE, 0},
	{"sv_voip", "0", CVARTYPE_NONE, 0},
	{"sv_maxClients", "32", CVARTYPE_NONE, CVAR_IGNORE_VM_DEFAULT},
	{"g_teamForceBalance", "0", CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE|CVAR_IGNORE_VM_DEFAULT},

	// Preferences
	{"name", "RedShirt", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED},
	{"model", "munro/red", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED},
	{"cl_allowDownload", "1", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"sensitivity", "5", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED|CVAR_NUMERIC},
	{"g_language", "", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED},
	{"k_language", "american", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED},
	{"s_language", "", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED},
	{"cg_crosshairSize", "24", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED},
	{"cmod_crosshair_selection", "076b9707", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cg_fov", "90", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IMPORT_ALLOWED|CVAR_IGNORE_VM_DEFAULT},
	{"rconPassword", "", CVARTYPE_PREFERENCES, CVAR_IMPORT_ALLOWED},
	{"cg_drawFPS", "0", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cg_drawTeamOverlay", "0", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cg_drawTimer", "1", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IGNORE_VM_DEFAULT},
	{"cg_lagometer", "0", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cg_marks", "1", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cg_simpleItems", "0", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cg_forceModel", "0", CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"cl_anglespeedkey", 0, CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_NUMERIC},
	{"in_joystick", 0, CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"joy_threshold", 0, CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_NUMERIC},
	{"m_filter", 0, CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"m_pitch", 0, CVARTYPE_PREFERENCES, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_NUMERIC},
	{"handicap", "100", CVARTYPE_PREFERENCES, CVAR_NOARCHIVE|CVAR_PROTECTED_MODIFIABLE},
	{"cl_voip", "0", CVARTYPE_PREFERENCES, CVAR_ARCHIVE},

	// Network settings
	{"rate", "100000", CVARTYPE_NETWORK, CVAR_PROTECTED_ARCHIVABLE},
	{"snaps", "100", CVARTYPE_NETWORK, CVAR_PROTECTED_MODIFIABLE},
	{"cl_maxPackets", "125", CVARTYPE_NETWORK, CVAR_PROTECTED_MODIFIABLE},

	// Graphics settings
	{"com_maxfps", "125", CVARTYPE_GRAPHICS, CVAR_PROTECTED_MODIFIABLE},
	{"r_fullscreen", "1", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_mode", "720x480", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_fullscreenMode", "-2", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_customWidth", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_customHeight", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_picmip", "0", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_dynamiclight", "1", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_ext_compress_textures", "0", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_ext_texture_filter_anisotropic", "1", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_ext_max_anisotropy", "16", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_textureMode", "GL_LINEAR_MIPMAP_LINEAR", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_flares", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_finish", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_fastsky", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_fastskyColor", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_gamma", "1.4", CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE|CVAR_NUMERIC},
	{"r_lodBias", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_subdivisions", 0, CVARTYPE_GRAPHICS, CVAR_PROTECTED_ARCHIVABLE},
	{"r_overBrightFactor", "1.5", CVARTYPE_GRAPHICS, 0},
	{"r_mapLightingFactor", "2", CVARTYPE_GRAPHICS, 0},
	{"r_mapLightingGamma", "1", CVARTYPE_GRAPHICS, 0},
	{"r_mapLightingGammaComponent", "1", CVARTYPE_GRAPHICS, 0},
	{"r_mapLightingClampMin", "0", CVARTYPE_GRAPHICS, 0},
	{"r_mapLightingClampMax", "1", CVARTYPE_GRAPHICS, 0},
	{"r_textureGamma", "1", CVARTYPE_GRAPHICS, 0},
	{"cmod_auto_brightness_enabled", "1", CVARTYPE_GRAPHICS, 0},
	{"cmod_anti_burnin", "0", CVARTYPE_GRAPHICS, 0},

	// Sound settings
	// Don't default to OpenAL since it currently doesn't work nicely with some EF maps
	{"s_useOpenAL", "0", CVARTYPE_SOUND, 0},
	{"s_volume", "0.6", CVARTYPE_SOUND, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"s_musicvolume", "0.6", CVARTYPE_SOUND, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},
	{"s_noDuplicate", "0", CVARTYPE_SOUND, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE},

	// UI Menu Settings
	{"ui_initialsetup", "1", CVARTYPE_MENU, CVAR_ARCHIVE|CVAR_PROTECTED_ARCHIVABLE|CVAR_IGNORE_VM_DEFAULT},
	{"ui_browserGameType", "0", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"ui_browserMaster", "1", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE|CVAR_IGNORE_VM_DEFAULT},
	{"ui_browserShowEmpty", "1", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"ui_browserShowFull", "1", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"ui_browserSortKey", "4", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server1", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server10", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server11", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server12", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server13", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server14", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server15", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server16", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server2", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server3", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server4", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server5", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server6", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server7", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server8", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},
	{"server9", "", CVARTYPE_MENU, CVAR_PROTECTED_ARCHIVABLE},

	// VM Cvars
	{"bot_challenge", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_fastchat", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_grapple", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_interbreedbots", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_interbreedchar", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_interbreedcycle", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_interbreedwrite", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_memorydump", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_minplayers", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_nochat", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_pause", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_report", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_rocketjump", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_testrchat", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_testsolid", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"bot_thinktime", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"capturelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_animspeed", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_autoswitch", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_bobpitch", 0, CVARTYPE_NONE, CVAR_ARCHIVE|CVAR_PROTECTED_MODIFIABLE},
	{"cg_bobroll", 0, CVARTYPE_NONE, CVAR_ARCHIVE|CVAR_PROTECTED_MODIFIABLE},
	{"cg_bobup", 0, CVARTYPE_NONE, CVAR_ARCHIVE|CVAR_PROTECTED_MODIFIABLE},
	{"cg_centertime", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_crosshairHealth", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_crosshairX", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_crosshairY", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_debuganim", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_debugevents", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_debugposition", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_deferPlayers", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_draw2D", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_draw3dIcons", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawAmmoWarning", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawAttacker", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawCrosshairNames", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawGun", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawIcons", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawRewards", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawSnapshot", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_drawStatus", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_errordecay", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_footsteps", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_gibs", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_gunX", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_gunY", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_gunZ", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_ignore", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_noplayeranims", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_nopredict", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_predictItems", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_reportDamage", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_runpitch", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_runroll", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_shadows", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_showmiss", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_stats", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_stereoSeparation", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_swingSpeed", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_teamChatHeight", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_teamChatTime", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_thirdPerson", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_thirdPersonAngle", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_thirdPersonRange", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_viewsize", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cg_zoomfov", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"cl_paused", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"com_blood", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"com_buildScript", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"dmflags", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"fraglimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_adaptrespawn", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_allowVote", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_arenasFile", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_arenasFile", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_banIPs", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_botsFile", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_botsFile", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_classChangeDebounceTime", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_debugAlloc", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_debugDamage", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_debugMove", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_dmflags", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_dmgmult", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_dowarmup", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_filterBan", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_forcerespawn", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_friendlyFire", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_gametype", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_ghostRespawn", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_gravity", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_holoIntro", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_inactivity", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_intermissionTime", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_knockback", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_log", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_logSync", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_maxGameClients", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_motd", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_needpass", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_nojointimeout", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_password", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_pModActionHero", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_pModAssimilation", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_pModDisintegration", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_pModElimination", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_pModSpecialties", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_podiumDist", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_podiumDrop", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_random_skin_limit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_restarted", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spAwards", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_speed", "300", CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE|CVAR_IGNORE_VM_DEFAULT},
	{"g_spScores1", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spScores2", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spScores3", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spScores4", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spScores5", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spSkill", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_spVideos", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_synchronousClients", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_team_group_blue", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_team_group_red", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_teamAutoJoin", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_warmup", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"g_weaponrespawn", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"gamedate", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"gamename", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"joy_xbutton", 0, CVARTYPE_NONE, 0},	// Appears deprecated
	{"joy_ybutton", 0, CVARTYPE_NONE, 0},	// Appears deprecated
	{"s_compression", 0, CVARTYPE_NONE, 0},	// Appears deprecated
	{"s_khz", 0, CVARTYPE_NONE, 0},	// Appears deprecated
	{"sv_mapname", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"sv_maxclients", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"teamoverlay", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"timelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"timelimitWinningTeam", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_cdkeychecked2", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_ctf_capturelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_ctf_friendly", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_ctf_timelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_ffa_fraglimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_ffa_timelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_playerclass", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_precacheweapons", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_spSelection", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_team_fraglimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_team_friendly", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_team_timelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_tourney_fraglimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
	{"ui_tourney_timelimit", 0, CVARTYPE_NONE, CVAR_PROTECTED_MODIFIABLE},
};

void register_special_cvars(void) {
	int i;
	for(i=0; i<ARRAY_LEN(specials); ++i) {
		special_cvar_t *special = &specials[i];
		localCvar_t *cvar = cvar_system_register(special->cvar_name, special->default_value, special->flags);
		cvar->category = special->category; } }

/* ******************************************************************************** */
// Config File Writing
/* ******************************************************************************** */

static qboolean cvar_matches_default(const localCvar_t *cvar, const char *value) {
	if(!cvar->system_default) return qfalse;
	if(cvar->s.flags & CVAR_NUMERIC) {
		if(atoi(value) == atoi(cvar->system_default) &&
			(float)atof(value) == (float)atof(cvar->system_default)) return qtrue; }
	else {
		if(!strcmp(value, cvar->system_default)) return qtrue; }
	return qfalse; }

static qboolean cvar_string_requires_quoting(const char *string) {
	// Returns qtrue if string needs quotes to avoid config file parsing issues
	if(!*string || strchr(string, ' ') || strchr(string, ';') ||
			Q_stristr(string, "//") || Q_stristr(string, "/*")) return qtrue;
	return qfalse; }

static int write_cvars_by_category(fileHandle_t f, int category, qboolean (enabled_fn)(localCvar_t *cvar), char *prelude) {
	int count = 0;
	localCvar_t *cvar;
	char data[70000];

	for(cvar=cvar_first; cvar; cvar=cvar->next) {
		char *value = 0;
		qboolean protect = qfalse;
		cvar_stream_t stream = {data, 0, sizeof(data)};

		// Make sure we're in the right category
		if(category != cvar->category) continue;
		if(enabled_fn && !enabled_fn(cvar)) continue;

		// Make sure the cvar is meant to be archived
		if(!(cvar->s.flags & CVAR_ARCHIVE) || (cvar->s.flags & CVAR_NOARCHIVE)) continue;

		// Try to get a valid value from protected or main variables
		if(cvar->protected_value && get_protected_permissions(cvar) == 2) {
			value = cvar->protected_value;
			protect = qtrue; }
		else if(cvar->main_value) {
			value = cvar->main_value; }
		else continue;

		// Don't write if it's the same as the default
		if(cvar_matches_default(cvar, value)) continue;

		// Don't write if value is excessively long or contains characters that could
		// cause problems parsing the config file
		if(strlen(cvar->s.name) > 256) continue;
		if(strlen(value) > 512) continue;
		if(strchr(value, '\n')) continue;
		if(strchr(value, '\r')) continue;
		if(strchr(value, '\"')) continue;

		// Place comment line above the first cvar in the section
		if(!count) ADD_TEXT(prelude);

		// Write the set command and flags
		ADD_TEXT("set");
		if(!(cvar->system_flags & CVAR_ARCHIVE)) ADD_TEXT("a");
		if(protect) ADD_TEXT("p");

		// cvar name
		ADD_TEXT(" ");
		if(cvar_string_requires_quoting(cvar->s.name)) {
			ADD_TEXT("\"");
			ADD_TEXT(cvar->s.name);
			ADD_TEXT("\""); }
		else {
			ADD_TEXT(cvar->s.name); }

		// cvar value
		ADD_TEXT(" ");
		if(cvar_string_requires_quoting(value)) {
			ADD_TEXT("\"");
			ADD_TEXT(value);
			ADD_TEXT("\""); }
		else {
			ADD_TEXT(value); }

		// newline
		ADD_TEXT(SYSTEM_NEWLINE);
	
		// write to file
		FS_Write(stream.data, stream.position, f);
	
		++count; }

	return count; }

static qboolean custom_cvars(localCvar_t *cvar) {
	return (!(cvar->s.flags & CVAR_SYSTEM_REGISTERED)); }

static qboolean noncustom_cvars(localCvar_t *cvar) {
	return !custom_cvars(cvar); }

void Cvar_WriteVariables(fileHandle_t f) {
	write_cvars_by_category(f, CVARTYPE_PREFERENCES, 0,
			SYSTEM_NEWLINE "// Preferences" SYSTEM_NEWLINE);

	write_cvars_by_category(f, CVARTYPE_GRAPHICS, 0,
			SYSTEM_NEWLINE "// Graphics settings" SYSTEM_NEWLINE);

	write_cvars_by_category(f, CVARTYPE_SOUND, 0,
			SYSTEM_NEWLINE "// Sound settings" SYSTEM_NEWLINE);

	write_cvars_by_category(f, CVARTYPE_NETWORK, 0,
			SYSTEM_NEWLINE "// Network settings" SYSTEM_NEWLINE);

	write_cvars_by_category(f, CVARTYPE_NONE, noncustom_cvars,
			SYSTEM_NEWLINE "// Advanced settings" SYSTEM_NEWLINE);

	write_cvars_by_category(f, CVARTYPE_NONE, custom_cvars,
			SYSTEM_NEWLINE "// Custom and mod-specific settings" SYSTEM_NEWLINE);

	write_cvars_by_category(f, CVARTYPE_MENU, 0,
			SYSTEM_NEWLINE "// Menu settings" SYSTEM_NEWLINE); }

/* ******************************************************************************** */
// Initialization
/* ******************************************************************************** */

void Cvar_Init(void) {
	int i;
	const char *set_aliases[] = {"set", "sets", "setu", "seta", "setp", "setap", "setr", "setn"};

	sv_cheats = Cvar_Get("sv_cheats", "1", CVAR_ROM | CVAR_SYSTEMINFO);

	register_special_cvars();

	for(i=0; i<ARRAY_LEN(set_aliases); ++i) {
		Cmd_AddProtectableCommand(set_aliases[i], Cvar_Set_f);
		Cmd_SetCommandCompletionFunc(set_aliases[i], Cvar_CompleteCvarName ); }
	Cmd_AddCommand("setf", cvar_cmd_setf);
	Cmd_SetCommandCompletionFunc("setf", Cvar_CompleteCvarName);

	Cmd_AddCommand("print", Cvar_Print_f);
	Cmd_AddProtectableCommand ("toggle", Cvar_Toggle_f);
	Cmd_SetCommandCompletionFunc( "toggle", Cvar_CompleteCvarName );
	Cmd_AddCommand ("reset", Cvar_Reset_f);
	Cmd_SetCommandCompletionFunc( "reset", Cvar_CompleteCvarName );
	Cmd_AddCommand ("unset", Cvar_Unset_f);
	Cmd_SetCommandCompletionFunc("unset", Cvar_CompleteCvarName);

	Cmd_AddCommand ("cvarlist", Cvar_List_f);
	Cmd_AddCommand ("cvar_restart", Cvar_Restart_f);

	Cmd_AddCommand("var", cvar_cmd_var); }

#endif
