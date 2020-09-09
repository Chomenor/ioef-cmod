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

#ifdef CMOD_MAPTABLE
#include "../../server/server.h"

#ifdef CMOD_CVAR_HANDLING
#define CVAR_SET(cvar_name, value) cvar_command_set(cvar_name, value, 0, CMD_NORMAL, qfalse, qfalse)
#else
#define CVAR_SET(cvar_name, value) Cvar_Set(cvar_name, value)
#endif

/* ******************************************************************************** */
// Map table loading
/* ******************************************************************************** */

#define MAPTABLE_MAX_ENTRIES 1024

static qboolean maptable_entry_loaded(const char *key, cmod_maptable_t *table) {
	int i;
	for(i=0; i<table->entry_count; ++i) {
		if(!Q_stricmp(table->entries[i].key, key)) return qtrue; }
	return qfalse; }

static void cmod_maptable_load_file(const char *path, cmod_maptable_t *table, qboolean verbose) {
	// Read the file
	void *data = 0;
	int length = FS_ReadFile(path, &data);
	if(!data || length < 0) {
		if(data) FS_FreeFile(data);
		if(verbose) Com_Printf("%s: failed to read file\n", path);
		return; }

	// Table is considered "loaded" if any source file is loaded
	table->maptable_loaded = qtrue;

	// Load entries to table
	const char *data_ptr = data;
	unsigned int duplicate_count = 0;
	while(table->entry_count < MAPTABLE_MAX_ENTRIES) {
		char line[2048];
		const char *line_ptr = line;
		char key[1024];
		char value[1024];
		if(!cmod_read_token(&data_ptr, line, sizeof(line), '\n')) break;
		cmod_read_token(&line_ptr, key, sizeof(key), '=');
		cmod_read_token(&line_ptr, value, sizeof(value), '\n');
		if(maptable_entry_loaded(key, table)) {
			++duplicate_count; }
		else {
			table->entries[table->entry_count++] = (cmod_maptable_entry_t){CopyString(key), CopyString(value)}; } }

	// Free source file data
	FS_FreeFile(data);

	// Print debug info
	if(verbose) {
		char duplicate_message[256] = {0};
		if(duplicate_count) {
			Com_sprintf(duplicate_message, sizeof(duplicate_message), " (%u duplicate entries skipped)", duplicate_count); }
		Com_Printf("%s: loaded %u entries%s\n", path, table->entry_count, duplicate_message); } }

cmod_maptable_t cmod_maptable_load(const char *map_name, qboolean verbose) {
	// Generates maptable for given map name

	// Initiate map table using temporary entry pointer buffer
	cmod_maptable_entry_t entry_buffer[MAPTABLE_MAX_ENTRIES];
	cmod_maptable_t table = {entry_buffer, 0, qfalse};

	// Load maptable files from each directory indicated by sv_maptable_source_dirs
	const char *srcdir_ptr = sv_maptable_source_dirs->string;
	char srcdir[256];
	while(cmod_read_token_ws(&srcdir_ptr, srcdir, sizeof(srcdir))) {
		char path[256];
		Com_sprintf(path, sizeof(path), "%s/%s.mt", srcdir, map_name);
		cmod_maptable_load_file(path, &table, verbose); }

	// Move entry pointers from temp buffer to malloc now that we know the entry count size
	table.entries = Z_Malloc(sizeof(*table.entries) * table.entry_count);
	Com_Memcpy(table.entries, entry_buffer, sizeof(*table.entries) * table.entry_count);

	return table; }

void cmod_maptable_free(cmod_maptable_t *maptable) {
	// Can be called on a zeroed or initialized maptable
	// Result maptable will be zeroed
	int i;
	if(maptable->entries) {
		for(i=0; i<maptable->entry_count; ++i) {
			Z_Free(maptable->entries[i].key);
			Z_Free(maptable->entries[i].value); }
		Z_Free(maptable->entries); }
	Com_Memset(maptable, 0, sizeof(*maptable)); }

const char *cmod_maptable_get_value(cmod_maptable_t *maptable, const char *key) {
	// Returns value if matching key found, empty string otherwise
	// Value is valid until map table is freed
	int i;
	for(i=0; i<maptable->entry_count; ++i) {
		if(!Q_stricmp(maptable->entries[i].key, key)) return maptable->entries[i].value; }
	return ""; }

/* ******************************************************************************** */
// Current maptable handling
/* ******************************************************************************** */

static cmod_maptable_t current_maptable;

static void update_maptable_info_cvars(void) {
	if(current_maptable.maptable_loaded) {
		Cvar_Set("sv_maptable_loaded", "true");
		Cvar_Set("sv_maptable_entry_count", va("%u", current_maptable.entry_count)); }
	else {
		Cvar_Set("sv_maptable_loaded", "false");
		Cvar_Set("sv_maptable_entry_count", "-1"); } }

static const char *maptable_cvar_deref(const char *value) {
	// Support cmdtools-like asterisk arguments
	int ref_count = 0;
	while(*value == '*') {
		++ref_count;
		++value; }
	while(ref_count) {
		value = Cvar_VariableString(value);
		--ref_count; }
	return value; }

static void maptable_load_cmd(void) {
	const char *map_name = maptable_cvar_deref(Cmd_Argv(1));

	if(!*map_name) {
		Com_Printf("Usage: maptable_load <map name>\n");
		return; }

	Com_Printf("Loading map table for '%s'...\n", map_name);
	cmod_maptable_free(&current_maptable);
	current_maptable = cmod_maptable_load(map_name, qtrue);
	update_maptable_info_cvars(); }

static void maptable_unload_cmd(void) {
	if(current_maptable.entries) Com_Printf("Unloading map table.\n");
	cmod_maptable_free(&current_maptable);
	update_maptable_info_cvars(); }

static void maptable_retrieve_cmd(void) {
	const char *maptable_key = maptable_cvar_deref(Cmd_Argv(1));
	const char *target_cvar = maptable_cvar_deref(Cmd_Argv(2));

	if(!*maptable_key || !*target_cvar) {
		Com_Printf("Usage: maptable_retrieve <maptable key> <target_cvar>\n");
		return; }

	CVAR_SET(target_cvar, cmod_maptable_get_value(&current_maptable, maptable_key)); }

static void maptable_status_cmd(void) {
	int i;
	if(!current_maptable.entries) {
		Com_Printf("Current map table is invalid/not loaded.\n"); }
	else if(current_maptable.entry_count == 0) {
		Com_Printf("Current map table is empty.\n"); }
	else {
		Com_Printf("Currently loaded map table:\n");
		for(i=0; i<current_maptable.entry_count; ++i) {
			Com_Printf("key(%s) value(%s)\n", current_maptable.entries[i].key, current_maptable.entries[i].value); } } }

void cmod_maptable_init(void) {
	Cmd_AddCommand("maptable_load", maptable_load_cmd);
	Cmd_AddCommand("maptable_unload", maptable_unload_cmd);
	Cmd_AddCommand("maptable_retrieve", maptable_retrieve_cmd);
	Cmd_AddCommand("maptable_status", maptable_status_cmd); }

#endif
