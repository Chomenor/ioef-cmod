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

#ifdef USE_LOCAL_HEADERS
#include "../../zlib/zlib.h"
#else
#include <zlib.h>
#endif

#define STACKPTR( pointer ) ( FSC_STACK_RETRIEVE( &fs->general_stack, pointer, fsc_false ) ) // non-null
#define STACKPTRN( pointer ) ( FSC_STACK_RETRIEVE( &fs->general_stack, pointer, fsc_true ) ) // null allowed
#define STACKPTR_LCL( pointer ) ( FSC_STACK_RETRIEVE( stack, pointer, fsc_false ) ) // non null, local stack parameter

// Somewhat arbitrary limit to avoid overflow issues...
#define FSC_MAX_PK3_SIZE 4240000000u

/*
###############################################################################################

PK3 File Indexing

###############################################################################################
*/

typedef struct {
	char *data;
	int cd_length;
	unsigned int zip_offset;
	int entry_count;
} central_directory_t;

/*
=================
FSC_IsLittleEndianSystem

Returns true on little endian system, false on big endian system.
=================
*/
static fsc_boolean FSC_IsLittleEndianSystem( void ) {
	static volatile int test = 1;
	if ( *(char *)&test )
		return fsc_true;
	return fsc_false;
}

/*
=================
FSC_ConvertLittleEndianInt

Converts an int from a little endian source to match the system format.
=================
*/
static unsigned int FSC_ConvertLittleEndianInt( unsigned int value ) {
	if ( FSC_IsLittleEndianSystem() ) {
		return value;
	}

	value = ( ( value << 8 ) & 0xFF00FF00 ) | ( ( value >> 8 ) & 0xFF00FF );
	return ( value << 16 ) | ( value >> 16 );
}

/*
=================
FSC_ConvertLittleEndianShort

Converts a short from a little endian source to match the system format.
=================
*/
static unsigned short FSC_ConvertLittleEndianShort( unsigned short value ) {
	if ( FSC_IsLittleEndianSystem() ) {
		return value;
	}

	return ( value << 8 ) | ( value >> 8 );
}

/*
=================
FSC_Pk3SeekSet

Workaround to support seeking in pk3 files larger than 2GB without adding any new potential library dependencies.
Returns true on error, false otherwise.
=================
*/
static fsc_boolean FSC_Pk3SeekSet( fsc_filehandle_t *fp, unsigned int offset ) {
	fsc_seek_type_t type = FSC_SEEK_SET;
	do {
		unsigned int seek_amount = offset;
		if ( seek_amount > 2000000000 )
			seek_amount = 2000000000;
		if ( FSC_FSeek( fp, (int)seek_amount, type ) ) {
			return fsc_true;
		}
		offset -= seek_amount;
		type = FSC_SEEK_CUR;
	} while ( offset );
	return fsc_false;
}

/*
=================
FSC_ReadPk3CentralDirectoryFP

Loads pk3 central directory to output structure. Returns true on error, false on success.
=================
*/
static fsc_boolean FSC_ReadPk3CentralDirectoryFP( fsc_filehandle_t *fp, unsigned int file_length, central_directory_t *output ) {
	int pass;
	char buffer[66000];
	int buffer_read_size = 0;	// Offset from end of file/buffer of data that has been successfully read to buffer
	int eocd_position = 0;		// Offset from end of file/buffer of the EOCD record
	unsigned int cd_position;	// Offset from beginning of file of central directory

	// End Of Central Directory Record (EOCD) can start anywhere in the last 65KB or so of the zip file,
	// determined by the presence of a magic number. For performance purposes first scan the last 4KB of the file,
	// and if the magic number isn't found do a second pass for the whole 65KB.

	for ( pass = 0; pass < 2; ++pass ) {
		// Get buffer_read_target, which is the offset from end of file/buffer that we are tring to read to buffer
		int i;
		int buffer_read_target = pass == 0 ? 4096 : sizeof( buffer );
		if ( (unsigned int)buffer_read_target > file_length ) {
			buffer_read_target = file_length;
		}
		if ( buffer_read_target <= buffer_read_size ) {
			return fsc_true;
		}

		// Read the data
		FSC_Pk3SeekSet( fp, file_length - buffer_read_target );
		FSC_FRead( buffer + sizeof( buffer ) - buffer_read_target, buffer_read_target - buffer_read_size, fp );

		// Search for magic number
		// EOCD cannot start less than 22 bytes from end of file, because it is 22 bytes long
		for ( i = 22; i < buffer_read_target; ++i ) {
			char *string = buffer + sizeof( buffer ) - i;
			if ( string[0] == 0x50 && string[1] == 0x4b && string[2] == 0x05 && string[3] == 0x06 ) {
				eocd_position = i;
				break;
			}
		}

		buffer_read_size = buffer_read_target;
		if ( eocd_position ) {
			break;
		}
	}

	if ( !eocd_position ) {
		return fsc_true;
	}

	#define EOCD_SHORT( offset ) FSC_ConvertLittleEndianShort( *(unsigned short *)( buffer + sizeof( buffer ) - eocd_position + offset ) )
	#define EOCD_INT( offset ) FSC_ConvertLittleEndianInt( *(unsigned int *)( buffer + sizeof( buffer ) - eocd_position + offset ) )

	output->entry_count = EOCD_SHORT( 8 );
	output->cd_length = EOCD_INT( 12 );

	// No reason for central directory to be over 100 MB
	if ( output->cd_length < 0 || output->cd_length > 100 << 20 ) {
		return fsc_true;
	}

	// Must be space for central directory between the beginning of the file and the EOCD start
	if ( (unsigned int)output->cd_length > file_length - eocd_position ) {
		return fsc_true;
	}

	// EOCD sanity checks that were in the original code: something to do with ensuring this is not a spanned archive
	if ( EOCD_INT( 4 ) != 0 ) {
		// "this file's disk number" and "disk number containing central directory" should both be 0
		return fsc_true;
	}
	if ( EOCD_SHORT( 8 ) != EOCD_SHORT( 8 ) ) {
		// "cd entries on this disk" and "cd entries total" should be equal
		return fsc_true;
	}

	// Determine real central directory position
	cd_position = file_length - eocd_position - output->cd_length;

	// Determine zip offset from error in reported cd position - all file offsets need to be adjusted by this value
	{
		unsigned int cd_position_reported = EOCD_INT( 16 );
		if ( cd_position_reported > cd_position ) {
			// cd_position is already the maximum valid position
			return fsc_true;
		}
		output->zip_offset = cd_position - cd_position_reported;
	}

	output->data = (char *)FSC_Malloc( output->cd_length );

	// Read central directory to output, but try to use already buffered data if available
	{
		unsigned int buffer_file_position = file_length - buffer_read_size;   // Position in file where buffered data begins
		unsigned int unbuffered_read_length = 0;   // Amount of non-buffered data read

		if ( cd_position < buffer_file_position ) {
			// Since central directory starts before buffer, read unbuffered part of central directory into output
			unbuffered_read_length = buffer_file_position - cd_position;
			if ( unbuffered_read_length > (unsigned int)output->cd_length )
				unbuffered_read_length = output->cd_length;
			FSC_Pk3SeekSet( fp, cd_position );
			FSC_FRead( output->data, unbuffered_read_length, fp );
		}

		if ( unbuffered_read_length < (unsigned int)output->cd_length ) {
			// Read remaining data from buffer into output
			char *buffer_data = buffer + sizeof( buffer ) - buffer_read_size;
			if ( cd_position > buffer_file_position )
				buffer_data += cd_position - buffer_file_position;
			FSC_Memcpy( output->data + unbuffered_read_length, buffer_data, output->cd_length - unbuffered_read_length );
		}
	}

	return fsc_false;
}

/*
=================
FSC_ReadPk3CentralDirectory

Loads pk3 central directory to output structure with source pk3 specified by path.
Returns true on error, false on success.
=================
*/
static fsc_boolean FSC_ReadPk3CentralDirectory( fsc_ospath_t *os_path, central_directory_t *output, fsc_file_direct_t *source_file ) {
	fsc_filehandle_t *fp = FSC_NULL;
	unsigned int length;

	// Open file
	fp = FSC_FOpenRaw( os_path, "rb" );
	if ( !fp ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "error opening pk3", source_file );
		return fsc_true;
	}

	// Get size
	FSC_FSeek( fp, 0, FSC_SEEK_END );
	length = FSC_FTell( fp );
	if ( !length ) {
		FSC_FClose( fp );
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "zero size pk3", source_file );
		return fsc_true;
	}
	if ( length > FSC_MAX_PK3_SIZE ) {
		FSC_FClose( fp );
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "excessively large pk3", source_file );
		return fsc_true;
	}

	// Get central directory
	if ( FSC_ReadPk3CentralDirectoryFP( fp, length, output ) ) {
		FSC_FClose( fp );
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "error retrieving pk3 central directory", source_file );
		return fsc_true;
	}
	FSC_FClose( fp );

	return fsc_false;
}

/*
=================
FSC_RegisterPk3HashLookup

Registers a pk3 file into the filesystem pk3 hash lookup table.
=================
*/
void FSC_RegisterPk3HashLookup( fsc_stackptr_t pk3_file_ptr, fsc_hashtable_t *pk3_hash_lookup, fsc_stack_t *stack ) {
	fsc_file_direct_t *pk3_file = (fsc_file_direct_t *)STACKPTR_LCL( pk3_file_ptr );
	fsc_stackptr_t hash_map_entry_ptr = FSC_StackAllocate( stack, sizeof( fsc_pk3_hash_map_entry_t ) );
	fsc_pk3_hash_map_entry_t *hash_map_entry = (fsc_pk3_hash_map_entry_t *)STACKPTR_LCL( hash_map_entry_ptr );
	hash_map_entry->pk3 = pk3_file_ptr;
	FSC_HashtableInsert( hash_map_entry_ptr, pk3_file->pk3_hash, pk3_hash_lookup );
}

/*
=================
FSC_RegisterPk3Subfile

Registers a file contained in a pk3 into the filesystem.
=================
*/
static void FSC_RegisterPk3Subfile( fsc_filesystem_t *fs, char *filename, int filename_length, fsc_stackptr_t sourcefile_ptr,
			unsigned int header_position, unsigned int compressed_size, unsigned int uncompressed_size,
			short compression_method, fsc_sanity_limit_t *sanity_limit ) {
	fsc_file_direct_t *sourcefile = (fsc_file_direct_t *)STACKPTR( sourcefile_ptr );
	fsc_stackptr_t file_ptr = FSC_StackAllocate( &fs->general_stack, sizeof( fsc_file_frompk3_t ) );
	fsc_file_frompk3_t *file = (fsc_file_frompk3_t *)STACKPTR( file_ptr );
	char buffer[FSC_MAX_QPATH];
	fsc_qpath_buffer_t qpath_split;

	// Copy filename into null-terminated buffer for process_qpath
	// Also convert to lowercase to match behavior of original filesystem...
	if ( filename_length >= FSC_MAX_QPATH )
		filename_length = FSC_MAX_QPATH - 1;
	FSC_StrncpyLower( buffer, filename, filename_length + 1 );

	// Process qpath
	FSC_SplitQpath( buffer, &qpath_split, fsc_false );

	// Write qpaths to file structure
	file->f.qp_dir_ptr = FSC_StringRepositoryGetString( qpath_split.dir, &fs->string_repository );
	file->f.qp_name_ptr = FSC_StringRepositoryGetString( qpath_split.name, &fs->string_repository );
	file->f.qp_ext_ptr = FSC_StringRepositoryGetString( qpath_split.ext, &fs->string_repository );

	// Load the rest of the fields
	file->f.sourcetype = FSC_SOURCETYPE_PK3;
	file->source_pk3 = sourcefile_ptr;
	file->header_position = header_position;
	file->compressed_size = compressed_size;
	file->compression_method = compression_method;
	file->f.filesize = uncompressed_size;

	// Register file and load contents
	FSC_RegisterFile( file_ptr, sanity_limit, fs );
	++sourcefile->pk3_subfile_count;
}

/*
=================
FSC_LoadPk3

Registers a pk3 file and all subcontents into the filesystem index.

Can also be called with receive_hash_data set to extract pk3 hash checksums without indexing anything.
=================
*/
void FSC_LoadPk3( fsc_ospath_t *os_path, fsc_filesystem_t *fs, fsc_stackptr_t sourcefile_ptr,
		void ( *receive_hash_data )( void *context, char *data, int size ), void *receive_hash_data_context ) {
	fsc_file_direct_t *sourcefile = fs ? (fsc_file_direct_t *)STACKPTRN( sourcefile_ptr ) : FSC_NULL;
	central_directory_t cd;
	int entry_position = 0;		// Position of current entry relative to central directory data
	int entry_counter;			// Number of current entry

	int filename_length;
	int entry_length;
	unsigned int uncompressed_size;
	unsigned int compressed_size;
	unsigned int header_position;

	int *crcs_for_hash;
	int crcs_for_hash_buffer[1024];
	int crcs_for_hash_count = 0;

	fsc_sanity_limit_t sanity_limit;
	FSC_Memset( &sanity_limit, 0, sizeof( sanity_limit ) );

	if ( !receive_hash_data ) {
		FSC_ASSERT( sourcefile_ptr );

		// Set sanity limits to prevent pk3s with excessively large contents from causing freezes/overflows
		sanity_limit.content_index_memory = ( sourcefile->f.filesize < 200000 ? sourcefile->f.filesize : 200000 ) * 5 +
			( sourcefile->f.filesize < 1000000000 ? sourcefile->f.filesize : 1000000000 ) / 10 + 16384;
		sanity_limit.content_cache_memory = ( sourcefile->f.filesize < 200000 ? sourcefile->f.filesize : 200000 ) +
			( sourcefile->f.filesize < 1000000000 ? sourcefile->f.filesize : 1000000000 ) / 50;
		sanity_limit.data_read = ( sourcefile->f.filesize < 200000 ? sourcefile->f.filesize : 200000 ) * 50 + 200000 +
			( sourcefile->f.filesize < 1000000000 ? sourcefile->f.filesize : 1000000000 );
		sanity_limit.pk3file = sourcefile;
	}

	// Load central directory
	if ( FSC_ReadPk3CentralDirectory( os_path, &cd, sourcefile ) ) {
		return;
	}

	// Try to use the stack buffer, but if it's not big enough resort to malloc
	if ( cd.entry_count > sizeof( crcs_for_hash_buffer ) / sizeof( *crcs_for_hash_buffer ) ) {
		crcs_for_hash = (int *)FSC_Malloc( ( cd.entry_count + 1 ) * 4 );
	} else {
		crcs_for_hash = crcs_for_hash_buffer;
	}

	// Process each file
	for ( entry_counter = 0; entry_counter < cd.entry_count; ++entry_counter ) {
		// Make sure there is enough space to read the entry (minimum 47 bytes if filename is 1 byte)
		if ( entry_position + 47 > cd.cd_length ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "invalid file cd entry position", sourcefile );
			goto freemem;
		}

		// Verify magic number
		if ( cd.data[entry_position] != 0x50 || cd.data[entry_position + 1] != 0x4b || cd.data[entry_position + 2] != 0x01 ||
				cd.data[entry_position + 3] != 0x02 ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "file cd entry does not have correct signature", sourcefile );
			goto freemem;
		}

		#define CD_ENTRY_SHORT( offset ) FSC_ConvertLittleEndianShort( *(unsigned short *)( cd.data + entry_position + offset ) )
		#define CD_ENTRY_INT( offset ) FSC_ConvertLittleEndianInt( *(unsigned int *)( cd.data + entry_position + offset ) )
		#define CD_ENTRY_INT_LE( offset ) ( *(unsigned int *)( cd.data + entry_position + offset ) )

		// Get filename_length and entry_length
		filename_length = (int)CD_ENTRY_SHORT( 28 );
		{
			int extrafield_length = (int)CD_ENTRY_SHORT( 30 );
			int comment_length = (int)CD_ENTRY_SHORT( 32 );
			entry_length = 46 + filename_length + extrafield_length + comment_length;
			if ( entry_position + entry_length > cd.cd_length ) {
				FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "invalid file cd entry position 2", sourcefile );
				goto freemem;
			}
		}

		// Get compressed_size and uncompressed_size
		compressed_size = CD_ENTRY_INT( 20 );
		uncompressed_size = CD_ENTRY_INT( 24 );

		// Get local header_position (which is indicated by CD header, but needs to be modified by zip offset)
		header_position = CD_ENTRY_INT( 42 ) + cd.zip_offset;

		// Sanity checks
		if ( header_position + compressed_size < header_position ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "invalid file local entry position 1", sourcefile );
			goto freemem;
		}
		if ( header_position + compressed_size > FSC_MAX_PK3_SIZE ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "invalid file local entry position 2", sourcefile );
			goto freemem;
		}

		if ( uncompressed_size ) {
			crcs_for_hash[crcs_for_hash_count++] = CD_ENTRY_INT_LE( 16 );
		}

		if ( !(void *)receive_hash_data && !( sourcefile->f.flags & FSC_FILEFLAG_REFONLY_PK3 ) &&
				!( !uncompressed_size && *( cd.data + entry_position + 46 + filename_length - 1 ) == '/' ) ) {
			// Not in hash mode and not a directory entry - load the file
			FSC_RegisterPk3Subfile( fs, cd.data + entry_position + 46, filename_length, sourcefile_ptr,
				header_position, compressed_size, CD_ENTRY_INT( 24 ), CD_ENTRY_SHORT( 10 ), &sanity_limit );
		}

		entry_position += entry_length;
	}

	if ( (void *)receive_hash_data ) {
		receive_hash_data( receive_hash_data_context, (char *)crcs_for_hash, crcs_for_hash_count * 4 );
		goto freemem;
	}

	sourcefile->pk3_hash = FSC_BlockChecksum( crcs_for_hash, crcs_for_hash_count * 4 );

	// Add the pk3 to the hash lookup table
	FSC_RegisterPk3HashLookup( sourcefile_ptr, &fs->pk3_hash_lookup, &fs->general_stack );

	freemem:
	FSC_Free( cd.data );
	if ( crcs_for_hash != crcs_for_hash_buffer ) {
		FSC_Free( crcs_for_hash );
	}
}

/*
=================
FSC_GetPk3HashCallback
=================
*/
static void FSC_GetPk3HashCallback( void *context, char *data, int size ) {
	*(unsigned int *)context = FSC_BlockChecksum( data, size );
}

/*
=================
FSC_GetPk3HashRawPath

Calculates standard hash value from a pk3 file on disk.
=================
*/
unsigned int FSC_GetPk3HashRawPath( fsc_ospath_t *os_path ) {
	unsigned int result = 0;
	FSC_LoadPk3( os_path, FSC_NULL, FSC_SPNULL, FSC_GetPk3HashCallback, &result );
	return result;
}

/*
=================
FSC_GetPk3Hash

Standard string path wrapper for FSC_GetPk3HashRawPath.
=================
*/
unsigned int FSC_GetPk3Hash( const char *path ) {
	fsc_ospath_t *os_path = FSC_StringToOSPath( path );
	unsigned int result = FSC_GetPk3HashRawPath( os_path );
	FSC_Free( os_path );
	return result;
}

/*
###############################################################################################

PK3 Handle Operations

###############################################################################################
*/

struct fsc_pk3handle_s {
	fsc_filehandle_t *input_handle;
	int compression_method;
	unsigned int input_remaining;	// Remaining to be read from input handle

	// For zlib streams only
	unsigned int input_buffer_size;
	char *input_buffer;
	z_stream zlib_stream;
};

/*
=================
FSC_Pk3HandleLoad

Initializes a provided pk3 handle. Returns true on error, false otherwise.
=================
*/
static int FSC_Pk3HandleLoad( fsc_pk3handle_t *handle, const fsc_file_frompk3_t *file, int input_buffer_size, const fsc_filesystem_t *fs ) {
	const fsc_file_direct_t *source_pk3 = (const fsc_file_direct_t *)STACKPTR( file->source_pk3 );
	char localheader[30];
	unsigned int data_position;

	// Open the file
	handle->input_handle = FSC_FOpenRaw( (const fsc_ospath_t *)STACKPTR( source_pk3->os_path_ptr ), "rb" );
	if ( !handle->input_handle ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "pk3_handle_open - failed to open pk3 file", FSC_NULL );
		return fsc_true;
	}

	// Read the local header to get data position
	FSC_Pk3SeekSet( handle->input_handle, file->header_position );
	if ( FSC_FRead( localheader, 30, handle->input_handle ) != 30 ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "pk3_handle_open - failed to read local header", FSC_NULL );
		return fsc_true;
	}
	if ( localheader[0] != 0x50 || localheader[1] != 0x4b || localheader[2] != 0x03 || localheader[3] != 0x04 ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "pk3_handle_open - incorrect signature in local header", FSC_NULL );
		return fsc_true;
	}

	#define LH_SHORT( offset ) FSC_ConvertLittleEndianShort( *(unsigned short *)( localheader + offset ) )
	data_position = file->header_position + LH_SHORT( 26 ) + LH_SHORT( 28 ) + 30;

	// Seek to data start position
	FSC_Pk3SeekSet( handle->input_handle, data_position );

	// Configure the handle
	handle->input_remaining = file->compressed_size;
	if ( file->compression_method == 8 ) {
		if ( inflateInit2( &handle->zlib_stream, -MAX_WBITS ) != Z_OK ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "pk3_handle_open - zlib inflateInit failed", FSC_NULL );
			return fsc_true;
		}

		handle->compression_method = 8;
		handle->input_buffer_size = input_buffer_size;
		handle->input_buffer = (char *)FSC_Malloc( input_buffer_size );
	} else if ( file->compression_method != 0 ) {
		FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_EXTRACT, "pk3_handle_open - unknown compression method", FSC_NULL );
		return fsc_true;
	}

	return fsc_false;
}

/*
=================
FSC_Pk3HandleOpen

Returns handle on success, null on error.
=================
*/
fsc_pk3handle_t *FSC_Pk3HandleOpen( const fsc_file_frompk3_t *file, int input_buffer_size, const fsc_filesystem_t *fs ) {
	fsc_pk3handle_t *handle = (fsc_pk3handle_t *)FSC_Calloc( sizeof( *handle ) );

	if ( FSC_Pk3HandleLoad( handle, file, input_buffer_size, fs ) ) {
		if ( handle->input_handle ) {
			FSC_FClose( handle->input_handle );
		}
		FSC_Free( handle );
		return FSC_NULL;
	}

	return handle;
}

/*
=================
FSC_Pk3HandleClose
=================
*/
void FSC_Pk3HandleClose( fsc_pk3handle_t *handle ) {
	if ( handle->input_handle )
		FSC_FClose( handle->input_handle );

	if ( handle->compression_method == 8 ) {
		FSC_Free( handle->input_buffer );
		inflateEnd( &handle->zlib_stream );
	}

	FSC_Free( handle );
}

/*
=================
FSC_Pk3HandleRead

Returns number of bytes read.
=================
*/
unsigned int FSC_Pk3HandleRead( fsc_pk3handle_t *handle, char *buffer, unsigned int length ) {
	if ( handle->compression_method == 8 ) {
		handle->zlib_stream.next_out = (Bytef *)buffer;
		handle->zlib_stream.avail_out = length;

		while ( handle->zlib_stream.avail_out ) {
			if ( !handle->zlib_stream.avail_in ) {
				// Load new batch of data into input
				unsigned int feed_amount = handle->input_remaining;
				if ( feed_amount > handle->input_buffer_size ) {
					feed_amount = handle->input_buffer_size;
				}
				if ( !feed_amount ) {
					// Ran out of input
					break;
				}
				if ( FSC_FRead( handle->input_buffer, (int)feed_amount, handle->input_handle ) != feed_amount ) {
					break;
				}
				handle->zlib_stream.avail_in += feed_amount;
				handle->input_remaining -= feed_amount;
				handle->zlib_stream.next_in = (Bytef *)handle->input_buffer;
			}

			if ( inflate( &handle->zlib_stream, Z_SYNC_FLUSH ) != Z_OK ) {
				break;
			}
		}

		return length - handle->zlib_stream.avail_out;

	} else {
		return FSC_FRead( buffer, length, handle->input_handle );
	}
}

/*
###############################################################################################

PK3 Sourcetype Operations

###############################################################################################
*/

/*
=================
FSC_Pk3_IsFileActive
=================
*/
static fsc_boolean FSC_Pk3_IsFileActive( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	return FSC_GetBaseFile( file, fs )->refresh_count == fs->refresh_count ? fsc_true : fsc_false;
}

/*
=================
FSC_Pk3_GetModDir
=================
*/
static const char *FSC_Pk3_GetModDir( const fsc_file_t *file, const fsc_filesystem_t *fs ) {
	return (const char *)STACKPTR( FSC_GetBaseFile( file, fs )->qp_mod_ptr );
}

/*
=================
FSC_Pk3_ExtractData
=================
*/
static unsigned int FSC_Pk3_ExtractData( const fsc_file_t *file, char *buffer, const fsc_filesystem_t *fs ) {
	unsigned int result = 0;
	const fsc_file_frompk3_t *typedFile = (fsc_file_frompk3_t *)file;
	fsc_pk3handle_t *handle = FSC_Pk3HandleOpen( typedFile, typedFile->compressed_size, fs );
	if ( !handle ) {
		return 0;
	}

	result = FSC_Pk3HandleRead( handle, buffer, file->filesize );
	FSC_ASSERT( result <= file->filesize );

	FSC_Pk3HandleClose( handle );
	return result;
}

fsc_sourcetype_t pk3_sourcetype = {
	FSC_SOURCETYPE_PK3,
	FSC_Pk3_IsFileActive,
	FSC_Pk3_GetModDir,
	FSC_Pk3_ExtractData,
};

#endif	// NEW_FILESYSTEM
