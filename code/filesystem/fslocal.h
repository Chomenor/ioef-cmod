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
#define FS_HAVE_CORE_TYPEDEFS
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifndef STANDALONE
// These paks get special precedence. Can be non-defined to disable.
#define FS_CORE_PAKS { -1864624895, 511014160, -1632328303, 1438664554, \
		1566731103, 298122907, 412165236, -1303471980, 1197932710, \
		-207895723, -585902437, 908855077, 977125798 }
#define FS_CORE_PAKS_TEAMARENA { 1566731103, 298122907, 412165236, -1303471980, \
		1197932710, -207895723, -585902437, 908855077, 977125798, \
		-1864624895, 511014160, -1632328303, 1438664554 }

// This has to do with blocking certain files from being auto-downloaded.
#define FS_NODOWNLOAD_PAKS 9
#define FS_NODOWNLOAD_PAKS_TEAMARENA 4
#endif

// Only enable servercfg directory support on dedicated server builds.
#ifdef DEDICATED
#define FS_SERVERCFG_ENABLED
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#define FS_XDG_HOME_SUPPORT
#endif

#define FS_MAX_SOURCEDIRS 16

typedef struct {
	const char *name;
	const char *path;
	qboolean active;
	qboolean writable;
#ifdef FS_XDG_HOME_SUPPORT
	xdg_home_type_t xdgType;
#endif
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
