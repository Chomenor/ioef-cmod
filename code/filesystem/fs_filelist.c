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
	const char *filter;
	int flags;
} filelist_query_t;

typedef struct {
	const char *extension;
	const char *filter;
	int flags;

	int crop_length;
	fsc_stack_t temp_stack;

	// Depth is max number of slash-separated sections allowed in output
	// i.e. depth=0 suppresses any output, depth=1 allows "file" and "dir1/", depth=2 allows "dir1/file" and "dir1/dir2/"
	// Direct depth applies to files on disk (outside of pk3s), general applies to files in pk3s
	int general_file_depth;
	int general_directory_depth;
	int direct_file_depth;
	int direct_directory_depth;
} filelist_work_t;

// Treat pk3dirs the same as pk3s here
#define DIRECT_NON_PK3DIR(file) (file->sourcetype == FSC_SOURCETYPE_DIRECT && !((fsc_file_direct_t *)file)->pk3dir_ptr)

/* ******************************************************************************** */
// Sort key handling
/* ******************************************************************************** */

typedef struct {
	int length;
	char key[];
} file_list_sort_key_t;

static file_list_sort_key_t *generate_sort_key(const fsc_file_t *file, fsc_stack_t *stack, const filelist_work_t *flw) {
	char buffer[1024];
	fsc_stream_t stream = {buffer, 0, sizeof(buffer), 0};
	file_list_sort_key_t *key;

	fs_generate_core_sort_key(file, &stream, (flw->flags & FLISTFLAG_IGNORE_PURE_LIST) ? qfalse : qtrue);

	key = (file_list_sort_key_t *)FSC_STACK_RETRIEVE(stack, fsc_stack_allocate(stack, sizeof(*key) + stream.position), 0);
	key->length = stream.position;
	fsc_memcpy(key->key, stream.data, stream.position);
	return key; }

static int compare_sort_keys(file_list_sort_key_t *key1, file_list_sort_key_t *key2) {
	return fsc_memcmp(key2->key, key1->key, key1->length < key2->length ? key1->length : key2->length); }

/* ******************************************************************************** */
// String processing functions
/* ******************************************************************************** */

static qboolean fs_pattern_match(const char *string, const char *pattern, qboolean initial_wildcard) {
	// Returns qtrue if string matches pattern containing '*' and '?' wildcards
	// Set initial_wildcard to qtrue to process pattern as if the first character was an asterisk
	while(1) {
		if(*pattern == '*' || initial_wildcard) {
			// Skip asterisks; auto match if no pattern remaining
			char lwr, upr;
			while(*pattern == '*') ++pattern;
			if(!*pattern) return qtrue;

			// Get 'lwr' and 'upr' versions of next char in pattern for fast comparison
			lwr = tolower(*pattern);
			upr = toupper(*pattern);

			// Read string looking for match with remaining pattern
			while(*string) {
				if(*string == lwr || *string == upr || *pattern == '?') {
					if(fs_pattern_match(string+1, pattern+1, qfalse)) return qtrue; }
				++string; }

			// Leftover pattern with no match
			return qfalse; }

		// Check for end of string cases
		if(!*pattern) {
			if(!*string) return qtrue;
			return qfalse; }
		if(!*string) return qfalse;

		// Check for character discrepancy
		if(*pattern != *string && *pattern != '?' && tolower(*pattern) != tolower(*string)) return qfalse;

		// Advance strings
		++pattern;
		++string; } }

static void sanitize_path_separators(const char *source, char *target, int target_size) {
	// Sanitize os-specific path separator content (like ./ or //) out of the path string
	int target_index = 0;
	qboolean slash_mode = qfalse;

	while(*source) {
		char current = *(source++);
		if(current == '\\') current = '/';

		// Defer writing slashes until a valid character is encountered
		if(current == '/') {
			slash_mode = qtrue;
			continue; }

		// Ignore periods that are followed by slashes or end of string
		if(current == '.' && (*source == '/' || *source == '\\' || *source == '\0')) continue;

		// Write out deferred slashes unless at the beginning of path
		if(slash_mode) {
			slash_mode = qfalse;
			if(target_index) {
				if(target_index + 2 >= target_size) break;
				target[target_index++] = '/'; } }

		// Write character
		if(target_index + 1 >= target_size) break;
		target[target_index++] = current; }

	target[target_index] = 0; }

static qboolean strip_trailing_slash(const char *source, char *target, int target_size) {
	// Removes single trailing slash from path; returns qtrue if found
	int length;
	Q_strncpyz(target, source, target_size);
	length = strlen(target);
	if(length && (target[length-1] == '/' || target[length-1] == '\\')) {
		target[length-1] = 0;
		return qtrue; }
	return qfalse; }

/* ******************************************************************************** */
// File list generation
/* ******************************************************************************** */

typedef struct {
	fs_hashtable_entry_t hte;
	char *string;
	const fsc_file_t *file;
	qboolean directory;
	file_list_sort_key_t *file_sort_key;
} temp_file_set_entry_t;

static char *allocate_string(const char *string) {
	int length = strlen(string);
	char *copy = (char *)Z_Malloc(length + 1);
	Com_Memcpy(copy, string, length);
	copy[length] = 0;
	return copy; }

static void temp_file_set_insert(fs_hashtable_t *ht, const fsc_file_t *file, const char *path,
			qboolean directory, file_list_sort_key_t **sort_key_ptr, filelist_work_t *flw) {
	// Loads a path/file combination into the file set, displacing lower precedence entries if needed
	unsigned int hash = fsc_string_hash(path, 0);
	fs_hashtable_iterator_t it = fs_hashtable_iterate(ht, hash, qfalse);
	temp_file_set_entry_t *entry;

	if(ht->element_count >= MAX_FOUND_FILES) return;

	// Generate sort key if one was not already created for this file
	if(!*sort_key_ptr) *sort_key_ptr = generate_sort_key(file, &flw->temp_stack, flw);

	while((entry = (temp_file_set_entry_t *)fs_hashtable_next(&it))) {
		// Check if file is match
		if(strcmp(path, entry->string)) continue;

		// Matching file found - replace it if new file is higher precedence
		if(compare_sort_keys(*sort_key_ptr, entry->file_sort_key) < 0) {
			entry->file = file;
			entry->directory = directory;
			entry->file_sort_key = *sort_key_ptr; }
		return; }

	// No matching file - create new entry
	entry = (temp_file_set_entry_t *)Z_Malloc(sizeof(*entry));
	entry->file = file;
	entry->directory = directory;
	entry->file_sort_key = *sort_key_ptr;
	entry->string = allocate_string(path);
	fs_hashtable_insert(ht, &entry->hte, hash); }

static qboolean check_path_enabled(fsc_stream_t *stream, const filelist_work_t *flw) {
	// Returns qtrue if path stored in stream matches filter/extension criteria, qfalse otherwise
	if(!stream->position) return qfalse;
	if(flw->extension) {
		if(!fs_pattern_match(stream->data, flw->extension, qtrue)) return qfalse; }
	if(flw->filter) {
		if(!Com_FilterPath((char *)flw->filter, stream->data, qfalse)) return qfalse; }
	return qtrue; }

static qboolean check_file_enabled(const fsc_file_t *file, const filelist_work_t *flw) {
	// Returns qtrue if file is valid to use, qfalse otherwise
	int disabled_checks = FD_CHECK_FILE_ENABLED|FD_CHECK_LIST_INACTIVE_MODS|FD_CHECK_LIST_SERVERCFG_LIMIT;
	if(!(flw->flags & FLISTFLAG_IGNORE_PURE_LIST) &&
			!((flw->flags & FLISTFLAG_PURE_ALLOW_DIRECT_SOURCE) && file->sourcetype == FSC_SOURCETYPE_DIRECT)) {
		disabled_checks |= FD_CHECK_PURE_LIST; }
	if(fs_file_disabled(file, disabled_checks)) return qfalse;
	if((flw->flags & FLISTFLAG_IGNORE_TAPAK0) && file->sourcetype == FSC_SOURCETYPE_PK3 &&
			fsc_get_base_file(file, &fs)->pk3_hash == 2430342401u) return qfalse;
	return qtrue; }

static void cut_stream(const fsc_stream_t *source, fsc_stream_t *target, int start_pos, int end_pos) {
	target->position = 0;
	if(start_pos < end_pos) fsc_write_stream_data(target, source->data + start_pos, end_pos - start_pos);
	fsc_stream_append_string(target, ""); }

static void temp_file_set_populate(const fsc_directory_t *base, fs_hashtable_t *output, filelist_work_t *flw) {
	char path_buffer[FS_FILE_BUFFER_SIZE];
	fsc_stream_t path_stream = {path_buffer, 0, sizeof(path_buffer), 0};
	char string_buffer[FS_FILE_BUFFER_SIZE];
	fsc_stream_t string_stream = {string_buffer, 0, sizeof(string_buffer), 0};
	fsc_file_t *file;
	fsc_directory_t *directory;

	file = (fsc_file_t *)STACKPTRN(base->sub_file);
	while(file) {
		if(check_file_enabled(file, flw)) {
			int directory_depth = DIRECT_NON_PK3DIR(file) ? flw->direct_directory_depth : flw->general_directory_depth;
			int file_depth = DIRECT_NON_PK3DIR(file) ? flw->direct_file_depth : flw->general_file_depth;
			int i, j;
			file_list_sort_key_t *sort_key = 0;
			int depth = 0;

			// Generate file and directory strings for each file, and call temp_file_set_insert
			// For example, a file with post-crop_length string "abc/def/temp.txt" will generate:
			// - file string "abc/def/temp.txt" if file depth >= 3
			// - if the file is in a pk3, "abc/" if dir depth >= 1, and "abc/def/" if dir depth >= 2
			// - if file is on disk, ["abc", ".", ".."] if dir depth >= 1, ["abc/def", "abc/.", "abc/.."]
			//       if dir depth >= 2, and ["abc/def/.", "abc/def/.."] if dir depth >= 3

			path_stream.position = 0;
			fs_file_to_stream(file, &path_stream, qfalse, qfalse, qfalse, qfalse);
			for(i=flw->crop_length; i<path_stream.position; ++i) {
				if(path_stream.data[i] == '/') {
					depth++;
					if(depth <= directory_depth) {
						// Process directory
						cut_stream(&path_stream, &string_stream, flw->crop_length, i);
						// Include trailing slash unless directory is from disk, as per original filesystem behavior
						if(!DIRECT_NON_PK3DIR(file)) fsc_stream_append_string(&string_stream, "/");
						if(check_path_enabled(&string_stream, flw)) {
							temp_file_set_insert(output, file, string_stream.data, qtrue, &sort_key, flw); } } }

				// Generate "." and ".." entries for directories from disk
				if(DIRECT_NON_PK3DIR(file) && (i == flw->crop_length || path_stream.data[i] == '/')
						&& depth < directory_depth) {
					cut_stream(&path_stream, &string_stream, flw->crop_length, i);
					if(i != flw->crop_length) fsc_stream_append_string(&string_stream, "/");
					for(j=0; j<2; ++j) {
						fsc_stream_append_string(&string_stream, ".");
						if(check_path_enabled(&string_stream, flw)) {
							temp_file_set_insert(output, file, string_stream.data, qtrue, &sort_key, flw); } } } }

			if(depth < file_depth) {
				// Process file
				cut_stream(&path_stream, &string_stream, flw->crop_length, path_stream.position);
				if(check_path_enabled(&string_stream, flw)) {
					temp_file_set_insert(output, file, string_stream.data, qfalse, &sort_key, flw); } } }

		file = (fsc_file_t *)STACKPTRN(file->next_in_directory); }

	// Process subdirectories
	directory = (fsc_directory_t *)STACKPTRN(base->sub_directory);
	while(directory) {
		temp_file_set_populate(directory, output, flw);
		directory = (fsc_directory_t *)STACKPTRN(directory->peer_directory); } }

static int temp_file_list_compare_string(const temp_file_set_entry_t *element1, const temp_file_set_entry_t *element2) {
	char buffer1[FS_FILE_BUFFER_SIZE];
	char buffer2[FS_FILE_BUFFER_SIZE];
	fsc_stream_t stream1 = {buffer1, 0, sizeof(buffer1), qfalse};
	fsc_stream_t stream2 = {buffer2, 0, sizeof(buffer2), qfalse};
	// Use shorter-path-first mode for sorting directories, as it is generally better and more consistent
	//    with original filesystem behavior
	fs_write_sort_string(element1->string, &stream1, element1->directory ? qtrue : qfalse);
	fs_write_sort_string(element2->string, &stream2, element2->directory ? qtrue : qfalse);
	return fsc_memcmp(stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position); }

static int temp_file_list_compare_element(const temp_file_set_entry_t *element1, const temp_file_set_entry_t *element2) {
	if(element1->file != element2->file) {
		int sort_result = compare_sort_keys(element1->file_sort_key, element2->file_sort_key);
		if(sort_result) return sort_result; }
	return temp_file_list_compare_string(element1, element2); }

static int temp_file_list_qsort(const void *element1, const void *element2) {
	return temp_file_list_compare_element(*(const temp_file_set_entry_t **)element1,
			*(const temp_file_set_entry_t **)element2); }

static char **temp_file_set_to_file_list(fs_hashtable_t *file_set, int *numfiles_out, filelist_work_t *flw) {
	int i;
	fs_hashtable_iterator_t it;
	temp_file_set_entry_t *entry;
	int position = 0;
	char **output = (char **)Z_Malloc(sizeof(*output) * (file_set->element_count + 1));
	temp_file_set_entry_t **temp_list = (temp_file_set_entry_t **)Z_Malloc(sizeof(*temp_list) * file_set->element_count);

	// Transfer entries from file set hashtable to temporary list
	it = fs_hashtable_iterate(file_set, 0, qtrue);
	while((entry = (temp_file_set_entry_t *)fs_hashtable_next(&it))) {
		if(position >= file_set->element_count) {
			Com_Error(ERR_FATAL, "fs_filelist.c->temp_file_set_to_file_list element_count overflow"); }
		temp_list[position++] = entry; }
	if(position != file_set->element_count) {
		Com_Error(ERR_FATAL, "fs_filelist.c->temp_file_set_to_file_list element_count underflow"); }

	// Sort the list
	qsort(temp_list, file_set->element_count, sizeof(*temp_list), temp_file_list_qsort);

	// Transfer strings from list to output array
	for(i=0; i<file_set->element_count; ++i) {
		output[i] = temp_list[i]->string; }
	output[i] = 0;

	if(numfiles_out) *numfiles_out = file_set->element_count;
	Z_Free(temp_list);
	return output; }

/* ******************************************************************************** */
// Main file list function (list_files)
/* ******************************************************************************** */

static fsc_directory_t *get_start_directory(const char *path) {
	// Path can be null to start at base directory
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t directory_ptr;
	fsc_directory_t *directory = 0;

	// Look for directory entry
	fsc_hashtable_open(&fs.directories, path ? fsc_string_hash(path, 0) : 0, &hti);
	while((directory_ptr = fsc_hashtable_next(&hti))) {
		directory = (fsc_directory_t *)STACKPTR(directory_ptr);
		if(path) {
			if(!Q_stricmp((const char *)STACKPTR(directory->qp_dir_ptr), path)) break;}
		else {
			if(directory->qp_dir_ptr == 0) break; } }

	if(!directory_ptr) return 0;
	return directory; }

static void filelist_debug_print_flags(int flags) {
	if(flags) {
		char buffer[256];
		fsc_stream_t stream = {buffer, 0, sizeof(buffer), 0};
		const char *flag_strings[3] = {0};

		flag_strings[0] = (flags & FLISTFLAG_IGNORE_TAPAK0) ? "ignore_tapak0" : 0;
		flag_strings[1] = (flags & FLISTFLAG_IGNORE_PURE_LIST) ? "ignore_pure_list" : 0;
		flag_strings[2] = (flags & FLISTFLAG_PURE_ALLOW_DIRECT_SOURCE) ? "pure_allow_direct_source" : 0;
		fs_comma_separated_list(flag_strings, ARRAY_LEN(flag_strings), &stream);

		FS_DPrintf("flags: %i (%s)\n", flags, buffer); }
	else {
		FS_DPrintf("flags: <none>\n"); } }

static char **list_files(const char *path, int *numfiles_out, filelist_query_t *query) {
	char path_buffer1[FSC_MAX_QPATH];
	char path_buffer2[FSC_MAX_QPATH];
	fsc_directory_t *start_directory = 0;
	fs_hashtable_t temp_file_set;
	filelist_work_t flw;
	char **result;
	int start_time = 0;
	int special_depth = 0;	// Account for certain depth-increasing quirks in original filesystem

	if(fs_debug_filelist->integer) {
		start_time = Sys_Milliseconds();
		FS_DPrintf("********** file list query **********\n");
		fs_debug_indent_start();
		FS_DPrintf("path: %s\n", path);
		FS_DPrintf("extension: %s\n", query->extension);
		FS_DPrintf("filter: %s\n", query->filter);
		filelist_debug_print_flags(query->flags); }

	// Initialize temp structures
	Com_Memset(&flw, 0, sizeof(flw));
	flw.extension = query->extension;
	flw.filter = query->filter;
	flw.flags = query->flags;
	fsc_stack_initialize(&flw.temp_stack);
	fs_hashtable_initialize(&temp_file_set, MAX_FOUND_FILES);

	// Determine start directory
	if(path) {
		if(strip_trailing_slash(path, path_buffer1, sizeof(path_buffer1))) {
			++special_depth; }
		sanitize_path_separators(path_buffer1, path_buffer2, sizeof(path_buffer2));
		if(*path_buffer2) {
			start_directory = get_start_directory(path_buffer2); }
		else {
			start_directory = get_start_directory(0);
			++special_depth; } }

	if(start_directory) {
		// Determine depths
		int extension_length;
		if(flw.filter) {
			// Unlimited depth in filter mode
			flw.general_file_depth = flw.general_directory_depth = 256;
			flw.direct_file_depth = flw.direct_directory_depth = 256; }
		else if(!Q_stricmp(flw.extension, "/")) {
			// This extension is handled specially by the original filesystem (via Sys_ListFiles)
			// Do a directory-only query, but skip the extension check because directories in this
			//    mode can be generated without the trailing slash
			flw.general_directory_depth = 1 + special_depth;
			flw.direct_directory_depth = 1;
			flw.extension = 0; }
		else {
			// Roughly emulate original filesystem depth behavior
			flw.general_file_depth = 2 + special_depth;
			flw.general_directory_depth = 1 + special_depth;
			flw.direct_file_depth = 1; }

		// Optimization to skip processing path types blocked by extension anyway
		extension_length = flw.extension ? strlen(flw.extension) : 0;
		if(extension_length) {
			if(flw.extension[extension_length-1] == '/') {
				flw.general_file_depth = flw.direct_file_depth = 0; }
			else if(flw.extension[extension_length-1] != '?' && flw.extension[extension_length-1] != '*') {
				flw.general_directory_depth = flw.direct_directory_depth = 0; } }

		// Disable non-direct files when emulating OS-specific behavior that would restrict
		//    output to direct files on original filesystem
		// NOTE: Consider restricting general depths to match direct depths in these cases instead of
		//    disabling them entirely?
		if(Q_stricmp(path_buffer1, path_buffer2)) {
			if(fs_debug_filelist->integer) FS_DPrintf("NOTE: Restricting to direct files only due to OS-specific"
					" path separator conversion: original(%s) converted(%s)\n", path_buffer1, path_buffer2);
			flw.general_file_depth = flw.general_directory_depth = 0; }
		if(flw.extension && (strchr(flw.extension, '*') || strchr(flw.extension, '?'))) {
			if(fs_debug_filelist->integer) FS_DPrintf("NOTE: Restricting to direct files only due to OS-specific"
					" extension wildcards\n");
			flw.general_file_depth = flw.general_directory_depth = 0; }

		// Debug print depths
		if(fs_debug_filelist->integer) {
			FS_DPrintf("depths: gf(%i) gd(%i) df(%i) dd(%i)\n", flw.general_file_depth, flw.general_directory_depth,
					flw.direct_file_depth, flw.direct_directory_depth); }

		// Determine prefix length
		if(!flw.filter && start_directory->qp_dir_ptr) {
			flw.crop_length = strlen((const char *)STACKPTR(start_directory->qp_dir_ptr)) + 1; }

		// Populate file set
		temp_file_set_populate(start_directory, &temp_file_set, &flw); }
	else if(fs_debug_filelist->integer) FS_DPrintf("NOTE: Failed to match start directory.\n");

	// Generate file list
	result = temp_file_set_to_file_list(&temp_file_set, numfiles_out, &flw);

	if(fs_debug_filelist->integer) {
		FS_DPrintf("result: %i elements\n", temp_file_set.element_count);
		FS_DPrintf("temp stack usage: %u\n", fsc_stack_get_export_size(&flw.temp_stack));
		FS_DPrintf("time: %i\n", Sys_Milliseconds() - start_time);
		fs_debug_indent_stop(); }

	fs_hashtable_free(&temp_file_set, 0);
	fsc_stack_free(&flw.temp_stack);
	return result; }

void FS_FreeFileList( char **list ) {
	int i;
	if(!list ) return;

	for(i=0; list[i]; i++) {
		Z_Free(list[i]); }

	Z_Free(list); }

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

	file = (fsc_file_t *)STACKPTRN(base->sub_file);
	while(file) {
		if(file->sourcetype == FSC_SOURCETYPE_DIRECT && fsc_is_file_enabled(file, &fs)) {
			const char *mod_dir = fsc_get_mod_dir(file, &fs);
			if(mod_dir != last_mod_dir) {
				add_mod_dir_to_list(list, fsc_get_mod_dir(file, &fs));
				last_mod_dir = mod_dir; } }

		file = (fsc_file_t *)STACKPTRN(file->next_in_directory); }

	directory = (fsc_directory_t *)STACKPTRN(base->sub_directory);
	while(directory) {
		generate_mod_dir_list2(directory, list);
		directory = (fsc_directory_t *)STACKPTRN(directory->peer_directory); } }

static int mod_list_qsort(const void *element1, const void *element2) {
	return Q_stricmp(*(const char **)element1, *(const char **)element2); }

static void generate_mod_dir_list(mod_dir_list_t *list) {
	list->count = 0;
	generate_mod_dir_list2(get_start_directory(0), list);
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
// External file list functions
/* ******************************************************************************** */

char **FS_FlagListFilteredFiles(const char *path, const char *extension, const char *filter,
		int *numfiles_out, int flags) {
	// path, extension, filter, and numfiles_out may be null
	filelist_query_t query = {extension, filter, flags};
	return list_files(path, numfiles_out, &query); }

char **FS_ListFiles( const char *path, const char *extension, int *numfiles ) {
	// path, extension, and numfiles may be null
	return FS_FlagListFilteredFiles( path, extension, NULL, numfiles, 0 ); }

int	FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize ) {
	// path and extension may be null
	int i;
	int flags = 0;
	int nFiles = 0;
	int nTotal = 0;
	int nLen;
	char **pFiles = NULL;
	FSC_ASSERT(listbuf);

	*listbuf = 0;

	if(!Q_stricmp(path, "$modlist")) {
		return FS_GetModList(listbuf, bufsize); }

	if(!Q_stricmp(path, "demos")) {
		// Check for new demos before displaying the UI demo menu
		fs_auto_refresh(); }

	if(!Q_stricmp(path, "models/players") && extension && !Q_stricmp(extension, "/")
			&& Q_stricmp(current_mod_dir, BASETA)) {
		// Special case to block missionpack pak0.pk3 models from the standard non-TA model list
		// which doesn't handle their skin setting correctly
		flags |= FLISTFLAG_IGNORE_TAPAK0; }

	{
		filelist_query_t query = {extension, 0, flags};
		pFiles = list_files(path, &nFiles, &query);
	}

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
