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

/* ******************************************************************************** */
// Hash Table
/* ******************************************************************************** */

void fs_hashtable_initialize(fs_hashtable_t *hashtable, int bucket_count) {
	// Valid for an uninitialized hash table
	hashtable->bucket_count = bucket_count;
	hashtable->buckets = Z_Malloc(sizeof(fs_hashtable_entry_t *) * bucket_count);
	hashtable->element_count = 0; }

void fs_hashtable_insert(fs_hashtable_t *hashtable, fs_hashtable_entry_t *entry, unsigned int hash) {
	// Valid for an initialized hash table
	int index = hash % hashtable->bucket_count;
	entry->next = hashtable->buckets[index];
	hashtable->buckets[index] = entry;
	++hashtable->element_count; }

fs_hashtable_iterator_t fs_hashtable_iterate(fs_hashtable_t *hashtable, unsigned int hash, qboolean iterate_all) {
	// Valid for an initialized or uninitialized (zeroed) hashtable
	fs_hashtable_iterator_t iterator;
	iterator.ht = hashtable;
	if(!hashtable->bucket_count || iterate_all) {
		iterator.current_bucket = 0;
		iterator.bucket_limit = hashtable->bucket_count; }
	else {
		iterator.current_bucket = hash % hashtable->bucket_count;
		iterator.bucket_limit = iterator.current_bucket + 1; }
	iterator.current_entry = 0;
	return iterator; }

void *fs_hashtable_next(fs_hashtable_iterator_t *iterator) {
	fs_hashtable_entry_t *entry = iterator->current_entry;
	while(!entry) {
		if(iterator->current_bucket >= iterator->bucket_limit) return 0;
		entry = iterator->ht->buckets[iterator->current_bucket++]; }
	iterator->current_entry = entry->next;
	return entry; }

static void fs_hashtable_free_entries(fs_hashtable_t *hashtable, void (*free_entry)(fs_hashtable_entry_t *entry)) {
	// Valid for an initialized or uninitialized (zeroed) hashtable
	fs_hashtable_iterator_t it = fs_hashtable_iterate(hashtable, 0, qtrue);
	fs_hashtable_entry_t *entry;
	if(free_entry) while((entry = fs_hashtable_next(&it))) free_entry(entry);
	else while((entry = fs_hashtable_next(&it))) Z_Free(entry); }

void fs_hashtable_free(fs_hashtable_t *hashtable, void (*free_entry)(fs_hashtable_entry_t *entry)) {
	// Valid for an initialized or uninitialized (zeroed) hashtable
	fs_hashtable_free_entries(hashtable, free_entry);
	if(hashtable->buckets) Z_Free(hashtable->buckets);
	Com_Memset(hashtable, 0, sizeof(*hashtable)); }

void fs_hashtable_reset(fs_hashtable_t *hashtable, void (*free_entry)(fs_hashtable_entry_t *entry)) {
	// Valid for an initialized hash table
	fs_hashtable_free_entries(hashtable, free_entry);
	Com_Memset(hashtable->buckets, 0, sizeof(*hashtable->buckets) * hashtable->bucket_count);
	hashtable->element_count = 0; }

/* ******************************************************************************** */
// Pk3 List
/* ******************************************************************************** */

void pk3_list_initialize(pk3_list_t *pk3_list, unsigned int bucket_count) {
	fs_hashtable_initialize(&pk3_list->ht, bucket_count); }

void pk3_list_insert(pk3_list_t *pk3_list, unsigned int hash) {
	pk3_list_entry_t *new_entry = S_Malloc(sizeof(pk3_list_entry_t));
	fs_hashtable_insert(&pk3_list->ht, &new_entry->hte, hash);
	new_entry->hash = hash;
	new_entry->position = pk3_list->ht.element_count; }

int pk3_list_lookup(const pk3_list_t *pk3_list, unsigned int hash, qboolean reverse) {
	fs_hashtable_iterator_t it = fs_hashtable_iterate((fs_hashtable_t *)&pk3_list->ht, hash, qfalse);
	pk3_list_entry_t *entry;
	while((entry = fs_hashtable_next(&it))) {
		if(entry->hash == hash) {
			if(reverse) return pk3_list->ht.element_count - entry->position + 1;
			return entry->position; } }
	return 0; }

void pk3_list_free(pk3_list_t *pk3_list) {
	fs_hashtable_free(&pk3_list->ht, 0); }

/* ******************************************************************************** */
// System pk3 checks
/* ******************************************************************************** */

// These are used to rank paks according to the definitions in fspublic.h

#define PROCESS_PAKS(paks) { \
	int i; \
	unsigned int hashes[] = paks; \
	for(i=0; i<ARRAY_LEN(hashes); ++i) { \
		if(hash == hashes[i]) return i + 1; } }

int system_pk3_position(unsigned int hash) {
	#ifdef FS_SYSTEM_PAKS_TEAMARENA
	if(!Q_stricmp(FS_GetCurrentGameDir(), BASETA)) {
		PROCESS_PAKS(FS_SYSTEM_PAKS_TEAMARENA)
		return 0; }
	#endif
	#ifdef FS_SYSTEM_PAKS
	PROCESS_PAKS(FS_SYSTEM_PAKS)
	#endif
	return 0; }

/* ******************************************************************************** */
// File helper functions
/* ******************************************************************************** */

char *fs_file_extension(const fsc_file_t *file) {
	return STACKPTR(file->qp_ext_ptr); }

qboolean fs_files_from_same_pk3(const fsc_file_t *file1, const fsc_file_t *file2) {
	// Returns qtrue if both files are located in the same pk3, qfalse otherwise
	// Used by renderer for md3 lod handling
	if(!file1 || !file2 || file1->sourcetype != FSC_SOURCETYPE_PK3 || file2->sourcetype != FSC_SOURCETYPE_PK3 ||
			((fsc_file_frompk3_t *)file1)->source_pk3 != ((fsc_file_frompk3_t *)file2)->source_pk3) return qfalse;
	return qtrue; }

int fs_get_source_dir_id(const fsc_file_t *file) {
	if(file->sourcetype == FSC_SOURCETYPE_DIRECT) {
		return ((const fsc_file_direct_t *)file)->source_dir_id; }
	else if (file->sourcetype == FSC_SOURCETYPE_PK3) {
		return ((const fsc_file_direct_t *)STACKPTR(((const fsc_file_frompk3_t *)file)->source_pk3))->source_dir_id; }
	else return -1; }

char *fs_get_source_dir_string(const fsc_file_t *file) {
	int id = fs_get_source_dir_id(file);
	if(id >= 0 && id < FS_SOURCEDIR_COUNT) return fs_sourcedirs[id].name;
	return "unknown"; }

int fs_get_mod_dir_state(const char *mod_dir) {
	// Returns 3 for current mod, 2 for basemod, 1 for basegame, 0 for inactive mod
	if(*current_mod_dir && !Q_stricmp(mod_dir, current_mod_dir)) return 3;
	else if(!Q_stricmp(mod_dir, "basemod")) return 2;
	else if(!Q_stricmp(mod_dir, com_basegame->string)) return 1;
	return 0; }

void fs_file_to_stream(const fsc_file_t *file, fsc_stream_t *stream, qboolean include_source_dir,
			qboolean include_mod, qboolean include_pk3_origin, qboolean include_size) {
	if(include_source_dir) {
		fsc_stream_append_string(stream, fs_get_source_dir_string(file));
		fsc_stream_append_string(stream, "/"); }
	fsc_file_to_stream(file, stream, &fs, include_mod, include_pk3_origin);

	if(include_size) {
		char buffer[20];
		Com_sprintf(buffer, sizeof(buffer), " (%i)", file->filesize);
		fsc_stream_append_string(stream, buffer); } }

void fs_file_to_buffer(const fsc_file_t *file, char *buffer, int buffer_size, qboolean include_source_dir,
			qboolean include_mod, qboolean include_pk3_origin, qboolean include_size) {
	fsc_stream_t stream = {buffer, 0, buffer_size};
	fs_file_to_stream(file, &stream, include_source_dir, include_mod, include_pk3_origin, include_size); }

void fs_print_file_location(const fsc_file_t *file) {
	char name_buffer[FS_FILE_BUFFER_SIZE];
	char source_buffer[FS_FILE_BUFFER_SIZE];
	fs_file_to_buffer(file, name_buffer, sizeof(name_buffer), qfalse, qfalse, qfalse, qfalse);
	if(file->sourcetype == FSC_SOURCETYPE_PK3) {
		fs_file_to_buffer(STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3), source_buffer, sizeof(source_buffer),
				qtrue, qtrue, qfalse, qfalse);
		Com_Printf("File %s found in %s\n", name_buffer, source_buffer); }
	else if(file->sourcetype == FSC_SOURCETYPE_DIRECT) {
		fs_file_to_buffer(file, source_buffer, sizeof(source_buffer), qtrue, qtrue, qfalse, qfalse);
		Com_Printf("File %s found at %s\n", name_buffer, source_buffer); }
	else Com_Printf("File %s has unknown sourcetype\n", name_buffer); }

/* ******************************************************************************** */
// File disabled check
/* ******************************************************************************** */

static qboolean inactive_mod_file_disabled(const fsc_file_t *file, int level) {
	// Check if a file is disabled by inactive mod settings
	if(level < 2) {
		// Look for active mod or basegame match
		if(fs_get_mod_dir_state(fsc_get_mod_dir(file, &fs)) >= 1) return qfalse;

		if(level == 1) {
			// For setting 1, also look for pure list or system pak match
			unsigned int hash = 0;
			if(file->sourcetype == FSC_SOURCETYPE_PK3)
				hash = ((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash;
			if(file->sourcetype == FSC_SOURCETYPE_DIRECT) hash = ((fsc_file_direct_t *)file)->pk3_hash;
			if(hash) {
				if(pk3_list_lookup(&connected_server_pk3_list, hash, qfalse)) return qfalse;
				if(system_pk3_position(hash)) return qfalse; } }
		return qtrue; }
	return qfalse; }

static qboolean file_in_server_pak_list(const fsc_file_t *file) {
	if(file->sourcetype == FSC_SOURCETYPE_PK3 && pk3_list_lookup(&connected_server_pk3_list,
			((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash, qfalse)) return qtrue;
	return qfalse; }

file_disabled_result_t fs_file_disabled(const fsc_file_t *file, int flags) {
	// Check if file is disabled in index (no longer on disk)
	if(!fsc_is_file_enabled(file, &fs)) return FD_RESULT_FILE_INACTIVE;

	// Check if file is disabled by inactive mod dir settings
	if(inactive_mod_file_disabled(file, (flags & FD_FLAG_FILELIST_QUERY) ?
			fs_list_inactive_mods->integer : fs_search_inactive_mods->integer)) return FD_RESULT_INACTIVE_MOD_BLOCKED;

	// Check if file is blocked by connected server pure list
	if((flags & FD_FLAG_CHECK_PURE) && fs_connected_server_pure_state() == 1 && !file_in_server_pak_list(file))
			return FD_RESULT_PURE_LIST_BLOCKED;

	return FD_RESULT_FILE_ENABLED; }

/* ******************************************************************************** */
// File Sorting Functions
/* ******************************************************************************** */

static const unsigned char *get_string_sort_table(void) {
	// The table maps characters to a precedence value
	// higher value = higher precedence
	qboolean initialized = qfalse;
	static unsigned char table[256];

	if(!initialized) {
		int i;
		unsigned char value = 255;
		for(i='z'; i>='a'; --i) table[i] = value--;
		value = 255;
		for(i='Z'; i>='A'; --i) table[i] = value--;
		for(i='9'; i>='0'; --i) table[i] = value--;
		for(i=255; i>=0; --i) if(!table[i]) table[i] = value--;
		initialized = qtrue; }

	return table; }

static unsigned int server_pak_precedence(const fsc_file_t *file) {
	if(file->sourcetype == FSC_SOURCETYPE_PK3) {
		return pk3_list_lookup(&connected_server_pk3_list,
				((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash, qtrue); }
	return 0; }

static unsigned int mod_dir_precedence(int mod_dir_state) {
	if(mod_dir_state >= 2) return mod_dir_state;
	return 0; }

static unsigned int system_pak_precedence(const fsc_file_t *file, int mod_dir_state) {
	if(mod_dir_state <= 1) {
		if(file->sourcetype == FSC_SOURCETYPE_PK3) {
			return system_pk3_position(((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash); }
		if(file->sourcetype == FSC_SOURCETYPE_DIRECT) {
			return system_pk3_position(((fsc_file_direct_t *)file)->pk3_hash); } }
	return 0; }

static unsigned int basegame_dir_precedence(int mod_dir_state) {
	if(mod_dir_state == 1) return 1;
	return 0; }

static void write_sort_string(const char *string, fsc_stream_t *output) {
	const unsigned char *sort_table = get_string_sort_table();
	while(*string && output->position < output->size) {
		output->data[output->position++] = (char)sort_table[*(unsigned char *)(string++)]; }
	if(output->position < output->size) output->data[output->position++] = 0; }

static void write_sort_filename(const fsc_file_t *file, fsc_stream_t *output) {
	char buffer[FS_FILE_BUFFER_SIZE];
	fs_file_to_buffer(file, buffer, sizeof(buffer), qfalse, qfalse, qfalse, qfalse);
	write_sort_string(buffer, output); }

static void write_sort_value(unsigned int value, fsc_stream_t *output) {
	static volatile int test = 1;
	if(*(char *)&test) {
		value = ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0xFF00FF);
		value = (value << 16) | (value >> 16); }
	if(output->position + 3 < output->size) {
		*((unsigned int *)(output->data + output->position)) = value;
		output->position += 4; } }

void fs_generate_file_sort_key(const fsc_file_t *file, fsc_stream_t *output, qboolean use_server_pak_list) {
	// This is a rough version of the lookup precedence for reference and file listing purposes
	int mod_dir_state = fs_get_mod_dir_state(fsc_get_mod_dir(file, &fs));
	if(use_server_pak_list) write_sort_value(server_pak_precedence(file), output);
	write_sort_value(mod_dir_precedence(mod_dir_state), output);
	write_sort_value(system_pak_precedence(file, mod_dir_state), output);
	write_sort_value(basegame_dir_precedence(mod_dir_state), output);
	if(file->sourcetype == FSC_SOURCETYPE_PK3) {
		fsc_file_direct_t *source_pk3 = STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3);
		write_sort_value(1, output);
		write_sort_value((source_pk3->f.flags & FSC_FILEFLAG_DLPK3) ? 0 : 1, output);
		write_sort_filename((fsc_file_t *)source_pk3, output);
		write_sort_value(~((fsc_file_frompk3_t *)file)->header_position, output); }
	else {
		write_sort_value(0, output);
		write_sort_value((file->flags & FSC_FILEFLAG_DLPK3) ? 0 : 1, output); }
	write_sort_filename(file, output);
	write_sort_value(fs_get_source_dir_id(file), output); }

int fs_compare_file(const fsc_file_t *file1, const fsc_file_t *file2, qboolean use_server_pak_list) {
	char buffer1[1024];
	char buffer2[1024];
	fsc_stream_t stream1 = {buffer1, 0, sizeof(buffer1), qfalse};
	fsc_stream_t stream2 = {buffer2, 0, sizeof(buffer2), qfalse};
	fs_generate_file_sort_key(file1, &stream1, use_server_pak_list);
	fs_generate_file_sort_key(file2, &stream2, use_server_pak_list);
	return fsc_memcmp(stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position); }

int fs_compare_file_name(const fsc_file_t *file1, const fsc_file_t *file2) {
	char buffer1[1024];
	char buffer2[1024];
	fsc_stream_t stream1 = {buffer1, 0, sizeof(buffer1), qfalse};
	fsc_stream_t stream2 = {buffer2, 0, sizeof(buffer2), qfalse};
	write_sort_filename(file1, &stream1);
	write_sort_filename(file2, &stream2);
	return fsc_memcmp(stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position); }

/* ******************************************************************************** */
// Misc Functions
/* ******************************************************************************** */

void fs_execute_config_file(const char *name, fs_config_type_t config_type, cbufExec_t exec_type, qboolean quiet) {
	char *data;
	unsigned int size = 0;

	if(com_journalDataFile && com_journal->integer == 2) {
		Com_Printf("execing %s from journal data file\n", name);
		data = fs_read_journal_data();
		if(!data) {
			Com_Printf("couldn't exec %s - not present in journal\n", name);
			return; } }
	else {
		const fsc_file_t *file;
		if(!quiet) Com_Printf("execing %s\n", name);
		fs_auto_refresh();
		file = fs_config_lookup(name, config_type, qfalse);
		if(!file) {
			Com_Printf("couldn't exec %s - file not found\n", name);
			fs_write_journal_data(0, 0);
			return; }
		data = fs_read_data(file, 0, &size);
		if(!data) {
			Com_Printf("couldn't exec %s - failed to read data\n", name);
			fs_write_journal_data(0, 0);
			return; } }

	fs_write_journal_data(data, size);

	Cbuf_ExecuteText(exec_type, data);
	if(exec_type == EXEC_APPEND) Cbuf_ExecuteText(EXEC_APPEND, "\n");
	fs_free_data(data); }

void *fs_load_game_dll(const fsc_file_t *dll_file, intptr_t (QDECL **entryPoint)(int, ...),
			intptr_t (QDECL *systemcalls)(intptr_t, ...)) {
	// Used by vm.c
	// Returns dll handle, or null on error
	char dll_info_string[FS_FILE_BUFFER_SIZE];
	const void *dll_path;
	char *dll_path_string;
	void *dll_handle;

	// Print the info message
	fs_file_to_buffer(dll_file, dll_info_string, sizeof(dll_info_string), qtrue, qtrue, qtrue, qfalse);
	Com_Printf("Attempting to load dll file at %s\n", dll_info_string);

	// Get dll path
	if(dll_file->sourcetype != FSC_SOURCETYPE_DIRECT) {
		// Shouldn't happen
		Com_Printf("Error: selected dll is not direct sourcetype\n");
		return 0; }
	dll_path = STACKPTR(((fsc_file_direct_t *)dll_file)->os_path_ptr);
	dll_path_string = fsc_os_path_to_string(dll_path);
	if(!dll_path_string) {
		// Generally shouldn't happen
		Com_Printf("Error: failed to convert dll path\n");
		return 0; }

	// Attemt to open the dll
	dll_handle = Sys_LoadGameDll(dll_path_string, entryPoint, systemcalls);
	if(!dll_handle) {
		Com_Printf("Error: failed to load game dll\n"); }
	fsc_free(dll_path_string);
	return dll_handle; }

void FS_GetModDescription(const char *modDir, char *description, int descriptionLen) {
	char *descPath = va("%s/description.txt", modDir);
	fileHandle_t descHandle;
	int descLen = FS_SV_FOpenFileRead(descPath, &descHandle);
	if(descLen > 0 && descHandle) {
		descLen = FS_Read(description, descriptionLen-1, descHandle);
		description[descLen] = 0; }
	if(descHandle) fs_handle_close(descHandle);
	if(descLen <= 0) {
		// Just use the mod name as the description
		Q_strncpyz(description, modDir, descriptionLen); } }

// From old filesystem
void FS_FilenameCompletion( const char *dir, const char *ext,
		qboolean stripExt, void(*callback)(const char *s), qboolean allowNonPureFilesOnDisk ) {
	char	**filenames;
	int		nfiles;
	int		i;
	char	filename[ MAX_STRING_CHARS ];

	filenames = FS_ListFilteredFiles( dir, ext, NULL, &nfiles, allowNonPureFilesOnDisk );

	for( i = 0; i < nfiles; i++ ) {
		Q_strncpyz( filename, filenames[ i ], MAX_STRING_CHARS );

		if( stripExt ) {
			COM_StripExtension(filename, filename, sizeof(filename));
		}

		callback( filename );
	}
	FS_FreeFileList( filenames );
}

// From old filesystem. Used in a couple of places.
qboolean FS_FilenameCompare( const char *s1, const char *s2 ) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}

		if (c1 != c2) {
			return qtrue;		// strings not equal
		}
	} while (c1);

	return qfalse;		// strings are equal
}

void QDECL FS_Printf(fileHandle_t h, const char *fmt, ...) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	Q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	FS_Write(msg, strlen(msg), h);
}

qboolean FS_idPak(char *pak, char *base, int numPaks)
{
	int i;

	for (i = 0; i < numPaks; i++) {
		if ( !FS_FilenameCompare(pak, va("%s/pak%d", base, i)) ) {
			break;
		}
	}
	if (i < numPaks) {
		return qtrue;
	}
	return qfalse;
}

/* ******************************************************************************** */
// VM Hash Verification
/* ******************************************************************************** */

// This section is used to check the hash of game QVMs against a list of known trusted
//   mod hashes, to allow QVMs that would otherwise be blocked by download folder restrictions.
// This is strictly a security safeguard against malicious VMs; it is not intended to
//   prioritize or determine the "correct" VM for a given mod. A VM being in the trusted list
//   does not necessarily mean it is even compatible with ioquake3, only that it belongs to a
//   "legitimate" mod and is likely not malicious.

#define SHA(b0,b1,b2,b3,b4,b5,b6,b7) {0x##b0,0x##b1,0x##b2,0x##b3,0x##b4,0x##b5,0x##b6,0x##b7}

static const unsigned int trusted_vms[][8] = {
	// Original Quake 3 VMs
	SHA(4ea18569,bf56a282,d26dc89e,b9efcc5e,edbe0b69,c10182fc,38446174,c1e55b49),	// baseq3/pak8.pk3 cgame
	SHA(3a6fd12b,889f5d35,df20a09b,51bf8eca,46966d01,4be55ffa,d38ddc2f,fb38c807),	// baseq3/pak8.pk3 ui
	SHA(57c52bf2,2e4f528c,064f8af1,553a7103,723bab0a,02276bb1,1eed944b,f829b219),	// baseq3/pak8.pk3 qagame
	SHA(c1935b68,bda4a225,dfe50893,a361c00e,660c9d70,461f2fa2,f6f89b4b,4535332c),	// baseq3/pak7.pk3 cgame
	SHA(73dde0b0,383b745e,39806769,650a18d7,79c8e8cd,1ceb5984,1f6e88b2,fc23861d),	// baseq3/pak7.pk3 ui
	SHA(407b8ec3,8e6347cf,186d31a5,1629a9cd,8da5d5c1,9bc4b5d1,a489672e,2a275666),	// baseq3/pak7.pk3 qagame
	SHA(d39dd311,d590550c,53fb328e,c384ae6f,09f4a67b,655209fa,e50cd919,3c046c80),	// baseq3/pak6.pk3 cgame
	SHA(bab2fd92,f58d0b5b,d1dc7f29,6417fba1,89e10aa3,76c91424,b048ae0c,55f29c6e),	// baseq3/pak6.pk3 ui
	SHA(961a2e2f,a713c139,f32de270,dde81420,cd33aa63,9d0eb79f,41ec3b72,ee52e82b),	// baseq3/pak6.pk3 qagame
	SHA(c769f4f4,ea301442,a4accdf0,4609e3ff,60dee762,db09c663,28232645,73d38bba),	// baseq3/pak4.pk3 cgame
	SHA(2bfb85cc,be44b8fa,05750eee,85254fc8,f24afcec,c7ca5fd2,6126305d,a4d696e9),	// baseq3/pak4.pk3 ui
	SHA(805ee625,5382a782,1c438aaa,5a330ca8,8184ebcd,44277629,e6308d13,a5e473e5),	// baseq3/pak4.pk3 qagame
	SHA(6a9d927d,a75d628a,5483bb52,a6add9a3,dfd50224,7a73a086,ec979db6,97420193),	// baseq3/pak3.pk3 cgame
	SHA(88d64b9d,152b4a63,246fe731,16e565eb,ff178ca8,b414ee62,b2cb4d18,88b94762),	// baseq3/pak3.pk3 ui
	SHA(76e79b92,e6e42552,c2dcf7b4,282798b1,f510cc22,4bfb5393,f79ef4db,45ae922a),	// baseq3/pak3.pk3 qagame
	SHA(10976bbf,b03ece66,deab2b03,ce7905e1,7b41da1d,61d2d070,e386be39,47c71106),	// baseq3/pak2.pk3 cgame
	SHA(88d64b9d,152b4a63,246fe731,16e565eb,ff178ca8,b414ee62,b2cb4d18,88b94762),	// baseq3/pak2.pk3 ui
	SHA(afc82d75,0ab836a8,9233888a,badf7663,f9516093,a679ba0d,ecde6f99,6aafec1d),	// baseq3/pak2.pk3 qagame
	SHA(bb77ad2b,a5373f40,d2ed1a0d,78ec5b94,fa74a002,5cb61dbe,2b18d255,5b15f4c9),	// baseq3/pak1.pk3 cgame
	SHA(f24823cc,938eb636,1a24ab8f,d10c6d20,ee208cbd,e5927c80,16c47832,859aa2b8),	// baseq3/pak1.pk3 ui
	SHA(b477f6e5,a2bcd8d7,92875177,49ca6931,b1e87464,c50c1737,c0d840fa,5168fc98),	// baseq3/pak1.pk3 qagame
	SHA(ee31bdb9,865c3e11,afdff3b5,f65dbe95,99de9527,f2493a25,a347a8b0,ce5eb098),	// baseq3/pak0.pk3 cgame
	SHA(826a342a,108ac8a7,fa45f4e7,52dfa5be,50fa5ddf,4fdb87d7,c404d833,c4989627),	// baseq3/pak0.pk3 ui
	SHA(73d07e34,1bd21bff,3e7ec2c9,61ea9caf,e7ff9150,e0ddcc5d,8055757e,dece0f72),	// baseq3/pak0.pk3 qagame
	SHA(6ac3a861,aa28e21c,2710bc89,3fc2f30c,ae8e4218,35e239f1,35c89f1f,514f53bb),	// missionpack/pak3.pk3 cgame
	SHA(ef956cca,46edbb5c,fc38d091,27c2788b,c3d5842d,f6de07b3,fa9e553a,98ba4e5b),	// missionpack/pak3.pk3 ui
	SHA(48435ea5,770eaee8,457c1fa7,057b6efd,fd919aa7,e66b05bb,06575744,67d4f183),	// missionpack/pak3.pk3 qagame
	SHA(1a650d91,65c44a97,87725257,b397ef58,23af4e5c,28bcfbd7,6b647495,773a9fba),	// missionpack/pak2.pk3 cgame
	SHA(1f071a96,2540cf34,b17feb12,5299ed9b,77abb483,12248f17,1dcab34a,ea137155),	// missionpack/pak2.pk3 ui
	SHA(f440e701,4b3987c0,8cafa843,5533f4b7,50c7274d,bbc836f9,96a437a1,7a7dfff7),	// missionpack/pak2.pk3 qagame
	SHA(197732a5,ea8a09fb,b67af801,6c0b4116,8196f627,5e2d6356,81371750,eb4134e2),	// missionpack/pak1.pk3 cgame
	SHA(37d55455,7d45b2f5,d5fdfa9e,6f362f45,f4040fe0,d828005c,f577f3f4,abe8410e),	// missionpack/pak1.pk3 ui
	SHA(0fe0f3e3,0104a272,a6d1771e,a69120dd,d225f238,d3621554,1b3e1587,8a2b66e6),	// missionpack/pak1.pk3 qagame
	SHA(09d0b6eb,41ea623d,67031d2d,7a73058c,cb3bc655,6ec044ea,d529d48b,58d15f4c),	// missionpack/pak0.pk3 cgame
	SHA(7b157f32,acdb21a3,904d0782,96672ed2,d32195c5,b7a20692,2f6f7d33,c6c40e40),	// missionpack/pak0.pk3 ui
	SHA(da041f17,f296feea,f8269eab,c9062cef,decddfd2,4ff4d84e,b291902e,527d1d8a),	// missionpack/pak0.pk3 qagame

	// Defrag
	SHA(72d9fcff,db4e6650,f50c284a,e8ca9626,c47f6e95,a81c201a,41f599e5,4e82d599),	// 1.91.08 zz-defrag_vm_191.pk3 cgame
	SHA(4d5cf74a,5e25367d,6d14edfb,06b8b62b,4da4a08f,7507d898,b7c8f830,a3e85aca),	// 1.91.08 zz-defrag_vm_191.pk3 ui
	SHA(5e5de89f,68c8e5c9,4bd6a9d6,a1536c6e,1cf4b1b3,a9a3feac,701a0067,dab1c3a6),	// 1.91.08 zz-defrag_vm_191.pk3 qagame
	SHA(e0241055,9cad5dfd,bbfe07e8,2d285df1,7ccbbebb,7c713c57,2053c01f,524d2ad6),	// 1.91.09 zz-defrag_vm_191.pk3 cgame
	SHA(6c66c6fe,83394dc7,caf3c208,1ad2fecc,53882ac4,4e1efa7b,da88e164,ac28b05f),	// 1.91.09 zz-defrag_vm_191.pk3 ui
	SHA(cbac543e,c646241a,94b725d1,6cee4d6c,b2ed7c21,44097365,cdfce365,f3ecbd18),	// 1.91.09 zz-defrag_vm_191.pk3 qagame
	SHA(4de95184,aa5e4a6e,6e0c67a8,16719a59,c5be4192,14977391,cc5da990,2e35f030),	// 1.91.12 zz-defrag_vm_191.pk3 cgame
	SHA(5312ecd0,b18c704b,4d37001a,63387d08,6f8dfabf,0af91a7f,fab1eed3,71e6d857),	// 1.91.12 zz-defrag_vm_191.pk3 ui
	SHA(76c97520,32c4b7a1,427c0e75,a7b9427a,18cc88d7,28fd6109,f9d03c73,eb88fc62),	// 1.91.12 zz-defrag_vm_191.pk3 qagame
	SHA(dad41168,cd655d7c,b04d4ee4,bf55166e,0447a92c,f0d3d068,8cd903d1,14645f98),	// 1.91.13 zz-defrag_vm_191.pk3 cgame
	SHA(f19e47e9,728ff36d,6c0558f0,2d742102,37675ab4,3f21ea1d,6341dcc6,3b9cdcbe),	// 1.91.13 zz-defrag_vm_191.pk3 ui
	SHA(8563ec68,5588591c,6f06126e,bd3345e7,b70ce9ed,8b8e809d,c712e4db,915b3c57),	// 1.91.13 zz-defrag_vm_191.pk3 qagame
	SHA(ef0558ed,5ba81cbc,18234a58,6b3adc7e,b4b8609f,02127637,234c5655,19bd00f4),	// 1.91.14 zz-defrag_vm_191.pk3 cgame
	SHA(e057a5ff,44fd6ff5,5012c35a,3106c2fc,ee68e0a0,4d660839,9b36ec25,344fb802),	// 1.91.14 zz-defrag_vm_191.pk3 ui
	SHA(61f04e71,3b4cdc94,5ea6e532,54a65d1e,a21a40c7,e9825379,39509b68,5128bc62),	// 1.91.14 zz-defrag_vm_191.pk3 qagame
	SHA(9a4f85b5,3baa0e96,57c3c0b3,a1af8eb2,d2f96954,1b7a5d03,e9117e74,82989e1b),	// 1.91.15 zz-defrag_vm_191.pk3 cgame
	SHA(d758b933,f8454fe9,52ff65fa,c6dddf6f,4a9d5151,c3e107ca,33253f00,14770c06),	// 1.91.15 zz-defrag_vm_191.pk3 ui
	SHA(b86f1d8d,c93c4330,581ec14a,6867bf9b,c287ab8a,6ca139ec,ae78900a,ae022dfa),	// 1.91.15 zz-defrag_vm_191.pk3 qagame
	SHA(4a446c8c,93fd97c6,85940fd0,813f4745,a32b3f96,dc7c47ac,f627a2de,490b224a),	// 1.91.16 zz-defrag_vm_191.pk3 cgame
	SHA(ef636acb,a99eede9,4f420ef3,481702d1,65ad6b25,c9eb4c0f,b0ba5867,7e0beb53),	// 1.91.16 zz-defrag_vm_191.pk3 ui
	SHA(32054981,8ace1ea9,428097f1,d8c13a9a,e5261853,e9edfb1f,fc306e6b,7123677a),	// 1.91.16 zz-defrag_vm_191.pk3 qagame
	SHA(14a5d251,5c018ca7,0fdc76a8,cc1d858f,3720807f,798e7990,b5524f74,582ea81b),	// 1.91.17 zz-defrag_vm_191.pk3 cgame
	SHA(58766e5d,9e592f51,9ba09d85,cc3f1083,a03f0a64,fc3b1d88,95826420,594634ac),	// 1.91.17 zz-defrag_vm_191.pk3 ui
	SHA(ce8a1695,7df90e1d,25b4afa0,2e91a61e,2b7f3e07,f7a4d1c5,a0c461b1,2c4d3cc2),	// 1.91.17 zz-defrag_vm_191.pk3 qagame
	SHA(01b34f3b,94a9d62b,6bb17cfa,40ab1216,2370f30a,02afd238,9b2e7974,3f4fe2af),	// 1.91.18 zz-defrag_vm_191.pk3 cgame
	SHA(f3f5495d,c00bca5e,6e3b601a,77bf73a1,04a367b2,deafa199,7fbcd8ff,a7ed1edd),	// 1.91.18 zz-defrag_vm_191.pk3 ui
	SHA(be70f4d7,eb488016,eb777b2c,42c1bb77,34bee663,d08465d6,e4443115,8e5e198c),	// 1.91.18 zz-defrag_vm_191.pk3 qagame
	SHA(70ec42d0,50364cfd,ee5ebd50,5b8af875,499a60c7,9a251935,f68490c7,2d787640),	// 1.91.19 zz-defrag_vm_191.pk3 cgame
	SHA(d646b072,a6717648,6c254bbe,cb5e2a6a,e1df3ec8,7f83fb84,4f5cc359,f9a35550),	// 1.91.19 zz-defrag_vm_191.pk3 ui
	SHA(a81b8fab,2b3057e4,cc6ae201,9bc2ccca,c8e83902,ccdb5f7d,d2ca2952,a3a6eed8),	// 1.91.19 zz-defrag_vm_191.pk3 qagame
	SHA(e7551948,ac4c5fc5,d50640a6,0a336b58,ec1fc6b1,cc572e6c,337f0ec3,9fb5b35b),	// 1.91.20 zz-defrag_vm_191.pk3 cgame
	SHA(72e9f3fb,af044bcf,88f4e897,57512f76,cc74abad,398fa12f,a978cd82,52c83077),	// 1.91.20 zz-defrag_vm_191.pk3 ui
	SHA(553bc0f2,9c2e4d05,14ec720a,1bb0a276,95b79d72,0eaf3305,3091912b,70ead9ef),	// 1.91.20 zz-defrag_vm_191.pk3 qagame
	SHA(33c33558,c63a0a2c,06bf813e,9f37f7b8,7fbf6e38,d517c0f4,bab119ab,fa2cec03),	// 1.91.21 zz-defrag_vm_191.pk3 cgame
	SHA(efb2d97c,4523f370,c50ad8ce,65b416cc,804de272,fc7b818f,a3cf4d08,b95991c8),	// 1.91.21 zz-defrag_vm_191.pk3 ui
	SHA(04877cfc,1e769450,e39d841d,bfdf61a4,8648b7a6,a2d2e928,179b11d1,3dec500b),	// 1.91.21 zz-defrag_vm_191.pk3 qagame
	SHA(224222a1,b435210a,ea2a980f,510e626a,bd9e0956,749840b3,0fb7e039,726b9aca),	// 1.91.22 zz-defrag_vm_191.pk3 cgame
	SHA(0bc890e4,5eaa190e,6c1a1735,3da7794d,5a3ba898,90f99397,e7a947ac,ce80e75c),	// 1.91.22 zz-defrag_vm_191.pk3 ui
	SHA(fd83b123,a5d5a243,0007d0c6,074070db,d8c03aa1,bfde3684,365ea60e,d86f6c3e),	// 1.91.22 zz-defrag_vm_191.pk3 qagame
	SHA(753a4747,c0e7e2b7,f0172ee2,2bca1f84,56f90e81,5940a36a,f79ba4be,44bb57ab),	// 1.91.23 zz-defrag_vm_191.pk3 cgame
	SHA(52a45d3e,c13533fe,7cd458a0,1a787b03,ec6e111a,61fbbe94,9328b5e8,f1e39159),	// 1.91.23 zz-defrag_vm_191.pk3 ui
	SHA(114edd74,03235177,8eff4f2d,6dce9494,34f693ec,feda608d,3d7a53c6,dc9b97e1),	// 1.91.23 zz-defrag_vm_191.pk3 qagame
	SHA(f7f814f4,f5960606,85f3c4f2,74d994e9,85014b3f,91b699ac,bc948089,34b990af),	// 1.91.24 zz-defrag_vm_191.pk3 cgame
	SHA(4b38aac9,ab6b7626,692cead2,810ad1a7,9aea7525,257d154d,7e786a79,b777a46a),	// 1.91.24 zz-defrag_vm_191.pk3 ui
	SHA(3aefd360,e9dce2bd,f4bddc95,7e08d1c6,d20cc059,f0500bf3,71a6eb42,01b32f88),	// 1.91.24 zz-defrag_vm_191.pk3 qagame
	SHA(b950f4d7,895cc4ff,f12877ec,7f120746,0b5db3ad,6c0ecf1c,44f732b8,c734344e),	// 1.92.00 zz-defrag_vm_192.pk3 cgame
	SHA(646e8473,354906fd,6a874c90,eaa198c6,127d5193,89702b63,71e35321,32010b4e),	// 1.92.00 zz-defrag_vm_192.pk3 ui
	SHA(e8fc046c,cf168b9a,15c45e8c,600913f3,e6b30b36,691ea45a,b087c6ce,5af8a846),	// 1.92.00 zz-defrag_vm_192.pk3 qagame
	SHA(5ebedeb4,840ce409,0a2ebce2,0836514c,74bd6913,890bb877,3c567ad7,f8c6b868),	// 1.92.01 zz-defrag_vm_192.pk3 cgame
	SHA(e3b23641,46899012,12cc5da1,2416a1b1,40267dd3,b421247b,3bdc4e97,ff61b6e6),	// 1.92.01 zz-defrag_vm_192.pk3 ui
	SHA(8191934f,8f9f49de,0bb4942b,70acf9fc,22b4a921,c72fc0c3,3e85cdcb,243c18de),	// 1.92.01 zz-defrag_vm_192.pk3 qagame
	SHA(a785d303,4443f3fb,47ff81c9,dcf7520f,cb025bcb,09d947f4,52fdd9e6,263adb3f),	// 1.92.02 zz-defrag_vm_192.pk3 cgame
	SHA(f7c7edea,6e95a626,25a6f958,8c559edb,07030c88,9984bdde,47d77d62,819a75ab),	// 1.92.02 zz-defrag_vm_192.pk3 ui
	SHA(e6af8c30,17e1fa32,cdc38444,7491017b,7b12d5dc,73c0662e,c69a294d,5748e117),	// 1.92.02 zz-defrag_vm_192.pk3 qagame

	// OSP Tourney
	SHA(48e17665,b0b274ba,a9567036,4364be37,3fbbda4a,ea6e74c1,fb587aaa,5feb33b3),	// zz-osp-pak3.pk3 cgame
	SHA(999e49d1,c24cdaf0,c4eff5ac,1241549a,b7559968,0eea3a73,e0ed07bc,1600f8be),	// zz-osp-pak2.pk3 cgame
	SHA(82b0c71b,3123e535,c3be3e31,511ae554,c720d6c7,d8ce7bb4,5a54ef5a,8ef5c710),	// zz-osp-pak1.pk3 cgame
	SHA(36133dea,02f56992,a8683f85,2893a414,e330cc87,06389567,1ee268b5,1891147c),	// zz-osp-pak1.pk3 ui
	SHA(b6ccc3fb,79390a4c,5222c0bd,687eef06,a086632c,23faf1a2,ca34a3a6,4157fb32),	// zz-osp-pak0.pk3 cgame
	SHA(e693f49f,b21805e6,c7d232ae,3a6994dc,f7f8adf3,19dfb0b5,7af6bfc1,ed3075a9),	// zz-osp-pak0.pk3 ui
	SHA(7ce6a6ac,1fcb4442,06461e19,129d4f5c,d1c4ba79,a3cbf881,653e6450,b8277160),	// zz-osp-server3a.pk3 qagame

	// Challenge ProMode Arena
	SHA(7bb9a938,173d804e,3c9a7d3c,5a923f32,a1620095,cb953200,c93142e9,7d1e5a87),	// z-cpma-pak148.pk3 cgame
	SHA(d9febc97,d8eb1732,efaa8a35,b6d344de,5dcf6e4e,94ce791a,c3b16aa9,1884142b),	// z-cpma-pak148.pk3 ui
	SHA(b7998090,e8864e43,66e8daac,14c3820e,96d7d0a0,b6045985,6452299f,335c0958),	// z-cpma-pak148.pk3 qagame
	SHA(95149049,5ae9bcaf,77ca8ca9,f09470c5,ec448f54,ee774514,cce8c039,2c7c8999),	// z-cpma-pak146.pk3 cgame
	SHA(eba6fc2d,6f1bf3fe,384eb6b7,0c30a72b,979fcb4a,3c11ebe2,6ed2d76e,1321c6c2),	// z-cpma-pak146.pk3 ui
	SHA(deeae01e,b23fdaa2,dcef0b50,5e21abd5,53316fc6,294ff6e4,067aa616,1d4cedec),	// z-cpma-pak146.pk3 qagame

	// Threewave CTF
	SHA(14858804,fb98609e,d8b3b3c3,b825f0a7,cb544063,f7e43735,c884cd5e,4a51157c),	// pak05.pk3 cgame
	SHA(259fa9a6,10659b0a,3b1c59b0,d83100dd,234cfe0f,a0ae8fb3,a891aca3,e0c722a7),	// pak05.pk3 ui
	SHA(9751bad9,9a2d138f,96a9b043,6d2ea2d9,65b86214,175dc33e,4cea95e0,59419337),	// pak05.pk3 qagame
	SHA(f13c94b3,60571ab1,a3735eeb,8a70c1fa,f6049f9d,0914fb87,7a7776b5,020135dc),	// pak04.pk3 cgame
	SHA(712ee5a9,8a60021b,7fc36601,dfed4cfc,68c51b48,d04970ab,92b69384,026d9ad0),	// pak04.pk3,pak03.pk3 ui
	SHA(1a9a5122,ee120684,00ea0496,001b9485,8161540e,912207e3,88721781,d984daee),	// pak04.pk3 qagame
	SHA(472cb89b,ca6681b6,8d2a790a,b10affa3,ddfa19a6,c1c798ce,6a0ea566,86a51daf),	// pak03.pk3 cgame
	SHA(3fc47bab,74f822c1,5922cb08,e8640cd4,363bf8df,2716f141,d2a4ccd8,ab5a8ef5),	// pak03.pk3 qagame
	SHA(6277405a,ff2288e8,468fb757,fca680c7,3be6d43e,4cceecc3,b4dd2f81,32372fb7),	// pak02.pk3 cgame
	SHA(247274bf,64787787,f89503ba,9e9c95d7,b113404c,699bab8d,c5b57aa4,5c0a4678),	// pak02.pk3 ui
	SHA(8f5fde11,841c00cc,43877f32,ad8081d1,6fcd5859,9974e42b,8334f5fd,3209fa0a),	// pak02.pk3 qagame
	SHA(661588fa,c009255a,257ebb49,a12a279e,19529b97,a6bb610b,1e0fa6c2,a23175b1),	// pak01.pk3 cgame
	SHA(1a655c47,d3506acc,c9ae0332,a3f544a8,a0ff8816,cf4001e7,723e37d9,d2af97a9),	// pak01.pk3 ui
	SHA(7e68a480,79a22404,2a3b8fb8,d7bfc6ba,becc6dce,d5b75796,01927477,8b45c987),	// pak01.pk3 qagame
	SHA(d462e4dc,a60dd4b9,38e9f937,90c8e569,64a121cb,de8e8b6a,0022b8cb,d2a4bb7a),	// pak00.pk3 cgame
	SHA(c8a59247,7737802e,5eb5ec20,7f9afaa7,84ba8449,b53726d3,d0d59018,bffffc6f),	// pak00.pk3 ui
	SHA(a587f8ce,9f34b1ce,0aa692d0,0cd7418b,1834c808,12f84c8c,6f56e1e6,8e2a9686),	// pak00.pk3 qagame

	// ExcessivePlus
	SHA(b73dd1f4,a69204ab,e532ed2f,3b697fb4,e86d14bf,7408a2e8,db4696e4,3708b45d),	// z-xp-2_3.pk3 cgame
	SHA(2ded5963,bc992ec4,ca8068ec,0b396116,b77e46ba,75fd7694,dccf2686,ef78b2c0),	// z-xp-2_3.pk3 ui
	SHA(2582a9b3,ececd83d,541a667b,ef035cec,7d70ba77,86c9936a,6ecf655a,c0ac4dc4),	// z-xp-2_3.pk3 qagame
	SHA(29187bc7,17bd375c,ee0bb8fe,f00d02f0,baedef3d,b473619d,b426f22d,49abfcdb),	// z-xp-2_2b.pk3 cgame
	SHA(ee7cd2b1,efae49a3,67155113,7559a3f2,30b31be0,e45b147b,e42429f0,0d32979b),	// z-xp-2_2b.pk3 ui
	SHA(d9ada8cd,ec1d31ae,419a1813,81aca870,7f224adc,e0720225,b05b210f,d88d38dd),	// z-xp-2_2b.pk3 qagame
	SHA(16e25f30,43d2d32e,43ef1c04,a21e201e,4ea02f30,970a8548,d5b46509,e3eceba0),	// z-xp-2_2a.pk3 cgame
	SHA(19339573,8667ccb2,cf875910,93906c8a,5a2a2dba,6883f776,3a4fcc7a,756b42a8),	// z-xp-2_2a.pk3 ui
	SHA(8a18446c,33f53d30,718641f1,6ccdd00e,85b21e26,b9b1bde4,f953cb3a,d0cd71dc),	// z-xp-2_2a.pk3 qagame
	SHA(dd68b033,5a68acd5,994c4602,d1909194,b3b5a03c,4ebfeec2,82a54a11,0ab47a9a),	// z-xp-2_1.pk3 cgame
	SHA(6f29922c,c8b9f2e3,8a7ed426,578e8b85,8e22d59b,65aea126,5e0c64ef,62bf419b),	// z-xp-2_1.pk3 ui
	SHA(cd3c90de,45f01a3d,1bbbbb90,b75bb050,92dfb495,27b74d38,e5cd7c5e,3611ed81),	// z-xp-2_0a.pk3 cgame
	SHA(0ed23cb9,f2b78509,50008405,6f81358b,ea81f19f,4e7567db,eb7ceb16,9ff1bde2),	// z-xp-2_0a.pk3 ui
	SHA(00b31196,e46717cb,0fd238ee,2c48120b,22ebb95e,4cbdda57,19b0b038,8723e395),	// z-xp-2_0a.pk3 qagame

	// Excessive Dawn (edawn-mod.org)
	SHA(ba4471e7,4b57d05d,6ff474d0,76b7f161,59a27e40,b0424f65,cfec561c,1fa1fc6a),	// z-edawn-1_5_0.pk3 cgame
	SHA(49ca495a,13b9bc21,c7e6a4e5,7feca87f,08253187,8d306757,e1968536,e8b52caa),	// z-edawn-1_5_0.pk3 ui
	SHA(f21140a6,41400e66,efc89c15,417dbdc1,e39408e7,e52208c4,6da2d1b5,207eb846),	// z-edawn-1_5_0.pk3 qagame
	SHA(bfc6035d,6ef78bcd,dfa160c2,fdcff061,d55320cc,9d9a1b06,b61b9f90,ca16eb7f),	// z-edawn-1_4_0.pk3 cgame
	SHA(0fb1fb2e,00ea5c42,3a381f05,1c94b2be,0cfcfa96,4788d5b0,80eefaef,e59cea50),	// z-edawn-1_4_0.pk3 ui
	SHA(25145a1b,d6a072f1,d11085e3,bea7edaf,aede8897,fe7a57c0,b6d4758c,fd6811d6),	// z-edawn-1_4_0.pk3 qagame

	// Freeze Tag (nbquakers.com/freezetag.htm)
	SHA(a9ad3329,889a53e8,66d44507,d4419897,bcf029b4,5bcae16b,70c018b0,5624da3f),	// freeze0.pk3 cgame
	SHA(152f78f5,6d17824d,d9fed8e6,a9cfb1f5,0effa09a,2fbfdd42,c57811ba,43c9e192),	// freeze1.pk3 qagame
	SHA(5e7555b5,ff609515,f447939e,573537d9,b4d49f38,66f93378,a5e28c44,267b2479),	// freeze_ta0.pk3 cgame
	SHA(bc2a35ba,7220b11b,596168e1,f13a4a43,b3957dcd,9c58f77d,e22159c0,0c1e08fd),	// freeze_ta0.pk3 qagame

	// Ultra Freeze Tag (nbquakers.com/ultrafreezetag.htm)
	SHA(db3cae1a,7bd6c12a,d33c9627,9c6fd222,79381e67,909fabcb,b3f890d8,1ecb4cff),	// ufreeze-22cg.pk3 cgame
	SHA(45f85fa7,5289e55e,d86ff221,8b568434,74230e93,ec88fd49,26cc0797,9be99531),	// ufreeze-22ui.pk3 ui
	SHA(e2d8e1d5,10b0668e,8bef2606,584cc1c8,3736aea2,f9abeafa,63782ae4,329991f1),	// ufreeze-22sv.pk3 qagame

	// Weapons Factory Arena
	SHA(2b430a4d,56de288c,44ab40f1,a181313c,5afdd3e7,271e43af,599d1264,abcd2dde),	// pak35b.pk3 cgame
	SHA(835157d6,2dc5431e,9efd6c92,d3a808a6,622d650a,06e02db1,e48401ba,f92ea264),	// pak35b.pk3 qagame
	SHA(188335be,886d500f,c2a03a9e,ebb75340,10b84e45,e3f36a1d,92ad861f,2bea01a3),	// pak35.pk3 cgame
	SHA(6867860a,68af55b3,90fb4ecf,bf3c345f,3e963ddb,561097fb,576df95c,da91a136),	// pak35.pk3 ui
	SHA(71d80288,6651181d,94308311,c3f6fd18,3df7fd0d,6a57895e,b7896ecf,05817eca),	// pak35.pk3 qagame

	// Rocket Arena
	SHA(1e234470,41681f10,05e1ccb5,62d2eb90,475512a9,18525fbe,dc1bbb0b,570365c7),	// vm.pk3 cgame
	SHA(5f871837,4daa2fda,cff14fdb,2d2b4f71,4d37727f,38f8e5c7,b6250419,77d14c39),	// vm.pk3 ui

	// True Combat
	SHA(ffddc7fa,33e2444c,23996a46,31920601,42e76162,211daa31,0bf6e63b,810b3b87),	// truecombat/pak2.pk3 cgame
	SHA(3764ef13,c8613837,22088147,bfc66fd8,05471168,ad6117d7,e56a570d,b91a961b),	// truecombat/pak2.pk3 ui
	SHA(d3c7c5c6,34a4df76,c6cac48b,cd4024e4,9dbb7886,a806f17d,74b4b583,5fccbad4),	// truecombat/pak2.pk3 qagame
	SHA(506fe6bf,66897cba,da2b8249,565b9da8,16eaefe7,03772f1d,ffcc5e7f,078cf069),	// q3tc045/pak6.pk3 cgame
	SHA(46427997,abbd31eb,dc5380f3,1bceffa8,3690f10c,05f46e43,f855a7b4,70c4ca41),	// q3tc045/pak6.pk3 ui
	SHA(0fec7933,50c547ee,38810f19,88d6ae6f,fbf3307c,0cf236bf,6d0acdac,1aa3e096),	// q3tc045/pak6.pk3 qagame
	SHA(c2b325bc,540d9ecd,918685e7,5c864e72,e314848c,0af1b6d6,9958f37e,53fcb07f),	// q3tc045/pak5.pk3 cgame
	SHA(efce2ab0,9c5f5bf0,636a5c74,5358dad0,fe09cc32,37e876a6,bffc99d4,7a2bfe97),	// q3tc045/pak5.pk3 ui
	SHA(6427e326,30f715b6,f167837d,5a7ac908,7acd4f84,e179eaed,1003c2c0,1d89e64b),	// q3tc045/pak5.pk3 qagame
	SHA(e590ba97,9191f6ac,69a00dea,52219d9f,255b2dae,1997fde1,955a1657,5d75d4ef),	// q3tc045/pak4.pk3 cgame
	SHA(f25ee7ef,50afa697,c8c32a74,fe3cabda,f746714a,e2723247,8b191b4c,f2116351),	// q3tc045/pak4.pk3 ui
	SHA(c20dc792,cccb0f8f,f63837a4,aee6f919,027096c4,af93ec1e,b8429434,a03c4a5e),	// q3tc045/pak4.pk3 qagame
	SHA(715321a2,09b9343e,b85b50ba,6ae64024,a9ed2fa2,ecb925b7,7ec6461d,71cf10ff),	// q3tc045/pak3.pk3 cgame
	SHA(1939108c,e0a4868b,afa904c3,91f7260c,3bbcb378,e52958f1,88ff1895,421aae19),	// q3tc045/pak3.pk3 ui
	SHA(41c02d25,5cb4e95b,f9b46bd3,72d44a4f,1d04ad68,e4d4cf22,f9d97a98,8940728f),	// q3tc045/pak3.pk3 qagame
	SHA(e2b1692f,077d6b82,181fe52e,104d05e9,1ae4b070,fae8451c,3f4ca6d8,5185bfd5),	// q3tc045/pak2.pk3 cgame
	SHA(177a43cb,758ec34b,7a66c854,7993f5bb,74bc7a2a,fcb005b5,fb5c5150,45c4ce4e),	// q3tc045/pak2.pk3 ui
	SHA(38eaa136,9d5365d1,fe38199c,fb23485e,c6630343,2575d95d,598c3a5b,384d1a28),	// q3tc045/pak2.pk3 qagame
	SHA(af4b9138,7d9304b5,b425aa1c,31e9f563,71ab194f,086ea9a0,2b3554ce,be02fe56),	// q3tc045/pak1.pk3 cgame
	SHA(40053b90,7c76316d,a3370adf,aba6910b,a9b82c5f,83e426cb,5bf36925,75d143f8),	// q3tc045/pak1.pk3 ui
	SHA(b9615dd0,e33b52cf,436b144a,73e13248,4b72ef12,88202771,3f2cf541,8d497dfd),	// q3tc045/pak1.pk3 qagame
	SHA(c1308b35,d0bf4042,1304bd99,0a38bda0,c694ffa9,9d87f746,328fd6db,57a636ed),	// q3tc045/pak0.pk3 cgame
	SHA(954ebd3f,440f0a08,e1c085bb,ca2a5b4f,322abb72,17ed833d,e7b03c33,727dc028),	// q3tc045/pak0.pk3 ui
	SHA(59695dfb,daea80d2,d64df7e9,f89a32ec,4551858b,f0bee5fa,c3b6aaf2,8b8529c6),	// q3tc045/pak0.pk3 qagame

	// Quake Revolution (quake-revolution.net)
	SHA(7039ee45,20113948,ec5a26b1,e22c88ce,164574c5,ba24240f,8d062ccb,4a096143),	// z-Revolution-client-200[Epsilon2].pk3 cgame
	SHA(9c12d1da,c8865664,f580c4e1,5c6195af,b2195197,4c039319,05636f07,b624dcd2),	// z-Revolution-server-200[Epsilon2].pk3 qagame
	SHA(8c62e928,d0fa7a67,672bdca8,32fc4db8,da5800b6,41270db7,6bde0326,e2a5b256),	// z-Revolution-client-200[Epsilon1].pk3 cgame
	SHA(1f42801b,6faa1cba,60c14757,bade8001,4be2218f,3b52685e,6256668f,a0e36251),	// z-Revolution-ui-200[Epsilon1].pk3 ui
	SHA(a7da4d5f,93fc9c07,b018b92d,91e17dcc,c00f898f,2654664e,b41ffc6c,771cfc7b),	// z-Revolution-server-200[Epsilon1].pk3 qagame
	SHA(e5d17064,445a52b7,5619418c,2c43251a,7b589ab3,f6ec3adc,949697da,86680a1d),	// z-Revolution-client-200[Delta6].pk3 cgame
	SHA(d5c75915,6311193e,047e8a95,4ff24cd4,b59f60f1,a49e1eed,dc4bdfa8,fb15a793),	// z-Revolution-server-200[Delta6].pk3 qagame
	SHA(35939d6e,a6aec584,f97939f4,1f9bb4b4,8f8dc4d8,8ea8c07b,278cd14c,12fbb4e1),	// z-Revolution-ui-200[Chi1].pk3 ui
	SHA(e3ea6e17,2c73cee8,1f3601e8,a0fe50ee,aa117fd0,9dd1948c,3182e3a2,ddb600be),	// z-Revolution-client-200[Beta1].pk3 cgame
	SHA(8032d831,00c51e60,7d57d221,a36755fe,c8b374a2,67059c06,5beceb93,7369621f),	// z-Revolution-server-200[Beta1].pk3 qagame
	SHA(0d90b458,0bfd1f09,7a1d9d11,de223475,39beacdd,ac0b62d1,7e8637d5,50c0a20b),	// z-Revolution-200[Alpha1].pk3 cgame
	SHA(02e9bd52,92119cc0,75822413,91a2b7ee,c1c6b2b3,5f5aa34f,e22e745a,ca1cf49d),	// z-Revolution-200[Alpha1].pk3 ui
	SHA(4904e60c,6d914309,266c7f97,97f7009c,e286c128,aa2738c7,dcf7049c,d72d7952),	// z-Revolution-200[Alpha1].pk3 qagame

	// Zero Ping Mode
	SHA(d0996f1e,a891741d,a06f6516,c365f7ae,868cfd7e,2a02c06f,24bdf8c2,2894db10),	// v4.3 pak0.pk3 cgame
	SHA(a67cb9b2,a241a78e,3c64d991,6d0531f1,5d34721f,3dcf7bd8,7e4973e7,0a197e9c),	// v4.3 pak0.pk3 ui
	SHA(9f6bb8b3,02df398b,1f8283aa,981d9464,44f1c150,36af77fb,414044b5,b25db659),	// v4.3 pak0.pk3 qagame
	SHA(06b07548,bb7f528a,f36d4700,50436a4a,99ea11d8,0692d07d,0f0febb6,e61645fc),	// v4.4 (?) pak0.pk3 cgame
	SHA(9364bdd2,2eb684db,30dd2f54,c5bcfa3f,f069ef31,e827149c,9a527b2d,588555ae),	// v4.4 (?) pak0.pk3 ui
	SHA(f26d2b8c,7a874cf3,293e93ec,9faffd39,a0f94f4a,02414973,09e608b6,bd89ebc9),	// v4.4 (?) pak0.pk3 qagame

	// RGGMod (rp.servequake.com)
	SHA(44c346b6,b0160384,4b37c192,4e741cc1,a8ff5260,c6ef1a62,655c6839,0254c49f),	// RGGmod_1.32b.3.pk3 cgame
	SHA(65121467,bd4f0027,69d02814,2e2e7efc,48eb000c,1784027c,e477e860,981bf9ca),	// RGGmod_1.32b.3.pk3 qagame

	// InstaUnlagged (?)
	SHA(25bba8c3,bf14017d,849e06e7,5238fae7,5db08d0e,4e277ca2,baeed356,44168ae2),	// pak0.pk3 cgame

	// Hunt Mod (lonebullet.com/file/mods/hunt-mod/37111)
	SHA(f187545a,3da187e4,7a59f1d6,5cb2bd94,a5335d7f,f0a8f57f,07e86b78,f6fa8286),	// hunt.pk3 cgame
	SHA(62230abd,f723d6c0,adbe0f66,bec34f8b,009a2812,16bcbc2f,fd2c3e8d,1494fc6e),	// hunt.pk3 ui
	SHA(c0fa2c7f,160c2d82,6929a560,7f811d82,e712ede1,3df6e1fe,e4dc76f1,02b95431),	// hunt.pk3 qagame

	// Western Q3
	SHA(fcb31ce6,852964b1,7bad9a86,e64758a5,4c7daad2,3b4e471a,448f827a,e77e62a9),	// wq3_pak3.pk3 cgame
	SHA(15b75a81,6615e7ad,e3936783,22bfba52,c82ae592,9d44e191,2368091d,c30f8f9e),	// wq3_pak3.pk3 ui
	SHA(bf02966b,a41415fa,c9ade96e,c445b1b1,b32d5240,8fa46e9e,10f42fe3,2a28081c),	// wq3_pak3.pk3 qagame
	SHA(83d5e677,80d02573,e8fb8c56,9e202b18,89637375,bbff0253,e9391c29,e230c88a),	// wq3_pak2.pk3 cgame
	SHA(f88fd545,4c7996ae,c58505d6,df3d89a9,cdc62815,62557314,a72949eb,96cea3bd),	// wq3_pak2.pk3 ui
	SHA(2bec1a03,8e86468e,611b1dd5,88e845e0,ba099dba,d98a4e81,6848fb81,cd046ee5),	// wq3_pak2.pk3 qagame
	SHA(e3ab1d1d,99f4c502,05324a4b,997a3543,9d45fdb2,3901be61,3684813e,7aa2ee9e),	// wq3_pak1.pk3 qagame
	SHA(281765c9,8c1835fa,d470d970,87b92e9d,994c6bcd,b82cf19a,43cc7945,3fbba188),	// wq3_pak0.pk3 cgame
	SHA(703bd9b1,dcf1ecae,2b553cbf,0b54c4a9,a7a3da97,ec8574af,a3e4bcf0,21344c16),	// wq3_pak0.pk3 ui
	SHA(c6915701,3293f03f,46cc5768,3806bdc8,75ba0b77,d7263626,95aa6391,5c22badb),	// wq3_pak0.pk3 qagame

	// World of Padman
	SHA(05d765a2,946f5347,bf36cf35,22f854b6,fb5097de,4451c0d8,66cc57a8,f8f2290d),	// wop_006.pk3 cgame
	SHA(10e8e292,e5572b29,2f8388c2,51950783,982aa9ed,676d005c,7c3e02e3,3d4591b5),	// wop_006.pk3 ui
	SHA(e3d80496,beefd222,8e10c8db,7aadb312,592fc134,fb7e21e0,63766df9,d68e52eb),	// wop_006.pk3 qagame
	SHA(cfa3973f,39256a54,abebe253,b65f289f,675a51a8,dae34907,b68ce305,a5586d57),	// wop_001.pk3 cgame
	SHA(0ea91c73,d3627e74,421a1c57,7c0d9a34,badde572,7f340322,2965c0d7,9edf07e0),	// wop_001.pk3 ui
	SHA(c089d1b0,b541c6a1,d5e0e444,099bf968,f0dba16d,63292801,cbd75ecc,c43944a1),	// wop_001.pk3 qagame

	// Reaction Q3
	SHA(ea706460,01cf5d9e,d4166c37,62daab83,099c3647,64430086,69a39a42,26041b12),	// reaction1.pk3 cgame
	SHA(935c37ac,c683a77f,4ee1fabd,c832d5f8,8a95c27a,525717d2,08f6cb39,d1b53dcb),	// reaction1.pk3 ui
	SHA(c55dac68,36e685ae,016fd0ed,b4148be0,9c70e0f9,92688fb9,1ed10ff9,720fc094),	// reaction1.pk3 qagame
};

qboolean calculate_file_sha256(const fsc_file_t *file, unsigned char *output) {
	// Returns qtrue on success, qfalse otherwise
	unsigned int size = 0;
	char *data = fs_read_data(file, 0, &size);
	if(!data) {
		Com_Memset(output, 0, 32);
		return qfalse; }
	fsc_calculate_sha256(data, size, output);
	fs_free_data(data);
	return qtrue; }

qboolean fs_check_trusted_vm_hash(unsigned char *hash) {
	// Returns qtrue if hash is trusted, qfalse otherwise
	int i;
	unsigned int int_hash[8];
	for(i=0; i<8; ++i) int_hash[i] = (((unsigned int)(hash[4*i]))<<24) + (((unsigned int)(hash[4*i+1]))<<16) +
			(((unsigned int)(hash[4*i+2]))<<8) + ((unsigned int)(hash[4*i+3]));
	for(i=0; i<ARRAY_LEN(trusted_vms); ++i) if(!memcmp(int_hash, trusted_vms[i], 32)) return qtrue;
	return qfalse; }

qboolean fs_check_trusted_vm_file(const fsc_file_t *file) {
	// Returns qtrue if file is trusted, qfalse otherwise
	unsigned char sha[32];
	if(!calculate_file_sha256(file, sha)) return qfalse;
	return fs_check_trusted_vm_hash(sha); }

void sha256_to_stream(unsigned char *sha, fsc_stream_t *output) {
	int i;
	char buffer[4];
	for(i=0; i<32; ++i) {
		Com_sprintf(buffer, sizeof(buffer), "%02x", sha[i]);
		fsc_stream_append_string(output, buffer); } }

/* ******************************************************************************** */
// System Pak Verification
/* ******************************************************************************** */

// This section is used to verify the system (ID) paks on startup, and produce
// appropriate warnings or errors if they are out of place

#ifndef STANDALONE
static const unsigned int core_hashes[] = {1566731103u, 298122907u, 412165236u,
	2991495316u, 1197932710u, 4087071573u, 3709064859u, 908855077u, 977125798u};

static const unsigned int missionpack_hashes[] = {2430342401u, 511014160u,
	2662638993u, 1438664554u};

static qboolean check_default_cfg_pk3(const char *mod, const char *filename, unsigned int hash) {
	// Returns qtrue if there is a pk3 containing default.cfg with either the given name or hash
	fsc_hashtable_iterator_t hti;
	fsc_file_t *file;

	fsc_hashtable_open(&fs.files, fsc_string_hash("default", 0), &hti);
	while((file = STACKPTR(fsc_hashtable_next(&hti)))) {
		const fsc_file_t *pk3;
		if(fs_file_disabled(file, 0)) continue;
		if(file->sourcetype != FSC_SOURCETYPE_PK3) continue;
		if(Q_stricmp(STACKPTR(file->qp_name_ptr), "default")) continue;
		if(Q_stricmp(STACKPTR(file->qp_ext_ptr), "cfg")) continue;
		if(file->qp_dir_ptr) continue;

		pk3 = STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3);
		if(((fsc_file_direct_t *)pk3)->pk3_hash == hash) return qtrue;
		if(mod && Q_stricmp(fsc_get_mod_dir(pk3, &fs), mod)) continue;
		if(!Q_stricmp(STACKPTR(pk3->qp_name_ptr), filename)) return qtrue; }

	return qfalse; }

typedef struct {
	const fsc_file_direct_t *name_match;
	const fsc_file_direct_t *hash_match;
} system_pak_state_t;

static system_pak_state_t get_pak_state(const char *mod, const char *filename, unsigned int hash) {
	// Locates name and hash matches for a given pak
	const fsc_file_direct_t *name_match = 0;
	fsc_hashtable_iterator_t hti;
	fsc_pk3_hash_map_entry_t *entry;
	const fsc_file_direct_t *file;

	fsc_hashtable_open(&fs.files, fsc_string_hash(filename, 0), &hti);
	while((file = STACKPTR(fsc_hashtable_next(&hti)))) {
		if(fs_file_disabled((fsc_file_t *)file, 0)) continue;
		if(file->f.sourcetype != FSC_SOURCETYPE_DIRECT) continue;
		if(Q_stricmp(STACKPTR(file->f.qp_name_ptr), filename)) continue;
		if(Q_stricmp(STACKPTR(file->f.qp_ext_ptr), "pk3")) continue;
		if(file->f.qp_dir_ptr) continue;
		if(mod && Q_stricmp(fsc_get_mod_dir((fsc_file_t *)file, &fs), mod)) continue;
		if(file->pk3_hash == hash) return (system_pak_state_t){file, file};
		name_match = file; }

	fsc_hashtable_open(&fs.pk3_hash_lookup, hash, &hti);
	while((entry = STACKPTR(fsc_hashtable_next(&hti)))) {
		file = STACKPTR(entry->pk3);
		if(fs_file_disabled((fsc_file_t *)file, 0)) continue;
		if((file->pk3_hash == hash)) return (system_pak_state_t){name_match, file}; }

	return (system_pak_state_t){name_match, 0}; }

static void generate_pak_warnings(const char *mod, const char *filename, system_pak_state_t *state,
		fsc_stream_t *warning_popup_stream) {
	// Prints console warning messages and appends warning popup string for a given pak
	if(state->hash_match) {
		if(!state->name_match) {
			char hash_match_buffer[FS_FILE_BUFFER_SIZE];
			fs_file_to_buffer((fsc_file_t *)state->hash_match, hash_match_buffer, sizeof(hash_match_buffer),
					qfalse, qtrue, qfalse, qfalse);
			Com_Printf("NOTE: %s/%s.pk3 is misnamed, found correct file at %s\n",
					mod, filename, hash_match_buffer); }
		else if(state->name_match != state->hash_match) {
			char hash_match_buffer[FS_FILE_BUFFER_SIZE];
			fs_file_to_buffer((fsc_file_t *)state->hash_match, hash_match_buffer, sizeof(hash_match_buffer),
					qfalse, qtrue, qfalse, qfalse);
			Com_Printf("WARNING: %s/%s.pk3 has incorrect hash, found correct file at %s\n",
				mod, filename, hash_match_buffer); } }
	else {
		if(state->name_match) {
			Com_Printf("WARNING: %s/%s.pk3 has incorrect hash\n", mod, filename);
			fsc_stream_append_string(warning_popup_stream, va("%s/%s.pk3: incorrect hash\n", mod, filename)); }
		else {
			Com_Printf("WARNING: %s/%s.pk3 not found\n", mod, filename);
			fsc_stream_append_string(warning_popup_stream, va("%s/%s.pk3: not found\n", mod, filename)); } } }

void fs_check_system_paks(void) {
	int i;
	system_pak_state_t core_states[ARRAY_LEN(core_hashes)];
	system_pak_state_t missionpack_states[ARRAY_LEN(missionpack_hashes)];
	qboolean missionpack_installed = qfalse;	// Any missionpack paks detected
	char warning_popup_buffer[1024];
	fsc_stream_t warning_popup_stream = {warning_popup_buffer, 0, sizeof(warning_popup_buffer), qfalse};

	// Generate pak states
	for(i=0; i<ARRAY_LEN(core_hashes); ++i) {
		core_states[i] = get_pak_state(BASEGAME, va("pak%i", i), core_hashes[i]); }
	for(i=0; i<ARRAY_LEN(missionpack_hashes); ++i) {
		missionpack_states[i] = get_pak_state("missionpack", va("pak%i", i), missionpack_hashes[i]);
		if(missionpack_states[i].name_match || missionpack_states[i].hash_match) missionpack_installed = qtrue; }

	// Check for standalone mode
	if(Q_stricmp(com_basegame->string, BASEGAME)) {
		qboolean have_id_pak = qfalse;
		for(i=0; i<ARRAY_LEN(core_hashes); ++i) if(core_states[i].hash_match) have_id_pak = qtrue;
		for(i=0; i<ARRAY_LEN(missionpack_hashes); ++i) if(missionpack_states[i].hash_match) have_id_pak = qtrue;
		if(!have_id_pak) {
			Com_Printf("Enabling standalone mode - no ID paks found\n");
			Cvar_Set("com_standalone", "1");
			return; } }

	// Print console warning messages and build warning popup string
	for(i=0; i<ARRAY_LEN(core_hashes); ++i) {
		generate_pak_warnings(BASEGAME, va("pak%i", i), &core_states[i], &warning_popup_stream); }
	if(missionpack_installed) for(i=0; i<ARRAY_LEN(missionpack_hashes); ++i) {
		generate_pak_warnings("missionpack", va("pak%i", i), &missionpack_states[i], &warning_popup_stream); }

	// Check for missing default.cfg
	if(!check_default_cfg_pk3(BASEGAME, "pak0", core_hashes[0])) {
		if(core_states[0].name_match || core_states[0].hash_match) Com_Error(ERR_FATAL,
			BASEGAME "/pak0.pk3 appears to be corrupt (missing default.cfg). Please recopy"
			" this file from your Quake 3 CD or reinstall Quake 3. You may also try"
			" deleting fscache.dat in case it has become corrupt");
		else Com_Error(ERR_FATAL, BASEGAME "/pak0.pk3 is missing. Please recopy"
			" this file from your Quake 3 CD or reinstall Quake 3"); }

#ifndef DEDICATED
	// If warning popup info was generated, display warning popup
	if(warning_popup_stream.position) {
		dialogResult_t result = Sys_Dialog(DT_OK_CANCEL, va("The following game files appear"
			" to be missing or corrupt. You can try to run the game anyway, but you may"
			" experience errors or problems connecting to remote servers.\n\n%s\n"
			"You may need to reinstall Quake 3, the v1.32 patch, and/or team arena.",
			warning_popup_buffer), "File Warning");
		if(result == DR_CANCEL) Sys_Quit(); }
#endif
}
#endif

#endif	// NEW_FILESYSTEM
