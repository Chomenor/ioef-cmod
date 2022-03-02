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

#ifdef CMOD_CROSSHAIR
// This file is used to extend the number of crosshairs selectable in the game,
// when combined with QVM support

#include "../filesystem/fslocal.h"
#include "../client/client.h"

// Crosshair Cvars
cvar_t *cmod_crosshair_enable;
cvar_t *cmod_crosshair_selection;

// Builtin Crosshairs
#define SOURCETYPE_CROSSHAIR 3
void crosshair_builtin_register(void);
qboolean crosshair_builtin_file_enabled(const fsc_file_t *file);

// Crosshair Index
typedef struct {
	fsc_file_t *file;
	unsigned int hash;
	int special_priority;
	qhandle_t handle;	// -1 = invalid, 0 = uninitialized
} crosshair_t;

crosshair_t crosshairs[256];
int crosshair_count;

// Current game status
static qboolean uiSupportRegistered;
static qboolean cgameSupportRegistered;
static qboolean cgameLoaded;

#define ENGINE_CROSSHAIR_ACTIVE ( cmod_crosshair_enable->integer && uiSupportRegistered && ( !cgameLoaded || cgameSupportRegistered ) )

/* ******************************************************************************** */
// Crosshair Index
/* ******************************************************************************** */

static unsigned int special_crosshairs[] = {
	// Helps sort crosshairs for a logical ordering in menu
	// First entry = first in crosshair menu, and default if crosshair setting is invalid
	0x076b9707,		// pak0 b
	0xd98b9513,		// pak0 c
	0x85585057,		// pak0 d
	0xf7bc361b,		// pak0 e
	0x0a3d6df9,		// pak0 f
	0x49ca88cd,		// pak0 g
	0xca445dd6,		// pak0 h
	0xeae7005d,		// pak0 i
	0xe8806c36,		// pak0 j
	0x0f5dd93d,		// pak0 k
	0x6453cfe4,		// pak0 l
	0xa0affa48,		// marksman b
	0x2d6ede50,		// marksman c
	0xb7bb746b,		// marksman d
	0xbab04a49,		// marksman e
	0xaff1e8a5,		// marksman f
	0x640460be,		// marksman g
	0x9fb736bc,		// marksman h
	0xc39a2a8d,		// marksman i
	0x9346c2db,		// marksman j
	0xc0710dd7,		// marksman k
	0x4bba8170,		// xhairsdsdn c
	0x835f47f2,		// xhairsdsdn d
	0xbdc83459,		// xhairsdsdn e
	0x70cb059a,		// xhairsdsdn g
	0xbddf5ebc,		// xhairsdsdn h
	0xddd628b8,		// xhairsdsdn i
	0xb66df595,		// xhairsdsdn j
	0xa9af4193,		// xhairsdsdn l
	0x78426651,		// xhairs b
	0xca469314,		// xhairs c
	0xc0d1265b,		// xhairs d
	0xa6e1b45a,		// xhairs e
	0x8b535601,		// xhairs f
	0xb87a7b14,		// xhairs g
	0x9f826909,		// xhairs h
	0xb053c705,		// xhairs i
	0x974dde80,		// xhairs j
	0x0d847954,		// pakhairs14 b
	0x44a8302a,		// pakhairs14 c
	0x29634e5c,		// pakhairs14 d
	0xbc044a60,		// pakhairs14 e
	0x235ebfba,		// pakhairs14 f
	0xbc4813c8,		// pakhairs14 g
	0x2a134e63,		// pakhairs14 i
	0xdcd4a326,		// pakhairs14 j
	0x027fb6d0,		// pakhairs16 a
	0x32b41930,		// pakhairs16 b
	0xc25e02d3,		// pakhairs16 c
	0x43258873,		// pakhairs16 d
	0x9a3a5892,		// pakhairs16 e
	0x962400c8,		// pakhairs16 f
	0x324b25ed,		// pakhairs16 g
	0xeda8cb55,		// pakhairs16 h
	0x7039e725,		// pakhairs16 i
	0x21a3c310,		// pakhairs16 j
};

/*
==================
CMCrosshair_GetSpecialPriorityByHash
==================
*/
static int CMCrosshair_GetSpecialPriorityByHash( unsigned int hash ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( special_crosshairs ); ++i ) {
		if ( special_crosshairs[i] == hash ) {
			return ARRAY_LEN( special_crosshairs ) - i;
		}
	}
	return 0;
}

/*
==================
CMCrosshair_GetCrosshairIndexByHash

Returns -1 if not found.
==================
*/
static int CMCrosshair_GetCrosshairIndexByHash( unsigned int hash ) {
	int i;
	for ( i = 0; i < crosshair_count; ++i ) {
		if ( crosshairs[i].hash == hash ) {
			return i;
		}
	}
	return -1;
}

/*
==================
CMCrosshair_GenSortKey
==================
*/
static void CMCrosshair_GenSortKey( const fsc_file_t *file, fsc_stream_t *output ) {
	FS_WriteCoreSortKey( file, output, qtrue );
	FS_WriteSortFilename( file, output );
	FS_WriteSortValue( FS_GetSourceDirID( file ), output );
}

/*
==================
CMCrosshair_CompareCrosshairFile
==================
*/
static int CMCrosshair_CompareCrosshairFile( const fsc_file_t *file1, const fsc_file_t *file2 ) {
	char buffer1[1024];
	char buffer2[1024];
	fsc_stream_t stream1 = FSC_InitStream( buffer1, sizeof( buffer1 ) );
	fsc_stream_t stream2 = FSC_InitStream( buffer2, sizeof( buffer2 ) );
	if ( file1->sourcetype == SOURCETYPE_CROSSHAIR && file2->sourcetype != SOURCETYPE_CROSSHAIR ) {
		return -1;
	}
	if ( file2->sourcetype == SOURCETYPE_CROSSHAIR && file1->sourcetype != SOURCETYPE_CROSSHAIR ) {
		return 1;
	}
	CMCrosshair_GenSortKey( file1, &stream1 );
	CMCrosshair_GenSortKey( file2, &stream2 );
	return FSC_Memcmp( stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position );
}

/*
==================
CMCrosshair_CompareCrosshairs
==================
*/
static int CMCrosshair_CompareCrosshairs( const void *c1, const void *c2 ) {
	if ( ( (crosshair_t *)c1 )->special_priority > ( (crosshair_t *)c2 )->special_priority ) {
		return -1;
	}
	if ( ( (crosshair_t *)c2 )->special_priority > ( (crosshair_t *)c1 )->special_priority ) {
		return 1;
	}
	return CMCrosshair_CompareCrosshairFile( ( (crosshair_t *)c1 )->file, ( (crosshair_t *)c2 )->file );
}

/*
==================
CMCrosshair_IsCrosshairFileEnabled
==================
*/
static qboolean CMCrosshair_IsCrosshairFileEnabled( const fsc_file_t *file ) {
	if ( file->sourcetype == SOURCETYPE_CROSSHAIR ) {
		return crosshair_builtin_file_enabled( file );
	}
	if ( !FSC_IsFileActive( file, &fs.index ) ) {
		return qfalse;
	}

	if ( FS_ConnectedServerPureState() == 1 ) {
		// Connected to pure server
		if ( file->sourcetype == FSC_SOURCETYPE_PK3 ) {
			unsigned int pk3_hash = FSC_GetBaseFile( file, &fs.index )->pk3_hash;
			if ( FS_Pk3List_Lookup( &fs.connected_server_pure_list, pk3_hash ) ) {
				return qtrue;
			}
		}
		return qfalse;
	}

	return qtrue;
}

/*
==================
CMCrosshair_BuildCrosshairIndex
==================
*/
static void CMCrosshair_BuildCrosshairIndex( void ) {
	fsc_hashtable_iterator_t hti;
	fsc_crosshair_t *entry;

	crosshair_count = 0;

	FSC_HashtableIterateBegin( &fs.index.crosshairs, 0, &hti );
	while ( ( entry = STACKPTRN( FSC_HashtableIterateNext( &hti ) ) ) ) {
		fsc_file_t *file = STACKPTR( entry->source_file_ptr );
		int index;
		if ( !CMCrosshair_IsCrosshairFileEnabled( file ) ) {
			continue;
		}
		index = CMCrosshair_GetCrosshairIndexByHash( entry->hash );
		if ( index == -1 ) {
			// Create new entry
			if ( crosshair_count >= ARRAY_LEN( crosshairs ) ) {
				return;
			}
			crosshairs[crosshair_count++] = ( crosshair_t ){
					file, entry->hash, CMCrosshair_GetSpecialPriorityByHash( entry->hash ), 0 };
		} else {
			// Use higher precedence file
			if ( CMCrosshair_CompareCrosshairFile( file, crosshairs[index].file ) < 0 ) {
				crosshairs[index].file = file;
			}
		}
	}

	qsort( crosshairs, crosshair_count, sizeof( *crosshairs ), CMCrosshair_CompareCrosshairs );
}

/* ******************************************************************************** */
// Crosshair Shader Registration
/* ******************************************************************************** */

static crosshair_t *registering_crosshair;

/*
==================
CMCrosshair_FileLookupHook

Returns crosshair file to override normal lookup handling, null otherwise.
==================
*/
fsc_file_t *CMCrosshair_FileLookupHook( const char *name ) {
	if ( registering_crosshair && *name == '#' ) {
		int i;
		char buffer[] = "#cmod_crosshair_";
		for ( i = 0; i < sizeof( buffer ) - 1; ++i ) {
			if ( buffer[i] != name[i] ) {
				return NULL;
			}
		}
		return registering_crosshair->file;
	}
	return NULL;
}

/*
==================
CMCrosshair_GetCrosshairShader
==================
*/
static qhandle_t CMCrosshair_GetCrosshairShader( crosshair_t *crosshair ) {
	if ( !crosshair->handle ) {
		if ( CMCrosshair_IsCrosshairFileEnabled( crosshair->file ) ) {
			char name[32];
			registering_crosshair = crosshair;
			Com_sprintf( name, sizeof( name ), "#cmod_crosshair_%08x", crosshair->hash );
			crosshair->handle = re.RegisterShaderNoMip( name );
		}
		if ( !crosshair->handle ) {
			crosshair->handle = -1;
		}
	}
	return crosshair->handle == -1 ? 0 : crosshair->handle;
}

/* ******************************************************************************** */
// Current Crosshair Handling
/* ******************************************************************************** */

/*
==================
CMCrosshair_GetCurrentCrosshairIndex

Returns -1 for no crosshair, index otherwise.
==================
*/
static int CMCrosshair_GetCurrentCrosshairIndex( void ) {
	static int cached_crosshair_index;
	unsigned int hash;

	if ( !crosshair_count ) {
		return -1;
	}
	if ( cmod_crosshair_selection->string[0] == '0' && cmod_crosshair_selection->string[1] == 0 ) {
		return -1;
	}

	hash = strtoul( cmod_crosshair_selection->string, NULL, 16 );
	if ( cached_crosshair_index >= crosshair_count || crosshairs[cached_crosshair_index].hash != hash ) {
		cached_crosshair_index = CMCrosshair_GetCrosshairIndexByHash( hash );
		if ( cached_crosshair_index < 0 ) {
			cached_crosshair_index = 0;
		}
	}

	return cached_crosshair_index;
}

/*
==================
CMCrosshair_GetCurrentCrosshair

Returns null for no crosshair, or crosshair object otherwise.
==================
*/
static crosshair_t *CMCrosshair_GetCurrentCrosshair( void ) {
	int index = CMCrosshair_GetCurrentCrosshairIndex();
	if ( index < 0 ) {
		return NULL;
	}
	return &crosshairs[index];
}

/*
==================
CMCrosshair_AdvanceCurrentCrosshair
==================
*/
static void CMCrosshair_AdvanceCurrentCrosshair( qboolean trusted ) {
	int index = CMCrosshair_GetCurrentCrosshairIndex() + 1;
	const char *value = "0";
	if ( index < crosshair_count ) {
		value = va( "%08x", crosshairs[index].hash );
	}
#ifdef CMOD_CVAR_HANDLING
	Cvar_SetSafe( "cmod_crosshair_selection", value, trusted );
#else
	Cvar_SetSafe( "cmod_crosshair_selection", value );
#endif
}

/*
==================
CMCrosshair_AdvanceCurrentCrosshairCmd
==================
*/
static void CMCrosshair_AdvanceCurrentCrosshairCmd( void ) {
	CMCrosshair_AdvanceCurrentCrosshair( qtrue );
}

/*
==================
CMCrosshair_VMAdvanceCurrentCrosshair

Returns 1 if successful, 0 if engine crosshair mode inactive.
==================
*/
int CMCrosshair_VMAdvanceCurrentCrosshair( qboolean trusted ) {
	if ( ENGINE_CROSSHAIR_ACTIVE ) {
		CMCrosshair_AdvanceCurrentCrosshair( trusted );
		return 1;
	}

	return 0;
}

/*
==================
CMCrosshair_GetCurrentShader

Returns -1 for no engine crosshair support, 0 if engine crosshair mode inactive, or crosshair shader otherwise.
==================
*/
qhandle_t CMCrosshair_GetCurrentShader( void ) {
	if ( ENGINE_CROSSHAIR_ACTIVE ) {
		crosshair_t *currentCrosshair = CMCrosshair_GetCurrentCrosshair();
		if ( currentCrosshair ) {
			return CMCrosshair_GetCrosshairShader( currentCrosshair );
		} else {
			return 0;
		}
	}

	return -1;
}

/* ******************************************************************************** */
// Init / Test Functions
/* ******************************************************************************** */

/*
==================
CMCrosshair_DebugIndexCmd

Print all index elements.
==================
*/
static void CMCrosshair_DebugIndexCmd( void ) {
	int i;
	for ( i = 0; i < crosshair_count; ++i ) {
		char buffer[FS_FILE_BUFFER_SIZE];
		FS_FileToBuffer( crosshairs[i].file, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
		Com_Printf( "********** crosshair index entry **********\nhash: %08x\nfile: %s\nindex: %i\nspecial_priority: %i\n",
				crosshairs[i].hash, buffer, i, crosshairs[i].special_priority );
	}
}

/*
==================
CMCrosshair_DebugFilesCmd

Print all file elements.
==================
*/
static void CMCrosshair_DebugFilesCmd( void ) {
	fsc_hashtable_iterator_t hti;
	fsc_crosshair_t *entry;

	FSC_HashtableIterateBegin( &fs.index.crosshairs, 0, &hti );
	while ( ( entry = STACKPTRN( FSC_HashtableIterateNext( &hti ) ) ) ) {
		char buffer[FS_FILE_BUFFER_SIZE];
		FS_FileToBuffer( STACKPTR( entry->source_file_ptr ), buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
		Com_Printf( "********** crosshair file **********\nhash: %08x\nfile: %s\n", entry->hash, buffer );
	}
}

/*
==================
CMCrosshair_StatusCmd
==================
*/
static void CMCrosshair_StatusCmd( void ) {
	Com_Printf( "cmod_crosshair_enable: %i\n", cmod_crosshair_enable->integer );
	crosshair_t *current = CMCrosshair_GetCurrentCrosshair();
	if ( current ) {
		char buffer[FS_FILE_BUFFER_SIZE];
		FS_FileToBuffer( current->file, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
		Com_Printf( "current crosshair: %08x - %s\n", current->hash, buffer );
	} else {
		Com_Printf( "current crosshair: none\n" );
	}
}

/*
==================
CMCrosshair_GeneralInit
==================
*/
static void CMCrosshair_GeneralInit( void ) {
	crosshair_builtin_register();
	cmod_crosshair_enable = Cvar_Get( "cmod_crosshair_enable", "0", CVAR_ARCHIVE );
	cmod_crosshair_selection = Cvar_Get( "cmod_crosshair_selection", "", CVAR_ARCHIVE );
	Cmd_AddCommand( "cmod_crosshair_status", CMCrosshair_StatusCmd );
	Cmd_AddCommand( "cmod_crosshair_advance", CMCrosshair_AdvanceCurrentCrosshairCmd );
	Cmd_AddCommand( "cmod_crosshair_debug_index", CMCrosshair_DebugIndexCmd );
	Cmd_AddCommand( "cmod_crosshair_debug_files", CMCrosshair_DebugFilesCmd );
}

/*
==================
CMCrosshair_UIInit
==================
*/
void CMCrosshair_UIInit( void ) {
	static qboolean general_init_complete = qfalse;
	if ( !general_init_complete ) {
		CMCrosshair_GeneralInit();
		general_init_complete = qtrue;
	}

	uiSupportRegistered = qfalse;
	cgameSupportRegistered = qfalse;
	cgameLoaded = qfalse;

	CMCrosshair_BuildCrosshairIndex();
}

/*
==================
CMCrosshair_CGameInit
==================
*/
void CMCrosshair_CGameInit( void ) {
	cgameLoaded = qtrue;
}

/*
==================
CMCrosshair_RegisterSupport
==================
*/
void CMCrosshair_RegisterVMSupport( vmType_t vm_type ) {
	if ( vm_type == VM_UI ) {
		uiSupportRegistered = qtrue;
	}
	if ( vm_type == VM_CGAME ) {
		cgameSupportRegistered = qtrue;
	}
}

#endif
