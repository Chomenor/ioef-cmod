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

// Max length of each subpath in FS_GeneratePath (does not affect FS_NO_SANITIZE subpaths)
#define MAX_SUBPATH_LENGTH 128

/*
###############################################################################################

Path Handling Functions

###############################################################################################
*/

/*
=================
FS_MkdirInRange

Base and position should be pointers to the same string buffer, where base represents the
beginning of the string and position is the part where actual directory creation starts.
If for_file is set, the final part of the path will not be created as a directory.
=================
*/
static void FS_MkdirInRange( char *base, char *position, qboolean for_file ) {
	while ( 1 ) {
		if ( *position == '/' ) {
			*position = '\0';
			FSC_Mkdir( base );
			*position = '/';
		}
		if ( !*position ) {
			if ( !for_file ) {
				FSC_Mkdir( base );
			}
			return;
		}
		++position;
	}
}

/*
=================
FS_ValidFilenameCharTable

Returns map of characters allowed in filenames for direct disk access, with
	invalid characters converted to underscores.
This table applies to write operations and certain read operations that use
	FS_GeneratePath, but not the main file index.
=================
*/
static const char *FS_ValidFilenameCharTable( void ) {
	static char table[256];
	static qboolean have_table = qfalse;
	if ( !have_table ) {
		int i;
		char valid_chars[] = " ~!@#$%^&_-+=()[]{}';,.";
		for ( i = 0; i < 256; ++i )
			table[i] = '_';
		for ( i = 'a'; i <= 'z'; ++i )
			table[i] = i;
		for ( i = 'A'; i <= 'Z'; ++i )
			table[i] = i;
		for ( i = '0'; i <= '9'; ++i )
			table[i] = i;
		for ( i = 0; i < sizeof( valid_chars ) - 1; ++i )
			table[( (unsigned char *)valid_chars )[i]] = valid_chars[i];
		have_table = qtrue;
	}
	return table;
}

/*
=================
FS_GeneratePathFilename

Sanitize name of single file or directory and write to stream.
Returns qtrue on success, qfalse on error.
=================
*/
static qboolean FS_GeneratePathFilename( fsc_stream_t *stream, const char *name, int flags ) {
	char sanitized_path[MAX_SUBPATH_LENGTH];
	int path_length;
	fsc_stream_t path_stream = FSC_InitStream( sanitized_path, sizeof( sanitized_path ) );

	// Perform character filtering
	FSC_StreamAppendStringSubstituted( &path_stream, name, FS_ValidFilenameCharTable() );
	path_length = FSC_Strlen( sanitized_path ); // Should equal path_stream.position, but recalculate to be safe
	if ( !path_length )
		return qfalse;

// Also replace certain characters at beginning or end of string with underscores
#define INVALID_EDGE_CHAR( c ) ( ( c ) == ' ' || ( c ) == '.' )
	if ( INVALID_EDGE_CHAR( sanitized_path[0] ) )
		sanitized_path[0] = '_';
	if ( INVALID_EDGE_CHAR( sanitized_path[path_length - 1] ) )
		sanitized_path[path_length - 1] = '_';

	// Check for possible backwards path
	if ( strstr( sanitized_path, ".." ) )
		return qfalse;

	// Check for disallowed extensions
	if ( path_length >= 4 && !Q_stricmp( sanitized_path + path_length - 4, ".qvm" ) )
		return qfalse;
	if ( path_length >= 4 && !Q_stricmp( sanitized_path + path_length - 4, ".exe" ) )
		return qfalse;
	if ( path_length >= 4 && !Q_stricmp( sanitized_path + path_length - 4, ".app" ) )
		return qfalse;
	if ( !( flags & FS_ALLOW_PK3 ) && path_length >= 4 && !Q_stricmp( sanitized_path + path_length - 4, ".pk3" ) )
		return qfalse;
	if ( !( flags & FS_ALLOW_DLL ) ) {
		if ( Sys_DllExtension( sanitized_path ) )
			return qfalse;
		// Do some extra checks to be safe
		if ( path_length >= 4 && !Q_stricmp( sanitized_path + path_length - 4, ".dll" ) )
			return qfalse;
		if ( path_length >= 3 && !Q_stricmp( sanitized_path + path_length - 3, ".so" ) )
			return qfalse;
		if ( path_length >= 6 && !Q_stricmp( sanitized_path + path_length - 6, ".dylib" ) )
			return qfalse;
	}
	if ( !( flags & FS_ALLOW_SPECIAL_CFG ) && ( !Q_stricmp( sanitized_path, Q3CONFIG_CFG ) ||
												!Q_stricmp( sanitized_path, "autoexec.cfg" ) ) )
		return qfalse;

	// Write out the string
	FSC_StreamAppendString( stream, sanitized_path );
	return qtrue;
}

/*
=================
FS_GenerateSubpath

Writes path to stream with sanitization and other operations based on flags.
Returns qtrue on success, qfalse on error.
=================
*/
static qboolean FS_GenerateSubpath( fsc_stream_t *stream, const char *path, int flags ) {
	int old_position = stream->position;

	if ( flags & FS_NO_SANITIZE ) {
		// If sanitize disabled, just write out the string
		FSC_StreamAppendString( stream, path );
	}

	else if ( flags & FS_ALLOW_DIRECTORIES ) {
		// Write each section of the path separated by slashes
		char name[MAX_SUBPATH_LENGTH];
		const char *path_ptr = path;
		qboolean first_element = qtrue;
		if ( FSC_Strlen( path ) >= MAX_SUBPATH_LENGTH ) {
			return qfalse;
		}
		while ( path_ptr ) {
			if ( !FSC_SplitLeadingDirectory( path_ptr, name, sizeof( name ), &path_ptr ) ) {
				// Ignore empty sections caused by excess slashes
				continue;
			}
			if ( !first_element ) {
				FSC_StreamAppendString( stream, "/" );
			}
			if ( !FS_GeneratePathFilename( stream, name, flags ) ) {
				// Abort on sanitize error
				return qfalse;
			}
			first_element = qfalse;
		}
	}

	else {
		// Write single path element
		if ( !FS_GeneratePathFilename( stream, path, flags ) ) {
			return qfalse;
		}
	}

	// Create directories for path
	if ( flags & FS_CREATE_DIRECTORIES_FOR_FILE ) {
		FS_MkdirInRange( stream->data, stream->data + old_position, qtrue );
	} else if ( flags & FS_CREATE_DIRECTORIES ) {
		FS_MkdirInRange( stream->data, stream->data + old_position, qfalse );
	}

	return qtrue;
}

/*
=================
FS_GeneratePath

Concatenates paths, adding '/' character as seperator, with sanitization
	and directory creation based on flags.
Returns output length on success, 0 on error (overflow or sanitize error).
=================
*/
unsigned int FS_GeneratePath( const char *path1, const char *path2, const char *path3,
		int path1_flags, int path2_flags, int path3_flags, char *target, unsigned int target_size ) {
	fsc_stream_t stream = FSC_InitStream( target, target_size );
	FSC_ASSERT( target );

	if ( path1 ) {
		if ( !FS_GenerateSubpath( &stream, path1, path1_flags ) ) {
			goto error;
		}
	}

	if ( path2 ) {
		if ( path1 ) {
			FSC_StreamAppendString( &stream, "/" );
		}
		if ( !FS_GenerateSubpath( &stream, path2, path2_flags ) ) {
			goto error;
		}
	}

	if ( path3 ) {
		if ( path1 || path2 ) {
			FSC_StreamAppendString( &stream, "/" );
		}
		if ( !FS_GenerateSubpath( &stream, path3, path3_flags ) ) {
			goto error;
		}
	}

	if ( !stream.position || stream.overflowed ) {
		goto error;
	}
	return stream.position;

error:
	*target = '\0';
	return 0;
}

/*
=================
FS_GeneratePathSourcedir

Generates path prefixed by a certain source directory.
=================
*/
unsigned int FS_GeneratePathSourcedir( int source_dir_id, const char *path1, const char *path2,
		int path1_flags, int path2_flags, char *target, unsigned int target_size ) {
	FSC_ASSERT( target );
	if ( !fs.sourcedirs[source_dir_id].active ) {
		*target = '\0';
		return 0;
	}
	return FS_GeneratePath( fs.sourcedirs[source_dir_id].path, path1, path2, FS_NO_SANITIZE,
			path1_flags, path2_flags, target, target_size );
}

/*
=================
FS_GeneratePathWritedir

Generates path prefixed by the current filesystem write directory.
=================
*/
unsigned int FS_GeneratePathWritedir( const char *path1, const char *path2,
		int path1_flags, int path2_flags, char *target, unsigned int target_size ) {
	FSC_ASSERT( target );
	if ( fs.read_only ) {
		*target = '\0';
		return 0;
	}
	return FS_GeneratePathSourcedir( 0, path1, path2, path1_flags, path2_flags, target, target_size );
}

/*
###############################################################################################

Misc functions

###############################################################################################
*/

/*
=================
FS_HomeRemove
=================
*/
void FS_HomeRemove( const char *homePath ) {
	char path[FS_MAX_PATH];
	if ( !FS_GeneratePathWritedir( FS_GetCurrentGameDir(), homePath, 0, FS_ALLOW_DIRECTORIES,
			path, sizeof( path ) ) ) {
		Com_Printf( "WARNING: FS_HomeRemove on %s failed due to invalid path\n", homePath );
		return;
	}

	FSC_DeleteFile( path );
}

/*
=================
FS_FileInPathExists
=================
*/
qboolean FS_FileInPathExists( const char *testpath ) {
	fsc_filehandle_t *handle = FSC_FOpen( testpath, "rb" );
	if ( handle ) {
		FSC_FClose( handle );
		return qtrue;
	}
	return qfalse;
}

/*
=================
FS_FileExists
=================
*/
qboolean FS_FileExists( const char *file ) {
	char path[FS_MAX_PATH];
	if ( !FS_GeneratePathSourcedir( 0, FS_GetCurrentGameDir(), file, 0, FS_ALLOW_DIRECTORIES,
			path, sizeof( path ) ) ) {
		return qfalse;
	}
	return FS_FileInPathExists( path );
}

/*
###############################################################################################

File read cache

###############################################################################################
*/

typedef struct cache_entry_s {
	unsigned int size;
	int lock_count;
	int stage;

	const fsc_file_t *file;
	unsigned int file_size;
	unsigned int file_timestamp;

	struct cache_entry_s *next_position;
	unsigned int lookup_hash;
	struct cache_entry_s *next_lookup;
	struct cache_entry_s *prev_lookup;
} cache_entry_t;

// ***** Cache lookup table *****

#define CACHE_LOOKUP_TABLE_SIZE 4096
static cache_entry_t *cache_lookup_table[CACHE_LOOKUP_TABLE_SIZE];

// ***** Cache data store *****

#define CACHE_ENTRY_DATA( cache_entry ) ( (char *)( cache_entry ) + sizeof( cache_entry_t ) )
#define CACHE_ALIGN( ptr ) ( ( ( uintptr_t )( ptr ) + 15 ) & ~15 )

int cache_stage = 0;
int cache_size = 0;
cache_entry_t *base_entry;
cache_entry_t *head_entry; // Last entry created. Null if just initialized.

/*
=================
FS_ReadCache_HashFile
=================
*/
static unsigned int FS_ReadCache_HashFile( const fsc_file_t *file ) {
	if ( !file ) {
		return 0;
	}
	return FSC_StringHash( (const char *)STACKPTR( file->qp_name_ptr ), (const char *)STACKPTR( file->qp_dir_ptr ) );
}

/*
=================
FS_ReadCache_LookupTableRegister
=================
*/
static void FS_ReadCache_LookupTableRegister( cache_entry_t *entry ) {
	int position = entry->lookup_hash % CACHE_LOOKUP_TABLE_SIZE;
	entry->next_lookup = cache_lookup_table[position];
	entry->prev_lookup = NULL;
	if ( cache_lookup_table[position] ) {
		cache_lookup_table[position]->prev_lookup = entry;
	}
	cache_lookup_table[position] = entry;
}

/*
=================
FS_ReadCache_LookupTableDeregister
=================
*/
static void FS_ReadCache_LookupTableDeregister( cache_entry_t *entry ) {
	int position = entry->lookup_hash % CACHE_LOOKUP_TABLE_SIZE;
	if ( entry->next_lookup ) {
		entry->next_lookup->prev_lookup = entry->prev_lookup;
	}
	if ( entry->prev_lookup ) {
		entry->prev_lookup->next_lookup = entry->next_lookup;
	} else {
		cache_lookup_table[position] = entry->next_lookup;
	}
}

/*
=================
FS_ReadCache_LookupTableDeregisterRange
=================
*/
static void FS_ReadCache_LookupTableDeregisterRange( cache_entry_t *start, cache_entry_t *end ) {
	while ( start && start != end ) {
		FS_ReadCache_LookupTableDeregister( start );
		start = start->next_position;
	}
}

/*
=================
FS_ReadCache_EntryMatchesFile

We need a little more extensive check than just comparing the file pointer because
FSC_LoadFile can reuse an existing file object when a file is modified in certain cases.
=================
*/
static qboolean FS_ReadCache_EntryMatchesFile( const fsc_file_t *file, const cache_entry_t *cache_entry ) {
	if ( file != cache_entry->file ) {
		return qfalse;
	}
	if ( cache_entry->file_size != cache_entry->file->filesize ) {
		return qfalse;
	}
	if ( file->sourcetype == FSC_SOURCETYPE_DIRECT &&
		 cache_entry->file_timestamp != ( (fsc_file_direct_t *)file )->os_timestamp ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
FS_ReadCache_LookupSearch
=================
*/
static cache_entry_t *FS_ReadCache_LookupSearch( const fsc_file_t *file ) {
	cache_entry_t *entry = cache_lookup_table[FS_ReadCache_HashFile( file ) % CACHE_LOOKUP_TABLE_SIZE];
	cache_entry_t *best_entry = NULL;

	while ( entry ) {
		if ( ( !best_entry || entry->stage > best_entry->stage ) && FS_ReadCache_EntryMatchesFile( file, entry ) ) {
			best_entry = entry;
		}
		entry = entry->next_lookup;
	}

	return best_entry;
}

/*
=================
FS_ReadCache_Allocate
=================
*/
static cache_entry_t *FS_ReadCache_Allocate( const fsc_file_t *file, unsigned int size ) {
	unsigned int required_space = size + sizeof( cache_entry_t );
	int wrapped_around = 0;
	cache_entry_t *lead_entry = head_entry; // Entry preceding new entry (can be null)
	cache_entry_t *limit_entry = lead_entry ? lead_entry->next_position : NULL; // Entry following new entry (can be null)
	char *start_point, *end_point; // Range of memory available for new entry: [start, end)
	cache_entry_t *new_entry;

	while ( 1 ) {
		// Check if we have enough space yet
		start_point = (char *)CACHE_ALIGN( lead_entry ? (char *)lead_entry + lead_entry->size + sizeof( cache_entry_t ) : (char *)base_entry );
		end_point = limit_entry ? (char *)limit_entry : (char *)base_entry + cache_size;
		FSC_ASSERT( end_point >= start_point );

		if ( end_point - start_point >= required_space ) {
			break;
		}

		// Wraparound check
		if ( !limit_entry ) {
			if ( !head_entry || wrapped_around++ ) {
				return NULL;
			}
			lead_entry = NULL;
			limit_entry = base_entry;
			continue;
		}

		// Don't advance limit over a locked entry
		while ( limit_entry && limit_entry->lock_count ) {
			lead_entry = limit_entry;
			limit_entry = lead_entry->next_position;
		}

		// Advance limit
		if ( limit_entry ) {
			limit_entry = limit_entry->next_position;
		}
	}

	// We have space for a new entry
	new_entry = (cache_entry_t *)start_point;

	// Deregister entries we are overwriting
	if ( lead_entry ) {
		FS_ReadCache_LookupTableDeregisterRange( lead_entry->next_position, limit_entry );
		lead_entry->next_position = new_entry;
	} else if ( head_entry ) {
		FS_ReadCache_LookupTableDeregisterRange( base_entry, limit_entry );
	}

	new_entry->next_position = limit_entry;
	head_entry = new_entry;

	new_entry->size = size;
	new_entry->lock_count = 0;
	new_entry->stage = cache_stage;
	new_entry->file = file;
	new_entry->file_size = file ? file->filesize : 0;
	new_entry->file_timestamp = file && file->sourcetype == FSC_SOURCETYPE_DIRECT ? ( (fsc_file_direct_t *)file )->os_timestamp : 0;
	new_entry->lookup_hash = FS_ReadCache_HashFile( file );

	FS_ReadCache_LookupTableRegister( new_entry );

	return new_entry;
}

/*
=================
FS_ReadCache_Initialize

This gets called directly from Com_Init, after the config files have been read,
to allow setting fs_read_cache_megs in the normal config file instead of the command line.
=================
*/
void FS_ReadCache_Initialize( void ) {
#ifdef DEDICATED
	cvar_t *cache_megs_cvar = Cvar_Get( "fs_read_cache_megs", "4", CVAR_LATCH | CVAR_ARCHIVE );
#else
	cvar_t *cache_megs_cvar = Cvar_Get( "fs_read_cache_megs", "64", CVAR_LATCH | CVAR_ARCHIVE );
#endif
	int cache_megs = cache_megs_cvar->integer;
	if ( cache_megs < 0 ) {
		cache_megs = 0;
	}
	if ( cache_megs > 1024 ) {
		cache_megs = 1024;
	}

	cache_size = cache_megs << 20;
	base_entry = (cache_entry_t *)FSC_Malloc( cache_size );
	head_entry = NULL;
}

/*
=================
FS_ReadCache_AdvanceStage

Causes existing files in cache to be recopied to the front of the cache on reference.
This may be called between level loads to help with performance. This is only for
optimization purposes and should not have any functional effects.
=================
*/
void FS_ReadCache_AdvanceStage( void ) {
	++cache_stage;
}

/*
=================
FS_ReadCache_CacheLookupStaged

Attempts to locate file in cache. Returns corresponding cache entry if found, null otherwise.

If file is found in an earlier cache stage, it will be duplicated to the front of the cache
and the new entry returned instead.
=================
*/
static cache_entry_t *FS_ReadCache_CacheLookupStaged( const fsc_file_t *file ) {
	cache_entry_t *entry = FS_ReadCache_LookupSearch( file );
	if ( !entry ) {
		return NULL;
	}

	if ( entry->stage != cache_stage ) {
		cache_entry_t *new_entry;
		++entry->lock_count;
		new_entry = FS_ReadCache_Allocate( file, entry->size );
		--entry->lock_count;
		if ( new_entry ) {
			FSC_Memcpy( CACHE_ENTRY_DATA( new_entry ), CACHE_ENTRY_DATA( entry ), entry->size );
			return new_entry;
		}
	}

	return entry;
}

// ***** Cache debugging *****

/*
=================
FS_ReadCache_EntryCountDirect
=================
*/
static int FS_ReadCache_EntryCountDirect( void ) {
	cache_entry_t *entry = base_entry;
	int count = 0;

	if ( !head_entry ) {
		return 0;
	}
	do {
		++count;
	} while ( ( entry = entry->next_position ) );

	return count;
}

/*
=================
FS_ReadCache_EntryCountTable
=================
*/
static int FS_ReadCache_EntryCountTable( void ) {
	int i;
	cache_entry_t *entry;
	int count = 0;

	for ( i = 0; i < CACHE_LOOKUP_TABLE_SIZE; ++i ) {
		entry = cache_lookup_table[i];
		while ( entry ) {
			++count;
			entry = entry->next_lookup;
		}
	}

	return count;
}

/*
=================
FS_ReadCache_Debug

Prints information about cache contents to console.
=================
*/
void FS_ReadCache_Debug( void ) {
	cache_entry_t *entry = base_entry;
	char data[1000];
	fsc_stream_t stream = FSC_InitStream( data, sizeof( data ) );
	int index_counter = 0;

	if ( !head_entry ) {
		return;
	}

#define ADD_STRING( string ) FSC_StreamAppendString( &stream, string )

	do {
		stream.position = 0;
		if ( !entry->file ) {
			ADD_STRING( va( "Null File Index(%i) Position(%i) Size(%i) Stage(%i) Lockcount(%i)",
							index_counter, (int)( (char *)entry - (char *)base_entry ), entry->size, entry->stage, entry->lock_count ) );
		} else {
			ADD_STRING( "File(" );
			FS_FileToStream( entry->file, &stream, qtrue, qtrue, qtrue, qfalse );
			ADD_STRING( va( ") Index(%i) Position(%i) Size(%i) Stage(%i) Lockcount(%i)",
							index_counter, (int)( (char *)entry - (char *)base_entry ), entry->size, entry->stage, entry->lock_count ) );
		}
		if ( entry == head_entry ) {
			ADD_STRING( " <head entry>" );
		}
		ADD_STRING( "\n\n" );
		Com_Printf( "%s", stream.data );
		++index_counter;
	} while ( ( entry = entry->next_position ) );

	// These should always be the same
	Com_Printf( "entry count from direct iteration: %i\n", FS_ReadCache_EntryCountDirect() );
	Com_Printf( "entry count from lookup table: %i\n", FS_ReadCache_EntryCountTable() );
}

/*
###############################################################################################

Data reading

###############################################################################################
*/

/*
=================
FS_ReadData

Input can be either file or path, not both.
Returns null on error, otherwise result needs to be freed by FS_FreeData.
Currently file-type read always reads file->filesize, otherwise it is an error and null is returned.
=================
*/
char *FS_ReadData( const fsc_file_t *file, const char *path, unsigned int *size_out, const char *calling_function ) {
	cache_entry_t *cache_entry = NULL;
	char *data = NULL;
	void *os_path = NULL;
	void *fsc_file_handle = NULL;
	unsigned int size;

	// Ensure we have file or path set but not both
	if ( ( file && path ) || ( !file && !path ) ) {
		Com_Error( ERR_DROP, "Invalid parameters to FS_ReadData." );
	}

	// Mark the file in reference tracking
	if ( file ) {
		FS_RegisterReference( file );
	}

	// Print leading debug info
	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** load file data **********\n" );
		FS_DPrintf( "  origin: %s\n", calling_function );
		if ( file ) {
			char buffer[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( file, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
			FS_DPrintf( "  type: indexed file\n" );
			FS_DPrintf( "  file: %s\n", buffer );
		} else {
			FS_DPrintf( "  type: direct path\n" );
			FS_DPrintf( "  path: %s\n", path );
		}
	}

	// Check if file is already available from cache
	if ( file ) {
		cache_entry = FS_ReadCache_CacheLookupStaged( file );
		if ( cache_entry ) {
			++cache_entry->lock_count;
			if ( size_out ) {
				*size_out = cache_entry->size - 1;
			}
			if ( fs.cvar.fs_debug_fileio->integer ) {
				FS_DPrintf( "  result: loaded %u bytes from cache\n", cache_entry->size - 1 );
			}
			return CACHE_ENTRY_DATA( cache_entry );
		}
	}

	// Derive os_path in case of path parameter or direct sourcetype file
	if ( path ) {
		os_path = FSC_StringToOSPath( path );
		if ( !os_path ) {
			goto error;
		}
	} else if ( file && file->sourcetype == FSC_SOURCETYPE_DIRECT ) {
		os_path = STACKPTR( ( (fsc_file_direct_t *)file )->os_path_ptr );
	}

	// Obtain handle (if applicable) and size
	if ( os_path ) {
		fsc_file_handle = FSC_FOpenRaw( os_path, "rb" );
		if ( path ) {
			FSC_Free( os_path );
		}
		if ( !fsc_file_handle ) {
			goto error;
		}

		FSC_FSeek( fsc_file_handle, 0, FSC_SEEK_END );
		size = FSC_FTell( fsc_file_handle );

		FSC_FSeek( fsc_file_handle, 0, FSC_SEEK_SET );
	} else {
		size = file->filesize;
	}

	// Set a file size limit of about 2GB as a catch-all to avoid overflow conditions
	// The game shouldn't normally need to read such big files using this function
	if ( size > 2000000000 ) {
		Com_Printf( "WARNING: Excessive file size in FS_ReadData\n" );
		goto error;
	}

	// Obtain buffer from cache or malloc
	if ( size < cache_size / 3 ) {
		// Don't use more than 1/3 of the cache for a single file to avoid flushing smaller files
		cache_entry = FS_ReadCache_Allocate( file, size + 1 );
	}
	if ( cache_entry ) {
		++cache_entry->lock_count;
		data = CACHE_ENTRY_DATA( cache_entry );
	} else {
		data = (char *)FSC_Malloc( size + 1 );
	}

	// Extract data into buffer
	if ( fsc_file_handle ) {
		unsigned int read_size = FSC_FRead( data, size + 1, fsc_file_handle );
		FSC_FClose( fsc_file_handle );
		if ( read_size != size ) {
			goto error;
		}
	} else {
		if ( FSC_ExtractFile( file, data, &fs.index ) ) {
			goto error;
		}
	}
	data[size] = '\0';

	if ( size_out ) {
		*size_out = size;
	}
	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "  result: loaded %u bytes from file\n", size );
	}
	return data;

	// Free buffer if there was an error extracting data
error:
	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "  result: failed to load file\n" );
	}
	if ( cache_entry ) {
		cache_entry->file = NULL;
		cache_entry->lock_count = 0;
	} else if ( data )
		FSC_Free( data );
	if ( size_out ) {
		*size_out = 0;
	}
	return NULL;
}

/*
=================
FS_FreeData
=================
*/
void FS_FreeData( char *data ) {
	FSC_ASSERT( data );
	if ( data >= (char *)base_entry && data < (char *)base_entry + cache_size ) {
		cache_entry_t *cache_entry = (cache_entry_t *)( data - sizeof( cache_entry_t ) );
		if ( cache_entry->lock_count <= 0 ) {
			Com_Error( ERR_DROP, "FS_FreeData on invalid or already freed entry." );
		}
		--cache_entry->lock_count;
	} else {
		FSC_Free( data );
	}
}

/*
=================
FS_ReadShader

Returns shader text allocated in Z_Malloc, or null if there was an error.
=================
*/
char *FS_ReadShader( const fsc_shader_t *shader ) {
	unsigned int size;
	char *source_data;
	char *shader_data;
	FSC_ASSERT( shader );

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** read shader **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", (char *)STACKPTR( shader->shader_name_ptr ) );
	}

	size = shader->end_position - shader->start_position;
	if ( size > 10000 ) {
		if ( fs.cvar.fs_debug_fileio->integer ) {
			FS_DPrintf( "result: failed due to invalid size\n" );
			FS_DebugIndentStop();
		}
		return NULL;
	}

	source_data = FS_ReadData( (const fsc_file_t *)STACKPTR( shader->source_file_ptr ), NULL, NULL, "FS_ReadShader" );
	if ( !source_data ) {
		if ( fs.cvar.fs_debug_fileio->integer ) {
			FS_DPrintf( "result: failed to read source file\n" );
			FS_DebugIndentStop();
		}
		return NULL;
	}

	shader_data = (char *)Z_Malloc( size + 1 );
	FSC_Memcpy( shader_data, source_data + shader->start_position, size );
	shader_data[size] = '\0';

	FS_FreeData( source_data );

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "result: loaded %i shader bytes\n", size );
		FS_DebugIndentStop();
	}

	return shader_data;
}

/*
###############################################################################################

File Handles

###############################################################################################
*/

typedef enum {
	FS_HANDLE_NONE,
	FS_HANDLE_CACHE_READ,
	FS_HANDLE_DIRECT_READ,
	FS_HANDLE_PK3_READ,
	FS_HANDLE_WRITE,
	FS_HANDLE_PIPE
} fs_handle_type_t;

typedef struct {
	fs_handle_type_t type;
	fileHandle_t ref;
	fs_handle_owner_t owner;
	char *debug_path;
	void *state;
} fs_handle_t;

typedef struct {
	fs_handle_type_t type;
	const char *type_string;

	// Returns length successfully read
	unsigned int ( *read )( fs_handle_t *handle, char *buffer, unsigned int length );

	// Returns length successfully written
	unsigned int ( *write )( fs_handle_t *handle, const char *buffer, unsigned int length );

	// Returns 0 on success, -1 on error
	int ( *fseek )( fs_handle_t *handle, int offset, fsOrigin_t mode );

	// Returns seek position
	unsigned int ( *ftell )( fs_handle_t *handle );

	// Frees any allocated handles or resources in handle state
	void ( *free )( fs_handle_t *handle );
} fs_handle_config_t;

// Note fileHandle_t values are incremented by 1 compared to the indices in this array.
#define MAX_HANDLES 64
fs_handle_t fs_handles[MAX_HANDLES];

/*
=================
FS_Handle_Init
=================
*/
static fs_handle_t *FS_Handle_Init( fs_handle_type_t type, fs_handle_owner_t owner, const char *debug_path, int state_size ) {
	int index;
	FSC_ASSERT( type > FS_HANDLE_NONE );

	// Locate free handle
	for ( index = 0; index < MAX_HANDLES; ++index ) {
		if ( fs_handles[index].type == FS_HANDLE_NONE ) {
			break;
		}
	}
	if ( index >= MAX_HANDLES ) {
		Com_Error( ERR_FATAL, "FS_Handle_Init failed to find free handle" );
	}

	// Configure
	Com_Memset( &fs_handles[index], 0, sizeof( fs_handles[index] ) );
	fs_handles[index].type = type;
	fs_handles[index].ref = index + 1;
	fs_handles[index].owner = owner;
	fs_handles[index].debug_path = CopyString( debug_path );
	fs_handles[index].state = S_Malloc( state_size );
	Com_Memset( fs_handles[index].state, 0, state_size );

	return &fs_handles[index];
}

/*
=================
FS_Handle_GetObject

Returns handle object for handle reference, or null if not valid handle.
=================
*/
static fs_handle_t *FS_Handle_GetObject( fileHandle_t handle ) {
	fs_handle_t *fs_handle;
	if ( handle <= 0 || handle > MAX_HANDLES ) {
		return NULL;
	}
	fs_handle = &fs_handles[handle - 1];
	if ( fs_handle->type == FS_HANDLE_NONE ) {
		return NULL;
	}
	FSC_ASSERT( fs_handle->ref == handle );
	return fs_handle;
}

static const fs_handle_config_t *FS_Handle_GetConfig( fs_handle_type_t type );

/*
=================
FS_Handle_Close
=================
*/
void FS_Handle_Close( fileHandle_t handle ) {
	fs_handle_t *fs_handle = FS_Handle_GetObject( handle );
	const fs_handle_config_t *config;
	if ( !fs_handle ) {
		Com_Error( ERR_DROP, "FS_Handle_Close on invalid handle" );
	}
	config = FS_Handle_GetConfig( fs_handle->type );
	FSC_ASSERT( config );
	if ( config->free ) {
		config->free( fs_handle );
	}
	Z_Free( fs_handle->debug_path );
	Z_Free( fs_handle->state );
	fs_handle->type = FS_HANDLE_NONE;
}

/*
=================
FS_Handle_Read
=================
*/
static unsigned int FS_Handle_Read( fileHandle_t handle, char *buffer, unsigned int length ) {
	fs_handle_t *fs_handle = FS_Handle_GetObject( handle );
	const fs_handle_config_t *config;
	if ( !fs_handle ) {
		Com_Error( ERR_DROP, "FS_Handle_Read on invalid handle" );
	}
	config = FS_Handle_GetConfig( fs_handle->type );
	FSC_ASSERT( config );
	if ( !config->read ) {
		Com_Error( ERR_DROP, "FS_Handle_Read on unsupported handle type" );
	}
	return config->read( fs_handle, buffer, length );
}

/*
=================
FS_Handle_Write
=================
*/
static unsigned int FS_Handle_Write( fileHandle_t handle, const char *buffer, unsigned int length ) {
	fs_handle_t *fs_handle = FS_Handle_GetObject( handle );
	const fs_handle_config_t *config;
	if ( !fs_handle ) {
		Com_Error( ERR_DROP, "FS_Handle_Write on invalid handle" );
	}
	config = FS_Handle_GetConfig( fs_handle->type );
	FSC_ASSERT( config );
	if ( !config->write ) {
		Com_Error( ERR_DROP, "FS_Handle_Write on unsupported handle type" );
	}
	return config->write( fs_handle, buffer, length );
}

/*
=================
FS_Handle_FSeek
=================
*/
static int FS_Handle_FSeek( fileHandle_t handle, int offset, fsOrigin_t origin_mode ) {
	fs_handle_t *fs_handle = FS_Handle_GetObject( handle );
	const fs_handle_config_t *config;
	if ( !fs_handle ) {
		Com_Error( ERR_DROP, "FS_Handle_FSeek on invalid handle" );
	}
	config = FS_Handle_GetConfig( fs_handle->type );
	FSC_ASSERT( config );
	if ( !config->fseek ) {
		Com_Error( ERR_DROP, "FS_Handle_FSeek on unsupported handle type" );
	}
	return config->fseek( fs_handle, offset, origin_mode );
}

/*
=================
FS_Handle_FTell
=================
*/
static unsigned int FS_Handle_FTell( fileHandle_t handle ) {
	fs_handle_t *fs_handle = FS_Handle_GetObject( handle );
	const fs_handle_config_t *config;
	if ( !fs_handle ) {
		Com_Error( ERR_DROP, "FS_Handle_FTell on invalid handle" );
	}
	config = FS_Handle_GetConfig( fs_handle->type );
	FSC_ASSERT( config );
	if ( !config->ftell ) {
		Com_Error( ERR_DROP, "FS_Handle_FTell on unsupported handle type" );
	}
	return config->ftell( fs_handle );
}

/*
=================
FS_Handle_SetOwner
=================
*/
static void FS_Handle_SetOwner( fileHandle_t handle, fs_handle_owner_t owner ) {
	fs_handle_t *handle_entry = FS_Handle_GetObject( handle );
	if ( !handle_entry ) {
		Com_Error( ERR_DROP, "FS_Handle_SetOwner on invalid handle" );
	}
	handle_entry->owner = owner;
}

/*
=================
FS_Handle_GetOwner
=================
*/
fs_handle_owner_t FS_Handle_GetOwner( fileHandle_t handle ) {
	fs_handle_t *handle_entry = FS_Handle_GetObject( handle );
	if ( !handle_entry ) {
		return FS_HANDLEOWNER_SYSTEM;
	}
	return handle_entry->owner;
}

/*
=================
FS_Handle_TypeString
=================
*/
static const char *FS_Handle_TypeString( fs_handle_type_t type ) {
	const fs_handle_config_t *config = FS_Handle_GetConfig( type );
	if ( !config ) {
		return "unknown";
	}
	return config->type_string;
}

/*
=================
FS_Handle_OwnerString
=================
*/
static const char *FS_Handle_OwnerString( fs_handle_owner_t owner ) {
	if ( owner == FS_HANDLEOWNER_SYSTEM )
		return "system";
	if ( owner == FS_HANDLEOWNER_CGAME )
		return "cgame";
	if ( owner == FS_HANDLEOWNER_UI )
		return "ui";
	if ( owner == FS_HANDLEOWNER_QAGAME )
		return "qagame";
	return "unknown";
}

/*
=================
FS_Handle_PrintList
=================
*/
void FS_Handle_PrintList( void ) {
	int i;
	for ( i = 0; i < MAX_HANDLES; ++i ) {
		if ( fs_handles[i].type == FS_HANDLE_NONE ) {
			continue;
		}
		Com_Printf( "********** handle %i **********\n  type: %s\n  owner: %s\n  path: %s\n",
				i + 1, FS_Handle_TypeString( fs_handles[i].type ), FS_Handle_OwnerString( fs_handles[i].owner ),
				fs_handles[i].debug_path );
	}
}

/*
=================
FS_Handle_CloseAllOwner

Closes all handles with the specified owner. Can be called when a VM is shutting down to avoid leaked handles.
=================
*/
void FS_Handle_CloseAllOwner( fs_handle_owner_t owner ) {
	int i;
	for ( i = 0; i < MAX_HANDLES; ++i ) {
		if ( fs_handles[i].type != FS_HANDLE_NONE && fs_handles[i].owner == owner ) {
			Com_Printf( "^1*****************\nWARNING: Auto-closing possible leaked handle\n"
					"type: %s\nowner: %s\npath: %s\n*****************\n",
					FS_Handle_TypeString( fs_handles[i].type ),
					FS_Handle_OwnerString( fs_handles[i].owner ), fs_handles[i].debug_path );
			FS_Handle_Close( i + 1 );
		}
	}
}

/*
=================
FS_Handle_CloseAll

Closes all handles. Can be called when the whole program is terminating just to be safe.
=================
*/
void FS_Handle_CloseAll( void ) {
	int i;
	for ( i = 0; i < MAX_HANDLES; ++i ) {
		if ( fs_handles[i].type != FS_HANDLE_NONE ) {
			FS_Handle_Close( i + 1 );
		}
	}
}

// ##################################
// ####### Cache Read Handles #######
// ##################################

typedef struct {
	char *data;
	unsigned int position;
	unsigned int size;
} fs_cache_read_handle_state_t;

/*
=================
FS_CacheReadHandle_Open

Only file or path should be set, not both. Does not include sanity check on path.
Returns handle on success, null on error.
=================
*/
static fileHandle_t FS_CacheReadHandle_Open( const fsc_file_t *file, const char *path, unsigned int *size_out ) {
	char buffer[FS_FILE_BUFFER_SIZE];
	const char *debug_path;
	char *data;
	fs_handle_t *handle;
	unsigned int size;
	fs_cache_read_handle_state_t *state;

	// Get debug path
	if ( file ) {
		FS_FileToBuffer( file, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
		debug_path = buffer;
	} else {
		debug_path = path;
	}

	// Set up handle entry
	data = FS_ReadData( file, path, &size, "FS_CacheReadHandle_Open" );
	if ( !data ) {
		if ( size_out ) {
			*size_out = 0;
		}
		return 0;
	}

	handle = FS_Handle_Init( FS_HANDLE_CACHE_READ, FS_HANDLEOWNER_SYSTEM, debug_path, sizeof( fs_cache_read_handle_state_t ) );
	state = (fs_cache_read_handle_state_t *)handle->state;
	state->data = data;
	state->size = size;

	if ( size_out ) {
		*size_out = size;
	}
	return handle->ref;
}

/*
=================
FS_CacheReadHandle_Read
=================
*/
static unsigned int FS_CacheReadHandle_Read( fs_handle_t *handle, char *buffer, unsigned int length ) {
	fs_cache_read_handle_state_t *state = (fs_cache_read_handle_state_t *)handle->state;

	// Don't read past end of file...
	if ( length > state->size - state->position ) {
		length = state->size - state->position;
	}

	// Read data to buffer and advance position
	FSC_Memcpy( buffer, state->data + state->position, length );
	state->position += length;
	return length;
}

/*
=================
FS_CacheReadHandle_Seek
=================
*/
static int FS_CacheReadHandle_Seek( fs_handle_t *handle, int offset, fsOrigin_t mode ) {
	fs_cache_read_handle_state_t *state = (fs_cache_read_handle_state_t *)handle->state;
	unsigned int origin = 0;
	unsigned int offset_origin;

	// Get origin
	switch ( mode ) {
		case FS_SEEK_CUR:
			origin = state->position;
			break;
		case FS_SEEK_END:
			origin = state->size;
			break;
		case FS_SEEK_SET:
			origin = 0;
			break;
		default:
			Com_Error( ERR_DROP, "FS_CacheReadHandle_Seek with invalid origin mode" );
	}

	// Get offset_origin and correct overflow conditions
	offset_origin = origin + offset;
	if ( offset < 0 && offset_origin > origin ) {
		offset_origin = 0;
	}
	if ( ( offset > 0 && offset_origin < origin ) || offset_origin > state->size ) {
		offset_origin = state->size;
	}

	// Write the new position
	state->position = offset_origin;

	if ( offset_origin == origin + offset ) {
		return 0;
	}
	return -1;
}

/*
=================
FS_CacheReadHandle_FTell
=================
*/
static unsigned int FS_CacheReadHandle_FTell( fs_handle_t *handle ) {
	fs_cache_read_handle_state_t *state = (fs_cache_read_handle_state_t *)handle->state;
	return state->position;
}

/*
=================
FS_CacheReadHandle_Free
=================
*/
static void FS_CacheReadHandle_Free( fs_handle_t *handle ) {
	fs_cache_read_handle_state_t *state = (fs_cache_read_handle_state_t *)handle->state;
	FS_FreeData( state->data );
}

static const fs_handle_config_t cache_read_handle_config = {
	FS_HANDLE_CACHE_READ,
	"cache read",
	FS_CacheReadHandle_Read,
	NULL,
	FS_CacheReadHandle_Seek,
	FS_CacheReadHandle_FTell,
	FS_CacheReadHandle_Free
};

// ###################################
// ####### Direct Read Handles #######
// ###################################

typedef struct {
	fsc_filehandle_t *fsc_handle;
} fs_direct_read_handle_state_t;

/*
=================
FS_DirectReadHandle_Open

Only file or path should be set, not both. Does not include sanity check on path.
Returns handle on success, null on error.
=================
*/
fileHandle_t FS_DirectReadHandle_Open( const fsc_file_t *file, const char *path, unsigned int *size_out ) {
	char debug_path[FS_MAX_PATH];
	fsc_ospath_t *os_path = NULL;
	fsc_filehandle_t *fsc_handle;
	fs_handle_t *handle;
	fs_direct_read_handle_state_t *state;

	if ( file ) {
		if ( file->sourcetype != FSC_SOURCETYPE_DIRECT ) {
			Com_Error( ERR_FATAL, "FS_DirectReadHandle_Open on non direct file" );
		}
		os_path = STACKPTR( ( (fsc_file_direct_t *)file )->os_path_ptr );
		FS_FileToBuffer( (fsc_file_t *)file, debug_path, sizeof( debug_path ), qtrue, qtrue, qtrue, qfalse );
	} else if ( path ) {
		os_path = FSC_StringToOSPath( path );
		Q_strncpyz( debug_path, path, sizeof( debug_path ) );
	} else {
		Com_Error( ERR_FATAL, "Invalid parameters to FS_DirectReadHandle_Open." );
	}

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** opening direct read handle **********\n" );
		FS_DPrintf( "  path: %s\n", debug_path );
	}

	fsc_handle = FSC_FOpenRaw( os_path, "rb" );
	if ( !file ) {
		FSC_Free( os_path );
	}
	if ( !fsc_handle ) {
		if ( fs.cvar.fs_debug_fileio->integer ) {
			FS_DPrintf( "  result: failed to open file\n" );
		}
		if ( size_out ) {
			*size_out = 0;
		}
		return 0;
	}

	// Set up handle entry
	handle = FS_Handle_Init( FS_HANDLE_DIRECT_READ, FS_HANDLEOWNER_SYSTEM, debug_path, sizeof( fs_direct_read_handle_state_t ) );
	state = (fs_direct_read_handle_state_t *)handle->state;
	state->fsc_handle = fsc_handle;

	// Get size
	if ( size_out ) {
		FSC_FSeek( fsc_handle, 0, FSC_SEEK_END );
		*size_out = FSC_FTell( fsc_handle );
		FSC_FSeek( fsc_handle, 0, FSC_SEEK_SET );
	}

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "  result: success\n" );
	}
	return handle->ref;
}

/*
=================
FS_DirectReadHandle_Read
=================
*/
static unsigned int FS_DirectReadHandle_Read( fs_handle_t *handle, char *buffer, unsigned int length ) {
	fs_direct_read_handle_state_t *state = (fs_direct_read_handle_state_t *)handle->state;
	return FSC_FRead( buffer, length, state->fsc_handle );
}

/*
=================
FS_DirectReadHandle_FSeek
=================
*/
static int FS_DirectReadHandle_FSeek( fs_handle_t *handle, int offset, fsOrigin_t origin_mode ) {
	fs_direct_read_handle_state_t *state = (fs_direct_read_handle_state_t *)handle->state;

	// Get type
	fsc_seek_type_t type = FSC_SEEK_SET;
	switch ( origin_mode ) {
		case FS_SEEK_CUR:
			type = FSC_SEEK_CUR;
			break;
		case FS_SEEK_END:
			type = FSC_SEEK_END;
			break;
		case FS_SEEK_SET:
			type = FSC_SEEK_SET;
			break;
		default:
			Com_Error( ERR_DROP, "FS_DirectReadHandle_FSeek with invalid origin mode" );
	}

	return FSC_FSeek( state->fsc_handle, offset, type );
}

/*
=================
FS_DirectReadHandle_FTell
=================
*/
static unsigned int FS_DirectReadHandle_FTell( fs_handle_t *handle ) {
	fs_direct_read_handle_state_t *state = (fs_direct_read_handle_state_t *)handle->state;
	return FSC_FTell( state->fsc_handle );
}

/*
=================
FS_DirectReadHandle_Free
=================
*/
static void FS_DirectReadHandle_Free( fs_handle_t *handle ) {
	fs_direct_read_handle_state_t *state = (fs_direct_read_handle_state_t *)handle->state;
	FSC_FClose( state->fsc_handle );
}

static const fs_handle_config_t direct_read_handle_config = {
	FS_HANDLE_DIRECT_READ,
	"direct read",
	FS_DirectReadHandle_Read,
	NULL,
	FS_DirectReadHandle_FSeek,
	FS_DirectReadHandle_FTell,
	FS_DirectReadHandle_Free
};

// ################################
// ####### Pk3 Read Handles #######
// ################################

typedef struct {
	const fsc_file_frompk3_t *file;
	fsc_pk3handle_t *fsc_handle;
	unsigned int position;
} fs_pk3_read_handle_state_t;

/*
=================
FS_Pk3ReadHandle_Open

Returns handle on success, null on error.
=================
*/
static fileHandle_t FS_Pk3ReadHandle_Open( const fsc_file_t *file ) {
	char debug_path[FS_MAX_PATH];
	fsc_pk3handle_t *fsc_handle;
	fs_handle_t *handle;
	fs_pk3_read_handle_state_t *state;

	if ( file->sourcetype != FSC_SOURCETYPE_PK3 ) {
		Com_Error( ERR_FATAL, "FS_Pk3ReadHandle_Open on non pk3 file" );
	}
	FS_FileToBuffer( (fsc_file_t *)file, debug_path, sizeof( debug_path ), qtrue, qtrue, qtrue, qfalse );

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** opening pk3 read handle **********\n" );
		FS_DPrintf( "  file: %s\n", debug_path );
	}

	fsc_handle = FSC_Pk3HandleOpen( (fsc_file_frompk3_t *)file, 16384, &fs.index );
	if ( !fsc_handle ) {
		if ( fs.cvar.fs_debug_fileio->integer ) {
			FS_DPrintf( "  result: failed to open file\n" );
		}
		return 0;
	}

	// Set up handle entry
	handle = FS_Handle_Init( FS_HANDLE_PK3_READ, FS_HANDLEOWNER_SYSTEM, debug_path, sizeof( fs_pk3_read_handle_state_t ) );
	state = (fs_pk3_read_handle_state_t *)handle->state;
	state->file = (fsc_file_frompk3_t *)file;
	state->fsc_handle = fsc_handle;
	state->position = 0;

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "  result: success\n" );
	}
	return handle->ref;
}

/*
=================
FS_Pk3ReadHandle_Read
=================
*/
static unsigned int FS_Pk3ReadHandle_Read( fs_handle_t *handle, char *buffer, unsigned int length ) {
	fs_pk3_read_handle_state_t *state = (fs_pk3_read_handle_state_t *)handle->state;
	unsigned int max_length = state->file->f.filesize - state->position;
	if ( length > max_length ) {
		length = max_length;
	}
	length = FSC_Pk3HandleRead( state->fsc_handle, buffer, length );
	state->position += length;
	return length;
}

/*
=================
FS_Pk3ReadHandle_Seek

Uses very inefficient method similar to the original filesystem.
This function is very rarely used but is supported for mod compatibility.
=================
*/
static int FS_Pk3ReadHandle_Seek( fs_handle_t *handle, int offset, fsOrigin_t origin_mode ) {
	fs_pk3_read_handle_state_t *state = (fs_pk3_read_handle_state_t *)handle->state;
	unsigned int origin = 0;
	unsigned int offset_origin;

	// Get origin
	switch ( origin_mode ) {
		case FS_SEEK_CUR:
			origin = state->position;
			break;
		case FS_SEEK_END:
			origin = state->file->f.filesize;
			break;
		case FS_SEEK_SET:
			origin = 0;
			break;
		default:
			Com_Error( ERR_DROP, "FS_Pk3ReadHandle_Seek with invalid origin mode" );
	}

	// Get offset_origin and correct overflow conditions
	offset_origin = origin + offset;
	if ( offset < 0 && offset_origin > origin ) {
		offset_origin = 0;
	}
	if ( ( offset > 0 && offset_origin < origin ) || offset_origin > state->file->f.filesize ) {
		offset_origin = state->file->f.filesize;
	}

	// If seeking to end, just set the position
	if ( offset_origin >= state->file->f.filesize ) {
		state->position = state->file->f.filesize;
		return 0;
	}

	// If seeking backwards, reset the handle
	if ( offset_origin < state->position ) {
		FSC_Pk3HandleClose( state->fsc_handle );
		state->fsc_handle = FSC_Pk3HandleOpen( state->file, 16384, &fs.index );
		if ( !state->fsc_handle ) {
			Com_Error( ERR_FATAL, "FS_Pk3ReadHandle_Seek failed to reopen handle" );
		}
		state->position = 0;
	}

	// Seek forwards by reading data to a temp buffer
	while ( state->position < offset_origin ) {
		char buffer[65536];
		unsigned int read_amount;
		unsigned int read_target = offset_origin - state->position;
		if ( read_target > sizeof( buffer ) ) {
			read_target = sizeof( buffer );
		}
		read_amount = FSC_Pk3HandleRead( state->fsc_handle, buffer, read_target );
		state->position += read_amount;
		if ( read_amount != read_target ) {
			return -1;
		}
	}

	if ( offset_origin == origin + offset ) {
		return 0;
	}
	return -1;
}

/*
=================
FS_Pk3ReadHandle_FTell
=================
*/
static unsigned int FS_Pk3ReadHandle_FTell( fs_handle_t *handle ) {
	fs_pk3_read_handle_state_t *state = (fs_pk3_read_handle_state_t *)handle->state;
	return state->position;
}

/*
=================
FS_Pk3ReadHandle_Free
=================
*/
static void FS_Pk3ReadHandle_Free( fs_handle_t *handle ) {
	fs_pk3_read_handle_state_t *state = (fs_pk3_read_handle_state_t *)handle->state;
	FSC_Pk3HandleClose( state->fsc_handle );
}

static const fs_handle_config_t pk3_read_handle_config = {
	FS_HANDLE_PK3_READ,
	"pk3 read",
	FS_Pk3ReadHandle_Read,
	NULL,
	FS_Pk3ReadHandle_Seek,
	FS_Pk3ReadHandle_FTell,
	FS_Pk3ReadHandle_Free
};

// #############################
// ####### Write Handles #######
// #############################

typedef struct {
	fsc_filehandle_t *fsc_handle;
	qboolean sync;
} fs_write_handle_state_t;

/*
=================
FS_WriteHandle_Open

Does not include directory creation or sanity checks.
Returns handle on success, null on error.
=================
*/
static fileHandle_t FS_WriteHandle_Open( const char *path, qboolean append, qboolean sync ) {
	fsc_filehandle_t *fsc_handle;
	fs_handle_t *handle;
	fs_write_handle_state_t *state;

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** opening write handle **********\n" );
		FS_DPrintf( "  path: %s\n", path );
	}

	// Attempt to open the file
	if ( append ) {
		fsc_handle = FSC_FOpen( path, "ab" );
	} else {
		fsc_handle = FSC_FOpen( path, "wb" );
	}
	if ( !fsc_handle ) {
		if ( fs.cvar.fs_debug_fileio->integer ) {
			FS_DPrintf( "  result: failed to open file\n" );
		}
		return 0;
	}

	// Set up handle entry
	handle = FS_Handle_Init( FS_HANDLE_WRITE, FS_HANDLEOWNER_SYSTEM, path, sizeof( fs_write_handle_state_t ) );
	state = (fs_write_handle_state_t *)handle->state;
	state->fsc_handle = fsc_handle;
	state->sync = sync;
	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "  result: success\n" );
	}
	return handle->ref;
}

/*
=================
FS_WriteHandle_Write
=================
*/
static unsigned int FS_WriteHandle_Write( fs_handle_t *handle, const char *data, unsigned int length ) {
	fs_write_handle_state_t *state = (fs_write_handle_state_t *)handle->state;
	unsigned int result = FSC_FWrite( data, length, state->fsc_handle );
	if ( state->sync ) {
		FSC_FFlush( state->fsc_handle );
	}
	return result;
}

/*
=================
FS_WriteHandle_FSeek
=================
*/
static int FS_WriteHandle_FSeek( fs_handle_t *handle, int offset, fsOrigin_t origin_mode ) {
	fs_write_handle_state_t *state = (fs_write_handle_state_t *)handle->state;

	// Get type
	fsc_seek_type_t type = FSC_SEEK_SET;
	switch ( origin_mode ) {
		case FS_SEEK_CUR:
			type = FSC_SEEK_CUR;
			break;
		case FS_SEEK_END:
			type = FSC_SEEK_END;
			break;
		case FS_SEEK_SET:
			type = FSC_SEEK_SET;
			break;
		default:
			Com_Error( ERR_DROP, "FS_WriteHandle_FSeek with invalid origin mode" );
	}

	return FSC_FSeek( state->fsc_handle, offset, type );
}

/*
=================
FS_WriteHandle_FTell
=================
*/
static unsigned int FS_WriteHandle_FTell( fs_handle_t *handle ) {
	fs_write_handle_state_t *state = (fs_write_handle_state_t *)handle->state;
	return FSC_FTell( state->fsc_handle );
}

/*
=================
FS_WriteHandle_Free
=================
*/
static void FS_WriteHandle_Free( fs_handle_t *handle ) {
	fs_write_handle_state_t *state = (fs_write_handle_state_t *)handle->state;
	FSC_FClose( state->fsc_handle );
}

static const fs_handle_config_t write_handle_config = {
	FS_HANDLE_WRITE,
	"write",
	NULL,
	FS_WriteHandle_Write,
	FS_WriteHandle_FSeek,
	FS_WriteHandle_FTell,
	FS_WriteHandle_Free
};

/*
=================
FS_WriteHandle_Flush
=================
*/
static void FS_WriteHandle_Flush( fileHandle_t handle, qboolean enable_sync ) {
	fs_handle_t *fs_handle = FS_Handle_GetObject( handle );
	if ( !fs_handle || fs_handle->type != FS_HANDLE_WRITE ) {
		Com_Error( ERR_DROP, "FS_WriteHandle_Flush on invalid handle" );
	} else {
		fs_write_handle_state_t *state = (fs_write_handle_state_t *)fs_handle->state;
		if ( enable_sync ) {
			state->sync = qtrue;
		}
		FSC_FFlush( state->fsc_handle );
	}
}

// ############################
// ####### Pipe Handles #######
// ############################

typedef struct {
	FILE *handle;
} fs_pipe_handle_state_t;

/*
=================
FS_FCreateOpenPipeFile
=================
*/
fileHandle_t FS_FCreateOpenPipeFile( const char *filename ) {
	char path[FS_MAX_PATH];
	FILE *fifo = NULL;
	fs_handle_t *handle;
	fs_pipe_handle_state_t *state;

	if ( FS_GeneratePathWritedir( FS_GetCurrentGameDir(), filename,
			0, FS_ALLOW_DIRECTORIES | FS_CREATE_DIRECTORIES_FOR_FILE, path, sizeof( path ) ) ) {
		fifo = Sys_Mkfifo( path );
	}

	if ( !fifo ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Could not create new com_pipefile at %s. "
								   "com_pipefile will not be used.\n",
					path );
		return 0;
	}

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** opening pipe handle **********\n" );
		FS_DPrintf( "  path: %s\n", path );
	}

	// Set up handle entry
	handle = FS_Handle_Init( FS_HANDLE_PIPE, FS_HANDLEOWNER_SYSTEM, filename, sizeof( fs_pipe_handle_state_t ) );
	state = (fs_pipe_handle_state_t *)handle->state;
	state->handle = fifo;
	return handle->ref;
}

/*
=================
FS_PipeHandle_Read
=================
*/
static unsigned int FS_PipeHandle_Read( fs_handle_t *handle, char *buffer, unsigned int len ) {
	fs_pipe_handle_state_t *state = (fs_pipe_handle_state_t *)handle->state;
	return fread( buffer, 1, len, state->handle );
}

/*
=================
FS_PipeHandle_Free
=================
*/
static void FS_PipeHandle_Free( fs_handle_t *handle ) {
	fs_pipe_handle_state_t *state = (fs_pipe_handle_state_t *)handle->state;
	fclose( state->handle );
}

static const fs_handle_config_t pipe_handle_config = {
	FS_HANDLE_PIPE,
	"pipe",
	FS_PipeHandle_Read,
	NULL,
	NULL,
	NULL,
	FS_PipeHandle_Free
};

// ################################
// ####### Handle Type List #######
// ################################

static const fs_handle_config_t *handle_configs[] = {
	&cache_read_handle_config,
	&direct_read_handle_config,
	&pk3_read_handle_config,
	&write_handle_config,
	&pipe_handle_config
};

/*
=================
FS_Handle_GetConfig

Returns null if type is invalid.
=================
*/
static const fs_handle_config_t *FS_Handle_GetConfig( fs_handle_type_t type ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( handle_configs ); ++i ) {
		if ( handle_configs[i]->type == type ) {
			return handle_configs[i];
		}
	}
	return NULL;
}

/*
###############################################################################################

Journal files

###############################################################################################
*/

/*
=================
FS_Journal_WriteData

Length 0 parameter indicates file not found.
=================
*/
void FS_Journal_WriteData( const char *data, unsigned int length ) {
	if ( !com_journalDataFile || com_journal->integer != 1 ) {
		return;
	}
	FS_Write( &length, sizeof( length ), com_journalDataFile );
	if ( length ) {
		FS_Write( data, length, com_journalDataFile );
	}
	FS_Flush( com_journalDataFile );
}

/*
=================
FS_Journal_ReadData

Returns next piece of data from journal data file, or null if not available.
If data was returned, result needs to be freed by FS_FreeData.
=================
*/
char *FS_Journal_ReadData( void ) {
	unsigned int length;
	int r;
	char *data;

	if ( !com_journalDataFile || com_journal->integer != 2 ) {
		return NULL;
	}

	r = FS_Read( &length, sizeof( length ), com_journalDataFile );
	if ( r != sizeof( length ) ) {
		return NULL;
	}
	if ( !length ) {
		return NULL;
	}

	// Obtain buffer from cache or malloc
	{
		cache_entry_t *cache_entry = FS_ReadCache_Allocate( NULL, length + 1 );
		if ( cache_entry ) {
			++cache_entry->lock_count;
			data = CACHE_ENTRY_DATA( cache_entry );
		} else {
			data = (char *)FSC_Malloc( length + 1 );
		}
	}

	// Attempt to read data
	r = FS_Read( data, length, com_journalDataFile );
	if ( (unsigned int)r != length ) {
		Com_Error( ERR_FATAL, "Failed to read data from journal data file" );
	}
	data[length] = '\0';
	return data;
}

/*
###############################################################################################

Config files

###############################################################################################
*/

/*
=================
FS_OpenSettingsFileWrite

This is used for writing the primary auto-saved settings file, e.g. q3config.cfg.
The save directory will be adjusted depending on the fs_mod_settings value.
=================
*/
fileHandle_t FS_OpenSettingsFileWrite( const char *filename ) {
	char path[FS_MAX_PATH];
	const char *mod_dir;

	if ( fs.cvar.fs_mod_settings->integer ) {
		mod_dir = FS_GetCurrentGameDir();
	} else {
		mod_dir = com_basegame->string;
	}

	if ( !FS_GeneratePathWritedir( mod_dir, filename, FS_CREATE_DIRECTORIES, FS_ALLOW_SPECIAL_CFG,
								   path, sizeof( path ) ) ) {
		return 0;
	}
	return FS_WriteHandle_Open( path, qfalse, qfalse );
}

/*
###############################################################################################

"Read-back" tracking

In rare cases, mods may attempt to read files that were just created by the mod/engine.
This may fail if the file index is not refreshed after the file is created.

To handle this situation, this module stores a log of files written by the game since
the last filesystem refresh. If a mod tries to open a file with the same path, a
filesystem refresh will be performed ahead of the read operation.

This isn't the most elegant solution but it does handle this rare special case while
avoiding worse workarounds down the line.

###############################################################################################
*/

#define MAX_READBACK_TRACKER_ENTRIES 32
static char *fs_readback_tracker_entries[MAX_READBACK_TRACKER_ENTRIES];
static int fs_readback_tracker_entry_count = 0;

/*
=================
FS_ReadbackTracker_ProcessPath

Returns qtrue if path exists in tracker, qfalse otherwise.
Set "insert" to qtrue to add path to tracker.
=================
*/
static qboolean FS_ReadbackTracker_ProcessPath( const char *path, qboolean insert ) {
	int i;
	for ( i = 0; i < fs_readback_tracker_entry_count; ++i ) {
		if ( !Q_stricmp( path, fs_readback_tracker_entries[i] ) ) {
			return qtrue;
		}
	}
	if ( insert && fs_readback_tracker_entry_count < MAX_READBACK_TRACKER_ENTRIES ) {
		fs_readback_tracker_entries[fs_readback_tracker_entry_count++] = CopyString( path );
	}
	return qfalse;
}

/*
=================
FS_ReadbackTracker_Reset

Resets all tracked files. Should be called after filesystem refresh.
=================
*/
void FS_ReadbackTracker_Reset( void ) {
	int i;
	for ( i = 0; i < fs_readback_tracker_entry_count; ++i ) {
		Z_Free( fs_readback_tracker_entries[i] );
	}
	fs_readback_tracker_entry_count = 0;
}

/*
###############################################################################################

FS_FOpenFile functions

The FS_FOpenFileByMode family of functions are accessed by both the engine and VM calls,
and have some peculiar syntax and return values inherited from the original filesystem
that need to be maintained for compatibility purposes.

FS_FOpenFileByMode with write-type mode (FS_WRITE, FS_APPEND, FS_APPEND_SYNC):
  On success, writes handle and returns 0
  On error, writes null handle and returns -1

FS_FOpenFileByMode with FS_READ and handle pointer set:
  On success, writes handle and returns file size value >= 0
  On error, writes null handle and returns -1

FS_FOpenFileByMode with FS_READ and null handle pointer (size check mode):
  If file invalid or doesn't exist, returns 0
  If file exists with size 0, returns 1
  If file exists with size > 0, returns size

###############################################################################################
*/

/*
=================
FS_FOpenFile_ReadHandleOpen

Can be called with null handle_out for a size/existance check.
Returns size according to original FS_FOpenFileReadDir conventions.
=================
*/
static int FS_FOpenFile_ReadHandleOpen( const char *filename, fileHandle_t *handle_out, int lookup_flags, qboolean allow_direct_handle ) {
	const fsc_file_t *fscfile;
	int size = -1;
	fileHandle_t handle = 0;

	// Get the file
	fscfile = FS_GeneralLookup( filename, lookup_flags, qfalse );
	if ( !fscfile ) {
		goto finish;
	}

	// For most size-check cases we can just return the fsc filesize without trying to open the file
	if ( !handle_out && !( allow_direct_handle && fscfile->sourcetype == FSC_SOURCETYPE_DIRECT ) ) {
		size = (int)( fscfile->filesize );
		goto finish;
	}

	// Get the handle and size
	if ( allow_direct_handle && fscfile->sourcetype == FSC_SOURCETYPE_DIRECT ) {
		handle = FS_DirectReadHandle_Open( fscfile, NULL, (unsigned int *)&size );
		if ( !handle ) {
			size = -1;
		}
	} else if ( allow_direct_handle && fscfile->sourcetype == FSC_SOURCETYPE_PK3 && fscfile->filesize > 65536 ) {
		handle = FS_Pk3ReadHandle_Open( fscfile );
		if ( handle ) {
			size = (int)( fscfile->filesize );
		}
	} else {
		handle = FS_CacheReadHandle_Open( fscfile, NULL, (unsigned int *)&size );
		if ( !handle ) {
			size = -1;
		}
	}

finish:
	if ( handle && size < 0 ) {
		// This should be very unlikely, but if for some reason we got a handle with an invalid size,
		// don't return it because it could cause bugs down the line
		FS_Handle_Close( handle );
		handle = 0;
		size = -1;
	}

	if ( handle_out ) {
		// Caller wants to keep handle
		*handle_out = handle;
	} else {
		// Size check only - modify size as per original FS_FOpenFileReadDir
		if ( size < 0 ) {
			size = 0;
		} else if ( size == 0 ) {
			size = 1;
		}
		if ( handle ) {
			FS_Handle_Close( handle );
		}
	}

	return size;
}

/*
=================
FS_FOpenFile_WriteHandleOpen

Includes directory creation and sanity checks. Returns handle on success, null on error.
=================
*/
static fileHandle_t FS_FOpenFile_WriteHandleOpen( const char *mod_dir, const char *path, qboolean append, qboolean sync, int flags ) {
	char full_path[FS_MAX_PATH];

	if ( !FS_GeneratePathWritedir( mod_dir, path, FS_CREATE_DIRECTORIES,
			FS_ALLOW_DIRECTORIES | FS_CREATE_DIRECTORIES_FOR_FILE | flags, full_path, sizeof( full_path ) ) ) {
		if ( fs.cvar.fs_debug_fileio->integer ) {
			FS_DPrintf( "WARNING: Failed to generate write path for %s/%s\n", mod_dir, path );
		}
		return 0;
	}

	if ( mod_dir ) {
		FS_ReadbackTracker_ProcessPath( path, qtrue );
	}

	return FS_WriteHandle_Open( full_path, append, sync );
}

/*
=================
FS_WriteModDir

Returns default mod directory to use for write operations.
=================
*/
static const char *FS_WriteModDir( void ) {
#ifdef FS_SERVERCFG_ENABLED
	if ( *fs.cvar.fs_servercfg_writedir->string ) {
		return fs.cvar.fs_servercfg_writedir->string;
	}
#endif
	return FS_GetCurrentGameDir();
}

/*
=================
FS_FOpenFileByModeGeneral

Can be called with a null filehandle pointer in read mode for a size/existance check.
=================
*/
static int FS_FOpenFileByModeGeneral( const char *qpath, fileHandle_t *f, fsMode_t mode, fs_handle_owner_t owner ) {
	int size = 0;
	fileHandle_t handle = 0;

	if ( !qpath ) {
		Com_Error( ERR_DROP, "FS_FOpenFileByMode: null path" );
	}
	if ( !f && mode != FS_READ ) {
		Com_Error( ERR_DROP, "FS_FOpenFileByMode: null handle pointer with non-read mode" );
	}

	if ( mode == FS_READ ) {
		if ( owner != FS_HANDLEOWNER_SYSTEM ) {
			int lookup_flags = 0;

			if ( FS_ReadbackTracker_ProcessPath( qpath, qfalse ) ) {
				// If file was potentially just written, run filesystem refresh to make sure it is registered
				if ( fs.cvar.fs_debug_fileio->integer ) {
					FS_DPrintf( "Running filesystem refresh due to recently written file %s\n", qpath );
				}
				FS_Refresh( qtrue );
			}

			if ( owner == FS_HANDLEOWNER_QAGAME ) {
				// Ignore pure list for server VM. This prevents the server mod from being affected
				// by the pure list when running a local game with sv_pure enabled.
				lookup_flags |= LOOKUPFLAG_IGNORE_PURE_LIST;
			} else {
				// For other VMs, allow opening files on disk when pure. This is a bit more permissive than
				// the original filesystem, which only allowed certain extensions, but this allows more
				// flexibility for mods and shouldn't cause any problems.
				lookup_flags |= LOOKUPFLAG_PURE_ALLOW_DIRECT_SOURCE;
			}

			// Use read with direct handle support option, to ensure recently/actively written files
			// on disk are opened properly, and to optimize for pk3 read operations that read only the
			// beginning of the file (e.g. UI Enhanced mod doing bulk bsp reads on startup)
			if ( !handle ) {
				size = FS_FOpenFile_ReadHandleOpen( qpath, f ? &handle : NULL, lookup_flags, qtrue );
			}
		}

		// Engine reads don't do anything fancy so just use the basic method
		else {
			size = FS_FOpenFile_ReadHandleOpen( qpath, f ? &handle : NULL, 0, qfalse );
		}
	} else if ( mode == FS_WRITE ) {
		handle = FS_FOpenFile_WriteHandleOpen( FS_WriteModDir(), qpath, qfalse, qfalse, 0 );
	} else if ( mode == FS_APPEND_SYNC ) {
		handle = FS_FOpenFile_WriteHandleOpen( FS_WriteModDir(), qpath, qtrue, qtrue, 0 );
	} else if ( mode == FS_APPEND ) {
		handle = FS_FOpenFile_WriteHandleOpen( FS_WriteModDir(), qpath, qtrue, qfalse, 0 );
	} else {
		Com_Error( ERR_DROP, "FS_FOpenFileByMode: bad mode" );
	}

	if ( f ) {
		// Caller wants to keep the handle
		*f = handle;
		if ( handle ) {
			FS_Handle_SetOwner( handle, owner );
		} else {
			size = -1;
		}
	}

	return size;
}

/*
=================
FS_ModeString
=================
*/
static const char *FS_ModeString( fsMode_t mode ) {
	if ( mode == FS_READ )
		return "read";
	if ( mode == FS_WRITE )
		return "write";
	if ( mode == FS_APPEND )
		return "append";
	if ( mode == FS_APPEND_SYNC )
		return "append-sync";
	return "unknown";
}

/*
=================
FS_FOpenFileByModeLogged
=================
*/
static int FS_FOpenFileByModeLogged( const char *qpath, fileHandle_t *f, fsMode_t mode, fs_handle_owner_t owner, const char *calling_function ) {
	int result;

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** file handle open **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "origin: %s\n", calling_function );
		FS_DPrintf( "path: %s\n", qpath );
		if ( mode == FS_READ && !f ) {
			FS_DPrintf( "mode: read (size check)\n" );
		} else {
			FS_DPrintf( "mode: %s\n", FS_ModeString( mode ) );
		}
		FS_DPrintf( "owner: %s\n", FS_Handle_OwnerString( owner ) );
	}

	result = FS_FOpenFileByModeGeneral( qpath, f, mode, owner );

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "result: return value %i (handle %i)\n", result, f ? *f : 0 );
		FS_DebugIndentStop();
	}

	return result;
}

/*
=================
FS_FOpenFileRead
=================
*/
long FS_FOpenFileRead( const char *filename, fileHandle_t *file, qboolean uniqueFILE ) {
	FSC_ASSERT( filename );
	return FS_FOpenFileByModeLogged( filename, file, FS_READ, FS_HANDLEOWNER_SYSTEM, "FS_FOpenFileRead" );
}

/*
=================
FS_FOpenFileWrite
=================
*/
fileHandle_t FS_FOpenFileWrite( const char *filename ) {
	fileHandle_t handle = 0;
	FSC_ASSERT( filename );
	FS_FOpenFileByModeLogged( filename, &handle, FS_WRITE, FS_HANDLEOWNER_SYSTEM, "FS_FOpenFileWrite" );
	return handle;
}

/*
=================
FS_FOpenFileAppend
=================
*/
fileHandle_t FS_FOpenFileAppend( const char *filename ) {
	fileHandle_t handle = 0;
	FSC_ASSERT( filename );
	FS_FOpenFileByModeLogged( filename, &handle, FS_APPEND, FS_HANDLEOWNER_SYSTEM, "FS_FOpenFileAppend" );
	return handle;
}

/*
=================
FS_FOpenFileByModeOwner
=================
*/
int FS_FOpenFileByModeOwner( const char *qpath, fileHandle_t *f, fsMode_t mode, fs_handle_owner_t owner ) {
	return FS_FOpenFileByModeLogged( qpath, f, mode, owner, "FS_FOpenFileByModeOwner" );
}

/*
=================
FS_FOpenFileByMode
=================
*/
int FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	FSC_ASSERT( qpath );
	return FS_FOpenFileByModeLogged( qpath, f, mode, FS_HANDLEOWNER_SYSTEM, "FS_FOpenFileByMode" );
}

/*
###############################################################################################

Misc handle operations

###############################################################################################
*/

/*
=================
FS_SV_FOpenFileRead
=================
*/
long FS_SV_FOpenFileRead( const char *filename, fileHandle_t *fp ) {
	int i;
	char path[FS_MAX_PATH];
	int size = -1;
	FSC_ASSERT( filename );
	FSC_ASSERT( fp );
	*fp = 0;

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "********** SV file read **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "path: %s\n", filename );
	}

	for ( i = 0; i < FS_MAX_SOURCEDIRS; ++i ) {
		if ( FS_GeneratePathSourcedir( i, filename, NULL, FS_ALLOW_DIRECTORIES, 0, path, sizeof( path ) ) ) {
			*fp = FS_DirectReadHandle_Open( NULL, path, (unsigned int *)&size );
			if ( *fp ) {
				break;
			}
		}
	}
	if ( !*fp ) {
		size = -1;
	}

	if ( fs.cvar.fs_debug_fileio->integer ) {
		FS_DPrintf( "result: return value %i (handle %i)\n", size, *fp );
		FS_DebugIndentStop();
	}

	return size;
}

/*
=================
FS_SV_FOpenFileWrite
=================
*/
fileHandle_t FS_SV_FOpenFileWrite( const char *filename ) {
	FSC_ASSERT( filename );
	return FS_FOpenFile_WriteHandleOpen( NULL, filename, qfalse, qfalse, 0 );
}

/*
=================
FS_FCloseFile
=================
*/
void FS_FCloseFile( fileHandle_t f ) {
	if ( !f ) {
		Com_DPrintf( "FS_FCloseFile on null handle\n" );
		return;
	}
	FS_Handle_Close( f );
}

/*
=================
FS_Read
=================
*/
int FS_Read( void *buffer, int len, fileHandle_t f ) {
	FSC_ASSERT( buffer );
	return FS_Handle_Read( f, (char *)buffer, len );
}

/*
=================
FS_Read2

Wrapper for FS_Read for compatibility with old code.
=================
*/
int FS_Read2( void *buffer, int len, fileHandle_t f ) {
	FSC_ASSERT( buffer );
	return FS_Read( buffer, len, f );
}

/*
=================
FS_Write
=================
*/
int FS_Write( const void *buffer, int len, fileHandle_t h ) {
	FSC_ASSERT( buffer );
	FS_Handle_Write( h, (const char *)buffer, len );
	return len;
}

/*
=================
FS_Seek
=================
*/
int FS_Seek( fileHandle_t f, long offset, int origin ) {
	return FS_Handle_FSeek( f, offset, (fsOrigin_t)origin );
}

/*
=================
FS_FTell
=================
*/
int FS_FTell( fileHandle_t f ) {
	return FS_Handle_FTell( f );
}

/*
=================
FS_Flush
=================
*/
void FS_Flush( fileHandle_t f ) {
	FS_WriteHandle_Flush( f, qfalse );
}

/*
=================
FS_ForceFlush
=================
*/
void FS_ForceFlush( fileHandle_t f ) {
	FS_WriteHandle_Flush( f, qtrue );
}

/*
###############################################################################################

Misc data operations

###############################################################################################
*/

/*
=================
FS_ReadFile

Returns -1 and nulls buffer on error. Returns size and sets buffer on success.
On success result buffer must be freed with FS_FreeFile.
Can be called with null buffer for size check.
=================
*/
long FS_ReadFile( const char *qpath, void **buffer ) {
	const fsc_file_t *file;
	unsigned int len;
	FSC_ASSERT( qpath );

	file = FS_GeneralLookup( qpath, 0, qfalse );

	if ( !file ) {
		// File not found
		if ( buffer ) {
			*buffer = NULL;
		}
		return -1;
	}

	if ( !buffer ) {
		// Size check
		return (long)file->filesize;
	}

	*buffer = FS_ReadData( file, NULL, &len, "FS_ReadFile" );
	return (long)len;
}

/*
=================
FS_FreeFile
=================
*/
void FS_FreeFile( void *buffer ) {
	if ( !buffer ) {
		Com_Error( ERR_FATAL, "FS_FreeFile( NULL )" );
	}
	FS_FreeData( (char *)buffer );
}

/*
=================
FS_WriteFile

Copied from original filesystem.
=================
*/
void FS_WriteFile( const char *qpath, const void *buffer, int size ) {
	fileHandle_t f;

	if ( !qpath || !buffer ) {
		Com_Error( ERR_FATAL, "FS_WriteFile: NULL parameter" );
	}

	f = FS_FOpenFileWrite( qpath );
	if ( !f ) {
		Com_Printf( "Failed to open %s\n", qpath );
		return;
	}

	FS_Write( buffer, size, f );

	FS_FCloseFile( f );
}

#endif // NEW_FILESYSTEM
