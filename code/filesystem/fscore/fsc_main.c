/*
===========================================================================
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
#include "fscore.h"

#define STACKPTR( pointer ) ( FSC_STACK_RETRIEVE( &fs->general_stack, pointer, fsc_false ) ) // non-null
#define STACKPTRN( pointer ) ( FSC_STACK_RETRIEVE( &fs->general_stack, pointer, fsc_true ) ) // null allowed

/*
###############################################################################################

Direct Sourcetype Operations

###############################################################################################
*/

/*
=================
FSC_DS_IsFileActive
=================
*/
static fsc_boolean FSC_DS_IsFileActive( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	return ( (fsc_file_direct_t *)file )->refresh_count == fs->refresh_count ? fsc_true : fsc_false;
}

/*
=================
FSC_DS_GetModDir
=================
*/
static const char *FSC_DS_GetModDir( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	return (const char *)STACKPTR( ( (fsc_file_direct_t *)file )->qp_mod_ptr );
}

/*
=================
FSC_DS_ExtractData
=================
*/
static unsigned int FSC_DS_ExtractData( const fsc_file_t *file, char *buffer, const fsc_filesystem_t *fs ) {
	fsc_filehandle_t *fp;
	unsigned int result;

	// Open the file
	fp = FSC_FOpenRaw( (const fsc_ospath_t *)STACKPTR( ( (fsc_file_direct_t *)file )->os_path_ptr ), "rb" );
	if ( !fp ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "failed to open file", FSC_NULL );
		return 0;
	}

	result = FSC_FRead( buffer, file->filesize, fp );
	FSC_ASSERT( result <= file->filesize );

	FSC_FClose( fp );
	return result;
}

// ----------------------------------------------------------------

static fsc_sourcetype_t direct_sourcetype = {
	FSC_SOURCETYPE_DIRECT,
	FSC_DS_IsFileActive,
	FSC_DS_GetModDir,
	FSC_DS_ExtractData,
};

/*
###############################################################################################

Common file operations

###############################################################################################
*/

/*
=================
FSC_GetSourcetype

Returns sourcetype object for given file.
=================
*/
static const fsc_sourcetype_t *FSC_GetSourcetype( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	int i;

	// Check built in sourcetypes
	if ( file->sourcetype == FSC_SOURCETYPE_DIRECT )
		return &direct_sourcetype;
	if ( file->sourcetype == FSC_SOURCETYPE_PK3 )
		return &pk3_sourcetype;

	// Check custom sourcetypes
	for ( i = 0; i < FSC_CUSTOM_SOURCETYPE_COUNT; ++i ) {
		if ( file->sourcetype == fs->custom_sourcetypes[i].sourcetype_id )
			return &fs->custom_sourcetypes[i];
	}

	return FSC_NULL;
}

/*
=================
FSC_GetBaseFile

Returns the source pk3 if file is from a pk3, the file itself if file is on disk,
or null if the file is from a custom sourcetype.
=================
*/
const fsc_file_direct_t *FSC_GetBaseFile( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	FSC_ASSERT( file );
	if ( file->sourcetype == FSC_SOURCETYPE_DIRECT ) {
		return (const fsc_file_direct_t *)file;
	}
	if ( file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		return (fsc_file_direct_t *)STACKPTR( ( (fsc_file_frompk3_t *)file )->source_pk3 );
	}
	return FSC_NULL;
}

/*
=================
FSC_ExtractFile

Extracts complete file contents into target buffer. Provided buffer should be size file->filesize.
Returns number of bytes successfully read, which equals file->filesize on success.
=================
*/
unsigned int FSC_ExtractFile( const fsc_file_t *file, char *buffer, const fsc_filesystem_t *fs ) {
	const fsc_sourcetype_t *sourcetype;
	unsigned int result;
	if ( file->contents_cache ) {
		FSC_Memcpy( buffer, STACKPTR( file->contents_cache ), file->filesize );
		return file->filesize;
	}
	sourcetype = FSC_GetSourcetype( file, fs );
	FSC_ASSERT( sourcetype && sourcetype->extract_data );

	result = sourcetype->extract_data( file, buffer, fs );
	FSC_ASSERT( result <= file->filesize );
	if ( result != file->filesize ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "failed to read all data from file", FSC_NULL );
	}
	return result;
}

/*
=================
FSC_ExtractFileAllocated

Extracts complete file contents into new allocated buffer. Returns null on error or data pointer on success.
On success, caller is responsible for calling FSC_Free on the returned pointer.
=================
*/
char *FSC_ExtractFileAllocated( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	char *data;
	if ( file->filesize + 1 < file->filesize )
		return FSC_NULL;
	data = (char *)FSC_Malloc( file->filesize + 1 );
	if ( FSC_ExtractFile( file, data, fs ) != file->filesize ) {
		FSC_Free( data );
		return FSC_NULL;
	}
	data[file->filesize] = '\0';
	return data;
}

/*
=================
FSC_IsFileActive

Returns true if file is active and expected to exist on disk, false otherwise.
=================
*/
fsc_boolean FSC_IsFileActive( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	const fsc_sourcetype_t *sourcetype = FSC_GetSourcetype( file, fs );
	FSC_ASSERT( sourcetype && sourcetype->is_file_active );
	return sourcetype->is_file_active( file, fs );
}

/*
=================
FSC_FromDownloadPk3

Returns true if file either is a pk3 in a download directory, or is contained in one.
=================
*/
fsc_boolean FSC_FromDownloadPk3( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	const fsc_file_direct_t *baseFile = FSC_GetBaseFile( file, fs );
	if ( baseFile && ( baseFile->f.flags & FSC_FILEFLAG_DLPK3 ) ) {
		return fsc_true;
	}
	return fsc_false;
}

/*
=================
FSC_GetModDir

Returns mod directory for given file. May return empty string if mod directory is invalid,
but should not return null.
=================
*/
const char *FSC_GetModDir( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	const fsc_sourcetype_t *sourcetype = FSC_GetSourcetype( file, fs );
	const char *mod_directory;
	FSC_ASSERT( sourcetype && sourcetype->get_mod_dir );
	mod_directory = sourcetype->get_mod_dir( file, fs );
	FSC_ASSERT( mod_directory );
	return mod_directory;
}

#define FSC_ADD_STRING(string) FSC_StreamAppendString(stream, string)

/*
=================
FSC_FileToStream

Writes a readable string representation of the given file to stream.
=================
*/

#define FSC_ADD_STRING(string) FSC_StreamAppendString(stream, string)

void FSC_FileToStream( const fsc_file_t *file, fsc_stream_t *stream, const fsc_filesystem_t *fs,
		fsc_boolean include_mod, fsc_boolean include_pk3_origin ) {
	if ( include_mod ) {
		const char *mod_dir = FSC_GetModDir( file, fs );
		if ( !*mod_dir )
			mod_dir = "<no-mod-dir>";
		FSC_ADD_STRING( mod_dir );
		FSC_ADD_STRING( "/" );
	}

	if ( include_pk3_origin ) {
		if ( file->sourcetype == FSC_SOURCETYPE_DIRECT && ( (fsc_file_direct_t *)file )->pk3dir_ptr ) {
			FSC_ADD_STRING( (const char *)STACKPTR( ( (fsc_file_direct_t *)file )->pk3dir_ptr ) );
			FSC_ADD_STRING( ".pk3dir->" );
		} else if ( file->sourcetype == FSC_SOURCETYPE_PK3 ) {
			FSC_FileToStream( (const fsc_file_t *)FSC_GetBaseFile( file, fs ), stream, fs, fsc_false, fsc_false );
			FSC_ADD_STRING( "->" );
		}
	}

	FSC_ADD_STRING( (const char *)STACKPTR( file->qp_dir_ptr ) );
	FSC_ADD_STRING( (const char *)STACKPTR( file->qp_name_ptr ) );
	FSC_ADD_STRING( (const char *)STACKPTR( file->qp_ext_ptr ) );
}

/*
###############################################################################################

File Indexing

###############################################################################################
*/

/*
=================
FSC_MergeStats
=================
*/
static void FSC_MergeStats( const fsc_stats_t *source, fsc_stats_t *target ) {
	target->valid_pk3_count += source->valid_pk3_count;
	target->pk3_subfile_count += source->pk3_subfile_count;
	target->shader_file_count += source->shader_file_count;
	target->shader_count += source->shader_count;
	target->total_file_count += source->total_file_count;
	target->cacheable_file_count += source->cacheable_file_count;
}

/*
=================
FSC_SanityLimit

Applies some limits to prevent potential vulnerabilities due to overloaded pk3 files.
Returns true if limit hit, otherwise decrements limit counter and returns false.
=================
*/
fsc_boolean FSC_SanityLimit( unsigned int size, unsigned int *limit_value, fsc_sanity_limit_t *sanity_limit ) {
	FSC_ASSERT( sanity_limit );
	FSC_ASSERT( limit_value );

	if ( *limit_value < size ) {
		if ( !sanity_limit->warned ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "pk3 content dropped due to sanity limits", (void *)sanity_limit->pk3file );
			sanity_limit->warned = fsc_true;
		}

		return fsc_true;
	}

	*limit_value -= size;
	return fsc_false;
}

/*
=================
FSC_RegisterFile

Registers file in index and loads secondary content such as shaders.
Called for both files on disk and in pk3s.
=================
*/
void FSC_RegisterFile( fsc_stackptr_t file_ptr, fsc_sanity_limit_t *sanity_limit, fsc_filesystem_t *fs ) {
	fsc_file_t *file = (fsc_file_t *)STACKPTR( file_ptr );
	fsc_file_direct_t *base_file = (fsc_file_direct_t *)FSC_GetBaseFile( file, fs );
	const char *qp_dir = (const char *)STACKPTR( file->qp_dir_ptr );
	const char *qp_name = (const char *)STACKPTR( file->qp_name_ptr );
	const char *qp_ext = (const char *)STACKPTR( file->qp_ext_ptr );

	// Check for index overflow
	if ( sanity_limit && FSC_SanityLimit( FSC_Strlen( qp_dir ) + FSC_Strlen( qp_name ) + FSC_Strlen( qp_ext ) + 64,
			&sanity_limit->content_index_memory, sanity_limit ) ) {
		return;
	}

	// Register file for main lookup and directory iteration
	FSC_HashtableInsert( file_ptr, FSC_StringHash( qp_name, qp_dir ), &fs->files );
	FSC_IterationRegisterFile( file_ptr, &fs->directories, &fs->string_repository, &fs->general_stack );

	// Index shaders and update shader counter on base file
	if ( !FSC_Stricmp( qp_dir, "scripts/" ) && !FSC_Stricmp( qp_ext, ".shader" ) ) {
		int count = FSC_IndexShaderFile( fs, file_ptr, sanity_limit );
		if ( base_file ) {
			base_file->shader_file_count += 1;
			base_file->shader_count += count;
			base_file->f.flags |= FSC_FILEFLAG_LINKED_CONTENT;
		}
	}

	// Index crosshairs
	if ( !FSC_Stricmp( qp_dir, "gfx/2d/" ) ) {
		char buffer[10];
		FSC_Strncpy( buffer, qp_name, sizeof( buffer ) );
		if ( !FSC_Stricmp( buffer, "crosshair" ) ) {
			FSC_IndexCrosshair( fs, file_ptr, sanity_limit );
			if ( base_file )
				base_file->f.flags |= FSC_FILEFLAG_LINKED_CONTENT;
		}
	}

	// Cache small arena and bot file contents
	if ( file->filesize < 16384 && !FSC_Stricmp( qp_dir, "scripts/" ) &&
		 ( !FSC_Stricmp( qp_ext, ".arena" ) || !FSC_Stricmp( qp_ext, ".bot" ) ) &&
		 ( !sanity_limit || !FSC_SanityLimit( file->filesize + 256, &sanity_limit->content_cache_memory, sanity_limit ) ) ) {
		char *source_data = FSC_ExtractFileAllocated( file, fs );
		if ( source_data ) {
			fsc_stackptr_t target_ptr = FSC_StackAllocate( &fs->general_stack, file->filesize );
			char *target_data = (char *)STACKPTR( target_ptr );
			FSC_Memcpy( target_data, source_data, file->filesize );
			file->contents_cache = target_ptr;
			FSC_Free( source_data );
		}
	}
}

/*
=================
FSC_NullStringCompare

Compares two potentially null strings. Returns true if matching, false otherwise.
=================
*/
static int FSC_NullStringCompare( const char *s1, const char *s2 ) {
	if ( !s1 && !s2 )
		return fsc_true;
	if ( !s1 || !s2 )
		return fsc_false;
	return !FSC_Strcmp( s1, s2 );
}

/*
=================
FSC_LoadFile

Registers a file on disk into the filesystem index.
=================
*/
void FSC_LoadFile( int source_dir_id, const fsc_ospath_t *os_path, const char *mod_dir, const char *pk3dir_name,
		const char *qp_dir, const char *qp_name, const char *qp_ext, unsigned int os_timestamp,
		unsigned int filesize, fsc_filesystem_t *fs ) {
	fsc_stackptr_t file_ptr;
	fsc_file_direct_t *file = FSC_NULL;
	fsc_hashtable_iterator_t hti;
	unsigned int fs_hash = FSC_StringHash( qp_name, qp_dir );
	fsc_boolean unindexed_file = fsc_false;		// File was not present in the index at all
	fsc_boolean new_file = fsc_false;			// File was not present in last refresh, but may have been in the index

	FSC_ASSERT( os_path );
	FSC_ASSERT( qp_dir );
	FSC_ASSERT( qp_name );
	FSC_ASSERT( qp_ext );
	FSC_ASSERT( fs );

	// Search filesystem to see if a sufficiently equivalent entry already exists.
	FSC_HashtableIterateBegin( &fs->files, fs_hash, &hti );
	while ( ( file_ptr = FSC_HashtableIterateNext( &hti ) ) ) {
		file = (fsc_file_direct_t *)STACKPTR( file_ptr );
		if ( file->f.sourcetype != FSC_SOURCETYPE_DIRECT )
			continue;
		if ( FSC_Strcmp( (char *)STACKPTR( file->f.qp_name_ptr ), qp_name ) )
			continue;
		if ( FSC_Strcmp( (char *)STACKPTR( file->f.qp_dir_ptr ), qp_dir ) )
			continue;
		if ( FSC_Strcmp( (char *)STACKPTR( file->f.qp_ext_ptr ), qp_ext ) )
			continue;
		if ( !FSC_NullStringCompare( (char *)STACKPTRN( file->qp_mod_ptr ), mod_dir ) )
			continue;
		if ( !FSC_NullStringCompare( (char *)STACKPTRN( file->pk3dir_ptr ), pk3dir_name ) )
			continue;
		if ( file->os_path_ptr && FSC_OSPathCompare( (const fsc_ospath_t *)STACKPTR( file->os_path_ptr ), os_path ) )
			continue;
		if ( file->f.filesize != filesize || file->os_timestamp != os_timestamp ) {
			if ( file->os_path_ptr && !( file->f.flags & FSC_FILEFLAG_LINKED_CONTENT ) && !file->f.contents_cache ) {
				// Reuse the same file object to save memory (this prevents files actively written
				// by the game such as logs generating a new file object every refresh)
				file->f.filesize = filesize;
				file->os_timestamp = os_timestamp;
				break;
			} else {
				// Otherwise treat the file as non-matching
				continue;
			}
		}
		break;
	}

	if ( file_ptr ) {
		// Have existing entry
		if ( file->refresh_count == fs->refresh_count ) {
			// Existing file already active. This can happen with if there are duplicate source directories
			// loaded in the same refresh cycle. Just leave the existing file unchanged.
			return;
		}

		// Activate the entry
		if ( file->refresh_count != fs->refresh_count - 1 ) {
			new_file = fsc_true;
		}
		file->refresh_count = fs->refresh_count;
	}

	else {
		// Create a new entry
		file_ptr = FSC_StackAllocate( &fs->general_stack, sizeof( *file ) );
		file = (fsc_file_direct_t *)STACKPTR( file_ptr );

		// Set up fields (other fields are zeroed by default due to stack allocation)
		file->f.sourcetype = FSC_SOURCETYPE_DIRECT;
		file->f.qp_dir_ptr = FSC_StringRepositoryGetString( qp_dir, &fs->string_repository );
		file->f.qp_name_ptr = FSC_StringRepositoryGetString( qp_name, &fs->string_repository );
		file->f.qp_ext_ptr = FSC_StringRepositoryGetString( qp_ext, &fs->string_repository );
		file->qp_mod_ptr = mod_dir ? FSC_StringRepositoryGetString( mod_dir, &fs->string_repository ) : FSC_SPNULL;
		file->pk3dir_ptr = pk3dir_name ? FSC_StringRepositoryGetString( pk3dir_name, &fs->string_repository ) : FSC_SPNULL;
		file->f.filesize = filesize;
		file->os_timestamp = os_timestamp;
		file->refresh_count = fs->refresh_count;

		unindexed_file = fsc_true;
		new_file = fsc_true;
	}

	// Update source dir and pk3 type flags
	file->source_dir_id = source_dir_id;
	file->f.flags &= ~FSC_FILEFLAGS_SPECIAL_PK3;
	if ( !FSC_Stricmp( qp_ext, ".pk3" ) ) {
		if ( !FSC_Stricmp( qp_dir, "downloads/" ) ) {
			file->f.flags |= FSC_FILEFLAG_DLPK3;
		} else if ( !FSC_Stricmp( qp_dir, "refonly/" ) ) {
			file->f.flags |= FSC_FILEFLAG_REFONLY_PK3;
		} else if ( !FSC_Stricmp( qp_dir, "nolist/" ) ) {
			file->f.flags |= FSC_FILEFLAG_NOLIST_PK3;
		}
	}

	// Save os path. This happens on loading a new file, and also when first activating an entry that was loaded from cache.
	if ( !file->os_path_ptr ) {
		int os_path_size = FSC_OSPathSize( os_path );
		file->os_path_ptr = FSC_StackAllocate( &fs->general_stack, os_path_size );
		FSC_Memcpy( STACKPTR( file->os_path_ptr ), os_path, os_path_size );
	}

	// Register file and load contents
	if ( unindexed_file ) {
		FSC_RegisterFile( file_ptr, FSC_NULL, fs );
		if ( !FSC_Stricmp( qp_ext, ".pk3" ) &&
				( !*qp_dir || ( file->f.flags & FSC_FILEFLAGS_SPECIAL_PK3 ) ) ) {
			FSC_LoadPk3( (fsc_ospath_t *)STACKPTR( file->os_path_ptr ), fs, file_ptr, FSC_NULL, FSC_NULL );
			file->f.flags |= FSC_FILEFLAG_LINKED_CONTENT;
		}
	}

	// Update stats
	{
		fsc_stats_t stats;
		FSC_Memset( &stats, 0, sizeof( stats ) );

		stats.total_file_count = 1 + file->pk3_subfile_count;

		stats.cacheable_file_count = file->pk3_subfile_count;
		if ( file->shader_count || file->pk3_subfile_count ) {
			++stats.cacheable_file_count;
		}

		stats.pk3_subfile_count = file->pk3_subfile_count;

		// By design, this field records only *valid* pk3s with a nonzero hash.
		// Perhaps create another field that includes invalid pk3s?
		if ( file->pk3_hash ) {
			stats.valid_pk3_count = 1;
		}

		stats.shader_file_count = file->shader_file_count;
		stats.shader_count = file->shader_count;

		FSC_MergeStats( &stats, &fs->active_stats );
		if ( unindexed_file ) {
			FSC_MergeStats( &stats, &fs->total_stats );
		}
		if ( new_file ) {
			FSC_MergeStats( &stats, &fs->new_stats );
		}
	}
}

/*
=================
FSC_HasAppExtension

Returns true if name matches mac app bundle extension, false otherwise.
=================
*/
static fsc_boolean FSC_HasAppExtension( const char *name ) {
	int length = FSC_Strlen( name );
	if ( length < 4 )
		return fsc_false;
	if ( !FSC_Stricmp( name + ( length - 4 ), ".app" ) )
		return fsc_true;
	return fsc_false;
}

/*
=================
FSC_LoadFileFromPath

Registers a file on disk into the filesystem index. Performs some additional path parsing
compared to the base FSC_LoadFile function.
=================
*/
void FSC_LoadFileFromPath( int source_dir_id, const fsc_ospath_t *os_path, const char *game_path,
		unsigned int os_timestamp, unsigned int filesize, fsc_filesystem_t *fs ) {
	char qp_mod[FSC_MAX_MODDIR];
	const char *qpath_start = FSC_NULL;
	fsc_boolean file_in_pk3dir = fsc_false;
	char pk3dir_buffer[FSC_MAX_QPATH];
	const char *pk3dir_remainder = FSC_NULL;
	fsc_qpath_buffer_t qpath_split;

	// Process mod directory prefix
	if ( !FSC_SplitLeadingDirectory( game_path, qp_mod, sizeof( qp_mod ), &qpath_start ) ) {
		return;
	}
	if ( !qpath_start ) {
		return;
	}
	if ( FSC_HasAppExtension( qp_mod ) ) {
		// Don't index mac app bundles as mods
		return;
	}

	// Process pk3dir prefix
	if ( FSC_SplitLeadingDirectory( qpath_start, pk3dir_buffer, sizeof( pk3dir_buffer ), &pk3dir_remainder ) ) {
		if ( pk3dir_remainder ) {
			int length = FSC_Strlen( pk3dir_buffer );
			if ( length >= 7 && !FSC_Stricmp( pk3dir_buffer + length - 7, ".pk3dir" ) ) {
				pk3dir_buffer[length - 7] = '\0';
				file_in_pk3dir = fsc_true;
				qpath_start = pk3dir_remainder;
			}
		}
	}

	// Process qpath
	FSC_SplitQpath( qpath_start, &qpath_split, fsc_false );

	// Load file
	FSC_LoadFile( source_dir_id, os_path, qp_mod, file_in_pk3dir ? pk3dir_buffer : FSC_NULL, qpath_split.dir,
				  qpath_split.name, qpath_split.ext, os_timestamp, filesize, fs );
}

typedef struct {
	int source_dir_id;
	fsc_filesystem_t *fs;
} iterate_context_t;

/*
=================
FSC_LoadFileFromIteration
=================
*/
static void FSC_LoadFileFromIteration( iterate_data_t *file_data, void *iterate_context ) {
	iterate_context_t *iterate_context_typed = (iterate_context_t *)iterate_context;
	FSC_LoadFileFromPath( iterate_context_typed->source_dir_id, file_data->os_path, file_data->qpath_with_mod_dir,
			file_data->os_timestamp, file_data->filesize, iterate_context_typed->fs );
}

/*
=================
FSC_FilesystemInitialize

Initializes an empty filesystem.
=================
*/
void FSC_FilesystemInitialize( fsc_filesystem_t *fs ) {
	FSC_Memset( fs, 0, sizeof( *fs ) );
	FSC_StackInitialize( &fs->general_stack );
	FSC_HashtableInitialize( &fs->files, &fs->general_stack, 65536 );
	FSC_HashtableInitialize( &fs->string_repository, &fs->general_stack, 65536 );
	FSC_HashtableInitialize( &fs->directories, &fs->general_stack, 16384 );
	FSC_HashtableInitialize( &fs->shaders, &fs->general_stack, 65536 );
	FSC_HashtableInitialize( &fs->crosshairs, &fs->general_stack, 1 );
	FSC_HashtableInitialize( &fs->pk3_hash_lookup, &fs->general_stack, 4096 );
}

/*
=================
FSC_FilesystemFree

Frees a filesystem object. Can be called on a nulled, freed, initialized, or in some cases partially
initialized filesystem.
=================
*/
void FSC_FilesystemFree( fsc_filesystem_t *fs ) {
	FSC_StackFree( &fs->general_stack );
	FSC_HashtableFree( &fs->files );
	FSC_HashtableFree( &fs->string_repository );
	FSC_HashtableFree( &fs->directories );
	FSC_HashtableFree( &fs->shaders );
	FSC_HashtableFree( &fs->crosshairs );
	FSC_HashtableFree( &fs->pk3_hash_lookup );
}

/*
=================
FSC_FilesystemReset

Resets all files in filesystem to inactive state, resulting in an 'empty' filesystem. Inactive files
can be reactivated during subsequent calls to FSC_LoadDirectory.
=================
*/
void FSC_FilesystemReset( fsc_filesystem_t *fs ) {
	++fs->refresh_count;
	FSC_Memset( &fs->active_stats, 0, sizeof( fs->active_stats ) );
	FSC_Memset( &fs->new_stats, 0, sizeof( fs->new_stats ) );
}

/*
=================
FSC_LoadDirectoryRawPath

Scans the given game directory for files and registers them into the file index.
=================
*/
void FSC_LoadDirectoryRawPath( fsc_filesystem_t *fs, fsc_ospath_t *os_path, int source_dir_id ) {
	iterate_context_t context;
	context.source_dir_id = source_dir_id;
	context.fs = fs;
	FSC_IterateDirectory( os_path, FSC_LoadFileFromIteration, &context );
}

/*
=================
FSC_LoadDirectory

Standard string path wrapper for FSC_LoadDirectoryRawPath.
=================
*/
void FSC_LoadDirectory( fsc_filesystem_t *fs, const char *path, int source_dir_id ) {
	fsc_ospath_t *os_path = FSC_StringToOSPath( path );
	FSC_LoadDirectoryRawPath( fs, os_path, source_dir_id );
	FSC_Free( os_path );
}

#endif	// NEW_FILESYSTEM
