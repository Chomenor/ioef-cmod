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

Indented Debug Print Support

This section is used to support indented prints for the cvar-enabled debug logging options
to make the output more readable, especially if there are nested calls to functions that
produce cluster-type prints.

Theoretically the level could be messed up by Com_Error, but since it's a pretty obscure
scenario and this is ONLY used for cvar-enabled debug prints I'm ignoring it for now.

###############################################################################################
*/

static int fs_debug_indent_level = 0;

/*
=================
FS_DebugIndentStart
=================
*/
void FS_DebugIndentStart( void ) {
	++fs_debug_indent_level;
}

/*
=================
FS_DebugIndentStop
=================
*/
void FS_DebugIndentStop( void ) {
	--fs_debug_indent_level;
	if ( fs_debug_indent_level < 0 ) {
		Com_Printf( "WARNING: Negative filesystem debug indent\n" );
		fs_debug_indent_level = 0;
	}
}

/*
=================
FS_DPrintf
=================
*/
void QDECL FS_DPrintf( const char *fmt, ... ) {
	va_list argptr;
	char msg[MAXPRINTMSG];
	unsigned int indent = (unsigned int)fs_debug_indent_level;
	char spaces[16] = "               ";

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	if ( indent > 4 ) {
		indent = 4;
	}
	spaces[indent * 2] = '\0';
	Com_Printf( "%s%s", spaces, msg );
}

/*
###############################################################################################

Hash Table

A common hashtable implementation used for some filesystem operations.

###############################################################################################
*/

/*
=================
FS_Hashtable_Initialize

Valid for an uninitialized hash table.
=================
*/
void FS_Hashtable_Initialize( fs_hashtable_t *hashtable, int bucket_count ) {
	FSC_ASSERT( hashtable );
	FSC_ASSERT( bucket_count > 0 );
	hashtable->bucket_count = bucket_count;
	hashtable->buckets = (fs_hashtable_entry_t **)Z_Malloc( sizeof( fs_hashtable_entry_t * ) * bucket_count );
	hashtable->element_count = 0;
}

/*
=================
FS_Hashtable_Insert

Valid for an initialized hash table.
=================
*/
void FS_Hashtable_Insert( fs_hashtable_t *hashtable, fs_hashtable_entry_t *entry, unsigned int hash ) {
	int index;
	FSC_ASSERT( hashtable );
	FSC_ASSERT( hashtable->bucket_count > 0 );
	FSC_ASSERT( entry );
	index = hash % hashtable->bucket_count;
	entry->next = hashtable->buckets[index];
	hashtable->buckets[index] = entry;
	++hashtable->element_count;
}

/*
=================
FS_Hashtable_Iterate

Valid for an initialized or uninitialized (zeroed) hashtable.
=================
*/
fs_hashtable_iterator_t FS_Hashtable_Iterate( fs_hashtable_t *hashtable, unsigned int hash, qboolean iterate_all ) {
	fs_hashtable_iterator_t iterator;
	FSC_ASSERT( hashtable );
	iterator.ht = hashtable;
	if ( !hashtable->bucket_count || iterate_all ) {
		iterator.current_bucket = 0;
		iterator.bucket_limit = hashtable->bucket_count;
	} else {
		iterator.current_bucket = hash % hashtable->bucket_count;
		iterator.bucket_limit = iterator.current_bucket + 1;
	}
	iterator.current_entry = NULL;
	return iterator;
}

/*
=================
FS_Hashtable_Next
=================
*/
void *FS_Hashtable_Next( fs_hashtable_iterator_t *iterator ) {
	fs_hashtable_entry_t *entry = iterator->current_entry;
	while ( !entry ) {
		if ( iterator->current_bucket >= iterator->bucket_limit ) {
			return NULL;
		}
		entry = iterator->ht->buckets[iterator->current_bucket++];
	}
	iterator->current_entry = entry->next;
	return entry;
}

/*
=================
FS_Hashtable_FreeEntries

Valid for an initialized or uninitialized (zeroed) hashtable.
=================
*/
static void FS_Hashtable_FreeEntries( fs_hashtable_t *hashtable, void ( *free_entry )( fs_hashtable_entry_t *entry ) ) {
	fs_hashtable_iterator_t it = FS_Hashtable_Iterate( hashtable, 0, qtrue );
	fs_hashtable_entry_t *entry;
	if ( free_entry ) {
		while ( ( entry = (fs_hashtable_entry_t *)FS_Hashtable_Next( &it ) ) ) {
			free_entry( entry );
		}
	} else {
		while ( ( entry = (fs_hashtable_entry_t *)FS_Hashtable_Next( &it ) ) ) {
			Z_Free( entry );
		}
	}
}

/*
=================
FS_Hashtable_Free

Valid for an initialized or uninitialized (zeroed) hashtable.
=================
*/
void FS_Hashtable_Free( fs_hashtable_t *hashtable, void ( *free_entry )( fs_hashtable_entry_t *entry ) ) {
	FSC_ASSERT( hashtable );
	FS_Hashtable_FreeEntries( hashtable, free_entry );
	if ( hashtable->buckets ) {
		Z_Free( hashtable->buckets );
	}
	Com_Memset( hashtable, 0, sizeof( *hashtable ) );
}

/*
=================
FS_Hashtable_Reset

Valid for an initialized hash table.
=================
*/
void FS_Hashtable_Reset( fs_hashtable_t *hashtable, void ( *free_entry )( fs_hashtable_entry_t *entry ) ) {
	FSC_ASSERT( hashtable );
	FS_Hashtable_FreeEntries( hashtable, free_entry );
	Com_Memset( hashtable->buckets, 0, sizeof( *hashtable->buckets ) * hashtable->bucket_count );
	hashtable->element_count = 0;
}

/*
###############################################################################################

Pk3 List

The pk3 list structure stores a list of pk3s, mapping pk3 hash to an index value.
First pk3 inserted has index 1, second pk3 has index 2, etc.
If same hash is inserted multiple times, the first index will be used.

###############################################################################################
*/

/*
=================
FS_Pk3List_Initialize
=================
*/
void FS_Pk3List_Initialize( pk3_list_t *pk3_list, unsigned int bucket_count ) {
	FSC_ASSERT( pk3_list );
	FS_Hashtable_Initialize( &pk3_list->ht, bucket_count );
}

/*
=================
FS_Pk3List_Lookup
=================
*/
int FS_Pk3List_Lookup( const pk3_list_t *pk3_list, unsigned int hash ) {
	fs_hashtable_iterator_t it;
	pk3_list_entry_t *entry;
	FSC_ASSERT( pk3_list );
	it = FS_Hashtable_Iterate( (fs_hashtable_t *)&pk3_list->ht, hash, qfalse );
	while ( ( entry = (pk3_list_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		if ( entry->hash == hash ) {
			return entry->position;
		}
	}
	return 0;
}

/*
=================
FS_Pk3List_Insert
=================
*/
void FS_Pk3List_Insert( pk3_list_t *pk3_list, unsigned int hash ) {
	pk3_list_entry_t *new_entry;
	FSC_ASSERT( pk3_list );
	if ( FS_Pk3List_Lookup( pk3_list, hash ) ) {
		return;
	}
	new_entry = (pk3_list_entry_t *)S_Malloc( sizeof( pk3_list_entry_t ) );
	FS_Hashtable_Insert( &pk3_list->ht, &new_entry->hte, hash );
	new_entry->hash = hash;
	new_entry->position = pk3_list->ht.element_count;
}

/*
=================
FS_Pk3List_Free
=================
*/
void FS_Pk3List_Free( pk3_list_t *pk3_list ) {
	FSC_ASSERT( pk3_list );
	FS_Hashtable_Free( &pk3_list->ht, NULL );
}

/*
###############################################################################################

Pk3 precedence functions

These are used to rank pk3s according to the definitions in fslocal.h.

###############################################################################################
*/

#define SEARCH_PAK_DEFS( paks ) { \
	int i; \
	unsigned int hashes[] = paks; \
	for ( i = 0; i < ARRAY_LEN( hashes ); ++i ) { \
		if ( hash == hashes[i] ) \
			return i + 1; \
	} \
}

/*
=================
FS_CorePk3Position

Checks if hash matches one of the known core pk3s (i.e. official game pk3s).

Returns 0 if no core pk3 match; otherwise higher value means higher precedence.
=================
*/
int FS_CorePk3Position( unsigned int hash ) {
#ifdef FS_CORE_PAKS_TEAMARENA
	if ( !Q_stricmp( FS_GetCurrentGameDir(), BASETA ) ) {
		SEARCH_PAK_DEFS( FS_CORE_PAKS_TEAMARENA )
		return 0;
	}
#endif
#ifdef FS_CORE_PAKS
	SEARCH_PAK_DEFS( FS_CORE_PAKS )
#endif
	return 0;
}

/*
=================
FS_GetModType
=================
*/
fs_modtype_t FS_GetModType( const char *mod_dir ) {
	if ( mod_dir ) {
		char sanitized_mod_dir[FSC_MAX_MODDIR];
		FS_SanitizeModDir( mod_dir, sanitized_mod_dir );
		if ( *sanitized_mod_dir && !Q_stricmp( sanitized_mod_dir, fs.current_mod_dir ) )
			return MODTYPE_CURRENT_MOD;
		else if ( !Q_stricmp( sanitized_mod_dir, "basemod" ) )
			return MODTYPE_OVERRIDE_DIRECTORY;
		else if ( !Q_stricmp( sanitized_mod_dir, com_basegame->string ) )
			return MODTYPE_BASE;
	}
	return MODTYPE_INACTIVE;
}

#ifdef FS_SERVERCFG_ENABLED
// Directory list
#define MAX_SERVERCFG_DIRS 32
static int servercfg_cvar_mod_count = -1;
static int servercfg_dir_count = 0;
static char servercfg_dirs[MAX_SERVERCFG_DIRS][FSC_MAX_MODDIR];

/*
=================
FS_Servercfg_UpdateState

Parse out servercfg directory names from fs_servercfg cvar.
=================
*/
static void FS_Servercfg_UpdateState( void ) {
	if ( fs.cvar.fs_servercfg->modificationCount != servercfg_cvar_mod_count ) {
		char *servercfg_ptr = fs.cvar.fs_servercfg->string;
		const char *token;

		servercfg_dir_count = 0;
		servercfg_cvar_mod_count = fs.cvar.fs_servercfg->modificationCount;

		while ( 1 ) {
			token = COM_ParseExt( &servercfg_ptr, qfalse );
			if ( !*token ) {
				break;
			}

			if ( servercfg_dir_count >= MAX_SERVERCFG_DIRS ) {
				Com_Printf( "MAX_SERVERCFG_DIRS hit\n" );
				break;
			}

			Q_strncpyz( servercfg_dirs[servercfg_dir_count], token, sizeof( servercfg_dirs[servercfg_dir_count] ) );
			++servercfg_dir_count;
		}
	}
}

/*
=================
FS_Servercfg_Priority

Checks if a particular mod directory is a servercfg directory.

Returns 0 if no servercfg directory match; otherwise higher value means higher precedence.
=================
*/
unsigned int FS_Servercfg_Priority( const char *mod_dir ) {
	int i;
	FS_Servercfg_UpdateState();
	for ( i = 0; i < servercfg_dir_count; ++i ) {
		if ( !Q_stricmp( mod_dir, servercfg_dirs[i] ) ) {
			return servercfg_dir_count - i;
		}
	}
	return 0;
}
#endif

/*
###############################################################################################

File helper functions

###############################################################################################
*/

/*
=================
FS_GetFileExtension

Returns empty string for no extension, otherwise extension includes leading period.
=================
*/
const char *FS_GetFileExtension( const fsc_file_t *file ) {
	FSC_ASSERT( file );
	return (const char *)STACKPTR( file->qp_ext_ptr );
}

/*
=================
FS_CheckFilesFromSamePk3

Returns qtrue if both files are located in the same pk3, qfalse otherwise.
Used by renderer for md3 lod handling.
=================
*/
qboolean FS_CheckFilesFromSamePk3( const fsc_file_t *file1, const fsc_file_t *file2 ) {
	if ( !file1 || !file2 || file1->sourcetype != FSC_SOURCETYPE_PK3 || file2->sourcetype != FSC_SOURCETYPE_PK3 ||
			( (fsc_file_frompk3_t *)file1 )->source_pk3 != ( (fsc_file_frompk3_t *)file2 )->source_pk3 ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
FS_GetSourceDirID
=================
*/
int FS_GetSourceDirID( const fsc_file_t *file ) {
	const fsc_file_direct_t *base_file;
	FSC_ASSERT( file );
	base_file = FSC_GetBaseFile( file, &fs.index );
	if ( base_file ) {
		return base_file->source_dir_id;
	}
	return -1;
}

/*
=================
FS_GetSourceDirString
=================
*/
const char *FS_GetSourceDirString( const fsc_file_t *file ) {
	int id = FS_GetSourceDirID( file );
	if ( id >= 0 && id < FS_MAX_SOURCEDIRS && fs.sourcedirs[id].active ) {
		return fs.sourcedirs[id].name;
	}
	return "unknown";
}

/*
=================
FS_FileToStream
=================
*/
void FS_FileToStream( const fsc_file_t *file, fsc_stream_t *stream, qboolean include_source_dir,
		qboolean include_mod, qboolean include_pk3_origin, qboolean include_size ) {
	FSC_ASSERT( file && stream );

	if ( include_source_dir ) {
		FSC_StreamAppendString( stream, FS_GetSourceDirString( file ) );
		FSC_StreamAppendString( stream, "->" );
	}

	FSC_FileToStream( file, stream, &fs.index, include_mod ? fsc_true : fsc_false, include_pk3_origin ? fsc_true : fsc_false );

	if ( include_size ) {
		char buffer[24];
		Com_sprintf( buffer, sizeof( buffer ), " (%i bytes)", file->filesize );
		FSC_StreamAppendString( stream, buffer );
	}
}

/*
=================
FS_FileToBuffer
=================
*/
void FS_FileToBuffer( const fsc_file_t *file, char *buffer, unsigned int buffer_size, qboolean include_source_dir,
		qboolean include_mod, qboolean include_pk3_origin, qboolean include_size ) {
	fsc_stream_t stream = FSC_InitStream( buffer, buffer_size );
	FSC_ASSERT( file && buffer );
	FS_FileToStream( file, &stream, include_source_dir, include_mod, include_pk3_origin, include_size );
}

/*
=================
FS_PrintFileLocation
=================
*/
void FS_PrintFileLocation( const fsc_file_t *file ) {
	char name_buffer[FS_FILE_BUFFER_SIZE];
	char source_buffer[FS_FILE_BUFFER_SIZE];
	FSC_ASSERT( file );
	FS_FileToBuffer( file, name_buffer, sizeof( name_buffer ), qfalse, qfalse, qfalse, qfalse );
	if ( file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		FS_FileToBuffer( (const fsc_file_t *)FSC_GetBaseFile( file, &fs.index ), source_buffer, sizeof( source_buffer ),
				qtrue, qtrue, qfalse, qfalse );
		Com_Printf( "File %s found in %s\n", name_buffer, source_buffer );
	} else if ( file->sourcetype == FSC_SOURCETYPE_DIRECT ) {
		FS_FileToBuffer( file, source_buffer, sizeof( source_buffer ), qtrue, qtrue, qfalse, qfalse );
		Com_Printf( "File %s found at %s\n", name_buffer, source_buffer );
	} else {
		Com_Printf( "File %s has unknown sourcetype\n", name_buffer );
	}
}

/*
###############################################################################################

File disabled check

For determining which files are valid for the filesystem to use.

###############################################################################################
*/

/*
=================
FS_GetPureListPosition
=================
*/
static int FS_GetPureListPosition( const fsc_file_t *file ) {
	if ( file->sourcetype != FSC_SOURCETYPE_PK3 ) {
		return 0;
	}
	return FS_Pk3List_Lookup( &fs.connected_server_pure_list, FSC_GetBaseFile( file, &fs.index )->pk3_hash );
}

/*
=================
FS_InactiveModFileDisabled

Check if a file is disabled by inactive mod settings.
=================
*/
static qboolean FS_InactiveModFileDisabled( const fsc_file_t *file, int level, qboolean ignore_servercfg ) {
	// Allow file if full inactive mod searching is enabled
	if ( level >= 2 ) {
		return qfalse;
	}

	// Allow file if not in inactive mod directory
	if ( FS_GetModType( FSC_GetModDir( file, &fs.index ) ) > MODTYPE_INACTIVE ) {
		return qfalse;
	}

	// For setting 1, also allow files from core paks or on pure list
	if ( level == 1 ) {
		const fsc_file_direct_t *base_file = FSC_GetBaseFile( file, &fs.index );
		if ( base_file ) {
			if ( FS_Pk3List_Lookup( &fs.connected_server_pure_list, base_file->pk3_hash ) ) {
				return qfalse;
			}
			if ( FS_CorePk3Position( base_file->pk3_hash ) ) {
				return qfalse;
			}
		}
	}

#ifdef FS_SERVERCFG_ENABLED
	// Allow files in servercfg directories, unless explicitly ignored
	if ( !ignore_servercfg && FS_Servercfg_Priority( FSC_GetModDir( file, &fs.index ) ) ) {
		return qfalse;
	}
#endif

	return qtrue;
}

/*
=================
FS_CheckFileDisabled

This function is used to perform various checks for whether a file should be used by the filesystem.
Returns value of one of the triggering checks if file is disabled, null otherwise.
=================
*/
int FS_CheckFileDisabled( const fsc_file_t *file, int checks ) {
	FSC_ASSERT( file );

	// Pure list check - blocks files disabled by pure settings of server we are connected to
	if ( ( checks & FD_CHECK_PURE_LIST ) && FS_ConnectedServerPureState() == 1 ) {
		if ( !FS_GetPureListPosition( file ) ) {
			return FD_CHECK_PURE_LIST;
		}
	}

	// Read inactive mods check - blocks files disabled by inactive mod settings for file reading
	if ( checks & FD_CHECK_READ_INACTIVE_MODS ) {
		if ( FS_InactiveModFileDisabled( file, fs.cvar.fs_read_inactive_mods->integer, qfalse ) ) {
			return FD_CHECK_READ_INACTIVE_MODS;
		}
	}
	if ( checks & FD_CHECK_READ_INACTIVE_MODS_IGNORE_SERVERCFG ) {
		if ( FS_InactiveModFileDisabled( file, fs.cvar.fs_read_inactive_mods->integer, qtrue ) ) {
			return FD_CHECK_READ_INACTIVE_MODS_IGNORE_SERVERCFG;
		}
	}

	// List inactive mods check - blocks files disabled by inactive mod settings for file listing
	if ( checks & FD_CHECK_LIST_INACTIVE_MODS ) {
		// Use read_inactive_mods setting if it is lower, because it doesn't make sense to list unreadable files
		int list_inactive_mods_level = fs.cvar.fs_read_inactive_mods->integer < fs.cvar.fs_list_inactive_mods->integer ?
				fs.cvar.fs_read_inactive_mods->integer : fs.cvar.fs_list_inactive_mods->integer;
		if ( FS_InactiveModFileDisabled( file, list_inactive_mods_level, qfalse ) ) {
			return FD_CHECK_LIST_INACTIVE_MODS;
		}
	}

	return 0;
}

/*
###############################################################################################

File Sorting Functions

The lookup, file list, and reference modules have their own sorting systems due
to differences in requirements, but sorting logic and functions that are shared
between multiple modules are included here.

###############################################################################################
*/

/*
=================
FS_GetPathSortCharacterMap

This table maps path characters to precedence values.
higher value = higher precedence
=================
*/
static const unsigned char *FS_GetPathSortCharacterMap( void ) {
	qboolean initialized = qfalse;
	static unsigned char table[256];

	if ( !initialized ) {
		int i;
		unsigned char value = 250;
		for ( i = 'z'; i >= 'a'; --i )
			table[i] = value--;
		value = 250;
		for ( i = 'Z'; i >= 'A'; --i )
			table[i] = value--;
		for ( i = '9'; i >= '0'; --i )
			table[i] = value--;
		for ( i = 255; i >= 0; --i )
			if ( !table[i] )
				table[i] = value--;
		initialized = qtrue;
	}

	return table;
}

/*
=================
FS_SortKey_PureList
=================
*/
static unsigned int FS_SortKey_PureList( const fsc_file_t *file ) {
	if ( file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		// Pure list stores pk3s by position, with index 1 at highest priority,
		//   so index values need to be inverted to get precedence
		unsigned int index = FS_Pk3List_Lookup( &fs.connected_server_pure_list, FSC_GetBaseFile( file, &fs.index )->pk3_hash );
		if ( index ) {
			return ~index;
		}
	}
	return 0;
}

/*
=================
FS_SortKey_CurrentModDir
=================
*/
static unsigned int FS_SortKey_CurrentModDir( fs_modtype_t mod_type ) {
	if ( mod_type >= MODTYPE_OVERRIDE_DIRECTORY ) {
		return (unsigned int)mod_type;
	}
	return 0;
}

/*
=================
FS_SortKey_CorePk3s
=================
*/
static unsigned int FS_SortKey_CorePk3s( const fsc_file_t *file, fs_modtype_t mod_type ) {
	if ( mod_type < MODTYPE_OVERRIDE_DIRECTORY ) {
		const fsc_file_direct_t *base_file = FSC_GetBaseFile( file, &fs.index );
		if ( base_file ) {
			return FS_CorePk3Position( base_file->pk3_hash );
		}
	}
	return 0;
}

/*
=================
FS_SortKey_BaseModDir
=================
*/
static unsigned int FS_SortKey_BaseModDir( fs_modtype_t mod_type ) {
	if ( mod_type == MODTYPE_BASE ) {
		return 1;
	}
	return 0;
}

/*
=================
FS_WriteSortString

Set prioritize_shorter true to prioritize shorter strings (i.e. "abc" over "abcd")
=================
*/
void FS_WriteSortString( const char *string, fsc_stream_t *output, qboolean prioritize_shorter ) {
	const unsigned char *sort_table = FS_GetPathSortCharacterMap();
	while ( *string && output->position < output->size ) {
		output->data[output->position++] = (char)sort_table[*(unsigned char *)( string++ )];
	}
	if ( output->position < output->size ) {
		output->data[output->position++] = prioritize_shorter ? (char)(unsigned char)255 : '\0';
	}
}

/*
=================
FS_WriteSortFilename

Write sort key of the file itself
=================
*/
void FS_WriteSortFilename( const fsc_file_t *file, fsc_stream_t *output ) {
	char buffer[FS_FILE_BUFFER_SIZE];
	FS_FileToBuffer( file, buffer, sizeof( buffer ), qfalse, qfalse, qfalse, qfalse );
	FS_WriteSortString( buffer, output, qfalse );
}

/*
=================
FS_WriteSortPk3SourceFilename

Write sort key of the pk3 file or pk3dir the file came from
=================
*/
static void FS_WriteSortPk3SourceFilename( const fsc_file_t *file, fsc_stream_t *output ) {
	if ( file->sourcetype == FSC_SOURCETYPE_DIRECT && ( (fsc_file_direct_t *)file )->pk3dir_ptr ) {
		FS_WriteSortString( (const char *)STACKPTR( ( (fsc_file_direct_t *)file )->pk3dir_ptr ), output, qfalse );
		FS_WriteSortValue( 1, output );
	} else if ( file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		const fsc_file_direct_t *source_pk3 = FSC_GetBaseFile( file, &fs.index );
		FS_WriteSortString( (const char *)STACKPTR( source_pk3->f.qp_name_ptr ), output, qfalse );
		FS_WriteSortValue( 0, output );
	}
}

/*
=================
FS_WriteSortValue
=================
*/
void FS_WriteSortValue( unsigned int value, fsc_stream_t *output ) {
	static volatile int test = 1;
	if ( *(char *)&test ) {
		value = ( ( value << 8 ) & 0xFF00FF00 ) | ( ( value >> 8 ) & 0xFF00FF );
		value = ( value << 16 ) | ( value >> 16 );
	}
	if ( output->position + 3 < output->size ) {
		*( (unsigned int *)( output->data + output->position ) ) = value;
		output->position += 4;
	}
}

/*
=================
FS_WriteCoreSortKey

This is a rough version of the lookup precedence for reference and file listing purposes.

This sorts the mod/pk3 origin of the file, but not the actual file name, or the source directory
since the file list system handles file names separately and currently ignores source directory.
=================
*/
void FS_WriteCoreSortKey( const fsc_file_t *file, fsc_stream_t *output, qboolean use_server_pure_list ) {
	fs_modtype_t mod_type = FS_GetModType( FSC_GetModDir( file, &fs.index ) );
#ifdef FS_SERVERCFG_ENABLED
	unsigned int servercfg_precedence = FS_Servercfg_Priority( FSC_GetModDir( file, &fs.index ) );
#else
	unsigned int servercfg_precedence = 0;
#endif
	unsigned int current_mod_precedence = FS_SortKey_CurrentModDir( mod_type );

	if ( use_server_pure_list ) {
		FS_WriteSortValue( FS_SortKey_PureList( file ), output );
	}
	FS_WriteSortValue( servercfg_precedence, output );
	FS_WriteSortValue( current_mod_precedence, output );
	if ( !servercfg_precedence && !current_mod_precedence ) {
		FS_WriteSortValue( FS_SortKey_CorePk3s( file, mod_type ), output );
	}
	FS_WriteSortValue( FS_SortKey_BaseModDir( mod_type ), output );

	// Deprioritize download folder pk3 contents
	FS_WriteSortValue( FSC_FromDownloadPk3( file, &fs.index ) ? 0 : 1, output );

	if ( file->sourcetype == FSC_SOURCETYPE_PK3 ||
			( file->sourcetype == FSC_SOURCETYPE_DIRECT && ( (fsc_file_direct_t *)file )->pk3dir_ptr ) ) {
		FS_WriteSortValue( 0, output );
		FS_WriteSortPk3SourceFilename( file, output );
		FS_WriteSortValue( ( file->sourcetype == FSC_SOURCETYPE_PK3 ) ? ~( (fsc_file_frompk3_t *)file )->header_position : ~0u, output );
	} else {
		FS_WriteSortValue( 1, output );
	}
}

/*
=================
FS_ComparePk3Source
=================
*/
int FS_ComparePk3Source( const fsc_file_t *file1, const fsc_file_t *file2 ) {
	char buffer1[1024];
	char buffer2[1024];
	fsc_stream_t stream1 = FSC_InitStream( buffer1, sizeof( buffer1 ) );
	fsc_stream_t stream2 = FSC_InitStream( buffer2, sizeof( buffer2 ) );
	FS_WriteSortPk3SourceFilename( file1, &stream1 );
	FS_WriteSortPk3SourceFilename( file2, &stream2 );
	return FSC_Memcmp( stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position );
}

/*
###############################################################################################

Misc Functions

###############################################################################################
*/

/*
=================
FS_ExecuteConfigFile
=================
*/
void FS_ExecuteConfigFile( const char *name, fs_config_type_t config_type, cbufExec_t exec_type, qboolean quiet ) {
	char *data;
	unsigned int size = 0;

	if ( com_journalDataFile && com_journal->integer == 2 ) {
		// In journal playback mode, try to load config files from journal data file
		Com_Printf( "execing %s from journal data file\n", name );
		data = FS_Journal_ReadData();
		if ( !data ) {
			Com_Printf( "couldn't exec %s - not present in journal\n", name );
			return;
		}
	}

	else {
		const fsc_file_t *file;
		int lookup_flags = LOOKUPFLAG_PURE_ALLOW_DIRECT_SOURCE | LOOKUPFLAG_IGNORE_CURRENT_MAP;
		if ( fs.cvar.fs_download_mode->integer >= 2 ) {
			// Don't allow config files from restricted download folder pk3s, because they could disable the download folder
			// restrictions to unrestrict themselves
			lookup_flags |= LOOKUPFLAG_NO_DOWNLOAD_FOLDER;
		}
		if ( config_type == FS_CONFIGTYPE_SETTINGS ) {
			// For q3config.cfg and autoexec.cfg - only load files on disk and from appropriate fs_mod_settings locations
			lookup_flags |= ( LOOKUPFLAG_SETTINGS_FILE | LOOKUPFLAG_DIRECT_SOURCE_ONLY );
		}
		if ( config_type == FS_CONFIGTYPE_DEFAULT ) {
			// For default.cfg - only load from appropriate fs_mod_settings locations
			lookup_flags |= LOOKUPFLAG_SETTINGS_FILE;
		}

		if ( !quiet ) {
			Com_Printf( "execing %s\n", name );
		}

		// Locate file
		FS_AutoRefresh();
		file = FS_GeneralLookup( name, lookup_flags, qfalse );
		if ( !file ) {
			Com_Printf( "couldn't exec %s - file not found\n", name );
			FS_Journal_WriteData( NULL, 0 );
			return;
		}

		// Load data
		data = FS_ReadData( file, NULL, &size, "FS_ExecuteConfigFile" );
		if ( !data ) {
			Com_Printf( "couldn't exec %s - failed to read data\n", name );
			FS_Journal_WriteData( NULL, 0 );
			return;
		}
	}

	FS_Journal_WriteData( data, size );

	Cbuf_ExecuteText( exec_type, data );
	if ( exec_type == EXEC_APPEND ) {
		Cbuf_ExecuteText( EXEC_APPEND, "\n" );
	}
	FS_FreeData( data );
}

/*
=================
FS_LoadGameDLL

Used by vm.c
Returns dll handle, or null on error
=================
*/
void *FS_LoadGameDLL( const fsc_file_t *dll_file, void *entryPoint,
		intptr_t( QDECL *systemcalls )( intptr_t, ... ) ) {
	char dll_info_string[FS_FILE_BUFFER_SIZE];
	const void *dll_path;
	char *dll_path_string;
	void *dll_handle;
	FSC_ASSERT( dll_file );

	// Print the info message
	FS_FileToBuffer( dll_file, dll_info_string, sizeof( dll_info_string ), qtrue, qtrue, qtrue, qfalse );
	Com_Printf( "Attempting to load dll file at %s\n", dll_info_string );

	// Get dll path
	if ( dll_file->sourcetype != FSC_SOURCETYPE_DIRECT ) {
		// Shouldn't happen
		Com_Printf( "Error: selected dll is not direct sourcetype\n" );
		return NULL;
	}

	dll_path = STACKPTR( ( (fsc_file_direct_t *)dll_file )->os_path_ptr );
	dll_path_string = FSC_OSPathToString( dll_path );
	if ( !dll_path_string ) {
		// Generally shouldn't happen
		Com_Printf( "Error: failed to convert dll path\n" );
		return NULL;
	}

	// Attemt to open the dll
	dll_handle = Sys_LoadGameDll( dll_path_string, (vmMainProc *)entryPoint, systemcalls );
	if ( !dll_handle ) {
		Com_Printf( "Error: failed to load game dll\n" );
	}
	FSC_Free( dll_path_string );
	return dll_handle;
}

/*
=================
FS_GetModDescription
=================
*/
void FS_GetModDescription( const char *modDir, char *description, int descriptionLen ) {
	const char *descPath = va( "%s/description.txt", modDir );
	fileHandle_t descHandle;
	int descLen = FS_BaseDir_FOpenFileRead( descPath, &descHandle );
	if ( descLen > 0 && descHandle ) {
		descLen = FS_Read( description, descriptionLen - 1, descHandle );
		description[descLen] = '\0';
	}
	if ( descHandle ) {
		FS_Handle_Close( descHandle );
	}
	if ( descLen <= 0 ) {
		// Just use the mod name as the description
		Q_strncpyz( description, modDir, descriptionLen );
	}
}

/*
=================
FS_FilenameCompletion
=================
*/
void FS_FilenameCompletion( const char *dir, const char *ext, char *filter, qboolean stripExt,
		void ( *callback )( const char *s ), qboolean allowNonPureFilesOnDisk ) {
	char **filenames;
	int nfiles;
	int i;
	char filename[MAX_STRING_CHARS];

	// Currently using the less restrictive LISTFLAG_IGNORE_PURE_LIST when allowNonPureFilesOnDisk is
	// false, since that's what's used for map completion, and we want to ignore the pure list there
	filenames = FS_ListFilteredFiles_Flags( dir, ext, filter, &nfiles,
		allowNonPureFilesOnDisk ? LISTFLAG_PURE_ALLOW_DIRECT_SOURCE : LISTFLAG_IGNORE_PURE_LIST );

	for ( i = 0; i < nfiles; i++ ) {
		Q_strncpyz( filename, filenames[i], MAX_STRING_CHARS );

		if ( stripExt ) {
			COM_StripExtension( filename, filename, sizeof( filename ) );
		}

		callback( filename );
	}

	FS_FreeFileList( filenames );
}

/*
=================
FS_FilenameCompare

From original filesystem. Used in a couple of places.
=================
*/
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

/*
=================
FS_Printf
=================
*/
void QDECL FS_Printf( fileHandle_t h, const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	Q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	FS_Write(msg, strlen(msg), h);
}

/*
=================
FS_CommaSeparatedList

Writes array of strings to stream separated by comma (useful for debug print purposes)
Ignores strings that are null or empty
Writes "<none>" if nothing was written
=================
*/
void FS_CommaSeparatedList( const char **strings, int count, fsc_stream_t *output ) {
	int i;
	qboolean have_item = qfalse;
	FSC_ASSERT( strings );
	FSC_ASSERT( output );
	FSC_StreamAppendString( output, "" );
	for ( i = 0; i < count; ++i ) {
		if ( strings[i] && *strings[i] ) {
			if ( have_item ) {
				FSC_StreamAppendString( output, ", " );
			}
			FSC_StreamAppendString( output, strings[i] );
			have_item = qtrue;
		}
	}
	if ( !have_item ) {
		FSC_StreamAppendString( output, "<none>" );
	}
}

/*
=================
FS_idPak
=================
*/
qboolean FS_idPak( const char *pak, const char *base, int numPaks )
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

/*
=================
FS_SanitizeModDir

Sanitizes mod dir string. If mod dir is invalid it will be replaced with empty string.
Target should be size FSC_MAX_MODDIR
=================
*/
void FS_SanitizeModDir( const char *source, char *target ) {
	char buffer[FSC_MAX_MODDIR];

	// Copy to buffer before calling FS_GeneratePath, to allow overly long mod names to be truncated
	//   instead of the normal FS_GeneratePath behavior of generating an empty string on overflow
	Q_strncpyz( buffer, source, sizeof( buffer ) );
	if ( !FS_GeneratePath( buffer, NULL, NULL, 0, 0, 0, target, FSC_MAX_MODDIR ) ) {
		*target = '\0';
	}
}

/*
###############################################################################################

VM Hash Verification

###############################################################################################
*/

/*
=================
FS_CalculateFileSha256

Calculates SHA256 hash of file. Returns qtrue on success, qfalse otherwise.
=================
*/
qboolean FS_CalculateFileSha256( const fsc_file_t *file, unsigned char *output ) {
	unsigned int size = 0;
	char *data = FS_ReadData( file, NULL, &size, "FS_CalculateFileSha256" );
	if ( !data ) {
		Com_Memset( output, 0, 32 );
		return qfalse;
	}
	FSC_CalculateSHA256( data, size, output );
	FS_FreeData( data );
	return qtrue;
}

/*
=================
FS_CheckTrustedVMFile

Returns qtrue if file matches a known trusted hash, qfalse otherwise.
=================
*/
qboolean FS_CheckTrustedVMFile( const fsc_file_t *file ) {
	unsigned char sha[32];
	if ( !FS_CalculateFileSha256( file, sha ) ) {
		return qfalse;
	}
	return FS_CheckTrustedVMHash( sha );
}

/*
=================
FS_Sha256ToStream

Writes a readable representation of a SHA256 hash to stream.
=================
*/
void FS_Sha256ToStream( unsigned char *sha, fsc_stream_t *output ) {
	int i;
	char buffer[4];
	for ( i = 0; i < 32; ++i ) {
		Com_sprintf( buffer, sizeof( buffer ), "%02x", sha[i] );
		FSC_StreamAppendString( output, buffer );
	}
}

/*
###############################################################################################

Core Pak Verification

This section is used to verify the core (ID) paks are present on startup, and produce
appropriate warnings or errors if they are missing or corrupt.

###############################################################################################
*/

#ifndef STANDALONE
static const unsigned int core_hashes[] = { 1566731103u, 298122907u, 412165236u,
	2991495316u, 1197932710u, 4087071573u, 3709064859u, 908855077u, 977125798u };

static const unsigned int missionpack_hashes[] = { 2430342401u, 511014160u,
	2662638993u, 1438664554u };

/*
=================
FS_CheckDefaultCfgPk3

Returns qtrue if there is a pk3 containing default.cfg with either the given name or hash.
=================
*/
static qboolean FS_CheckDefaultCfgPk3( const char *mod, const char *filename, unsigned int hash ) {
	fsc_file_iterator_t it = FSC_FileIteratorOpen( &fs.index, "", "default" );

	while ( FSC_FileIteratorAdvance( &it ) ) {
		const fsc_file_direct_t *source_pk3;
		if ( FS_CheckFileDisabled( it.file, FD_CHECK_READ_INACTIVE_MODS ) )
			continue;
		if ( it.file->sourcetype != FSC_SOURCETYPE_PK3 )
			continue;
		if ( Q_stricmp( (const char *)STACKPTR( it.file->qp_ext_ptr ), ".cfg" ) )
			continue;

		source_pk3 = FSC_GetBaseFile( it.file, &fs.index );
		if ( source_pk3->pk3_hash == hash )
			return qtrue;
		if ( mod && Q_stricmp( FSC_GetModDir( (const fsc_file_t *)source_pk3, &fs.index ), mod ) )
			continue;
		if ( !Q_stricmp( (const char *)STACKPTR( source_pk3->f.qp_name_ptr ), filename ) )
			return qtrue;
	}

	return qfalse;
}

typedef struct {
	const fsc_file_direct_t *name_match;
	const fsc_file_direct_t *hash_match;
} core_pak_state_t;

/*
=================
FS_GetCorePakState

Locates name and hash matches for a given core pak.
=================
*/
static core_pak_state_t FS_GetCorePakState( const char *mod, const char *filename, unsigned int hash ) {
	const fsc_file_direct_t *name_match = NULL;
	fsc_file_iterator_t it_files = FSC_FileIteratorOpen( &fs.index, "", filename );
	fsc_pk3_iterator_t it_pk3s = FSC_Pk3IteratorOpen( &fs.index, hash );

	while ( FSC_FileIteratorAdvance( &it_files ) ) {
		const fsc_file_direct_t *pk3 = (fsc_file_direct_t *)it_files.file;
		if ( it_files.file->sourcetype != FSC_SOURCETYPE_DIRECT )
			continue;
		if ( FS_CheckFileDisabled( it_files.file, FD_CHECK_READ_INACTIVE_MODS ) )
			continue;
		if ( Q_stricmp( (const char *)STACKPTR( it_files.file->qp_ext_ptr ), ".pk3" ) )
			continue;
		if ( mod && Q_stricmp( FSC_GetModDir( it_files.file, &fs.index ), mod ) )
			continue;
		if ( pk3->pk3_hash == hash ) {
			core_pak_state_t result = { pk3, pk3 };
			return result;
		}
		name_match = pk3;
	}

	while ( FSC_Pk3IteratorAdvance( &it_pk3s ) ) {
		if ( FS_CheckFileDisabled( (fsc_file_t *)it_pk3s.pk3, FD_CHECK_READ_INACTIVE_MODS ) )
			continue;
		core_pak_state_t result = { name_match, it_pk3s.pk3 };
		return result;
	}

	{
		core_pak_state_t result = { name_match, NULL };
		return result;
	}
}

/*
=================
FS_GeneratePakWarnings

Prints appropriate console warning messages and appends warning popup string for a given core pak.
=================
*/
static void FS_GeneratePakWarnings( const char *mod, const char *filename, core_pak_state_t *state, fsc_stream_t *warning_popup_stream ) {
	if ( state->hash_match ) {
		if ( !state->name_match ) {
			char hash_match_buffer[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( (fsc_file_t *)state->hash_match, hash_match_buffer, sizeof( hash_match_buffer ),
					qfalse, qtrue, qfalse, qfalse );
			Com_Printf( "NOTE: %s/%s.pk3 is misnamed, found correct file at %s\n",
					mod, filename, hash_match_buffer );
		} else if ( state->name_match != state->hash_match ) {
			char hash_match_buffer[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( (fsc_file_t *)state->hash_match, hash_match_buffer, sizeof( hash_match_buffer ),
					qfalse, qtrue, qfalse, qfalse );
			Com_Printf( "WARNING: %s/%s.pk3 has incorrect hash, found correct file at %s\n",
					mod, filename, hash_match_buffer );
		}
	} else {
		if ( state->name_match ) {
			Com_Printf( "WARNING: %s/%s.pk3 has incorrect hash\n", mod, filename );
			FSC_StreamAppendString( warning_popup_stream, va( "%s/%s.pk3: incorrect hash\n", mod, filename ) );
		} else {
			Com_Printf( "WARNING: %s/%s.pk3 not found\n", mod, filename );
			FSC_StreamAppendString( warning_popup_stream, va( "%s/%s.pk3: not found\n", mod, filename ) );
		}
	}
}

/*
=================
FS_CheckCorePaks

Checks and generates warnings if any core pk3s are potentially missing or corrupt.
=================
*/
void FS_CheckCorePaks( void ) {
	int i;
	core_pak_state_t core_states[ARRAY_LEN( core_hashes )];
	core_pak_state_t missionpack_states[ARRAY_LEN( missionpack_hashes )];
	qboolean missionpack_installed = qfalse; // Any missionpack paks detected
	char warning_popup_buffer[1024];
	fsc_stream_t warning_popup_stream = FSC_InitStream( warning_popup_buffer, sizeof( warning_popup_buffer ) );

	// Generate pak states
	for ( i = 0; i < ARRAY_LEN( core_hashes ); ++i ) {
		core_states[i] = FS_GetCorePakState( BASEGAME, va( "pak%i", i ), core_hashes[i] );
	}
	for ( i = 0; i < ARRAY_LEN( missionpack_hashes ); ++i ) {
		missionpack_states[i] = FS_GetCorePakState( "missionpack", va( "pak%i", i ), missionpack_hashes[i] );
		if ( missionpack_states[i].name_match || missionpack_states[i].hash_match )
			missionpack_installed = qtrue;
	}

	// Check for standalone mode
	if ( Q_stricmp( com_basegame->string, BASEGAME ) ) {
		qboolean have_id_pak = qfalse;
		for ( i = 0; i < ARRAY_LEN( core_hashes ); ++i )
			if ( core_states[i].hash_match )
				have_id_pak = qtrue;
		for ( i = 0; i < ARRAY_LEN( missionpack_hashes ); ++i )
			if ( missionpack_states[i].hash_match )
				have_id_pak = qtrue;
		if ( !have_id_pak ) {
			Com_Printf( "Enabling standalone mode - no ID paks found\n" );
			Cvar_Set( "com_standalone", "1" );
			return;
		}
	}

	// Print console warning messages and build warning popup string
	for ( i = 0; i < ARRAY_LEN( core_hashes ); ++i ) {
		FS_GeneratePakWarnings( BASEGAME, va( "pak%i", i ), &core_states[i], &warning_popup_stream );
	}
	if ( missionpack_installed )
		for ( i = 0; i < ARRAY_LEN( missionpack_hashes ); ++i ) {
			FS_GeneratePakWarnings( "missionpack", va( "pak%i", i ), &missionpack_states[i], &warning_popup_stream );
		}

	// Print additional warning if pak0.pk3 exists by name or hash, but doesn't contain default.cfg
	if ( ( core_states[0].name_match || core_states[0].hash_match ) &&
				!FS_CheckDefaultCfgPk3( BASEGAME, "pak0", core_hashes[0] ) ) {
		Com_Printf( "WARNING: default.cfg not found - pak0.pk3 may be corrupt\n" );
		FSC_StreamAppendString( &warning_popup_stream, "default.cfg not found - pak0.pk3 may be corrupt\n" );
	}

#ifndef DEDICATED
	// If warning popup info was generated, display warning popup
	if ( warning_popup_stream.position ) {
		dialogResult_t result = Sys_Dialog( DT_OK_CANCEL,
				va(
					"The following game files appear"
					" to be missing or corrupt. You can try to run the game anyway, but you may"
					" experience errors or problems connecting to remote servers.\n\n%s\n"
					"You may need to reinstall Quake 3, the v1.32 patch, and/or team arena.",
					warning_popup_buffer ),
				"File Warning" );
		if ( result == DR_CANCEL ) {
			Sys_Quit();
		}
	}
#endif
}
#endif

#endif	// NEW_FILESYSTEM
