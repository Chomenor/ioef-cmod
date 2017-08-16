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

/* ******************************************************************************** */
// Definitions
/* ******************************************************************************** */

#ifndef STANDALONE
// These paks get special precedence. Can be non-defined to disable.
#define FS_SYSTEM_PAKS {2430342401u, 511014160u, 2662638993u, 1438664554u, \
		1566731103u, 298122907u, 412165236u, 2991495316u, 1197932710u, \
		4087071573u, 3709064859u, 908855077u, 977125798u}
#define FS_SYSTEM_PAKS_TEAMARENA {1566731103u, 298122907u, 412165236u, 2991495316u, \
		1197932710u, 4087071573u, 3709064859u, 908855077u, 977125798u, \
		2430342401u, 511014160u, 2662638993u, 1438664554u}

// This has to do with blocking certain files from being auto-downloaded.
#define FS_NODOWNLOAD_PAKS 9
#define FS_NODOWNLOAD_PAKS_TEAMARENA 4
#endif

#ifdef DEDICATED
#	define Q3CONFIG_CFG "q3config_server.cfg"
#else
#	define Q3CONFIG_CFG "q3config.cfg"
#endif

#ifdef WIN32
#define SYSTEM_NEWLINE "\r\n"
#else
#define SYSTEM_NEWLINE "\n"
#endif

typedef struct fsc_file_s fsc_file_t;
typedef struct fsc_shader_s fsc_shader_t;

// Path Generation
#define FS_MAX_PATH 512
#define FS_NO_SANITIZE 1
#define FS_CREATE_DIRECTORIES 2
#define FS_CREATE_DIRECTORIES_FOR_FILE 4
#define FS_ALLOW_SLASH 8
#define FS_ALLOW_PK3 16
#define FS_ALLOW_DLL 32
#define FS_ALLOW_SPECIAL_CFG 64

typedef enum {
	FS_CONFIGTYPE_NONE,
	FS_CONFIGTYPE_DEFAULT,
	FS_CONFIGTYPE_SETTINGS
} fs_config_type_t;

typedef enum {
	FS_HANDLEOWNER_SYSTEM,
	FS_HANDLEOWNER_CGAME,
	FS_HANDLEOWNER_UI,
	FS_HANDLEOWNER_QAGAME
} fs_handle_owner_t;

#ifdef FSLOCAL
typedef struct {
	char *name;
	cvar_t *path_cvar;
	qboolean active;
} fs_source_directory_t;

typedef struct fs_hashtable_entry_s {
	struct fs_hashtable_entry_s *next;
} fs_hashtable_entry_t;

typedef struct {
	fs_hashtable_entry_t **buckets;
	int bucket_count;
	int element_count;
} fs_hashtable_t;

typedef struct {
	fs_hashtable_t *ht;
	int current_bucket;
	int bucket_limit;
	fs_hashtable_entry_t *current_entry;
} fs_hashtable_iterator_t;

typedef struct pk3_list_entry_s {
	fs_hashtable_entry_t hte;
	unsigned int hash;
	int position;
} pk3_list_entry_t;

typedef struct {
	fs_hashtable_t ht;
} pk3_list_t;

#define STACKPTR(pointer) ( fsc_stack_retrieve(&fs.general_stack, pointer) )
#endif

/* ******************************************************************************** */
// Main
/* ******************************************************************************** */

#ifdef FSLOCAL
extern cvar_t *fs_dirs;
extern cvar_t *fs_mod_settings;
extern cvar_t *fs_index_cache;
extern cvar_t *fs_search_inactive_mods;
extern cvar_t *fs_reference_inactive_mods;
extern cvar_t *fs_redownload_across_mods;
extern cvar_t *fs_full_pure_validation;
extern cvar_t *fs_saveto_dlfolder;
extern cvar_t *fs_restrict_dlfolder;

extern cvar_t *fs_debug_state;
extern cvar_t *fs_debug_refresh;
extern cvar_t *fs_debug_fileio;
extern cvar_t *fs_debug_lookup;
extern cvar_t *fs_debug_references;
extern cvar_t *fs_debug_filelist;

#define FS_SOURCEDIR_COUNT 4
extern fs_source_directory_t fs_sourcedirs[FS_SOURCEDIR_COUNT];
extern qboolean fs_read_only;

extern fsc_filesystem_t fs;

extern cvar_t *fs_game;
extern char current_mod_dir[FSC_MAX_MODDIR];
extern const fsc_file_direct_t *current_map_pk3;
extern int checksum_feed;

extern int connected_server_sv_pure;
extern pk3_list_t connected_server_pk3_list;
#endif

// State Accessors
const char *FS_GetCurrentGameDir(void);
const char *fs_pid_file_directory(void);
qboolean FS_Initialized( void );
#ifdef FSLOCAL
int fs_connected_server_pure_state(void);
#endif

// State Modifiers
void fs_register_current_map(const char *name);
void fs_set_connected_server_sv_pure_value(int sv_pure);
void FS_PureServerSetLoadedPaks(const char *hash_list, const char *name_list);
void fs_disconnect_cleanup(void);
void fs_set_mod_dir(const char *value, qboolean move_pid);
qboolean FS_ConditionalRestart(int checksumFeed, qboolean disconnect);

// Filesystem Refresh
void fs_refresh(qboolean quiet);
void fs_auto_refresh(void);

// Filesystem Initialization
#ifdef FSLOCAL
void fs_indexcache_write(void);
#endif
void fs_startup(void);

/* ******************************************************************************** */
// Lookup
/* ******************************************************************************** */

#ifdef FSLOCAL
void debug_resource_comparison(int resource1_position, int resource2_position);
const fsc_file_t *fs_config_lookup(const char *name, fs_config_type_t type, qboolean debug);
#endif

const fsc_file_t *fs_general_lookup(const char *name, qboolean use_pure_list, qboolean use_current_map, qboolean debug);
const fsc_shader_t *fs_shader_lookup(const char *name, int flags, qboolean debug);
const fsc_file_t *fs_image_lookup(const char *name, int flags, qboolean debug);
const fsc_file_t *fs_sound_lookup(const char *name, qboolean debug);
const fsc_file_t *fs_vm_lookup(const char *name, qboolean qvm_only, qboolean debug, qboolean *is_dll_out);

/* ******************************************************************************** */
// File Listing
/* ******************************************************************************** */

#ifdef FSLOCAL
char **FS_ListFilteredFiles(const char *path, const char *extension, const char *filter,
		int *numfiles_out, qboolean allowNonPureFilesOnDisk);
#endif

void FS_FreeFileList( char **list );
char **FS_ListFiles( const char *path, const char *extension, int *numfiles );
int	FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize );

/* ******************************************************************************** */
// File IO
/* ******************************************************************************** */

// Path Handling
#ifdef FSLOCAL
int fs_generate_path_sourcedir(int source_dir_id, const char *path1, const char *path2,
		int path1_flags, int path2_flags, char *target, int target_size);
#endif
int fs_generate_path(const char *path1, const char *path2, const char *path3,
		int path1_flags, int path2_flags, int path3_flags,
		char *target, int target_size);
int fs_generate_path_writedir(const char *path1, const char *path2, int path1_flags, int path2_flags,
		char *target, int target_size);

// Direct file access
#ifdef FSLOCAL
void *fs_open_file(const char *path, const char *mode);
void fs_rename_file(const char *source, const char *target);
void fs_delete_file(const char *path);
qboolean FS_FileInPathExists(const char *testpath);
#endif
void FS_HomeRemove( const char *homePath );
qboolean FS_FileExists(const char *file);

// File read cache
#ifdef FSLOCAL
void fs_readcache_debug(void);
#endif
void fs_cache_initialize(void);
void fs_advance_cache_stage(void);

// Data reading
char *fs_read_data(const fsc_file_t *file, const char *path, unsigned int *size_out);
void fs_free_data(char *data);
char *fs_read_shader(const fsc_shader_t *shader);

// Direct read handle operations
fileHandle_t fs_direct_read_handle_open(const fsc_file_t *file, const char *path, unsigned int *size_out);

// Pipe files
fileHandle_t FS_FCreateOpenPipeFile( const char *filename );

// Common handle operations
#ifdef FSLOCAL
void fs_print_handle_list(void);
#endif
void fs_handle_close(fileHandle_t handle);
void fs_close_all_handles(void);
fs_handle_owner_t fs_handle_get_owner(fileHandle_t handle);
void fs_close_owner_handles(fs_handle_owner_t owner);

// Journal Data File Functions
#ifdef FSLOCAL
void fs_write_journal_data(const char *data, unsigned int length);
char *fs_read_journal_data(void);
#endif

// Config file functions
fileHandle_t fs_open_settings_file_write(const char *filename);

// Misc Handle Operations
long FS_ReadFile(const char *qpath, void **buffer);
void FS_FreeFile(void *buffer);
long FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE);
long FS_SV_FOpenFileRead(const char *filename, fileHandle_t *fp);
fileHandle_t FS_FOpenFileWrite(const char *filename);
fileHandle_t FS_SV_FOpenFileWrite(const char *filename);
fileHandle_t FS_FOpenFileAppend(const char *filename);
int FS_FOpenFileByModeOwner(const char *qpath, fileHandle_t *f, fsMode_t mode, fs_handle_owner_t owner);
int FS_FOpenFileByMode(const char *qpath, fileHandle_t *f, fsMode_t mode);
void FS_FCloseFile(fileHandle_t f);
int FS_Read(void *buffer, int len, fileHandle_t f);
int FS_Read2(void *buffer, int len, fileHandle_t f);
int FS_Write(const void *buffer, int len, fileHandle_t h);
int FS_Seek(fileHandle_t f, long offset, int origin);
int FS_FTell(fileHandle_t f);
void FS_Flush(fileHandle_t f);
void FS_ForceFlush(fileHandle_t f);
void FS_WriteFile( const char *qpath, const void *buffer, int size );

/* ******************************************************************************** */
// Commands
/* ******************************************************************************** */

#ifdef FSLOCAL
void fs_register_commands(void);
#endif

/* ******************************************************************************** */
// Download
/* ******************************************************************************** */

// Download List Handling
void fs_advance_download(void);
void fs_register_download_list(const char *hash_list, const char *name_list);

// Attempted Download Tracking
void fs_register_current_download_attempt(qboolean http);
void fs_clear_attempted_downloads(void);

// Download List Advancement
void fs_advance_next_needed_download(qboolean curl_disconnected);
qboolean fs_get_current_download_info(char **local_name_out, char **remote_name_out,
		qboolean *curl_already_attempted_out);

// Download Completion
void fs_finalize_download(void);

/* ******************************************************************************** */
// Referenced Pak Tracking
/* ******************************************************************************** */

#ifdef FSLOCAL
void fs_register_reference(const fsc_file_t *file);
#endif
void FS_ClearPakReferences( int flags );
const char *FS_ReferencedPakNames( void );
const char *FS_ReferencedPakPureChecksums( void );
void fs_set_download_list(void);
fileHandle_t fs_open_download_pak(const char *path, unsigned int *size_out);
const char *FS_LoadedPakChecksums( void );
const char *FS_LoadedPakNames( void );

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

#ifdef FSLOCAL
// Hash Table
void fs_hashtable_initialize(fs_hashtable_t *hashtable, int bucket_count);
void fs_hashtable_insert(fs_hashtable_t *hashtable, fs_hashtable_entry_t *entry, unsigned int hash);
fs_hashtable_iterator_t fs_hashtable_iterate(fs_hashtable_t *hashtable, unsigned int hash, qboolean iterate_all);
void *fs_hashtable_next(fs_hashtable_iterator_t *iterator);
void fs_hashtable_free(fs_hashtable_t *hashtable, void (*free_entry)(fs_hashtable_entry_t *entry));
void fs_hashtable_reset(fs_hashtable_t *hashtable, void (*free_entry)(fs_hashtable_entry_t *entry));

// Pk3 List
void pk3_list_initialize(pk3_list_t *pk3_list, unsigned int bucket_count);
void pk3_list_insert(pk3_list_t *pk3_list, unsigned int hash);
int pk3_list_lookup(const pk3_list_t *pk3_list, unsigned int hash, qboolean reverse);
void pk3_list_free(pk3_list_t *pk3_list);

// System pk3 checks
int system_pk3_position(unsigned int hash);

// File Sorting Functions
void fs_generate_file_sort_key(const fsc_file_t *file, fsc_stream_t *output, qboolean use_server_pak_list);
int fs_compare_file(const fsc_file_t *file1, const fsc_file_t *file2, qboolean use_server_pak_list);
int fs_compare_file_name(const fsc_file_t *file1, const fsc_file_t *file2);
#endif

// File helper functions
#ifdef FSLOCAL
#define FS_FILE_BUFFER_SIZE 512
int fs_get_source_dir_id(const fsc_file_t *file);
char *fs_get_source_dir_string(const fsc_file_t *file);
qboolean fs_inactive_mod_file_disabled(const fsc_file_t *file, int level);
void fs_file_to_stream(const fsc_file_t *file, fsc_stream_t *stream, qboolean include_source_dir,
			qboolean include_mod, qboolean include_pk3_origin, qboolean include_size);
void fs_file_to_buffer(const fsc_file_t *file, char *buffer, int buffer_size, qboolean include_source_dir,
			qboolean include_mod, qboolean include_pk3_origin, qboolean include_size);
#endif
char *fs_file_extension(const fsc_file_t *file);
void fs_print_file_location(const fsc_file_t *file);

// Misc Functions
#ifdef FSLOCAL
qboolean FS_idPak(char *pak, char *base, int numPaks);
#endif
void fs_execute_config_file(const char *name, fs_config_type_t config_type, cbufExec_t exec_type, qboolean quiet);
void *fs_load_game_dll(const fsc_file_t *dll_file, intptr_t (QDECL **entryPoint)(int, ...),
			intptr_t (QDECL *systemcalls)(intptr_t, ...));
int fs_valid_md3_lods(int max_lods, const char *name, const char *extension);
void FS_GetModDescription(const char *modDir, char *description, int descriptionLen);
void FS_FilenameCompletion( const char *dir, const char *ext,
		qboolean stripExt, void(*callback)(const char *s), qboolean allowNonPureFilesOnDisk );
qboolean FS_FilenameCompare( const char *s1, const char *s2 );
void QDECL FS_Printf( fileHandle_t f, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));

#ifdef FSLOCAL
// QVM Hash Verification
qboolean calculate_file_sha256(const fsc_file_t *file, unsigned char *output);
qboolean fs_check_trusted_vm_hash(unsigned char *hash);
qboolean fs_check_trusted_vm_file(const fsc_file_t *file);
void sha256_to_stream(unsigned char *sha, fsc_stream_t *output);

// System Pak Verification
void fs_check_system_paks(void);
#endif
