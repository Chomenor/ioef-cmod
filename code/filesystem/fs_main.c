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

// Main filesystem state. The convention is that this can be accessed from anywhere in the
// filesystem code, but should only be modified through this file.
fs_local_t fs;

/*
###############################################################################################

Filesystem State Accessors

###############################################################################################
*/

/*
=================
FS_GetCurrentGameDir

Returns current mod dir, but with empty dir replaced by basegame.
=================
*/
const char *FS_GetCurrentGameDir( void ) {
	if ( *fs.current_mod_dir ) {
		return fs.current_mod_dir;
	}
	return com_basegame->string;
}

/*
=================
FS_PidFileDirectory

Returns mod dir to use for storing PID file. The PID file is used to generate a reset
prompt if the settings file in a certain mod directory might be causing crashes.
=================
*/
const char *FS_PidFileDirectory( void ) {
	if ( fs.cvar.fs_mod_settings->integer ) {
		return FS_GetCurrentGameDir();
	}
	return com_basegame->string;
}

/*
=================
FS_Initialized

Returns whether filesystem is initialized.
=================
*/
qboolean FS_Initialized( void ) {
	return fs.initialized;
}

/*
=================
FS_ConnectedServerPureState

Returns:
- 2 if connected to semi-pure server
- 1 if connected to a pure server
- 0 if not connected to any server or connected to unpure server
=================
*/
int FS_ConnectedServerPureState( void ) {
	if ( !fs.connected_server_pure_list.ht.element_count )
		return 0;
	if ( fs.connected_server_sv_pure == 2 )
		return 2;
	return 1;
}

/*
###############################################################################################

Filesystem State Modifiers

###############################################################################################
*/

/*
=================
FS_RegisterCurrentMap

Registers the pk3 of the current map with the filesystem, to support granting it some precedence elevations.
=================
*/
void FS_RegisterCurrentMap( const char *name ) {
	const fsc_file_t *bsp_file = FS_GeneralLookup( name, LOOKUPFLAG_IGNORE_CURRENT_MAP, qfalse );
	if ( !bsp_file || bsp_file->sourcetype != FSC_SOURCETYPE_PK3 ) {
		fs.current_map_pk3 = NULL;
	} else {
		fs.current_map_pk3 = FSC_GetBaseFile( bsp_file, &fs.index );
	}

	if ( fs.cvar.fs_debug_state->integer ) {
		char buffer[FS_FILE_BUFFER_SIZE];
		if ( fs.current_map_pk3 ) {
			FS_FileToBuffer( (fsc_file_t *)fs.current_map_pk3, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
		} else {
			Q_strncpyz( buffer, "<none>", sizeof( buffer ) );
		}
		Com_Printf( "fs_state: current_map_pk3 set to '%s'\n", buffer );
	}
}

/*
=================
FS_SetConnectedServerPureValue

Registers the sv_pure value of the server we are connecting to with the filesystem.
=================
*/
void FS_SetConnectedServerPureValue( int sv_pure ) {
	fs.connected_server_sv_pure = sv_pure;
	if ( fs.cvar.fs_debug_state->integer ) {
		Com_Printf( "fs_state: connected_server_sv_pure set to %i\n", sv_pure );
	}
}

/*
=================
FS_PureServerSetLoadedPaks

Registers the pure pk3 list of the server we are connecting to with the filesystem.
=================
*/
void FS_PureServerSetLoadedPaks( const char *hash_list, const char *name_list ) {
	int i;
	int count;

	FS_Pk3List_Free( &fs.connected_server_pure_list );
	FS_Pk3List_Initialize( &fs.connected_server_pure_list, 100 );

	Cmd_TokenizeString( hash_list );
	count = Cmd_Argc();

	// Sanity check
	if ( count > 4096 ) {
		count = 4096;
	}

	for ( i = 0; i < count; ++i ) {
		FS_Pk3List_Insert( &fs.connected_server_pure_list, atoi( Cmd_Argv( i ) ) );
	}

	if ( fs.cvar.fs_debug_state->integer ) {
		Com_Printf( "fs_state: connected_server_pure_list set to '%s'\n", hash_list );
	}
}

/*
=================
FS_DisconnectCleanup

Reset server-specific parameters when disconnecting from a remote server or ending a local game.
=================
*/
void FS_DisconnectCleanup( void ) {
	fs.current_map_pk3 = NULL;
	fs.connected_server_sv_pure = 0;
	FS_Pk3List_Free( &fs.connected_server_pure_list );

	if ( fs.cvar.fs_debug_state->integer ) {
		Com_Printf( "fs_state: disconnect cleanup\n   > current_map_pk3 cleared"
			"\n   > connected_server_sv_pure set to 0\n   > connected_server_pure_list cleared\n" );
	}
}

#ifndef STANDALONE
void Com_AppendCDKey( const char *filename );
void Com_ReadCDKey( const char *filename );
#endif

/*
=================
FS_UpdateModDirExt

Transitions filesystem to a new mod directory. The working mod directory (fs.current_mod_dir)
will be updated to match fs_game.
=================
*/
static void FS_UpdateModDirExt( qboolean move_pid ) {
#ifndef CMOD_NO_SAFE_SETTINGS_PROMPT
	char old_pid_dir[FSC_MAX_MODDIR];
	Q_strncpyz( old_pid_dir, FS_PidFileDirectory(), sizeof( old_pid_dir ) );

#endif
	// Update fs.current_mod_dir and convert basegame to empty value
	Cvar_Get( "fs_game", "", 0 ); // make sure unlatched
	FS_SanitizeModDir( fs.cvar.fs_game->string, fs.current_mod_dir );
	if ( !Q_stricmp( fs.current_mod_dir, "basemod" ) ) {
		fs.current_mod_dir[0] = '\0';
	}
	if ( !Q_stricmp( fs.current_mod_dir, com_basegame->string ) ) {
		fs.current_mod_dir[0] = '\0';
	}

#ifndef CMOD_NO_SAFE_SETTINGS_PROMPT
	// Move pid file to new mod dir if necessary
	if ( move_pid && strcmp( old_pid_dir, FS_PidFileDirectory() ) ) {
		if ( fs.cvar.fs_debug_state->integer ) {
			Com_Printf( "fs_state: switching PID file from %s to %s\n", old_pid_dir, FS_PidFileDirectory() );
		}
		Sys_RemovePIDFile( old_pid_dir );
		Sys_InitPIDFile( FS_PidFileDirectory() );
	}
#endif

	// Read CD keys
#ifndef CMOD_DISABLE_AUTH_STUFF
#ifndef STANDALONE
	if ( !com_standalone->integer ) {
		Com_ReadCDKey( BASEGAME );
		if ( strcmp( FS_GetCurrentGameDir(), BASEGAME ) ) {
			Com_AppendCDKey( FS_GetCurrentGameDir() );
		}
	}
#endif
#endif

	Cvar_Set( "fs_game", fs.current_mod_dir );
	if ( fs.cvar.fs_debug_state->integer ) {
		Com_Printf( "fs_state: fs.current_mod_dir set to %s\n", *fs.current_mod_dir ? fs.current_mod_dir : "<none>" );
	}
}

/*
=================
FS_UpdateModDir
=================
*/
void FS_UpdateModDir( void ) {
	FS_UpdateModDirExt( qtrue );
}

/*
=================
FS_ConditionalRestart

Updates the current mod dir, using a game restart if necessary to load new settings.
Also sets the checksum feed (used for pure validation) and clears references from previous maps.
Returns qtrue if restarting due to changed mod dir, qfalse otherwise.
=================
*/
qboolean FS_ConditionalRestart( int checksumFeed, qboolean disconnect ) {
	char old_mod_dir[FSC_MAX_MODDIR];
	Q_strncpyz( old_mod_dir, fs.current_mod_dir, sizeof( old_mod_dir ) );

	if ( fs.cvar.fs_debug_state->integer ) {
		Com_Printf( "fs_state: FS_ConditionalRestart invoked\n" );
	}

	// Perform some state stuff traditionally done in this function
	FS_ClearPakReferences( 0 );
	fs.checksum_feed = checksumFeed;

	// Update mod dir
	FS_UpdateModDir();

	// Check for default.cfg here and attempt an ERR_DROP if it isn't found, to avoid getting
	// an ERR_FATAL later due to broken pure list.
	if ( !FS_GeneralLookup( "default.cfg", LOOKUPFLAG_PURE_ALLOW_DIRECT_SOURCE, qfalse ) ) {
		Com_Error( ERR_DROP, "Failed to find default.cfg, assuming invalid configuration" );
	}

	// Check if we need to do a restart to load new config files
	if ( fs.cvar.fs_mod_settings->integer && strcmp( fs.current_mod_dir, old_mod_dir ) ) {
		Com_GameRestart( checksumFeed, disconnect );
		return qtrue;
	}

	return qfalse;
}

/*
###############################################################################################

Source Directory Initialization

###############################################################################################
*/

/*
=================
FS_InitWritableDirectory

Attempts to create new source directory and tests writability.
Returns qtrue if test passed, qfalse otherwise.
=================
*/
static qboolean FS_InitWritableDirectory( const char *directory ) {
	char path[FS_MAX_PATH];
	void *fp;

	if ( !FS_GeneratePath( directory, "writetest.dat", NULL, FS_CREATE_DIRECTORIES | FS_NO_SANITIZE,
			0, 0, path, sizeof( path ) ) ) {
		return qfalse;
	}
	fp = FSC_FOpen( path, "wb" );
	if ( !fp ) {
		return qfalse;
	}
	FSC_FClose( fp );

	FSC_DeleteFile( path );
	return qtrue;
}

typedef struct {
	fs_source_directory_t s;
	int fs_dirs_position;	// lower means higher priority
} temp_source_directory_t;

/*
=================
FS_CompareTempSourceDirs

Places inactive directories last, write directory first, and sorts remaining directories
according to position in fs_dirs cvar.
=================
*/
static int FS_CompareTempSourceDirs( const temp_source_directory_t *dir1, const temp_source_directory_t *dir2 ) {
	if ( dir1->s.active && !dir2->s.active )
		return -1;
	if ( dir2->s.active && !dir1->s.active )
		return 1;
	if ( dir1->s.writable && !dir2->s.writable )
		return -1;
	if ( dir2->s.writable && !dir1->s.writable )
		return 1;
	if ( dir1->fs_dirs_position < dir2->fs_dirs_position )
		return -1;
	return 1;
}

/*
=================
FS_CompareTempSourceDirsQsort
=================
*/
static int FS_CompareTempSourceDirsQsort( const void *dir1, const void *dir2 ) {
	return FS_CompareTempSourceDirs( (const temp_source_directory_t *)dir1, (const temp_source_directory_t *)dir2 );
}

/*
=================
FS_InitSourceDirs

Loads source directory paths from cvars into fs_sourcedirs.
=================
*/
static void FS_InitSourceDirs( void ) {
	int i;
	char *fs_dirs_ptr;
	temp_source_directory_t temp_dirs[FS_MAX_SOURCEDIRS];
	const char *homepath = Sys_DefaultNonXdgHomepath();
	qboolean haveFullWriteSupport = qfalse;
#ifdef FS_XDG_HOME_SUPPORT
	qboolean xdgLoaded = qfalse;
	int xdgWritableCount = 0;
#endif

	// Initialize default path cvars
	Cvar_Get( "fs_homepath", homepath ? homepath : "", CVAR_INIT | CVAR_PROTECTED );
	Cvar_Get( "fs_basepath", Sys_DefaultInstallPath(), CVAR_INIT | CVAR_PROTECTED );
#ifndef ELITEFORCE
	Cvar_Get( "fs_steampath", Sys_SteamPath(), CVAR_INIT | CVAR_PROTECTED );
#endif
	Cvar_Get( "fs_gogpath", Sys_GogPath(), CVAR_INIT | CVAR_PROTECTED );
#ifdef __APPLE__
	Cvar_Get( "fs_apppath", Sys_DefaultAppPath(), CVAR_INIT | CVAR_PROTECTED );
#ifdef CMOD_MAC_APP_RESOURCES
	Cvar_Get( "fs_appresources", va( "%s/../Resources", Sys_DefaultAppPath() ), CVAR_INIT | CVAR_PROTECTED );
#endif
#endif

	// Generate temp_dirs based on fs_dirs entries
	Com_Memset( temp_dirs, 0, sizeof( temp_dirs ) );
	fs_dirs_ptr = fs.cvar.fs_dirs->string;
	while ( 1 ) {
		qboolean write_flag = qfalse;
		qboolean write_dir = qfalse;
		const char *token;
		const char *path;

		// Get next token (source dir name) from fs_dirs
		token = COM_ParseExt( &fs_dirs_ptr, qfalse );
		if ( !*token )
			break;

		// Process writable flag
		if ( *token == '*' ) {
			write_flag = qtrue;
			++token;
			if ( !*token ) {
				continue;
			}
		}

		// Look for duplicate entry or next empty slot
		for ( i = 0; i < FS_MAX_SOURCEDIRS; ++i ) {
			if ( !temp_dirs[i].s.active ) {
				break;
			}
			if ( !Q_stricmp( token, temp_dirs[i].s.name ) ) {
				break;
			}
		}
		if ( i >= FS_MAX_SOURCEDIRS ) {
			Com_Printf( "WARNING: FS_MAX_SOURCEDIRS exceeded parsing fs_dirs\n" );
			break;
		}
		if ( temp_dirs[i].s.active ) {
			Com_Printf( "WARNING: Duplicate entry '%s' parsing fs_dirs\n", token );
			continue;
		}

		if ( !Q_stricmp( token, "_xdg_home" ) ) {
#ifdef FS_XDG_HOME_SUPPORT
			int j;

			static const struct {
				xdg_home_type_t xdgType;
				const char *cvar_name;
			} xdg_sources[] = {
				// First entries listed here will have higher search precedence
				{ XDG_CONFIG, "fs_homeconfigpath" },
				{ XDG_DATA, "fs_homedatapath" },
				{ XDG_STATE, "fs_homestatepath" },
				{ XDG_CACHE, "fs_homecachepath" },
			};

			if ( xdgLoaded ) {
				continue;
			}

			for ( j = 0; j < ARRAY_LEN( xdg_sources ); ++j ) {
				const char *typestr = FS_XdgTypeToString( xdg_sources[j].xdgType );
				cvar_t *cvar = Cvar_Get( xdg_sources[j].cvar_name, Sys_DefaultXdgHomepath(xdg_sources[j].xdgType),
					CVAR_INIT | CVAR_PROTECTED );
				write_dir = qfalse;

				if ( !cvar->string[0] ) {
					Com_Printf( "WARNING: Got empty path for xdg-%s directory.\n", typestr );
					continue;
				}

				if ( i >= FS_MAX_SOURCEDIRS ) {
					Com_Printf( "WARNING: FS_MAX_SOURCEDIRS exceeded parsing fs_dirs\n" );
					continue;
				}

				if ( write_flag && !haveFullWriteSupport ) {
					Com_Printf( "Checking if xdg-%s is writable...\n", typestr );
					if ( FS_InitWritableDirectory( cvar->string ) ) {
						Com_Printf( "Confirmed writable.\n" );
						write_dir = qtrue;
						++xdgWritableCount;
					} else {
						Com_Printf( "Not writable due to failed write test.\n" );
					}
				}

				temp_dirs[i] = (temp_source_directory_t){
					{ CopyString( va( "xdg-%s", typestr ) ),
					  CopyString( cvar->string ),
					  qtrue,
					  write_dir,
					  xdg_sources[j].xdgType },
					i,
				};
				i++;
			}

			// If all xdg directories are writable, no need to look for any further write locations
			if ( xdgWritableCount == ARRAY_LEN( xdg_sources ) ) {
				haveFullWriteSupport = qtrue;
			}

			xdgLoaded = qtrue;
#else
			Com_Printf( "WARNING: Source directory _xdg_home not supported on this platform.\n" );
#endif

			continue;
		}

		// Determine path from cvar
		path = Cvar_VariableString( token );
		if ( !*path ) {
			continue;
		}

		// If write flag is set and no write directory yet, test writability
		if ( write_flag && !haveFullWriteSupport ) {
			Com_Printf( "Checking if %s is writable...\n", token );
#ifdef __APPLE__
			if ( Q_stristr( va( "%s/", path ), "/Applications/" ) ) {
				Com_Printf( "Not writable due to mac applications directory.\n" );
			}
			else
#endif
			if ( FS_InitWritableDirectory( path ) ) {
				Com_Printf( "Confirmed writable.\n" );
				write_dir = qtrue;
				haveFullWriteSupport = qtrue;
			} else {
				Com_Printf( "Not writable due to failed write test.\n" );
			}
		}

		// Create entry in available slot
		temp_dirs[i] = (temp_source_directory_t){
			{ CopyString( token ),
			  CopyString( path ),
			  qtrue,
			  write_dir },
			i,
		};
	}

	// Sort temp_dirs
	qsort( temp_dirs, ARRAY_LEN( temp_dirs ), sizeof( *temp_dirs ), FS_CompareTempSourceDirsQsort );

	// Check for read-only mode
	if ( !haveFullWriteSupport ) {
#ifdef FS_XDG_HOME_SUPPORT
		if ( xdgWritableCount > 0 ) {
			Com_Printf( "WARNING: Some XDG paths are not writable. Some file types may not be written.\n" );
		} else
#endif
		{
			Com_Printf( "WARNING: No write directory selected. Filesystem in read-only mode.\n" );
		}
	}

	// Transfer entries from temp_dirs to fs.sourcedirs
	for ( i = 0; i < FS_MAX_SOURCEDIRS; ++i ) {
		fs.sourcedirs[i] = temp_dirs[i].s;
		if ( fs.sourcedirs[i].active ) {
			Com_Printf( "Source directory %i%s: %s (%s)\n", i + 1, fs.sourcedirs[i].writable ? " (write)" : "",
				fs.sourcedirs[i].name, fs.sourcedirs[i].path );
		}
	}
}

/*
###############################################################################################

Filesystem Refresh

###############################################################################################
*/

static qboolean fs_useRefreshErrorHandler = qfalse;

/*
=================
FS_RefreshErrorHandler
=================
*/
static void FS_RefreshErrorHandler( fsc_error_level_t level, fsc_error_category_t category, const char *msg, void *element ) {
	if ( fs.cvar.fs_debug_refresh->integer ) {
		const char *type = "general";
		if ( category == FSC_ERROR_PK3FILE )
			type = "pk3";
		if ( category == FSC_ERROR_SHADERFILE )
			type = "shader";
		Com_Printf( "********** refresh %s error **********\n", type );

		if ( element && ( category == FSC_ERROR_PK3FILE || category == FSC_ERROR_SHADERFILE ) ) {
			char buffer[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( (fsc_file_t *)element, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
			Com_Printf( "file: %s\n", buffer );
		}
		Com_Printf( "message: %s\n", msg );
	}
}

/*
=================
FS_IndexDirectory
=================
*/
static void FS_IndexDirectory( const char *directory, int dir_id, qboolean quiet ) {
	fsc_stats_t old_active_stats = fs.index.active_stats;
	fsc_stats_t old_total_stats = fs.index.total_stats;

	fs_useRefreshErrorHandler = qtrue;
	FSC_LoadDirectory( &fs.index, directory, dir_id );
	fs_useRefreshErrorHandler = qfalse;

	#define NON_PK3_FILES( stats ) ( stats.total_file_count - stats.pk3_subfile_count - stats.valid_pk3_count )

	if ( !quiet ) {
		Com_Printf( "Indexed %i files in %i pk3s, %i other files, and %i shaders.\n",
				fs.index.active_stats.pk3_subfile_count - old_active_stats.pk3_subfile_count,
				fs.index.active_stats.valid_pk3_count - old_active_stats.valid_pk3_count,
				NON_PK3_FILES(fs.index.active_stats) - NON_PK3_FILES(old_active_stats),
				fs.index.active_stats.shader_count - old_active_stats.shader_count );

		Com_Printf( "%i files in %i pk3s and %i shaders had not been previously indexed.\n",
				fs.index.total_stats.pk3_subfile_count - old_total_stats.pk3_subfile_count,
				fs.index.total_stats.valid_pk3_count - old_total_stats.valid_pk3_count,
				fs.index.total_stats.shader_count - old_total_stats.shader_count );
	}
}

extern int com_frameNumber;
static int fs_refresh_frame = 0;

/*
=================
FS_Refresh

Updates file index to reflect files recently added/changed on disk.
=================
*/
void FS_Refresh( qboolean quiet ) {
	int i;
	if ( fs.cvar.fs_debug_refresh->integer ) {
		quiet = qfalse;
	}
	if ( !quiet ) {
		Com_Printf( "----- FS_Refresh -----\n" );
	}

	FSC_FilesystemReset( &fs.index );

	for ( i = 0; i < FS_MAX_SOURCEDIRS; ++i ) {
		if ( !fs.sourcedirs[i].active ) {
			continue;
		}
#ifdef FS_XDG_HOME_SUPPORT
		// no need to run index on cache directory, since currently it
		// is only acessed via direct paths
		if ( fs.sourcedirs[i].xdgType == XDG_CACHE ) {
			continue;
		}
#endif
		if ( !quiet ) {
			Com_Printf( "Indexing %s...\n", fs.sourcedirs[i].name );
		}
		FS_IndexDirectory( fs.sourcedirs[i].path, i, quiet );
	}

	if ( !quiet ) {
		Com_Printf( "Index memory usage at %iMB.\n", FSC_MemoryUseEstimate( &fs.index ) / 1048576 + 1 );
	}

	fs_refresh_frame = com_frameNumber;
	FS_ReadbackTracker_Reset();
}

/*
=================
FS_RecentlyRefreshed

Returns qtrue if filesystem has already been refreshed within the last few frames.
=================
*/
qboolean FS_RecentlyRefreshed( void ) {
	int frames_elapsed = com_frameNumber - fs_refresh_frame;
	if ( frames_elapsed >= 0 && frames_elapsed < 5 ) {
		return qtrue;
	}
	return qfalse;
}

/*
=================
FS_AutoRefresh

Calls FS_Refresh to check for updated files, but maximum once per several frames to avoid
redundant refreshes during load operations.
=================
*/
void FS_AutoRefresh( void ) {
	if ( !fs.cvar.fs_auto_refresh_enabled->integer ) {
		if ( fs.cvar.fs_debug_refresh->integer ) {
			Com_Printf( "Skipping fs auto refresh due to disabled fs_auto_refresh_enabled cvar.\n" );
		}
		return;
	}
	if ( FS_RecentlyRefreshed() ) {
		if ( fs.cvar.fs_debug_refresh->integer ) {
			Com_Printf( "Skipping fs auto refresh due to existing recent refresh.\n" );
		}
		return;
	}
#ifdef CMOD_SERVER_CMD_TRIGGERS
	if ( Cvar_VariableIntegerValue( "cmod_in_trigger" ) ) {
		if ( fs.cvar.fs_debug_refresh->integer ) {
			Com_Printf( "Skipping fs auto refresh due to active trigger.\n" );
		}
		return;
	}
#endif
	FS_Refresh( qtrue );
}

/*
###############################################################################################

Filesystem Initialization

###############################################################################################
*/

/*
=================
FS_GetIndexCachePath

Writes path of index cache file to buffer, or empty string on error.
=================
*/
static void FS_GetIndexCachePath( char *buffer, unsigned int size ) {
	FS_GeneratePathWritedir( XDG_CACHE, "fscache.dat", NULL, 0, 0, buffer, size );
}

/*
=================
FS_WriteIndexCache
=================
*/
void FS_WriteIndexCache( void ) {
	char path[FS_MAX_PATH];
	FS_GetIndexCachePath( path, sizeof( path ) );
	if ( *path ) {
		FSC_CacheExportFile( &fs.index, path );
	}
}

/*
=================
FS_TrackedRefresh

Calls FS_Refresh and tracks the number of new files added. Returns qtrue if enough changed
to justify rewriting fscache.dat, qfalse otherwise.
=================
*/
static qboolean FS_TrackedRefresh( void ) {
	fsc_stats_t old_total_stats = fs.index.total_stats;
	FS_Refresh( qfalse );
	if ( fs.index.total_stats.valid_pk3_count - old_total_stats.valid_pk3_count > 20 ||
			fs.index.total_stats.pk3_subfile_count - old_total_stats.pk3_subfile_count > 5000 ||
			fs.index.total_stats.shader_file_count - old_total_stats.shader_file_count > 100 ||
			fs.index.total_stats.shader_count - old_total_stats.shader_count > 5000 ) {
		return qtrue;
	}
	return qfalse;
}

/*
=================
FS_InitIndex

Initializes the filesystem index, using cache file if possible to speed up initial refresh.
=================
*/
static void FS_InitIndex( void ) {
	qboolean cache_loaded = qfalse;

	if ( fs.cvar.fs_index_cache->integer ) {
		char path[FS_MAX_PATH];
		FS_GetIndexCachePath( path, sizeof( path ) );

		Com_Printf( "Loading fscache.dat...\n" );
		if ( *path && !FSC_CacheImportFile( path, &fs.index ) ) {
			cache_loaded = qtrue;
		} else {
			Com_Printf( "Failed to load fscache.dat.\n" );
		}
	}

	if ( cache_loaded ) {
		Com_Printf( "Index data loaded for %i files, %i pk3s, and %i shaders.\n",
				fs.index.files.utilization - fs.index.pk3_hash_lookup.utilization,
				fs.index.pk3_hash_lookup.utilization, fs.index.shaders.utilization );

		if ( fs.cvar.fs_debug_refresh->integer ) {
			Com_Printf( "WARNING: Using index cache may prevent fs_debug_refresh error messages from being logged."
					" For full debug info consider setting fs_index_cache to 0 or temporarily removing fscache.dat.\n" );
		}

	} else {
		FSC_FilesystemInitialize( &fs.index );
	}
}

/*
=================
FS_CoreErrorHandler
=================
*/
static void FS_CoreErrorHandler( fsc_error_level_t level, fsc_error_category_t category, const char *msg, void *element ) {
	if ( level == FSC_ERRORLEVEL_FATAL ) {
		Com_Error( ERR_FATAL, "filesystem error: %s", msg );
	}

	if ( fs_useRefreshErrorHandler ) {
		FS_RefreshErrorHandler( level, category, msg, element );
	}
}

/*
=================
FS_Startup

Initial filesystem startup. Should only be called once.
=================
*/
void FS_Startup( void ) {
	FSC_ASSERT( !fs.initialized );
	Com_Printf( "\n----- FS_Startup -----\n" );

	FSC_RegisterErrorHandler( FS_CoreErrorHandler );

#ifdef ELITEFORCE
#ifdef __APPLE__
#ifdef CMOD_MAC_APP_RESOURCES
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*fs_basepath *fs_homepath fs_gogpath fs_apppath fs_appresources", CVAR_INIT | CVAR_PROTECTED );
#else
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*fs_basepath *fs_homepath fs_gogpath fs_apppath", CVAR_INIT | CVAR_PROTECTED );
#endif
#else
#ifdef FS_XDG_HOME_SUPPORT
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*fs_basepath *_xdg_home fs_homepath fs_gogpath", CVAR_INIT | CVAR_PROTECTED );
#else
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*fs_basepath *fs_homepath fs_gogpath", CVAR_INIT | CVAR_PROTECTED );
#endif
#endif
#else
#if defined( __APPLE__ )
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*fs_homepath fs_basepath fs_steampath fs_gogpath fs_apppath", CVAR_INIT | CVAR_PROTECTED );
#elif defined( FS_XDG_HOME_SUPPORT )
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*_xdg_home fs_homepath fs_basepath fs_steampath fs_gogpath", CVAR_INIT | CVAR_PROTECTED );
#else
	fs.cvar.fs_dirs = Cvar_Get( "fs_dirs", "*fs_homepath fs_basepath fs_steampath fs_gogpath", CVAR_INIT | CVAR_PROTECTED );
#endif
#endif
#ifdef DEDICATED
	fs.cvar.fs_game = Cvar_Get( "fs_game", "", CVAR_LATCH | CVAR_SYSTEMINFO );
#else
	fs.cvar.fs_game = Cvar_Get( "fs_game", "", CVAR_INIT | CVAR_SYSTEMINFO );
#endif
#ifdef CMOD_SETTINGS
	fs.cvar.fs_mod_settings = Cvar_Get( "fs_mod_settings", "0", CVAR_ROM );
#else
	fs.cvar.fs_mod_settings = Cvar_Get( "fs_mod_settings", "0", CVAR_INIT );
#endif
	fs.cvar.fs_index_cache = Cvar_Get( "fs_index_cache", "1", CVAR_INIT );
	fs.cvar.fs_read_inactive_mods = Cvar_Get( "fs_read_inactive_mods", "1", CVAR_ARCHIVE );
	fs.cvar.fs_list_inactive_mods = Cvar_Get( "fs_list_inactive_mods", "1", CVAR_ARCHIVE );
	fs.cvar.fs_download_manifest = Cvar_Get( "fs_download_manifest",
									 "#mod_paks #cgame_pak #ui_pak #currentmap_pak #referenced_paks", CVAR_ARCHIVE );
	fs.cvar.fs_pure_manifest = Cvar_Get( "fs_pure_manifest", "#mod_paks #base_paks #inactivemod_paks", CVAR_ARCHIVE );
	fs.cvar.fs_redownload_across_mods = Cvar_Get( "fs_redownload_across_mods", "1", CVAR_ARCHIVE );
	fs.cvar.fs_full_pure_validation = Cvar_Get( "fs_full_pure_validation", "0", CVAR_ARCHIVE );
	fs.cvar.fs_download_mode = Cvar_Get( "fs_download_mode", "0", CVAR_ARCHIVE );
	fs.cvar.fs_auto_refresh_enabled = Cvar_Get( "fs_auto_refresh_enabled", "1", 0 );
#ifdef FS_SERVERCFG_ENABLED
	fs.cvar.fs_servercfg = Cvar_Get( "fs_servercfg", "servercfg", 0 );
	fs.cvar.fs_servercfg_writedir = Cvar_Get( "fs_servercfg_writedir", "", 0 );
#endif

	fs.cvar.fs_debug_state = Cvar_Get( "fs_debug_state", "0", 0 );
	fs.cvar.fs_debug_refresh = Cvar_Get( "fs_debug_refresh", "0", 0 );
	fs.cvar.fs_debug_fileio = Cvar_Get( "fs_debug_fileio", "0", 0 );
	fs.cvar.fs_debug_lookup = Cvar_Get( "fs_debug_lookup", "0", 0 );
	fs.cvar.fs_debug_references = Cvar_Get( "fs_debug_references", "0", 0 );
	fs.cvar.fs_debug_filelist = Cvar_Get( "fs_debug_filelist", "0", 0 );

	Cvar_Get( "new_filesystem", "1", CVAR_ROM ); // Enables new filesystem calls in renderer

	FS_InitSourceDirs();
	FS_InitIndex();
	FS_UpdateModDirExt( qfalse );

	Com_Printf( "\n" );
	if ( FS_TrackedRefresh() && fs.cvar.fs_index_cache->integer && FS_IsWritedirAvailable( XDG_CACHE ) ) {
		Com_Printf( "Writing fscache.dat due to updated files...\n" );
		FS_WriteIndexCache();
	}
	Com_Printf( "\n" );

	FS_RegisterCommands();
	fs.initialized = qtrue;

#ifndef STANDALONE
	FS_CheckCorePaks();
#endif
}

#endif	// NEW_FILESYSTEM
