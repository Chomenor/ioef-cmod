/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
              2017 Noah Metzger (chomenor@gmail.com)

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

#ifdef NEW_FILESYSTEM
#include "fslocal.h"

/* ******************************************************************************** */
// Download List Creation
/* ******************************************************************************** */

#define MAX_DOWNLOAD_NAME 64	// Max length of the pk3 filename

typedef struct download_entry_s {
	struct download_entry_s *next;
	unsigned int hash;
	char *local_name;
	char *remote_name;
	char *filename;
	char *mod_dir;
} download_entry_t;

static download_entry_t *current_download;
static download_entry_t *next_download;

static void fs_free_download_entry(download_entry_t *entry) {
	Z_Free(entry->local_name);
	Z_Free(entry->remote_name);
	Z_Free(entry->filename);
	Z_Free(entry->mod_dir);
	Z_Free(entry); }

static void fs_advance_download(void) {
	// Frees any existing current_download, and moves download from next_download to current_download
	if(current_download) fs_free_download_entry(current_download);
	current_download = next_download;
	if(next_download) next_download = next_download->next; }

static void fs_add_next_download(download_entry_t *entry) {
	entry->next = next_download;
	next_download = entry; }

static void fs_free_download_list(void) {
	while(current_download || next_download) fs_advance_download(); }

static download_entry_t *create_download_entry(char *name) {
	// Returns new entry on success, null on error.
	// Download entries should be freed by fs_free_download_entry.
	download_entry_t *entry;
	char temp_mod_dir[FSC_MAX_MODDIR];
	char *temp_filename;
	char mod_dir[FSC_MAX_MODDIR];
	char filename[MAX_DOWNLOAD_NAME + 1];

	// Generate mod_dir and filename
	if(!fsc_process_mod_dir(name, temp_mod_dir, &temp_filename)) return 0;
	if(!temp_filename) return 0;
	if(!fs_generate_path(temp_mod_dir, 0, 0, 0, 0, 0, mod_dir, sizeof(mod_dir), qfalse)) return 0;
	if(!fs_generate_path(temp_filename, 0, 0, 0, 0, 0, filename, sizeof(filename), qfalse)) return 0;

	// Patch mod dir capitalization
	if(!Q_stricmp(mod_dir, com_basegame->string)) Q_strncpyz(mod_dir, com_basegame->string, sizeof(mod_dir));
	if(!Q_stricmp(mod_dir, FS_GetCurrentGameDir())) Q_strncpyz(mod_dir, FS_GetCurrentGameDir(), sizeof(mod_dir));

	// Set the download entry strings
	entry = Z_Malloc(sizeof(*entry));
	entry->local_name = CopyString(va("%s/%s%s.pk3", mod_dir, fs_saveto_dlfolder->integer ? "downloads/" : "", filename));
	entry->remote_name = CopyString(va("%s.pk3", name));
	entry->mod_dir = CopyString(mod_dir);
	entry->filename = CopyString(filename);
	return entry; }

void fs_register_download_list(const char *hash_list, const char *name_list) {
	// This command is used to process the list of potential downloads received from the server.
	int i;
	int count;
	int hashes[4096];

	fs_free_download_list();

	Cmd_TokenizeString(hash_list);
	count = Cmd_Argc();
	if(count > 4096) count = 4096;	// Sanity check
	for(i=0; i<count; ++i) {
		hashes[i] = atoi(Cmd_Argv(i)); }

	Cmd_TokenizeString(name_list);
	if(Cmd_Argc() < count) count = Cmd_Argc();
	for(i=count-1; i>=0; --i) {
		download_entry_t *entry = create_download_entry(Cmd_Argv(i));
		if(!entry) {
			Com_Printf("WARNING: Ignoring download %s due to invalid name.\n", Cmd_Argv(i));
			continue; }
		entry->hash = hashes[i];
		fs_add_next_download(entry);

		//Com_Printf("fs_register_download_list: got %s - %u\n", entry->name, entry->hash);
	} }

/* ******************************************************************************** */
// Download List Advancement
/* ******************************************************************************** */

static qboolean fs_is_download_id_pak(download_entry_t *entry) {
	char test_path[FS_MAX_PATH];
	Com_sprintf(test_path, sizeof(test_path), "%s/%s", entry->mod_dir, entry->filename);
	#ifndef STANDALONE
	if(FS_idPak(test_path, BASEGAME, FS_NODOWNLOAD_PAKS)) return qtrue;
	if(FS_idPak(test_path, BASETA, FS_NODOWNLOAD_PAKS_TEAMARENA)) return qtrue;
	#endif
	return qfalse; }

static qboolean fs_is_valid_download(download_entry_t *entry, unsigned int recheck_hash) {
	// Returns qtrue if file should be downloaded, qfalse otherwise.
	// recheck_hash can be set to retest a file that was downloaded and has an unexpected hash
	unsigned int hash = recheck_hash ? recheck_hash : entry->hash;
	qboolean system_pak = system_pk3_position(hash) ? qtrue : qfalse;
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t candidate_entry_ptr;
	fsc_file_direct_t *different_moddir_match = 0;

	if(!sourcedirs[0].writable) {
		Com_Printf("WARNING: Ignoring download %s because filesystem is in read-only state.\n",
			entry->local_name);
		return qfalse; }
	if(!Q_stricmp(entry->mod_dir, "basemod")) {
		Com_Printf("WARNING: Ignoring download %s because downloads to basemod directory are not allowed.\n",
			entry->local_name);
		return qfalse; }

	fsc_hashtable_open(&fs.pk3_hash_lookup, hash, &hti);
	while((candidate_entry_ptr = fsc_hashtable_next(&hti))) {
		fsc_pk3_hash_map_entry_t *candidate_entry = STACKPTR(candidate_entry_ptr);
		fsc_file_direct_t *candidate_pk3 = STACKPTR(candidate_entry->pk3);
		if(!fsc_is_file_enabled((fsc_file_t *)candidate_pk3, &fs)) continue;
		if(candidate_pk3->pk3_hash != hash) continue;
		// If "fs_redownload_across_mods" is set, ignore match from different mod dir,
		// but record it so we can display a warning later
		if(fs_redownload_across_mods->integer && !system_pak && (!candidate_pk3->qp_mod_ptr ||
				Q_stricmp(STACKPTR(candidate_pk3->qp_mod_ptr), entry->mod_dir))) {
			different_moddir_match = candidate_pk3;
			continue; }
		// Have match
		if(recheck_hash) {
			Com_Printf("WARNING: Downloaded pk3 %s has unexpected hash which already exists in index."
				" Download not saved.\n", current_download->local_name); }
		return qfalse; }

	// NOTE: Consider using hash-based check instead of the old filename check?
	if(fs_is_download_id_pak(entry)) {
		Com_Printf("WARNING: Ignoring download %s as possible system pak.\n", entry->local_name);
		return qfalse; }

	if(different_moddir_match) {
		char buffer[FS_FILE_BUFFER_SIZE];
		fs_file_to_buffer((fsc_file_t *)different_moddir_match, buffer, sizeof(buffer),
				qfalse, qtrue, qfalse, qfalse);
		Com_Printf("WARNING: %s %s, even though the file already appears to exist at %s."
			" Set fs_redownload_across_mods to 0 to disable this behavior.\n",
			recheck_hash ? "Saving" : "Downloading", entry->local_name, buffer); }

	return qtrue; }

void fs_advance_next_needed_download(void) {
	// Advances current download to the next file that is needed and valid to download
	fs_advance_download();
	while(current_download) {
		if(fs_is_valid_download(current_download, 0)) break;
		fs_advance_download(); } }

qboolean fs_get_current_download_names(char **local_name_out, char **remote_name_out) {
	// Returns qtrue and writes names if current_download is available, qfalse otherwise
	if(!current_download) return qfalse;
	*local_name_out = current_download->local_name;
	*remote_name_out = current_download->remote_name;
	return qtrue; }

/* ******************************************************************************** */
// Attempted Download Tracking
/* ******************************************************************************** */

// This section is used to prevent trying to unsuccessfully download the same file over
// and over again in the same session.

pk3_list_t attempted_downloads_http;
pk3_list_t attempted_downloads;

static void register_attempted_download(unsigned int hash, qboolean http) {
	pk3_list_t *target = http ? &attempted_downloads_http : &attempted_downloads;
	if(!target->ht.bucket_count) pk3_list_initialize(target, 20);
	pk3_list_insert(target, hash); }

static qboolean check_attempted_download(unsigned int hash, qboolean http) {
	// Returns qtrue if download already attempted
	pk3_list_t *target = http ? &attempted_downloads_http : &attempted_downloads;
	return pk3_list_lookup(target, hash, qfalse) ? qtrue : qfalse; }

void fs_register_current_download_attempt(qboolean http) {
	register_attempted_download(current_download->hash, http); }

void fs_clear_attempted_downloads(void) {
	pk3_list_free(&attempted_downloads_http);
	pk3_list_free(&attempted_downloads); }

int fs_get_current_download_attempt_status(void) {
	// Returns 1 if only http download attempted, 2 if UDP download attempted
	if(!current_download) return 0;
	if(check_attempted_download(current_download->hash, qfalse)) return 2;
	if(check_attempted_download(current_download->hash, qtrue)) return 1;
	return 0; }

/* ******************************************************************************** */
// Download Completion
/* ******************************************************************************** */

static void get_temp_file_hash_callback(void *context, char *data, int size) {
	*(unsigned int *)context = fsc_block_checksum(data, size); }

static unsigned int get_temp_file_hash(const char *tempfile_path) {
	void *os_path = fsc_string_to_os_path(tempfile_path);
	unsigned int result = 0;
	fsc_load_pk3(os_path, &fs, 0, 0, get_temp_file_hash_callback, &result);
	fsc_free(os_path);
	return result; }

void fs_finalize_download(void) {
	// Does some final verification and moves the download, which hopefully has been written to
	// the temporary file, to its final location.
	char tempfile_path[FS_MAX_PATH];
	char target_path[FS_MAX_PATH];
	unsigned int actual_hash;

	if(!fs_generate_path_writedir("download.temp", 0, 0, 0, tempfile_path, sizeof(tempfile_path), qfalse)) {
		Com_Printf("ERROR: Failed to get tempfile path for download\n");
		return; }
	if(!fs_generate_path_writedir(current_download->local_name, 0, FS_ALLOW_PK3|FS_ALLOW_SLASH|FS_CREATE_DIRECTORIES_FOR_FILE,
				0, target_path, sizeof(target_path), qfalse)) {
		Com_Printf("ERROR: Failed to get target path for download\n");
		return; }

	actual_hash = get_temp_file_hash(tempfile_path);
	if(!actual_hash) {
		Com_Printf("WARNING: Downloaded pk3 %s appears to be missing or corrupt. Download not saved.\n",
				current_download->local_name);
		return; }

	if(actual_hash != current_download->hash) {
		// Wrong hash - this could be a malicious attempt to spoof a system pak or maybe a corrupt
		//    download, but probably is just a server configuration issue mixing up pak versions.
		//    Run the file needed check with the new hash to see if it still passes.
		if(!fs_is_valid_download(current_download, actual_hash)) {
			return; }
		else {
			Com_Printf("WARNING: Downloaded pk3 %s has unexpected hash.\n", current_download->local_name); } }

	if(FS_FileInPathExists(target_path)) {
		char *new_name = va("%s/%s%s.%08x.pk3", current_download->mod_dir, fs_saveto_dlfolder->integer ? "downloads/" : "",
				current_download->filename, actual_hash);
		Com_Printf("WARNING: Downloaded pk3 %s conflicts with existing file. Using name %s instead.\n",
				current_download->local_name, new_name);
		if(!fs_generate_path_writedir(new_name, 0, FS_ALLOW_SLASH|FS_ALLOW_PK3, 0, target_path, sizeof(target_path), qfalse)) {
			Com_Printf("ERROR: Failed to get nonconflicted target path for download\n");
			return; }

		fs_delete_file(target_path); }

	fs_rename_file(tempfile_path, target_path);
	if(FS_FileInPathExists(tempfile_path)) {
		Com_Printf("ERROR: There was a problem moving downloaded pk3 %s from temporary file to target"
				" location. Download may not be saved.\n", current_download->local_name); }
	else {
		// Download appears successful, so it should be logged as fully attempted
		// Otherwise it could be treated as an unsuccessful cURL download and be eligible for a non-cURL retry
		register_attempted_download(actual_hash, qfalse); } }

#endif	// NEW_FILESYSTEM
