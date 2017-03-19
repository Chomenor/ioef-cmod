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

// Limit max files returned by searches to avoid Z_Malloc overflows
#define	MAX_FOUND_FILES	32768

typedef struct {
	const char *extension;
	int extension_length;
	const char *filter;
	qboolean use_pure_list;
	int search_inactive_mods;
} filelist_query_t;

/* ******************************************************************************** */
// Sort key handling
/* ******************************************************************************** */

typedef struct {
	int length;
	char key[];
} file_list_sort_key_t;

static file_list_sort_key_t *generate_sort_key(const fsc_file_t *file, fsc_stack_t *stack, const filelist_query_t *query) {
	char buffer[1024];
	fsc_stream_t stream = {buffer, 0, sizeof(buffer), 0};
	file_list_sort_key_t *key;

	fs_generate_file_sort_key(file, &stream, query->use_pure_list);

	key = fsc_stack_retrieve(stack, fsc_stack_allocate(stack, sizeof(*key) + stream.position));
	key->length = stream.position;
	fsc_memcpy(key->key, stream.data, stream.position);
	return key; }

static int compare_sort_keys(file_list_sort_key_t *key1, file_list_sort_key_t *key2) {
	return fsc_memcmp(key2->key, key1->key, key1->length < key2->length ? key1->length : key2->length); }

/* ******************************************************************************** */
// Phase 1 - Temporary file set handling
/* ******************************************************************************** */

// Currently "directories" are represented by a single file from the directory,
// and only directories with a direct file descendant will be listed. This seems
// to be sufficient for all current usages

typedef struct {
	fs_hashtable_entry_t hte;
	const fsc_file_t *file;
	file_list_sort_key_t *sort_key;
} temp_file_set_entry_t;

typedef struct {
	fs_hashtable_t files;
	fs_hashtable_t directories;
} temp_file_set_t;

static unsigned int temp_file_set_hash(const fsc_file_t *file, qboolean directory) {
	unsigned int hash = fsc_string_hash(STACKPTR(file->qp_dir_ptr), 0);
	if(!directory) hash |= fsc_string_hash(STACKPTR(file->qp_name_ptr), STACKPTR(file->qp_ext_ptr));
	return hash; }

static qboolean filelist_string_match(const char *string1, const char *string2) {
	if(string1 == string2) return qtrue;
	if(!string1 || !string2) return qfalse;
	return Q_stricmp(string1, string2) ? qfalse : qtrue; }

static void temp_file_set_insert(fs_hashtable_t *ht, const fsc_file_t *file, file_list_sort_key_t *sort_key, qboolean directory) {
	if(ht->bucket_count) {
		unsigned int hash = temp_file_set_hash(file, directory);
		fs_hashtable_iterator_t it = fs_hashtable_iterate(ht, hash, qfalse);
		temp_file_set_entry_t *entry;

		while((entry = fs_hashtable_next(&it))) {
			// Check if file is match
			if(!filelist_string_match(STACKPTR(entry->file->qp_dir_ptr), STACKPTR(file->qp_dir_ptr))) continue;
			if(!directory) {
				if(!filelist_string_match(STACKPTR(entry->file->qp_name_ptr), STACKPTR(file->qp_name_ptr))) continue;
				if(!filelist_string_match(STACKPTR(entry->file->qp_ext_ptr), STACKPTR(file->qp_ext_ptr))) continue; }

			// Matching file found - replace it if new file is higher precedence
			if(compare_sort_keys(sort_key, entry->sort_key) < 0) {
				entry->file = file;
				entry->sort_key = sort_key; }
			return; }

		// No matching file - create new entry
		entry = Z_Malloc(sizeof(*entry));
		entry->file = file;
		entry->sort_key = sort_key;
		fs_hashtable_insert(ht, &entry->hte, hash); } }

static qboolean check_filter(fsc_stream_t *stream, const filelist_query_t *query) {
	// Returns qtrue if path stored in stream matches filter/extension criteria, qfalse otherwise
	if(query->extension) {
		if(stream->position < query->extension_length) return qfalse;
		if(Q_stricmpn(stream->data + stream->position - query->extension_length,
				query->extension, query->extension_length)) return qfalse; }
	if(query->filter) {
		if(!Com_FilterPath((char *)query->filter, stream->data, qfalse)) return qfalse; }
	return qtrue; }

static qboolean file_in_server_pak_list(const fsc_file_t *file) {
	if(file->sourcetype == FSC_SOURCETYPE_PK3 && pk3_list_lookup(&connected_server_pk3_list,
			((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash, qfalse)) return qtrue;
	return qfalse; }

static qboolean check_file_enabled(const fsc_file_t *file, const filelist_query_t *query) {
	// Returns qtrue if file is valid to use, qfalse otherwise
	if(!fsc_is_file_enabled(file, &fs)) return qfalse;
	if(connected_server_pure_mode && query->use_pure_list && !file_in_server_pak_list(file)) return qfalse;
	if(fs_inactive_mod_file_disabled(file, query->search_inactive_mods)) return qfalse;
	return qtrue; }

static void temp_file_set_populate(const fsc_directory_t *base, const filelist_query_t *query,
		temp_file_set_t *output, int file_depth, int directory_depth, fsc_stack_t *sort_key_stack) {
	char buffer[1024];
	fsc_stream_t stream = {buffer, 0, 0, qfalse};
	fsc_file_t *file;

	file = STACKPTR(base->sub_file);
	while(file) {
		if(output->directories.element_count + output->files.element_count >= MAX_FOUND_FILES) return;

		if(check_file_enabled(file, query)) {
			file_list_sort_key_t *sort_key = 0;

			if(file_depth > 0 && output->files.bucket_count) {
				stream.position = 0;
				fs_file_to_stream(file, &stream, qfalse, qfalse, qfalse, qfalse);
				if(check_filter(&stream, query)) {
					if(!sort_key) sort_key = generate_sort_key(file, sort_key_stack, query);
					temp_file_set_insert(&output->files, file, sort_key, qfalse); } }

			if(directory_depth > 0 && output->directories.bucket_count && file->qp_dir_ptr) {
				stream.position = 0;
				fsc_stream_append_string(&stream, STACKPTR(file->qp_dir_ptr));
				fsc_stream_append_string(&stream, "/");
				if(check_filter(&stream, query)) {
					if(!sort_key) sort_key = generate_sort_key(file, sort_key_stack, query);
					temp_file_set_insert(&output->directories, file, sort_key, qtrue); } } }

		file = STACKPTR(file->next_in_directory); }

	if(file_depth > 1 || directory_depth > 1) {
		fsc_directory_t *directory = STACKPTR(base->sub_directory);
		while(directory) {
			temp_file_set_populate(directory, query, output, file_depth - 1, directory_depth - 1, sort_key_stack);
			directory = STACKPTR(directory->peer_directory); } } }

static void temp_file_set_initialize(temp_file_set_t *file_set, qboolean search_files, qboolean search_directories) {
	Com_Memset(file_set, 0, sizeof(*file_set));
	if(search_files) fs_hashtable_initialize(&file_set->files, MAX_FOUND_FILES);
	if(search_directories) fs_hashtable_initialize(&file_set->directories, MAX_FOUND_FILES/4); }

static void temp_file_set_free(temp_file_set_t *file_set) {
	fs_hashtable_free(&file_set->files, 0);
	fs_hashtable_free(&file_set->directories, 0); }

/* ******************************************************************************** */
// Phase 2 - Temporary file list handling
/* ******************************************************************************** */

typedef struct {
	const fsc_file_t *file;
	qboolean directory;
	file_list_sort_key_t *sort_key;
} temp_file_list_element_t;

typedef struct {
	temp_file_list_element_t *elements;
	int element_count;
} temp_file_list_t;

static void temp_file_set_to_file_list(temp_file_set_t *source, temp_file_list_t *target) {
	fs_hashtable_iterator_t it;
	const temp_file_set_entry_t *entry;
	int position = 0;

	target->element_count = source->files.element_count + source->directories.element_count;
	target->elements = Z_Malloc(sizeof(*target->elements) * target->element_count);

	it = fs_hashtable_iterate(&source->files, 0, qtrue);
	while((entry = fs_hashtable_next(&it))) {
		target->elements[position].file = entry->file;
		target->elements[position].sort_key = entry->sort_key;
		target->elements[position].directory = qfalse;
		++position; }

	it = fs_hashtable_iterate(&source->directories, 0, qtrue);
	while((entry = fs_hashtable_next(&it))) {
		target->elements[position].file = entry->file;
		target->elements[position].sort_key = entry->sort_key;
		target->elements[position].directory = qtrue;
		++position; } }

static int temp_file_list_compare_element(const temp_file_list_element_t *element1, const temp_file_list_element_t *element2) {
	if(element1->file == element2->file) {
		if(element1->directory) return -1;
		return 1; }
	return compare_sort_keys(element1->sort_key, element2->sort_key); }

static int temp_file_list_qsort(const void *element1, const void *element2) {
	return temp_file_list_compare_element(element1, element2); }

static void temp_file_list_sort(temp_file_list_t *file_list) {
	qsort(file_list->elements, file_list->element_count, sizeof(*file_list->elements), temp_file_list_qsort); }

static void temp_file_list_free(temp_file_list_t *file_list) {
	Z_Free(file_list->elements); }

/* ******************************************************************************** */
// Phase 3 - Output list handling
/* ******************************************************************************** */

static char *list_copystring(const char *string) {
	int length = strlen(string);
	char *copy = Z_Malloc(length + 1);
	Com_Memcpy(copy, string, length);
	copy[length] = 0;
	return copy; }

static char **temp_file_list_to_output_list(temp_file_list_t *temp_list, int prefix_length, int *numfiles_out) {
	char buffer[1024];
	int i;
	char **output = Z_Malloc(sizeof(char *) * (temp_list->element_count + 1));

	for(i=0; i<temp_list->element_count; ++i) {
		fsc_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};
		if(temp_list->elements[i].directory) {
			fsc_stream_append_string(&stream, STACKPTR(temp_list->elements[i].file->qp_dir_ptr));
			fsc_stream_append_string(&stream, "/"); }
		else {
			fs_file_to_stream(temp_list->elements[i].file, &stream, qfalse, qfalse, qfalse, qfalse); }

		if(stream.position < prefix_length) output[i] = CopyString("");
		else output[i] = list_copystring(stream.data + prefix_length); }

	if(numfiles_out) *numfiles_out = temp_list->element_count;
	return output; }

void FS_FreeFileList( char **list ) {
	int i;
	if(!list ) return;

	for(i=0; list[i]; i++) {
		Z_Free(list[i]); }

	Z_Free(list); }

/* ******************************************************************************** */
// Main file list function (FS_ListFilteredFiles)
/* ******************************************************************************** */

static fsc_directory_t *get_start_directory(const char *path, int length) {
	// Path can be null to start at base directory
	char path2[FSC_MAX_QPATH];
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t directory_ptr;
	fsc_directory_t *directory = 0;

	// Make path lowercase and crop to length
	if(path) {
		Q_strncpyz(path2, path, sizeof(path2));
		if(length < sizeof(path2)) path2[length] = 0;
		Q_strlwr(path2); }

	// Look for directory entry
	fsc_hashtable_open(&fs.directories, path ? fsc_string_hash(path2, 0) : 0, &hti);
	while((directory_ptr = fsc_hashtable_next(&hti))) {
		directory = STACKPTR(directory_ptr);
		if(path) {
			if(!Q_stricmp(STACKPTR(directory->qp_dir_ptr), path2)) break;}
		else {
			if(directory->qp_dir_ptr == 0) break; } }

	if(!directory_ptr) return 0;
	return directory; }

static char **FS_ListFilteredFiles2(const char *path, int *numfiles_out, filelist_query_t *query) {
	int path_length = strlen(path);
	fsc_directory_t *start_directory;
	fsc_stack_t sort_key_stack;
	temp_file_set_t temp_file_set;
	temp_file_list_t temp_file_list;
	char **result;
	int start_time = 0;

	if(fs_debug_filelist->integer) {
		start_time = Sys_Milliseconds();
		Com_Printf("********** file list query **********\n");
		Com_Printf("path: %s\nextension: %s\nfilter: %s\n", path, query->extension, query->filter); }

	if(path_length && (path[path_length-1] == '/' || path[path_length-1] == '\\')) --path_length;
	fsc_stack_initialize(&sort_key_stack);

	// Determine start directory
	if(path_length) start_directory = get_start_directory(path, path_length);
	else start_directory = get_start_directory(0, 0);

	if(!start_directory) {
		// Start directory not found; just use an empty file set
		temp_file_set_initialize(&temp_file_set, qfalse, qfalse); }
	else {
		// Initialize file set; optimize by skipping file/directory iteration if blocked by extension
		if(query->extension_length && *query->extension) {
			if(query->extension[query->extension_length-1] == '/') temp_file_set_initialize(&temp_file_set, qfalse, qtrue);
			else temp_file_set_initialize(&temp_file_set, qtrue, qfalse); }
		else temp_file_set_initialize(&temp_file_set, qtrue, qtrue);

		// Populate file set
		temp_file_set_populate(start_directory, query, &temp_file_set, 2, 2, &sort_key_stack); }

	// Generate file list
	temp_file_set_to_file_list(&temp_file_set, &temp_file_list);
	temp_file_set_free(&temp_file_set);
	temp_file_list_sort(&temp_file_list);

	// Generate final output
	result = temp_file_list_to_output_list(&temp_file_list, path_length ? path_length + 1 : 0, numfiles_out);
	temp_file_list_free(&temp_file_list);

	if(fs_debug_filelist->integer) {
		Com_Printf("result: %i elements\n", temp_file_list.element_count);
		Com_Printf("key stack usage: %u\n", fsc_stack_get_export_size(&sort_key_stack));
		Com_Printf("time: %i\n", Sys_Milliseconds() - start_time); }

	fsc_stack_free(&sort_key_stack);
	return result; }

char **FS_ListFilteredFiles(const char *path, const char *extension, const char *filter,
		int *numfiles_out, qboolean allowNonPureFilesOnDisk) {
	filelist_query_t query = {extension, extension ? strlen(extension) : 0, filter,
			!allowNonPureFilesOnDisk, fs_search_inactive_mods->integer};
	return FS_ListFilteredFiles2(path, numfiles_out, &query); }

/* ******************************************************************************** */
// Mod directory listing (FS_GetModList)
/* ******************************************************************************** */

#define MAX_MOD_DIRS 128

typedef struct {
	char *mod_dirs[MAX_MOD_DIRS];
	int count;
} mod_dir_list_t;

static void add_mod_dir_to_list(mod_dir_list_t *list, const char *mod_dir) {
	int i;
	if(list->count >= MAX_MOD_DIRS) return;
	for(i=0; i<list->count; ++i) if(!Q_stricmp(list->mod_dirs[i], mod_dir)) return;
	list->mod_dirs[list->count++] = CopyString(mod_dir); }

static void generate_mod_dir_list2(fsc_directory_t *base, mod_dir_list_t *list) {
	fsc_file_t *file;
	fsc_directory_t *directory;
	const char *last_mod_dir = 0;

	file = STACKPTR(base->sub_file);
	while(file) {
		if(file->sourcetype == FSC_SOURCETYPE_DIRECT && fsc_is_file_enabled(file, &fs)) {
			const char *mod_dir = fsc_get_mod_dir(file, &fs);
			if(mod_dir != last_mod_dir) {
				add_mod_dir_to_list(list, fsc_get_mod_dir(file, &fs));
				last_mod_dir = mod_dir; } }

		file = STACKPTR(file->next_in_directory); }

	directory = STACKPTR(base->sub_directory);
	while(directory) {
		generate_mod_dir_list2(directory, list);
		directory = STACKPTR(directory->peer_directory); } }

static int mod_list_qsort(const void *element1, const void *element2) {
	return Q_stricmp(*(const char **)element1, *(const char **)element2); }

static void generate_mod_dir_list(mod_dir_list_t *list) {
	list->count = 0;
	generate_mod_dir_list2(get_start_directory(0, 0), list);
	qsort(list->mod_dirs, list->count, sizeof(*list->mod_dirs), mod_list_qsort); }

static void mod_list_free(mod_dir_list_t *list) {
	int i;
	for(i=0; i<list->count; ++i) Z_Free(list->mod_dirs[i]); }

static int FS_GetModList(char *listbuf, int bufsize) {
	int i;
	int nTotal = 0;		// Amount of buffer used so far
	int nMods = 0;		// Number of mods
	mod_dir_list_t list;
	char description[49];
	int mod_name_length;
	int description_length;

	generate_mod_dir_list(&list);

	for(i=0; i<list.count; ++i) {
		char *mod_name = list.mod_dirs[i];
		if(!Q_stricmp(mod_name, com_basegame->string)) continue;
		if(!Q_stricmp(mod_name, "basemod")) continue;

		FS_GetModDescription(mod_name, description, sizeof(description));
		mod_name_length = strlen(mod_name) + 1;
		description_length = strlen(description) + 1;

		if(nTotal + mod_name_length + description_length < bufsize) {
			strcpy(listbuf, mod_name);
			listbuf += mod_name_length;
			strcpy(listbuf, description);
			listbuf += description_length;
			nTotal += mod_name_length + description_length;
			++nMods; } }

	mod_list_free(&list);
	return nMods; }

/* ******************************************************************************** */
// Additional file list functions
/* ******************************************************************************** */

char **FS_ListFiles( const char *path, const char *extension, int *numfiles ) {
	return FS_ListFilteredFiles( path, extension, NULL, numfiles, qfalse );
}

int	FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize ) {
	int		nFiles, i, nTotal, nLen;
	char **pFiles = NULL;

	*listbuf = 0;
	nFiles = 0;
	nTotal = 0;

	if (Q_stricmp(path, "$modlist") == 0) {
		return FS_GetModList(listbuf, bufsize);
	}

	if(!Q_stricmp(path, "demos")) {
		// Check for new demos before displaying the UI demo menu
		fs_auto_refresh(); }

	pFiles = FS_ListFiles(path, extension, &nFiles);

	for (i =0; i < nFiles; i++) {
		nLen = strlen(pFiles[i]) + 1;
		if (nTotal + nLen + 1 < bufsize) {
			strcpy(listbuf, pFiles[i]);
			listbuf += nLen;
			nTotal += nLen;
		}
		else {
			nFiles = i;
			break;
		}
	}

	FS_FreeFileList(pFiles);

	return nFiles;
}

#endif	// NEW_FILESYSTEM
