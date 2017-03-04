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

static unsigned int mod_dir_precedence(const fsc_file_t *file) {
	const char *mod_dir = fsc_get_mod_dir(file, &fs);
	if(*current_mod_dir && !Q_stricmp(mod_dir, current_mod_dir)) return 2;
	if(!Q_stricmp(mod_dir, "basemod")) return 1;
	return 0; }

static unsigned int system_pak_precedence(const fsc_file_t *file) {
	if(file->sourcetype == FSC_SOURCETYPE_PK3) {
		return system_pk3_position(((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash); }
	if(file->sourcetype == FSC_SOURCETYPE_DIRECT) {
		return system_pk3_position(((fsc_file_direct_t *)file)->pk3_hash); }
	return 0; }

static unsigned int basegame_dir_precedence(const fsc_file_t *file) {
	const char *mod_dir = fsc_get_mod_dir(file, &fs);
	if(!Q_stricmp(mod_dir, com_basegame->string)) return 1;
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
	if(use_server_pak_list) write_sort_value(server_pak_precedence(file), output);
	write_sort_value(mod_dir_precedence(file), output);
	write_sort_value(system_pak_precedence(file), output);
	write_sort_value(basegame_dir_precedence(file), output);
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
// File helper functions
/* ******************************************************************************** */

char *fs_file_extension(const fsc_file_t *file) {
	return STACKPTR(file->qp_ext_ptr); }

int fs_get_source_dir_id(const fsc_file_t *file) {
	if(file->sourcetype == FSC_SOURCETYPE_DIRECT) {
		return ((const fsc_file_direct_t *)file)->source_dir_id; }
	else if (file->sourcetype == FSC_SOURCETYPE_PK3) {
		return ((const fsc_file_direct_t *)STACKPTR(((const fsc_file_frompk3_t *)file)->source_pk3))->source_dir_id; }
	else return -1; }

char *fs_get_source_dir_string(const fsc_file_t *file) {
	int id = fs_get_source_dir_id(file);
	if(id >= 0 && id < FS_SOURCEDIR_COUNT) return sourcedirs[id].name;
	return "unknown"; }

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

// Used by vm.c
void *fs_load_game_dll(const char *name, intptr_t (QDECL **entryPoint)(int, ...),
			intptr_t (QDECL *systemcalls)(intptr_t, ...)) {
	// Returns dll handle, or null if no dll loaded
	const fsc_file_t *dll_file;
	char dll_info_string[FS_FILE_BUFFER_SIZE];
	const void *dll_path;
	char *dll_path_string;
	void *dll_handle;

	// Abort if no file is selected or a qvm is selected rather than a dll
	dll_file = fs_gamedll_lookup(name, 0);
	if(!dll_file) return 0;
	if(dll_file->qp_ext_ptr && !Q_stricmp(STACKPTR(dll_file->qp_ext_ptr), "qvm")) return 0;

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

int fs_valid_md3_lods(int max_lods, const char *name, const char *extension) {
	// Returns number of valid md3 lods to attempt loading
	// Ensures all lods are from the same location/pk3 to avoid inconsistencies
	char namebuf[FSC_MAX_QPATH];
	const fsc_file_t *base_file;
	int lod;

	for(lod=0; lod<max_lods; ++lod) {
		// Get current_file
		const fsc_file_t *current_file;
		if(lod) Com_sprintf(namebuf, sizeof(namebuf), "%s_%d.%s", name, lod, extension);
		else Com_sprintf(namebuf, sizeof(namebuf), "%s.%s", name, extension);
		current_file = fs_general_lookup(namebuf, qtrue, qtrue, qfalse);
		if(!current_file) return lod;

		// If it's lod 0 save it as base file
		if(!lod) base_file = current_file;

		// Otherwise make sure it's compatible with base file
		else {
			if(current_file->sourcetype != base_file->sourcetype) {
				if(com_developer->integer || fs_debug_lookup->integer)
					Com_Printf("WARNING: Skipping md3 lod from different sourcetypes for %s\n", name);
				return lod; }
			if(current_file->sourcetype == FSC_SOURCETYPE_PK3 &&
					((fsc_file_frompk3_t *)current_file)->source_pk3 !=
					((fsc_file_frompk3_t *)base_file)->source_pk3) {
				if(com_developer->integer || fs_debug_lookup->integer)
					Com_Printf("WARNING: Skipping md3 lod from different paks for %s\n", name);
				return lod; } } }

	return max_lods; }

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

/* ******************************************************************************** */
// ID Pak Verification
/* ******************************************************************************** */

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

#ifndef STANDALONE

#define	DEMO_PAK0_CHECKSUM	2985612116u
static const unsigned int pak_checksums[] = {
	1566731103u,
	298122907u,
	412165236u,
	2991495316u,
	1197932710u,
	4087071573u,
	3709064859u,
	908855077u,
	977125798u
};

#define NUM_ID_PAKS		9
#define NUM_TA_PAKS		4

static const unsigned int missionpak_checksums[] =
{
	2430342401u,
	511014160u,
	2662638993u,
	1438664554u
};

/*
===================
FS_CheckPak0

Check whether any of the original id pak files is present,
and start up in standalone mode, if there are none and a
different com_basegame was set.
Note: If you're building a game that doesn't depend on the
Q3 media pak0.pk3, you'll want to remove this by defining
STANDALONE in q_shared.h
===================
*/
void FS_CheckPak0( void )
{
	int i;
	fsc_hashtable_iterator_t hti;
	fsc_pk3_hash_map_entry_t *hash_entry;
	qboolean founddemo = qfalse;
	unsigned int foundPak = 0, foundTA = 0;

	for(i=0; i<fs.pk3_hash_lookup.bucket_count; ++i) {
		fsc_hashtable_open(&fs.pk3_hash_lookup, i, &hti);
		while((hash_entry = STACKPTR(fsc_hashtable_next(&hti)))) {
			const fsc_file_direct_t *pak = STACKPTR(hash_entry->pk3);
			const char *pakBasename = STACKPTR(pak->f.qp_name_ptr);
			const char *pakGamename = fsc_get_mod_dir((fsc_file_t *)pak, &fs);
			unsigned int pakChecksum = pak->pk3_hash;
			if(!fsc_is_file_enabled((fsc_file_t *)pak, &fs)) continue;

			if(!Q_stricmpn( pakGamename, "demoq3", MAX_OSPATH )
					&& !Q_stricmpn( pakBasename, "pak0", MAX_OSPATH ))
			{
				if(pakChecksum == DEMO_PAK0_CHECKSUM)
					founddemo = qtrue;
			}

			else if(!Q_stricmpn( pakGamename, BASEGAME, MAX_OSPATH )
					&& strlen(pakBasename) == 4 && !Q_stricmpn( pakBasename, "pak", 3 )
					&& pakBasename[3] >= '0' && pakBasename[3] <= '0' + NUM_ID_PAKS - 1)
			{
				if( pakChecksum != pak_checksums[pakBasename[3]-'0'] )
				{
					if(pakBasename[3] == '0')
					{
						Com_Printf("\n\n"
								"**************************************************\n"
								"WARNING: " BASEGAME "/pak0.pk3 is present but its checksum (%u)\n"
								"is not correct. Please re-copy pak0.pk3 from your\n"
								"legitimate Q3 CDROM.\n"
								"**************************************************\n\n\n",
								pakChecksum );
					}
					else
					{
						Com_Printf("\n\n"
								"**************************************************\n"
								"WARNING: " BASEGAME "/pak%d.pk3 is present but its checksum (%u)\n"
								"is not correct. Please re-install the point release\n"
								"**************************************************\n\n\n",
								pakBasename[3]-'0', pakChecksum );
					}
				}

				foundPak |= 1<<(pakBasename[3]-'0');
			}
			else if(!Q_stricmpn(pakGamename, BASETA, MAX_OSPATH)
					&& strlen(pakBasename) == 4 && !Q_stricmpn(pakBasename, "pak", 3)
					&& pakBasename[3] >= '0' && pakBasename[3] <= '0' + NUM_TA_PAKS - 1)

			{
				if(pakChecksum != missionpak_checksums[pakBasename[3]-'0'])
				{
					Com_Printf("\n\n"
							"**************************************************\n"
							"WARNING: " BASETA "/pak%d.pk3 is present but its checksum (%u)\n"
							"is not correct. Please re-install Team Arena\n"
							"**************************************************\n\n\n",
							pakBasename[3]-'0', pakChecksum );
				}

				foundTA |= 1 << (pakBasename[3]-'0');
			}
			else
			{
				int index;

				// Finally check whether this pak's checksum is listed because the user tried
				// to trick us by renaming the file, and set foundPak's highest bit to indicate this case.

				for(index = 0; index < ARRAY_LEN(pak_checksums); index++)
				{
					if(pakChecksum == pak_checksums[index])
					{
						char buffer[FS_FILE_BUFFER_SIZE];
						fs_file_to_buffer((fsc_file_t *)pak, buffer, sizeof(buffer), qtrue, qtrue, qtrue, qfalse);

						Com_Printf("\n\n"
								"**************************************************\n"
								"WARNING: %s is renamed pak file %s%cpak%d.pk3\n"
								"Running in standalone mode won't work\n"
								"Please rename, or remove this file\n"
								"**************************************************\n\n\n",
								buffer, BASEGAME, PATH_SEP, index);


						foundPak |= 0x80000000;
					}
				}

				for(index = 0; index < ARRAY_LEN(missionpak_checksums); index++)
				{
					if(pakChecksum == missionpak_checksums[index])
					{
						char buffer[FS_FILE_BUFFER_SIZE];
						fs_file_to_buffer((fsc_file_t *)pak, buffer, sizeof(buffer), qtrue, qtrue, qtrue, qfalse);

						Com_Printf("\n\n"
								"**************************************************\n"
								"WARNING: %s is renamed pak file %s%cpak%d.pk3\n"
								"Running in standalone mode won't work\n"
								"Please rename, or remove this file\n"
								"**************************************************\n\n\n",
								buffer, BASETA, PATH_SEP, index);

						foundTA |= 0x80000000;
					}
				}
			}
		}
	}

	if(!foundPak && !foundTA && Q_stricmp(com_basegame->string, BASEGAME))
	{
		Cvar_Set("com_standalone", "1");
	}
	else
		Cvar_Set("com_standalone", "0");

	if(!com_standalone->integer)
	{
		if(!(foundPak & 0x01))
		{
			if(founddemo)
			{
				Com_Printf( "\n\n"
						"**************************************************\n"
						"WARNING: It looks like you're using pak0.pk3\n"
						"from the demo. This may work fine, but it is not\n"
						"guaranteed or supported.\n"
						"**************************************************\n\n\n" );

				foundPak |= 0x01;
			}
		}
	}


	if(!com_standalone->integer && (foundPak & 0x1ff) != 0x1ff)
	{
		char errorText[MAX_STRING_CHARS] = "";

		if((foundPak & 0x01) != 0x01)
		{
			Q_strcat(errorText, sizeof(errorText),
					"\"pak0.pk3\" is missing. Please copy it "
					"from your legitimate Q3 CDROM. ");
		}

		if((foundPak & 0x1fe) != 0x1fe)
		{
			Q_strcat(errorText, sizeof(errorText),
					"Point Release files are missing. Please "
					"re-install the 1.32 point release. ");
		}

		Q_strcat(errorText, sizeof(errorText),
				va("Also check that your ioq3 executable is in "
					"the correct place and that every file "
					"in the \"%s\" directory is present and readable", BASEGAME));

		Com_Error(ERR_FATAL, "%s", errorText);
	}

	if(!com_standalone->integer && foundTA && (foundTA & 0x0f) != 0x0f)
	{
		char errorText[MAX_STRING_CHARS] = "";

		if((foundTA & 0x01) != 0x01)
		{
			Com_sprintf(errorText, sizeof(errorText),
					"\"" BASETA "%cpak0.pk3\" is missing. Please copy it "
					"from your legitimate Quake 3 Team Arena CDROM. ", PATH_SEP);
		}

		if((foundTA & 0x0e) != 0x0e)
		{
			Q_strcat(errorText, sizeof(errorText),
					"Team Arena Point Release files are missing. Please "
					"re-install the latest Team Arena point release.");
		}

		Com_Error(ERR_FATAL, "%s", errorText);
	}
}
#endif

#endif	// NEW_FILESYSTEM
