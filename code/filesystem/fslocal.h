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

#include "fscore/fscore.h"
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef ELITEFORCE
// These paks get special precedence. Can be non-defined to disable.
#define FS_CORE_PAKS { 3376297517, 596947475, 3960871590, 1592359207 }
#define FS_NODOWNLOAD_PAKS 4

#ifdef CMOD_QVM_SELECTION
#define CMOD_PAKS { \
	401438010,		/* pakcmod-current-2021-09-18.pk3 */ \
	-749739206,		/* pakcmod-current-2021-09-25.pk3 */ \
	-1518584883,	/* pakcmod-current-2021-10-15.pk3 */ \
	34943118,		/* pakcmod-current-2021-11-11.pk3 */ \
	1803491023,		/* pakcmod-current-2021-12-03.pk3 */ \
	1289620810,		/* pakcmod-current-2021-12-28.pk3 */ \
	278974329,		/* pakcmod-current-2021-04-03.pk3 */ \
}
#endif
#else
#ifndef STANDALONE
// These paks get special precedence. Can be non-defined to disable.
#define FS_CORE_PAKS { 2430342401u, 511014160u, 2662638993u, 1438664554u, \
		1566731103u, 298122907u, 412165236u, 2991495316u, 1197932710u, \
		4087071573u, 3709064859u, 908855077u, 977125798u }
#define FS_CORE_PAKS_TEAMARENA { 1566731103u, 298122907u, 412165236u, 2991495316u, \
		1197932710u, 4087071573u, 3709064859u, 908855077u, 977125798u, \
		2430342401u, 511014160u, 2662638993u, 1438664554u }

// This has to do with blocking certain files from being auto-downloaded.
#define FS_NODOWNLOAD_PAKS 9
#define FS_NODOWNLOAD_PAKS_TEAMARENA 4
#endif
#endif

// Only enable servercfg directory support on dedicated server builds.
#ifdef DEDICATED
#define FS_SERVERCFG_ENABLED
#endif

#define FS_MAX_SOURCEDIRS 16

typedef struct {
	const char *name;
	const char *path;
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

typedef enum {
	MODTYPE_INACTIVE,
	MODTYPE_BASE,
	MODTYPE_OVERRIDE_DIRECTORY,
	MODTYPE_CURRENT_MOD
} fs_modtype_t;

#define FS_FILE_BUFFER_SIZE 512

#define FD_CHECK_PURE_LIST 1		// Check if file is blocked by connected server pure configuration
#define FD_CHECK_READ_INACTIVE_MODS 2		// Check if file is blocked for file lookup by fs_read_inactive_mods setting
#define FD_CHECK_READ_INACTIVE_MODS_IGNORE_SERVERCFG 4		// Same as above, but also treat servercfg as an inactive mod
#define FD_CHECK_LIST_INACTIVE_MODS 8		// Check if file is blocked for file listing by fs_list_inactive_mods setting
#define FD_CHECK_LIST_SERVERCFG_LIMIT 16	// Check if files is blocked for file listing due to fs_servercfg_listlimit

#define STACKPTR( pointer ) ( FSC_STACK_RETRIEVE( &fs.index.general_stack, pointer, fsc_false ) )	// non-null
#define STACKPTRN( pointer ) ( FSC_STACK_RETRIEVE( &fs.index.general_stack, pointer, fsc_true ) )	// null allowed

typedef struct {
	cvar_t *fs_dirs;
	cvar_t *fs_game;
	cvar_t *fs_mod_settings;
	cvar_t *fs_index_cache;
	cvar_t *fs_read_inactive_mods;
	cvar_t *fs_list_inactive_mods;
	cvar_t *fs_download_manifest;
	cvar_t *fs_pure_manifest;
	cvar_t *fs_redownload_across_mods;
	cvar_t *fs_full_pure_validation;
	cvar_t *fs_download_mode;
	cvar_t *fs_auto_refresh_enabled;
	#ifdef FS_SERVERCFG_ENABLED
	cvar_t *fs_servercfg;
	cvar_t *fs_servercfg_listlimit;
	cvar_t *fs_servercfg_writedir;
	#endif

	cvar_t *fs_debug_state;
	cvar_t *fs_debug_refresh;
	cvar_t *fs_debug_fileio;
	cvar_t *fs_debug_lookup;
	cvar_t *fs_debug_references;
	cvar_t *fs_debug_filelist;
} fs_cvars_t;

typedef struct {
	qboolean initialized;
	fsc_filesystem_t index;
	fs_cvars_t cvar;

	fs_source_directory_t sourcedirs[FS_MAX_SOURCEDIRS];
	qboolean read_only;

	char current_mod_dir[FSC_MAX_MODDIR];
	const fsc_file_direct_t *current_map_pk3;
	int checksum_feed;

	int connected_server_sv_pure;
	pk3_list_t connected_server_pure_list;
} fs_local_t;

extern fs_local_t fs;

// Perform second pass including fspublic.h, loading only the DEF_LOCAL defines.
#define FSLOCAL
#undef DEF_PUBLIC
#undef DEF_LOCAL
#define DEF_PUBLIC( f )
#define DEF_LOCAL( f ) f;
#include "fspublic.h"
