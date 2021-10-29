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

#ifdef CMOD_COPYDEBUG_CMD_SUPPORTED
#include "../filesystem/fslocal.h"
#include <windows.h>
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef CMOD_COMMON_STRING_FUNCTIONS
void cmod_stream_append_string(cmod_stream_t *stream, const char *string) {
	// If stream runs out of space, output is truncated.
	// Non-zero size stream will always be null terminated.
	if(stream->position >= stream->size) {
		if(stream->size) stream->data[stream->size-1] = 0;
		stream->overflowed = qtrue;
		return; }
	if(string) while(*string) {
		if(stream->position >= stream->size-1) {
			stream->overflowed = qtrue;
			break; }
		stream->data[stream->position++] = *(string++); }
	stream->data[stream->position] = 0; }

void cmod_stream_append_string_separated(cmod_stream_t *stream, const char *string, const char *separator) {
	// Appends string, adding separator prefix if both stream and input are non-empty
	if(stream->position && string && *string) cmod_stream_append_string(stream, separator);
	cmod_stream_append_string(stream, string); }

void cmod_stream_append_data(cmod_stream_t *stream, const char *data, unsigned int length) {
	// Appends bytes to stream. Does not add null terminator.
	if(stream->position > stream->size) {
		stream->overflowed = qtrue;
		return; }
	if(length > stream->size - stream->position) {
		length = stream->size - stream->position;
		stream->overflowed = qtrue; }
	Com_Memcpy(stream->data + stream->position, data, length);
	stream->position += length; }

#define IS_WHITESPACE(chr) ((chr) == ' ' || (chr) == '\t' || (chr) == '\n' || (chr) == '\r')

unsigned int cmod_read_token(const char **current, char *buffer, unsigned int buffer_size, char delimiter) {
	// Returns number of characters read to output buffer (not including null terminator)
	// Null delimiter uses any whitespace as delimiter
	// Any leading and trailing whitespace characters will be skipped
	unsigned int char_count = 0;

	// Skip leading whitespace
	while(**current && IS_WHITESPACE(**current)) ++(*current);

	// Read item to buffer
	while(**current && **current != delimiter) {
		if(!delimiter && IS_WHITESPACE(**current)) break;
		if(buffer_size && char_count < buffer_size - 1) {
			buffer[char_count++] = **current; }
		++(*current); }

	// Skip input delimiter and trailing whitespace
	if(**current) ++(*current);
	while(**current && IS_WHITESPACE(**current)) ++(*current);

	// Skip output trailing whitespace
	while(char_count && IS_WHITESPACE(buffer[char_count-1])) --char_count;

	// Add null terminator
	if(buffer_size) buffer[char_count] = 0;
	return char_count; }

unsigned int cmod_read_token_ws(const char **current, char *buffer, unsigned int buffer_size) {
	// Reads whitespace-separated token from current to buffer, and advances current pointer
	// Returns number of characters read to buffer (not including null terminator, 0 if no data remaining)
	return cmod_read_token(current, buffer, buffer_size, 0); }
#endif

#ifdef CMOD_VM_STRNCPY_FIX
// Simple strncpy function to avoid overlap check issues with some library implementations
void vm_strncpy(char *dst, char *src, int length) {
	int i;
	for(i=0; i<length; ++i) {
		dst[i] = src[i];
		if(!src[i]) break; }
	for(; i<length; ++i) {
		dst[i] = 0; } }
#endif

#ifdef CMOD_ANTI_BURNIN
float cmod_anti_burnin_shift(float val) {
	if(cmod_anti_burnin->value <= 0.0f) return val;
	if(cmod_anti_burnin->value >= 1.0f) return 0.5f;
	float result = val * (1.0f - cmod_anti_burnin->value) + 0.5f * cmod_anti_burnin->value;
	if(result < 0.0f) result = 0.0f;
	if(result > 1.0f) result = 1.0f;
	return result; }
#endif

#ifdef CMOD_COPYDEBUG_CMD
#ifdef CMOD_COPYDEBUG_CMD_SUPPORTED
static void cmod_debug_get_config(cmod_stream_t *stream) {
	// Based on fs_execute_config_file
	char *data = 0;
	char path[FS_MAX_PATH];
	if(fs_generate_path_sourcedir(0, "cmod.cfg", 0, FS_ALLOW_SPECIAL_CFG, 0, path, sizeof(path))) {
		data = fs_read_data(0, path, 0, "cmod_debug_get_config"); }

	cmod_stream_append_string_separated(stream, data ? data : "[file not found]", "\n"); }

static void cmod_debug_get_autoexec(cmod_stream_t *stream) {
	// Based on fs_execute_config_file
	const fsc_file_t *file;
	const char *data = 0;
	unsigned int size;
	int lookup_flags = LOOKUPFLAG_PURE_ALLOW_DIRECT_SOURCE | LOOKUPFLAG_IGNORE_CURRENT_MAP;
	if(fs_download_mode->integer >= 2) {
		// Don't allow config files from restricted download folder pk3s, because they could disable the download folder
		// restrictions to unrestrict themselves
		lookup_flags |= LOOKUPFLAG_NO_DOWNLOAD_FOLDER; }
	// For q3config.cfg and autoexec.cfg - only load files on disk and from appropriate fs_mod_settings locations
	lookup_flags |= (LOOKUPFLAG_SETTINGS_FILE | LOOKUPFLAG_DIRECT_SOURCE_ONLY);

	file = fs_general_lookup("autoexec.cfg", lookup_flags, qfalse);
	if(file) {
		data = fs_read_data(file, 0, &size, "cmod_debug_get_autoexec"); }

	cmod_stream_append_string_separated(stream, data ? data : "[file not found]", "\n"); }

static void cmod_debug_get_filelist(cmod_stream_t *stream) {
	int i;
	fsc_hashtable_iterator_t hti;
	const fsc_file_t *file;
	char buffer[FS_FILE_BUFFER_SIZE];

	for(i=0; i<fs.files.bucket_count; ++i) {
		fsc_hashtable_open(&fs.files, i, &hti);
		while((file = (fsc_file_t *)STACKPTRN(fsc_hashtable_next(&hti)))) {
			if(!fsc_is_file_enabled(file, &fs)) continue;
			if(file->sourcetype != FSC_SOURCETYPE_DIRECT) continue;

			fs_file_to_buffer(file, buffer, sizeof(buffer), qtrue, qtrue, qtrue, qfalse);
			cmod_stream_append_string_separated(stream, buffer, "\n");

			if(((fsc_file_direct_t *)file)->pk3_hash) {
				cmod_stream_append_string(stream, va(" (hash:%i)", (int)((fsc_file_direct_t *)file)->pk3_hash)); } } } }

static void copydebug_write_clipboard(cmod_stream_t *stream) {
	// Based on sys_win32.c->Sys_ErrorDialog
	HGLOBAL memoryHandle;
	char *clipMemory;

	memoryHandle = GlobalAlloc( GMEM_MOVEABLE|GMEM_DDESHARE, stream->position+1);
	clipMemory = (char *)GlobalLock( memoryHandle );

	if( clipMemory )
	{
		Com_Memcpy(clipMemory, stream->data, stream->position);
		clipMemory[stream->position+1] = 0;

		if( OpenClipboard( NULL ) && EmptyClipboard( ) )
			SetClipboardData( CF_TEXT, memoryHandle );

		GlobalUnlock( clipMemory );
		CloseClipboard( );
	}
}
#endif

void cmod_copydebug_cmd(void) {
#ifdef CMOD_COPYDEBUG_CMD_SUPPORTED
	char buffer[65536];
	cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};

	cmod_stream_append_string_separated(&stream, "console history\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>", "\n\n");
	cmod_debug_get_console(&stream);
	cmod_stream_append_string_separated(&stream, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<", "\n");

	cmod_stream_append_string_separated(&stream, "cmod.cfg\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>", "\n\n");
	cmod_debug_get_config(&stream);
	cmod_stream_append_string_separated(&stream, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<", "\n");

	cmod_stream_append_string_separated(&stream, "autoexec.cfg\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>", "\n\n");
	cmod_debug_get_autoexec(&stream);
	cmod_stream_append_string_separated(&stream, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<", "\n");

	cmod_stream_append_string_separated(&stream, "file list\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>", "\n\n");
	cmod_debug_get_filelist(&stream);
	cmod_stream_append_string_separated(&stream, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<", "\n");

	cmod_stream_append_string_separated(&stream, "End of debug output.", "\n\n");
	copydebug_write_clipboard(&stream);
	Com_Printf("Debug info copied to clipboard.\n");
#else
	Com_Printf("Command not supported on this operating system or build configuration.\n");
#endif
}
#endif

#ifdef CMOD_CLIENT_ALT_SWAP_SUPPORT
#define EF_BUTTON_ATTACK 1
#define EF_BUTTON_ALT_ATTACK 32

static qboolean clientAltSwapActive;

void ClientAltSwap_CGameInit( void ) {
	// Don't leave settings from a previous mod
	clientAltSwapActive = qfalse;
}

void ClientAltSwap_ModifyCommand( usercmd_t *cmd ) {
	if ( clientAltSwapActive ) {
		if ( cmd->buttons & EF_BUTTON_ALT_ATTACK ) {
			cmd->buttons &= ~EF_BUTTON_ALT_ATTACK;
			cmd->buttons |= EF_BUTTON_ATTACK;
		} else if ( cmd->buttons & EF_BUTTON_ATTACK ) {
			cmd->buttons |= ( EF_BUTTON_ATTACK | EF_BUTTON_ALT_ATTACK );
		}
	}
}

void ClientAltSwap_SetState( qboolean swap ) {
	clientAltSwapActive = swap;
}
#endif
