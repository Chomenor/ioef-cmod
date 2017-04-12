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

#ifdef NEW_FILESYSTEM
#include "fslocal.h"

#define MAX_REFERENCE_SET_ENTRIES 2048
#define MAX_PURE_CHECKSUM_CACHE 256

/* ******************************************************************************** */
// Reference Set
/* ******************************************************************************** */

typedef struct {
	fs_hashtable_entry_t hte;
	fsc_file_direct_t *pak;
} reference_set_entry_t;

typedef fs_hashtable_t reference_set_t;

static void reference_set_free(reference_set_t *reference_set) {
	fs_hashtable_free(reference_set, 0); }

static qboolean reference_set_add(reference_set_t *reference_set, fsc_file_direct_t *pak) {
	// Returns qtrue on success, qfalse if already inserted or maximum hit
	fs_hashtable_iterator_t it = fs_hashtable_iterate(reference_set, pak->pk3_hash, qfalse);
	reference_set_entry_t *entry;

	if(reference_set->element_count >= MAX_REFERENCE_SET_ENTRIES) return qfalse;

	while((entry = fs_hashtable_next(&it))) {
		if(entry->pak == pak) return qfalse;
		if(entry->pak->pk3_hash == pak->pk3_hash) {
			// Different file with same hash - only keep the higher precedence file
			if(fs_compare_file((fsc_file_t *)entry->pak, (fsc_file_t *)pak, qfalse) > 0) entry->pak = pak;
			return qfalse; } }

	entry = S_Malloc(sizeof(reference_set_entry_t));
	entry->pak = pak;
	fs_hashtable_insert(reference_set, (fs_hashtable_entry_t *)entry, pak->pk3_hash);
	return qtrue; }

/* ******************************************************************************** */
// Reference List
/* ******************************************************************************** */

typedef struct {
	fsc_file_direct_t **paks;
	int pak_count;
} reference_list_t;

static void reference_set_to_reference_list(fs_hashtable_t *reference_set, reference_list_t *reference_list) {
	fs_hashtable_iterator_t it = fs_hashtable_iterate(reference_set, 0, qtrue);
	reference_set_entry_t *entry;
	int position = 0;

	reference_list->pak_count = reference_set->element_count;
	reference_list->paks = Z_Malloc(reference_list->pak_count * sizeof(fsc_file_direct_t *));

	while((entry = fs_hashtable_next(&it))) {
		if(position >= reference_list->pak_count) Com_Error(ERR_FATAL, "reference_set_to_reference_list overflow");
		reference_list->paks[position++] = entry->pak; }
	if(position != reference_list->pak_count) Com_Error(ERR_FATAL, "reference_set_to_reference_list underflow"); }

static void free_reference_list(reference_list_t *reference_list) {
	Z_Free(reference_list->paks); }

static int compare_reference_pk3_qsort_pure(const void *pak1, const void *pak2) {
	return fs_compare_file(*(fsc_file_t **)pak1, *(fsc_file_t **)pak2, qtrue); }

static int compare_reference_pk3_qsort_nonpure(const void *pak1, const void *pak2) {
	return fs_compare_file(*(fsc_file_t **)pak1, *(fsc_file_t **)pak2, qfalse); }

static void sort_reference_list(reference_list_t *reference_list, qboolean use_pure_list) {
	qsort(reference_list->paks, reference_list->pak_count, sizeof(fsc_file_direct_t *),
			use_pure_list ? compare_reference_pk3_qsort_pure : compare_reference_pk3_qsort_nonpure); }

/* ******************************************************************************** */
// Reference String Building
/* ******************************************************************************** */

static void reference_name_string(fsc_file_direct_t *pak, fsc_stream_t *stream) {
	const char *mod_dir = pak->qp_mod_ptr ? STACKPTR(pak->qp_mod_ptr) : com_basegame->string;

	// Replace basemod with com_basegame to avoid issues
	// Servers should avoid using basemod for download-enabled paks if possible
	if(!Q_stricmp(mod_dir, "basemod")) mod_dir = com_basegame->string;

	// Patch mod dir capitalization
	if(!Q_stricmp(mod_dir, com_basegame->string)) mod_dir = com_basegame->string;
	if(!Q_stricmp(mod_dir, FS_GetCurrentGameDir())) mod_dir = FS_GetCurrentGameDir();

	fsc_stream_append_string(stream, mod_dir);
	fsc_stream_append_string(stream, "/");
	fsc_stream_append_string(stream, STACKPTR(pak->f.qp_name_ptr)); }

static void reference_hash_string(fsc_file_direct_t *pak, fsc_stream_t *stream) {
	char buffer[20];
	Com_sprintf(buffer, sizeof(buffer), "%i", pak->pk3_hash);
	fsc_stream_append_string(stream, buffer); }

static void reference_list_to_stream(reference_list_t *reference_list, fsc_stream_t *stream,
		void (data_function)(fsc_file_direct_t *pak, fsc_stream_t *stream)) {
	int i;
	for(i=0; i<reference_list->pak_count; ++i) {
		if(stream->position) fsc_stream_append_string(stream, " ");
		data_function(reference_list->paks[i], stream);
		if(stream->overflowed) break; } }

static qboolean reference_list_to_buffer(reference_list_t *reference_list, char *output, int output_size,
			void (data_function)(fsc_file_direct_t *pak, fsc_stream_t *stream)) {
	// Returns qtrue on success, qfalse on overflow
	fsc_stream_t stream = {output, 0, output_size, 0};
	*output = 0;	// In case there are 0 references
	reference_list_to_stream(reference_list, &stream, data_function);
	if(stream.overflowed) {
		*output = 0;
		return qfalse; }
	return qtrue; }

/* ******************************************************************************** */
// Pure Checksum Determination
/* ******************************************************************************** */

// Pure checksum entry handling

typedef struct {
	const fsc_file_direct_t *pk3;
	int data_size;
	int pure_checksum;
	int checksum_feed;
} pure_checksum_entry_t;

static void get_pure_checksum_entry_callback(void *context, char *data, int size) {
	pure_checksum_entry_t **entry = context;
	*entry = fsc_malloc(sizeof(pure_checksum_entry_t) + size);
	(*entry)->data_size = size;
	Com_Memcpy((char *)*entry + sizeof(pure_checksum_entry_t), data, size); }

static pure_checksum_entry_t *get_pure_checksum_entry(const fsc_file_direct_t *pk3) {
	pure_checksum_entry_t *entry = 0;
	fsc_load_pk3(STACKPTR(pk3->os_path_ptr), &fs, 0, 0, get_pure_checksum_entry_callback, &entry);
	if(entry) entry->pk3 = pk3;
	return entry; }

static void update_pure_checksum_entry(pure_checksum_entry_t *entry, int checksum_feed) {
	entry->checksum_feed = LittleLong(checksum_feed);
	entry->pure_checksum = fsc_block_checksum(&entry->checksum_feed, entry->data_size + 4); }

// Pure checksum cache handling

typedef struct pure_checksum_node_s {
	struct pure_checksum_node_s *next;
	pure_checksum_entry_t *entry;
	int rank;
} pure_checksum_node_t;

static int pure_checksum_rank = 0;
static pure_checksum_node_t *pure_checksum_cache;

static int get_pure_checksum_for_pk3(const fsc_file_direct_t *pk3, int checksum_feed) {
	int entry_count = 0;
	pure_checksum_node_t *node = pure_checksum_cache;
	pure_checksum_node_t *deletion_node = 0;

	while(node) {
		if(node->entry && node->entry->pk3 == pk3) {
			// Use existing entry
			if(node->entry->checksum_feed != checksum_feed) {
				update_pure_checksum_entry(node->entry, checksum_feed); }
			node->rank = ++pure_checksum_rank;
			return node->entry->pure_checksum; }

		if(!deletion_node || node->rank < deletion_node->rank) deletion_node = node;

		++entry_count;
		node = node->next; }

	// Prepare new node
	if(entry_count >= MAX_PURE_CHECKSUM_CACHE) {
		node = deletion_node;
		if(node->entry) fsc_free(node->entry); }
	else {
		node = S_Malloc(sizeof(*node));
		node->next = pure_checksum_cache;
		pure_checksum_cache = node; }

	// Create new entry
	node->entry = get_pure_checksum_entry(pk3);
	if(!node->entry) return 0;
	update_pure_checksum_entry(node->entry, checksum_feed);
	node->rank = ++pure_checksum_rank;
	return node->entry->pure_checksum; }

static int get_pure_checksum_for_file(const fsc_file_t *file, int checksum_feed) {
	if(!file) return 0;
	if(file->sourcetype != FSC_SOURCETYPE_PK3) return 0;
	return get_pure_checksum_for_pk3(STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3), checksum_feed); }

/* ******************************************************************************** */
// Pure Validation String Building
/* ******************************************************************************** */

static void add_referenced_pure_pk3s(fsc_stream_t *stream, fs_hashtable_t *referenced_paks) {
	int i;
	reference_list_t reference_list;
	char buffer[20];
	int lump_checksum = 0;

	reference_set_to_reference_list(referenced_paks, &reference_list);
	sort_reference_list(&reference_list, qtrue);

	for(i=0; i<reference_list.pak_count; ++i) {
		int pure_checksum = get_pure_checksum_for_pk3(reference_list.paks[i], checksum_feed);

		if(fs_debug_references->integer) {
			char temp[FS_FILE_BUFFER_SIZE];
			fs_file_to_buffer((fsc_file_t *)reference_list.paks[i], temp, sizeof(temp), qtrue, qtrue, qtrue, qtrue);
			Com_Printf("adding pak to pure validation list: %s\n", temp); }

		lump_checksum ^= pure_checksum;
		Com_sprintf(buffer, sizeof(buffer), " %i", pure_checksum);
		fsc_stream_append_string(stream, buffer); }

	Com_sprintf(buffer, sizeof(buffer), " %i ", checksum_feed ^ lump_checksum ^ reference_list.pak_count);
	fsc_stream_append_string(stream, buffer);

	free_reference_list(&reference_list); }

static void build_pure_validation_string(char *output, int output_size, fs_hashtable_t *referenced_paks) {
	fsc_stream_t stream = {output, 0, output_size, 0};
	char buffer[50];
	int cgame_checksum = get_pure_checksum_for_file(fs_general_lookup("vm/cgame.qvm", qtrue, qfalse, qfalse), checksum_feed);
	int ui_checksum = get_pure_checksum_for_file(fs_general_lookup("vm/ui.qvm", qtrue, qfalse, qfalse), checksum_feed);

	Com_sprintf(buffer, sizeof(buffer), "%i %i @", cgame_checksum, ui_checksum);
	fsc_stream_append_string(&stream, buffer);

	if(fs_full_pure_validation->integer) {
		add_referenced_pure_pk3s(&stream, referenced_paks); }
	else {
		Com_sprintf(buffer, sizeof(buffer), " %i %i ", cgame_checksum, checksum_feed ^ cgame_checksum ^ 1);
		fsc_stream_append_string(&stream, buffer); } }

/* ******************************************************************************** */
// Referenced Paks
/* ******************************************************************************** */

static reference_set_t referenced_paks;

void fs_register_reference(const fsc_file_t *file) {
	// Adds the source pk3 of the given file to the current referenced paks set
	if(file->sourcetype != FSC_SOURCETYPE_PK3) return;

	// Don't register references for certain extensions
	if(file->qp_ext_ptr) {
		int i;
		static const char *special_extensions[] = {"shader", "txt", "cfg", "config", "bot", "arena", "menu"};
		const char *extension = STACKPTR(file->qp_ext_ptr);
		for(i=0; i<ARRAY_LEN(special_extensions); ++i) {
			if(!Q_stricmp(extension, special_extensions[i])) return; } }

	// Don't register reference in certain special cases
	if(!Q_stricmp(STACKPTR(file->qp_name_ptr), "qagame") &&
			file->qp_ext_ptr && !Q_stricmp(STACKPTR(file->qp_ext_ptr), "qvm") &&
			file->qp_dir_ptr && !Q_stricmp(STACKPTR(file->qp_dir_ptr), "vm")) return;
	if(file->qp_dir_ptr && !Q_stricmp(STACKPTR(file->qp_dir_ptr), "levelshots")) return;

	// Initialize referenced_paks if it isn't already
	if(!referenced_paks.bucket_count) {
		fs_hashtable_initialize(&referenced_paks, 32); }

	// Add the reference
	if(reference_set_add(&referenced_paks, STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))) {
		if(fs_debug_references->integer) {
			char temp[FS_FILE_BUFFER_SIZE];
			fs_file_to_buffer(file, temp, sizeof(temp), qtrue, qtrue, qtrue, qtrue);
			Com_Printf("recording reference: %s\n", temp); } } }

void FS_ClearPakReferences( int flags ) {
	if(fs_debug_references->integer) Com_Printf("clearing referenced paks\n");
	fs_hashtable_reset(&referenced_paks, 0); }

const char *FS_ReferencedPakNames( void ) {
	// This is just used for a certain debug command now
	static char buffer[1000];
	reference_list_t reference_list;
	reference_set_to_reference_list(&referenced_paks, &reference_list);
	sort_reference_list(&reference_list, qfalse);
	reference_list_to_buffer(&reference_list, buffer, sizeof(buffer), reference_name_string);
	free_reference_list(&reference_list);
	return buffer; }

const char *FS_ReferencedPakPureChecksums( void ) {
	// Returns a space separated string containing the pure checksums of all referenced pk3 files.
	// Servers with sv_pure set will get this string back from clients for pure validation
	// The string has a specific order, "cgame ui @ ref1 ref2 ref3 ..."
	static char buffer[1000];
	build_pure_validation_string(buffer, sizeof(buffer), &referenced_paks);
	return buffer; }

/* ******************************************************************************** */
// Server Download Paks
/* ******************************************************************************** */

static reference_set_t download_paks;

static qboolean is_pak_downloadable(fsc_file_direct_t *pak) {
	char pak_name[FSC_MAX_QPATH];
	fsc_stream_t stream = {pak_name, 0, sizeof(pak_name), 0};
	reference_name_string(pak, &stream);

	// Don't put downloads from outside basegame and the current mod dir in the download list
	// unless specifically enabled, because it will cause problems for clients using old filesystem
	if(!fs_reference_inactive_mods->integer) {
		const char *mod_dir = fsc_get_mod_dir((fsc_file_t *)pak, &fs);
		if(Q_stricmp(mod_dir, com_basegame->string) && Q_stricmp(mod_dir, FS_GetCurrentGameDir()) &&
				Q_stricmp(mod_dir, "basemod")) return qfalse; }

	// Don't put paks that fail the id pak check in download list because clients won't download
	// them anyway and may throw an error
#ifndef STANDALONE
	if(FS_idPak(pak_name, BASEGAME, FS_NODOWNLOAD_PAKS) || FS_idPak(pak_name, BASETA, FS_NODOWNLOAD_PAKS_TEAMARENA)) return qfalse;
#endif

	return qtrue; }

static void add_download_pak(fsc_file_direct_t *pak) {
	if(!is_pak_downloadable(pak)) return;
	reference_set_add(&download_paks, pak); }

void fs_set_download_list(void) {
	// Called by server to set "sv_referencedPaks" and "sv_referencedPakNames"
	// Also updates the download_paks structure above which is used to match
	//    path string from the client to the actual pk3 path

	// Clear out old references
	if(download_paks.bucket_count) fs_hashtable_reset(&download_paks, 0);
	else fs_hashtable_initialize(&download_paks, 32);

	// Add current referenced paks
	{	fs_hashtable_iterator_t it = fs_hashtable_iterate(&referenced_paks, 0, qtrue);
		reference_set_entry_t *entry;
		while((entry = fs_hashtable_next(&it))) {
			add_download_pak(entry->pak); } }

	// Add all paks from current mod directory
	if(*current_mod_dir) {
		int i;
		fsc_hashtable_iterator_t hti;
		fsc_pk3_hash_map_entry_t *hash_entry;
		fsc_stackptr_t mod_ptr = fsc_string_repository_getstring(current_mod_dir, 0, &fs.string_repository, &fs.general_stack);

		if(mod_ptr) for(i=0; i<fs.pk3_hash_lookup.bucket_count; ++i) {
			fsc_hashtable_open(&fs.pk3_hash_lookup, i, &hti);
			while((hash_entry = STACKPTR(fsc_hashtable_next(&hti)))) {
				fsc_file_direct_t *pak = STACKPTR(hash_entry->pk3);
				if(!fsc_is_file_enabled((fsc_file_t *)pak, &fs)) continue;
				if(pak->qp_mod_ptr != mod_ptr) continue;
				add_download_pak(pak); } } }

	// Add cgame and ui paks
	{	const fsc_file_t *cgame_file = fs_general_lookup("vm/cgame.qvm", qtrue, qfalse, qfalse);
		const fsc_file_t *ui_file = fs_general_lookup("vm/ui.qvm", qtrue, qfalse, qfalse);
		if(cgame_file && cgame_file->sourcetype == FSC_SOURCETYPE_PK3) {
			add_download_pak(STACKPTR(((fsc_file_frompk3_t *)cgame_file)->source_pk3)); }
		if(ui_file && ui_file->sourcetype == FSC_SOURCETYPE_PK3) {
			add_download_pak(STACKPTR(((fsc_file_frompk3_t *)ui_file)->source_pk3)); } }

	// Generate and set the reference strings
	{	reference_list_t reference_list;
		char reference_hashes[1000];
		char reference_names[1000];

		reference_set_to_reference_list(&download_paks, &reference_list);
		sort_reference_list(&reference_list, qfalse);
		reference_list_to_buffer(&reference_list, reference_hashes, sizeof(reference_hashes), reference_hash_string);
		reference_list_to_buffer(&reference_list, reference_names, sizeof(reference_names), reference_name_string);
		free_reference_list(&reference_list);

		Cvar_Set("sv_referencedPaks", reference_hashes);
		Cvar_Set("sv_referencedPakNames", reference_names); } }

fileHandle_t fs_open_download_pak(const char *path, unsigned int *size_out) {
	// Used by the server UDP download system to open paks instead of the regular
	//    read functions, because the download path may not correspond exactly
	//    to the filesystem path for various reasons
	// Also uses a direct read handle instead of the normal read method to avoid
	//    lagging the server by reading a big pk3 all at once
	char buffer[FSC_MAX_QPATH];
	fs_hashtable_iterator_t it = fs_hashtable_iterate(&download_paks, 0, qtrue);
	reference_set_entry_t *entry;

	// Search all the current referenced paks to find one with a matching path
	while((entry = fs_hashtable_next(&it))) {
		fsc_stream_t stream = {buffer, 0, sizeof(buffer), 0};
		reference_name_string(entry->pak, &stream);
		fsc_stream_append_string(&stream, ".pk3");
		if(!Q_stricmp(stream.data, path)) {
			// Found match
			return fs_direct_read_handle_open((fsc_file_t *)entry->pak, 0, size_out); } }

	return 0; }

/* ******************************************************************************** */
// Loaded Pak Functions
/* ******************************************************************************** */

static void get_loaded_paks(reference_list_t *reference_list) {
	// Generate sorted list of all loaded paks (not just referenced)
	// Provide an uninitialized reference list for output
	int i;
	fs_hashtable_t loaded_paks;
	fsc_hashtable_iterator_t hti;
	fsc_pk3_hash_map_entry_t *hash_entry;

	fs_hashtable_initialize(&loaded_paks, 256);

	for(i=0; i<fs.pk3_hash_lookup.bucket_count; ++i) {
		fsc_hashtable_open(&fs.pk3_hash_lookup, i, &hti);
		while((hash_entry = STACKPTR(fsc_hashtable_next(&hti)))) {
			fsc_file_direct_t *pk3 = STACKPTR(hash_entry->pk3);
			if(!fsc_is_file_enabled((fsc_file_t *)pk3, &fs)) continue;
			if(fs_inactive_mod_file_disabled((fsc_file_t *)pk3, fs_search_inactive_mods->integer)) continue;
			reference_set_add(&loaded_paks, pk3); } }

	reference_set_to_reference_list(&loaded_paks, reference_list);
	sort_reference_list(reference_list, qfalse);
	reference_set_free(&loaded_paks); }

const char *FS_LoadedPakChecksums( void ) {
	// Returns a space separated string containing the checksums of all loaded pk3 files.
	// Servers with sv_pure set will get this string and pass it to clients.
	// Returns NULL if list overflowed.
	static char buffer[BIG_INFO_STRING];
	qboolean overflow = qfalse;
	reference_list_t reference_list;
	get_loaded_paks(&reference_list);
	if(!reference_list_to_buffer(&reference_list, buffer, sizeof(buffer), reference_hash_string)) overflow = qtrue;
	free_reference_list(&reference_list);
	return overflow ? 0 : buffer; }

const char *FS_LoadedPakNames( void ) {
	// Returns a space separated string containing the names of all loaded pk3 files.
	// Servers with sv_pure set will get this string and pass it to clients.
	// Returns NULL if list overflowed.
	static char buffer[BIG_INFO_STRING];
	qboolean overflow = qfalse;
	reference_list_t reference_list;
	get_loaded_paks(&reference_list);
	if(!reference_list_to_buffer(&reference_list, buffer, sizeof(buffer), reference_name_string)) overflow = qtrue;
	free_reference_list(&reference_list);
	return overflow ? 0 : buffer; }

#endif	// NEW_FILESYSTEM
