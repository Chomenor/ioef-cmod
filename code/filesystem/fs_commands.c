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

/*
###############################################################################################

Lookup Test Commands

###############################################################################################
*/

/*
=================
FS_FindFile_f
=================
*/
static void FS_FindFile_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: find_file <path> <optional flag value>\n" );
		return;
	}

	FS_GeneralLookup( Cmd_Argv( 1 ), atoi( Cmd_Argv( 2 ) ), qtrue );
}

/*
=================
FS_FindShader_f
=================
*/
static void FS_FindShader_f( void ) {
	int flags = 0;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: find_shader <shader/image name> <optional flag value>\n" );
		return;
	}

	if ( *Cmd_Argv( 2 ) ) {
		flags = atoi( Cmd_Argv( 2 ) );
	} else {
		if ( !Q_stricmp( Cvar_VariableString( "cl_renderer" ), "opengl2" ) ) {
			// try to guess flags that gl2 renderer uses
			flags |= LOOKUPFLAG_ENABLE_MTR;
#ifdef ELITEFORCE
			if ( Cvar_VariableIntegerValue( "r_ext_compress_textures" ) ) {
#else
			if ( Cvar_VariableIntegerValue( "r_ext_compressed_textures" ) ) {
#endif
				flags |= LOOKUPFLAG_ENABLE_DDS;
			}
			Com_Printf( "Note: Performing lookup using GL2 renderer flags (%i) due to cl_renderer value.\n\n", flags );
		}
	}

	FS_ShaderLookup( Cmd_Argv( 1 ), flags, qtrue );
}

/*
=================
FS_FindSound_f
=================
*/
static void FS_FindSound_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: find_sound <sound name> <optional flag value>\n" );
		return;
	}

	FS_SoundLookup( Cmd_Argv( 1 ), atoi( Cmd_Argv( 2 ) ), qtrue );
}

/*
=================
FS_FindVM_f
=================
*/
static void FS_FindVM_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: find_vm <vm/dll name>\n" );
		return;
	}

	FS_VMLookup( Cmd_Argv( 1 ), qfalse, qtrue, NULL );
}

/*
=================
FS_FSCompare_f
=================
*/
static void FS_FSCompare_f( void ) {
	if ( Cmd_Argc() != 3 ) {
		Com_Printf( "Usage: fs_compare <resource #> <resource #>\n\nRun this command following a 'find_file', 'find_shader', "
				"'find_sound', or 'find_vm' command and specify the resource numbers you wish to compare.\n\n"
				"Example: 'fs_compare 1 2' to compare first and second resources.\n" );
		return;
	}

	FS_DebugCompareResources( atoi( Cmd_Argv( 1 ) ), atoi( Cmd_Argv( 2 ) ) );
}

/*
###############################################################################################

Other Commands

###############################################################################################
*/

/*
=================
FS_Refresh_f

Usage: FS_Refresh <force> <quiet>
=================
*/
static void FS_Refresh_f( void ) {
	if ( !atoi( Cmd_Argv( 1 ) ) && FS_RecentlyRefreshed() ) {
		Com_Printf( "Ignoring fs_refresh command due to existing recent refresh.\n" );
		return;
	}
	FS_Refresh( atoi( Cmd_Argv( 2 ) ) ? qtrue : qfalse );
}

/*
=================
FS_ReadCacheDebug_f
=================
*/
static void FS_ReadCacheDebug_f( void ) {
	FS_ReadCache_Debug();
}

/*
=================
FS_IndexCacheWrite_f
=================
*/
static void FS_IndexCacheWrite_f( void ) {
	FS_WriteIndexCache();
}

/*
=================
FS_Dir_f
=================
*/
static void FS_Dir_f( void ) {
	const char	*path;
	const char	*extension;
	char	**dirnames;
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf( "usage: dir <directory> [extension]\n" );
		return;
	}

	if ( Cmd_Argc() == 2 ) {
		path = Cmd_Argv( 1 );
		extension = "";
	} else {
		path = Cmd_Argv( 1 );
		extension = Cmd_Argv( 2 );
	}

	Com_Printf( "Directory of %s %s\n", path, extension );
	Com_Printf( "---------------\n" );

	dirnames = FS_ListFiles( path, extension, &ndirs );

	for ( i = 0; i < ndirs; i++ ) {
		Com_Printf( "%s\n", dirnames[i] );
	}
	FS_FreeFileList( dirnames );
}

/*
=================
FS_NewDir_f
=================
*/
static void FS_NewDir_f( void ) {
	const char	*filter;
	char	**dirnames;
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: fdir <filter>\n" );
		Com_Printf( "example: fdir *q3dm*.bsp\n");
		return;
	}

	filter = Cmd_Argv( 1 );

	Com_Printf( "---------------\n" );

	dirnames = FS_ListFilteredFiles_Flags( "", "", filter, &ndirs, 0 );

	for ( i = 0; i < ndirs; i++ ) {
		Com_Printf( "%s\n", dirnames[i] );
	}
	Com_Printf( "%d files listed\n", ndirs );
	FS_FreeFileList( dirnames );
}

/*
=================
FS_Which_f

The lookup functions are more powerful, but this is kept for users who are familiar with it.
=================
*/
static void FS_Which_f( void ) {
	const char *filename = Cmd_Argv( 1 );
	const fsc_file_t *file;

	if ( !filename[0] ) {
		Com_Printf( "Usage: which <file>\n" );
		return;
	}

	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	file = FS_GeneralLookup( filename, 0, qfalse );
	if ( file ) {
		FS_PrintFileLocation( file );
	} else {
		Com_Printf( "File not found: \"%s\"\n", filename );
	}
}

/*
=================
FS_TouchFile_f
=================
*/
static void FS_TouchFile_f( void ) {
	fileHandle_t f;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: touchFile <file>\n" );
		return;
	}

	FS_FOpenFileRead( Cmd_Argv( 1 ), &f, qfalse );
	if ( f ) {
		FS_FCloseFile( f );
	}
}

/*
=================
FS_Path_f

Quick implementation without sorting
=================
*/
static void FS_Path_f( void ) {
	fsc_pk3_iterator_t it;
	int sourceid;

	for ( sourceid = 0; sourceid < FS_MAX_SOURCEDIRS; ++sourceid ) {
		if ( !fs.sourcedirs[sourceid].active ) {
			continue;
		}
		Com_Printf( "Looking in %s (%s)\n", fs.sourcedirs[sourceid].name, fs.sourcedirs[sourceid].path );

		it = FSC_Pk3IteratorOpenAll( &fs.index );
		while ( FSC_Pk3IteratorAdvance( &it ) ) {
			if ( it.pk3->source_dir_id == sourceid && !FS_CheckFileDisabled( (fsc_file_t *)it.pk3, FD_CHECK_READ_INACTIVE_MODS ) ) {
				char buffer[FS_FILE_BUFFER_SIZE];
				FS_FileToBuffer( (fsc_file_t *)it.pk3, buffer, sizeof( buffer ), qfalse, qtrue, qfalse, qfalse );
				Com_Printf( "%s (%i files)\n", buffer, it.pk3->pk3_subfile_count );
				Com_Printf( "    hash(%i) FS_CorePk3Position(%i)\n", (int)it.pk3->pk3_hash, FS_CorePk3Position( it.pk3->pk3_hash ) );
				if ( FS_ConnectedServerPureState() ) {
					Com_Printf( "    %son the pure list\n",
							FS_Pk3List_Lookup( &fs.connected_server_pure_list, it.pk3->pk3_hash ) ? "" : "not " );
				}
			}
		}
	}

	Com_Printf( "\n" );
	FS_Handle_PrintList();
}

/*
###############################################################################################

Command Register Function

###############################################################################################
*/

/*
=================
FS_RegisterCommands
=================
*/
void FS_RegisterCommands( void ) {
	Cmd_AddCommand( "find_file", FS_FindFile_f );
	Cmd_AddCommand( "find_shader", FS_FindShader_f );
	Cmd_AddCommand( "find_sound", FS_FindSound_f );
	Cmd_AddCommand( "find_vm", FS_FindVM_f );
	Cmd_AddCommand( "fs_compare", FS_FSCompare_f );

	Cmd_AddCommand( "fs_refresh", FS_Refresh_f );
	Cmd_AddCommand( "readcache_debug", FS_ReadCacheDebug_f );
	Cmd_AddCommand( "indexcache_write", FS_IndexCacheWrite_f );

	Cmd_AddCommand( "dir", FS_Dir_f );
	Cmd_AddCommand( "fdir", FS_NewDir_f );
	Cmd_AddCommand( "which", FS_Which_f );
	Cmd_AddCommand( "touchfile", FS_TouchFile_f );
	Cmd_AddCommand( "path", FS_Path_f );
}

#endif	// NEW_FILESYSTEM
