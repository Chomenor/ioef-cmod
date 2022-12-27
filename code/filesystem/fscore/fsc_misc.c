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

#define STACKPTR_LCL( pointer ) ( FSC_STACK_RETRIEVE( stack, pointer, fsc_false ) ) // non-null, local stack parameter

/*
###############################################################################################

Data stream functions

###############################################################################################
*/

/*
=================
FSC_StreamReadData

Returns true on error, false on success.
=================
*/
fsc_boolean FSC_StreamReadData( fsc_stream_t *stream, void *output, unsigned int length ) {
	FSC_ASSERT( stream );
	FSC_ASSERT( output );
	if ( stream->position + length > stream->size || stream->position + length < stream->position ) {
		return fsc_true;
	}
	FSC_Memcpy( output, stream->data + stream->position, length );
	stream->position += length;
	return fsc_false;
}

/*
=================
FSC_StreamWriteData

Returns true on error, false on success.
=================
*/
fsc_boolean FSC_StreamWriteData( fsc_stream_t *stream, const void *data, unsigned int length ) {
	FSC_ASSERT( stream );
	FSC_ASSERT( data );
	if ( stream->position + length > stream->size || stream->position + length < stream->position ) {
		stream->overflowed = fsc_true;
		return fsc_true;
	}
	FSC_Memcpy( stream->data + stream->position, data, length );
	stream->position += length;
	return fsc_false;
}

/*
=================
FSC_StreamAppendStringSubstituted

Writes string to stream using character substitution table. If stream runs out of space, output is truncated.
Output will always be null terminated.
=================
*/
void FSC_StreamAppendStringSubstituted( fsc_stream_t *stream, const char *string, const char *substitution_table ) {
	FSC_ASSERT( stream );
	FSC_ASSERT( stream->size > 0 );
	if ( !string ) {
		string = "<null>";
	}

	while ( *string ) {
		if ( stream->position >= stream->size - 1 ) {
			stream->overflowed = fsc_true;
			break;
		}
		if ( substitution_table ) {
			stream->data[stream->position++] = substitution_table[*(unsigned char *)( string++ )];
		} else {
			stream->data[stream->position++] = *( string++ );
		}
	}

	if ( stream->position >= stream->size ) {
		stream->position = stream->size - 1;
	}
	stream->data[stream->position] = '\0';
}

/*
=================
FSC_StreamAppendString

Writes string to stream. If stream runs out of space, output is truncated.
Output will always be null terminated.
=================
*/
void FSC_StreamAppendString( fsc_stream_t *stream, const char *string ) {
	FSC_StreamAppendStringSubstituted( stream, string, FSC_NULL );
}

/*
=================
FSC_InitStream

Initialize an empty stream. Buffer size must be greater than 0.
=================
*/
fsc_stream_t FSC_InitStream( char *buffer, unsigned int bufSize ) {
	fsc_stream_t stream = { buffer, 0, bufSize, fsc_false };
	FSC_ASSERT( bufSize > 0 );
	*buffer = '\0';
	return stream;
}

/*
###############################################################################################

Filesystem 'stack' memory structure

The filesystem stack is a memory structure that allocates memory but never frees
anything without freeing the whole structure. It uses its own pointer format so
it can be written and read back from a file and keep using the same pointers.

###############################################################################################
*/

#define STACK_INITIAL_BUCKETS 16
#define STACK_BUCKET_POSITION_BITS 20	// Determines size of each bucket

#define STACK_BUCKET_ID_BITS (32 - STACK_BUCKET_POSITION_BITS)
#define STACK_MAX_BUCKETS (1 << STACK_BUCKET_ID_BITS)
#define STACK_BUCKET_SIZE (1 << STACK_BUCKET_POSITION_BITS)
#define STACK_BUCKET_DATA_SIZE (STACK_BUCKET_SIZE - sizeof(fsc_stack_bucket_t))

/*
=================
FSC_StackAddBucket
=================
*/
static void FSC_StackAddBucket( fsc_stack_t *stack ) {
	FSC_ASSERT( stack );
	stack->buckets_position++;
	FSC_ASSERT( stack->buckets_position >= 0 );
	FSC_ASSERT( stack->buckets_position < STACK_MAX_BUCKETS );

	// Resize the bucket array if necessary
	if ( stack->buckets_position >= stack->buckets_size ) {
		int new_size;
		fsc_stack_bucket_t **new_allocation;

		// Try to double the buckets allocation size
		new_size = stack->buckets_size * 2;
		if ( new_size > STACK_MAX_BUCKETS )
			new_size = STACK_MAX_BUCKETS;

		new_allocation = (fsc_stack_bucket_t **)FSC_Malloc( new_size * sizeof( fsc_stack_bucket_t * ) );
		FSC_Memcpy( new_allocation, stack->buckets, ( stack->buckets_position + 1 ) * sizeof( fsc_stack_bucket_t * ) );
		FSC_Free( stack->buckets );
		stack->buckets = new_allocation;
		stack->buckets_size = new_size;
	}

	// Stack allocations are assumed to be zeroed
	// so it needs to be accounted for if you change the allocation method here
	stack->buckets[stack->buckets_position] = (fsc_stack_bucket_t *)FSC_Calloc( STACK_BUCKET_SIZE );
	stack->buckets[stack->buckets_position]->position = 0;
}

/*
=================
FSC_StackInitialize
=================
*/
void FSC_StackInitialize( fsc_stack_t *stack ) {
	FSC_ASSERT( stack );
	stack->buckets_position = -1;	// FSC_StackAddBucket will increment to 0
	stack->buckets_size = STACK_INITIAL_BUCKETS;
	stack->buckets = (fsc_stack_bucket_t **)FSC_Malloc( stack->buckets_size * sizeof( fsc_stack_bucket_t * ) );
	FSC_StackAddBucket( stack );
}

/*
=================
FSC_StackRetrieve

Converts stackptr associated with a certain stack to normal pointer.
=================
*/
void *FSC_StackRetrieve( const fsc_stack_t *stack, const fsc_stackptr_t pointer, fsc_boolean allow_null,
			const char *caller, const char *expression ) {
	FSC_ASSERT( stack );
	if ( pointer ) {
		int bucket = pointer >> STACK_BUCKET_POSITION_BITS;
		unsigned int offset = pointer & ( ( 1 << STACK_BUCKET_POSITION_BITS ) - 1 );

		if ( bucket < 0 || bucket > stack->buckets_position || offset < sizeof( fsc_stack_bucket_t ) ||
				offset - sizeof( fsc_stack_bucket_t ) > stack->buckets[bucket]->position ) {
			FSC_FatalErrorTagged( "stackptr out of range", caller, expression );
		}

		return (void *)( (char *)stack->buckets[bucket] + offset );
	} else {
		if ( !allow_null ) {
			FSC_FatalErrorTagged( "unexpected null stackptr", caller, expression );
		}
		return FSC_NULL;
	}
}

/*
=================
FSC_StackAllocate

Allocates block of memory from given memory stack.
=================
*/
fsc_stackptr_t FSC_StackAllocate( fsc_stack_t *stack, unsigned int size ) {
	fsc_stack_bucket_t *bucket;
	unsigned int aligned_position;
	char *output;
	FSC_ASSERT( stack );

	bucket = stack->buckets[stack->buckets_position];
	aligned_position = ( bucket->position + 3 ) & ~3;

	// Add a new bucket if we are out of space
	FSC_ASSERT( size < STACK_BUCKET_DATA_SIZE );
	if ( size > STACK_BUCKET_DATA_SIZE - aligned_position ) {
		FSC_StackAddBucket( stack );
		bucket = stack->buckets[stack->buckets_position];
		aligned_position = ( bucket->position + 3 ) & ~3;
	}

	// Allocate the entry
	output = (char *)bucket + sizeof( *bucket ) + aligned_position;
	bucket->position = aligned_position + size;

	return stack->buckets_position * STACK_BUCKET_SIZE + (unsigned int)( output - (char *)bucket );
}

/*
=================
FSC_StackFree

Frees entire stack. Can be called on a nulled, freed, initialized, or in some cases partially initialized stack.
=================
*/
void FSC_StackFree( fsc_stack_t *stack ) {
	int i;
	FSC_ASSERT( stack );

	if ( stack->buckets ) {
		for ( i = 0; i <= stack->buckets_position; ++i ) {
			if ( stack->buckets[i] ) {
				FSC_Free( stack->buckets[i] );
			}
		}
		FSC_Free( stack->buckets );
		stack->buckets = FSC_NULL;
	}
}

/*
=================
FSC_StackExportSize

Returns the precise number of bytes required to export the stack via FSC_StackExport.
=================
*/
unsigned int FSC_StackExportSize( fsc_stack_t *stack ) {
	unsigned int size = 4; // 4 bytes for bucket count field
	int i;
	FSC_ASSERT( stack );

	// Then add the actual length of each bucket + 4 bytes for position field
	for ( i = 0; i <= stack->buckets_position; ++i ) {
		size += stack->buckets[i]->position + 4;
	}

	return size;
}

/*
=================
FSC_StackExport

Writes stack contents to stream. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_StackExport( fsc_stack_t *stack, fsc_stream_t *stream ) {
	int i;
	FSC_ASSERT( stack );
	FSC_ASSERT( stream );

	// Write the number of buckets
	if ( FSC_StreamWriteData( stream, &stack->buckets_position, 4 ) ) {
		return fsc_true;
	}

	// Write each bucket (current position followed by data)
	for ( i = 0; i <= stack->buckets_position; ++i ) {
		if ( FSC_StreamWriteData( stream, &stack->buckets[i]->position, 4 ) ) {
			return fsc_true;
		}
		if ( FSC_StreamWriteData( stream, (char *)stack->buckets[i] + sizeof( fsc_stack_bucket_t ),
				stack->buckets[i]->position ) ) {
			return fsc_true;
		}
	}

	return fsc_false;
}

/*
=================
FSC_StackImport

Imports stack from stream. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_StackImport( fsc_stack_t *stack, fsc_stream_t *stream ) {
	int i;
	FSC_ASSERT( stack );
	FSC_ASSERT( stream );

	// Read number of active buckets
	if ( FSC_StreamReadData( stream, &stack->buckets_position, 4 ) ) {
		return fsc_true;
	}
	if ( stack->buckets_position < 0 || stack->buckets_position >= STACK_MAX_BUCKETS ) {
		return fsc_true;
	}

	// Allocate bucket array
	stack->buckets_size = stack->buckets_position + 1;
	if ( stack->buckets_size < STACK_INITIAL_BUCKETS ) {
		stack->buckets_size = STACK_INITIAL_BUCKETS;
	}
	stack->buckets = (fsc_stack_bucket_t **)FSC_Calloc( stack->buckets_size * sizeof( fsc_stack_bucket_t * ) );

	// Read data for each bucket
	for ( i = 0; i <= stack->buckets_position; ++i ) {
		stack->buckets[i] = (fsc_stack_bucket_t *)FSC_Calloc( STACK_BUCKET_SIZE );
		if ( FSC_StreamReadData( stream, &stack->buckets[i]->position, 4 ) ) {
			goto error;
		}
		if ( stack->buckets[i]->position >= STACK_BUCKET_SIZE ) {
			goto error;
		}
		if ( FSC_StreamReadData( stream, (char *)stack->buckets[i] + sizeof( fsc_stack_bucket_t ),
				stack->buckets[i]->position ) ) {
			goto error;
		}
	}

	return fsc_false;

	error:
	FSC_StackFree( stack );
	return fsc_true;
}

/*
###############################################################################################

Stack-allocated hash table

This structure provides a standard hash table for filesystem operations. Each hashtable
is associated with a particular fsc_stack_t on initialization. If the associated stack
is freed the hashtable will no longer be valid.

Like the stack, entries can only be added to the hashtable and not removed, and it is
possible to export the hashtable to a file.

###############################################################################################
*/

/*
=================
FSC_HashtableInitialize
=================
*/
void FSC_HashtableInitialize( fsc_hashtable_t *ht, fsc_stack_t *stack, int bucket_count ) {
	if ( bucket_count < 1 ) {
		bucket_count = 1;
	}
	if ( bucket_count > FSC_HASHTABLE_MAX_BUCKETS ) {
		bucket_count = FSC_HASHTABLE_MAX_BUCKETS;
	}
	ht->bucket_count = bucket_count;
	ht->buckets = (fsc_stackptr_t *)FSC_Calloc( sizeof( fsc_stackptr_t ) * bucket_count );
	ht->utilization = 0;
	ht->stack = stack;
}

/*
=================
FSC_HashtableIterateBegin

Initializes iterator to begin iterating elements in the hashtable potentially matching hash.
=================
*/
void FSC_HashtableIterateBegin( fsc_hashtable_t *ht, unsigned int hash, fsc_hashtable_iterator_t *iterator ) {
	iterator->stack = ht->stack;
	iterator->next_ptr = &ht->buckets[hash % ht->bucket_count];
}

/*
=================
FSC_HashtableIterateNext

Retrieves next element in hashtable iteration. Returns null if no more elements are available.
=================
*/
fsc_stackptr_t FSC_HashtableIterateNext( fsc_hashtable_iterator_t *iterator ) {
	fsc_stackptr_t current = *iterator->next_ptr;
	if ( current ) {
		iterator->next_ptr = &( ( (fsc_hashtable_entry_t *)FSC_STACK_RETRIEVE( iterator->stack, current, fsc_false ) )->next );
	}
	return current;
}

/*
=================
FSC_HashtableInsert

Adds an element to hashtable. Element must be castable to fsc_hashtable_entry_t!
=================
*/
void FSC_HashtableInsert( fsc_stackptr_t entry_ptr, unsigned int hash, fsc_hashtable_t *ht ) {
	fsc_hashtable_entry_t *entry = (fsc_hashtable_entry_t *)FSC_STACK_RETRIEVE( ht->stack, entry_ptr, fsc_false );
	fsc_stackptr_t *bucket = &ht->buckets[hash % ht->bucket_count];
	entry->next = *bucket;
	*bucket = entry_ptr;
	++ht->utilization;
}

/*
=================
FSC_HashtableFree

Frees entire hashtable. Can be called on a nulled, freed, initialized, or in some cases partially initialized hashtable.
=================
*/
void FSC_HashtableFree( fsc_hashtable_t *ht ) {
	if ( ht->buckets ) {
		FSC_Free( ht->buckets );
	}
	ht->buckets = FSC_NULL;
}

/*
=================
FSC_HashtableExportSize

Returns the precise number of bytes required to export the hashtable via FSC_HashtableExport.
=================
*/
unsigned int FSC_HashtableExportSize( fsc_hashtable_t *ht ) {
	return 8 + ht->bucket_count * sizeof( *ht->buckets );
}

/*
=================
FSC_HashtableExport

Writes hashtable to stream. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_HashtableExport( fsc_hashtable_t *ht, fsc_stream_t *stream ) {
	if ( FSC_StreamWriteData( stream, &ht->bucket_count, 4 ) ) {
		return fsc_true;
	}
	if ( FSC_StreamWriteData( stream, &ht->utilization, 4 ) ) {
		return fsc_true;
	}
	if ( FSC_StreamWriteData( stream, ht->buckets, ht->bucket_count * sizeof( *ht->buckets ) ) ) {
		return fsc_true;
	}
	return fsc_false;
}

/*
=================
FSC_HashtableImport

Imports hashtable from stream. Returns true on error, false on success.
Stack parameter must be the same stack (or reimported equivalent) that the hashtable was originally created with.
=================
*/
fsc_boolean FSC_HashtableImport( fsc_hashtable_t *ht, fsc_stack_t *stack, fsc_stream_t *stream ) {
	if ( FSC_StreamReadData( stream, &ht->bucket_count, 4 ) ) {
		return fsc_true;
	}
	if ( ht->bucket_count < 1 || ht->bucket_count > FSC_HASHTABLE_MAX_BUCKETS ) {
		return fsc_true;
	}
	if ( FSC_StreamReadData( stream, &ht->utilization, 4 ) ) {
		return fsc_true;
	}
	ht->buckets = (fsc_stackptr_t *)FSC_Malloc( ht->bucket_count * sizeof( *ht->buckets ) );
	if ( FSC_StreamReadData( stream, ht->buckets, ht->bucket_count * sizeof( *ht->buckets ) ) ) {
		FSC_HashtableFree( ht );
		return fsc_true;
	}
	ht->stack = stack;
	return fsc_false;
}

/*
###############################################################################################

Qpath Handling

###############################################################################################
*/

/*
=================
FSC_SplitQpath

Splits input path into qpath directory + name + extension format.
Assumes input is size FSC_MAX_QPATH; larger inputs are truncated.
'\' separators are replaced with '/' for consistency.
If ignore_extension set, output extension will be empty, and any extension-like text
	will be included in name instead.
=================
*/
void FSC_SplitQpath( const char *input, fsc_qpath_buffer_t *output, fsc_boolean ignore_extension ) {
	int i;
	int slash_pos = -1;
	int period_pos = -1;
	int input_len;
	int name_index;
	int ext_index;
	fsc_stream_t stream;

	FSC_ASSERT( input );
	FSC_ASSERT( output );
	stream = FSC_InitStream( output->buffer, sizeof( output->buffer ) );

	// Get slash_pos and period_pos
	for ( i = 0; i < FSC_MAX_QPATH - 1; ++i ) {
		if ( !input[i] )
			break;
		if ( input[i] == '\\' || input[i] == '/' ) {
			slash_pos = i;
			period_pos = -1;
		}
		if ( !ignore_extension && input[i] == '.' ) {
			period_pos = i;
		}
	}

	input_len = i;
	name_index = slash_pos + 1;
	ext_index = period_pos >= 0 ? period_pos : input_len;

	// Write directory
	output->dir = &output->buffer[stream.position];
	FSC_StreamWriteData( &stream, input, name_index );
	FSC_StreamWriteData( &stream, "", 1 );

	// Write name
	output->name = &output->buffer[stream.position];
	FSC_StreamWriteData( &stream, input + name_index, ext_index - name_index );
	FSC_StreamWriteData( &stream, "", 1 );

	// Write extension
	output->ext = &output->buffer[stream.position];
	FSC_StreamWriteData( &stream, input + ext_index, input_len - ext_index );
	FSC_StreamWriteData( &stream, "", 1 );

	FSC_ASSERT( !stream.overflowed );

	// Convert slashes
	for ( i = 0; i < (int)stream.position; ++i ) {
		if ( stream.data[i] == '\\' ) {
			stream.data[i] = '/';
		}
	}
}

/*
=================
FSC_SplitLeadingDirectory

Writes leading directory (text before first slash) to buffer, truncating on overflow.
Writes pointer to remaining (post-slash) string to remainder, or null if not found.
Returns total number of chars in leading directory, without truncation, not counting null terminator.
If (return value >= buffer_length) output was truncated.
=================
*/
unsigned int FSC_SplitLeadingDirectory( const char *input, char *buffer, unsigned int buffer_length, const char **remainder ) {
	unsigned int i;
	unsigned int chars_written;
	FSC_ASSERT( input );
	FSC_ASSERT( buffer );
	FSC_ASSERT( buffer_length > 0 );

	// Start with null remainder
	if ( remainder ) {
		*remainder = FSC_NULL;
	}

	// Write buffer and remainder (if slash encountered)
	for ( i = 0; input[i]; ++i ) {
		if ( input[i] == '/' || input[i] == '\\' ) {
			if ( remainder )
				*remainder = (char *)( input + i + 1 );
			break;
		}
		if ( i < buffer_length ) {
			buffer[i] = input[i];
		}
	}

	chars_written = i < buffer_length ? i : buffer_length - 1;
	buffer[chars_written] = '\0';
	return i;
}

/*
###############################################################################################

Sanity Limits

###############################################################################################
*/

/*
=================
FSC_SanityLimitContent

Applies some limits to prevent potential vulnerabilities due to overloaded pk3 files.
Returns true if limit hit, otherwise decrements limit counter and returns false.
=================
*/
fsc_boolean FSC_SanityLimitContent( unsigned int size, unsigned int *limit_value, fsc_sanity_limit_t *sanity_limit ) {
	FSC_ASSERT( sanity_limit );
	FSC_ASSERT( limit_value );

	if ( *limit_value < size ) {
		if ( !sanity_limit->warned ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "pk3 content dropped due to sanity limits",
					(void *)sanity_limit->pk3file );
			sanity_limit->warned = fsc_true;
		}

		return fsc_true;
	}

	*limit_value -= size;
	return fsc_false;
}

/*
=================
FSC_SanityLimitHash

Apply limit to prevent exploits involving tons of files or shaders with the same hash.
Returns true if limit hit, otherwise decrements limit counter and returns false.
=================
*/
fsc_boolean FSC_SanityLimitHash( unsigned int hash, fsc_sanity_limit_t *sanity_limit ) {
	unsigned char *bucket;
	FSC_ASSERT( sanity_limit );

	bucket = &sanity_limit->hash_buckets[hash % FSC_SANITY_HASH_BUCKETS];
	if ( *bucket >= FSC_SANITY_MAX_PER_HASH_BUCKET ) {
		if ( !sanity_limit->warned ) {
			FSC_ReportError( FSC_ERRORLEVEL_WARNING, FSC_ERROR_PK3FILE, "pk3 content dropped due to hash sanity limits",
					(void *)sanity_limit->pk3file );
			sanity_limit->warned = fsc_true;
		}

		return fsc_true;
	}

	*bucket += 1;
	return fsc_false;
}

/*
###############################################################################################

Error Handling

###############################################################################################
*/

static fsc_error_handler_t fsc_error_handler = FSC_NULL;

/*
=================
FSC_ReportError

Calls handler to process an error or warning event.
=================
*/
void FSC_ReportError( fsc_error_level_t level, fsc_error_category_t category, const char *msg, void *element ) {
	if ( fsc_error_handler ) {
		fsc_error_handler( level, category, msg, element );
	}

	if ( level == FSC_ERRORLEVEL_FATAL ) {
		// If the main error handler didn't trigger a jump, abort the appliation now.
		FSC_ErrorAbort( msg );
	}
}

/*
=================
FSC_RegisterErrorHandler

Registers a function to call in the event an error or warning is encountered.
=================
*/
void FSC_RegisterErrorHandler( fsc_error_handler_t handler ) {
	fsc_error_handler = handler;
}

/*
=================
FSC_FatalErrorTagged

Calls fatal error handler with calling function and expression logging parameters.
=================
*/
void FSC_FatalErrorTagged( const char *msg, const char *caller, const char *expression ) {
	char buffer[1024];
	fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );
	FSC_StreamAppendString( &stream, msg );
	FSC_StreamAppendString( &stream, " - function(" );
	FSC_StreamAppendString( &stream, caller );
	FSC_StreamAppendString( &stream, ") expression(" );
	FSC_StreamAppendString( &stream, expression );
	FSC_StreamAppendString( &stream, ")" );
	FSC_ReportError( FSC_ERRORLEVEL_FATAL, FSC_ERROR_GENERAL, buffer, FSC_NULL );
}

/*
###############################################################################################

Misc

###############################################################################################
*/

/*
=================
FSC_StringHash

Hash function which only processes alphanumeric characters, so any symbol sanitizing routines don't change hash.
=================
*/
unsigned int FSC_StringHash( const char *input1, const char *input2 ) {
	unsigned int hash = 5381;
	int c;

	if ( input1 )
		while ( ( c = *(unsigned char *)input1++ ) ) {
			if ( c >= 'A' && c <= 'Z' )
				c += 'a' - 'A';
			if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) )
				hash = ( ( hash << 5 ) + hash ) + c;
		}
	if ( input2 )
		while ( ( c = *(unsigned char *)input2++ ) ) {
			if ( c >= 'A' && c <= 'Z' )
				c += 'a' - 'A';
			if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) )
				hash = ( ( hash << 5 ) + hash ) + c;
		}

	return hash;
}

/*
=================
FSC_MemoryUseEstimate

Returns an estimate of memory used by the filesystem suitable for debug print purposes.
=================
*/
unsigned int FSC_MemoryUseEstimate( fsc_filesystem_t *fs ) {
	return FSC_StackExportSize( &fs->general_stack ) +
		FSC_HashtableExportSize( &fs->string_repository ) +
		FSC_HashtableExportSize( &fs->files ) +
		FSC_HashtableExportSize( &fs->directories ) +
		FSC_HashtableExportSize( &fs->shaders ) +
		FSC_HashtableExportSize( &fs->crosshairs ) +
		FSC_HashtableExportSize( &fs->pk3_hash_lookup );
}

/*
=================
FSC_StringRepositoryGetString

Allocates string in stack in deduplicated fashion so the same string is stored only once.
=================
*/
fsc_stackptr_t FSC_StringRepositoryGetString( const char *input, fsc_hashtable_t *string_repository ) {
	unsigned int hash = FSC_StringHash( input, FSC_NULL );
	fsc_stack_t *stack = string_repository->stack;
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t entry_ptr;
	fsc_hashtable_entry_t *entry;

	// Look for existing entry
	FSC_HashtableIterateBegin( string_repository, hash, &hti );
	while ( ( entry_ptr = FSC_HashtableIterateNext( &hti ) ) ) {
		entry = (fsc_hashtable_entry_t *)STACKPTR_LCL( entry_ptr );
		if ( !FSC_Strcmp( (char *)entry + sizeof( *entry ), input ) ) {
			break;
		}
	}

	if ( !entry_ptr ) {
		// Allocate new entry
		int length = FSC_Strlen( input ) + 1; // Include null terminator
		entry_ptr = FSC_StackAllocate( stack, sizeof( fsc_hashtable_entry_t ) + length );
		entry = (fsc_hashtable_entry_t *)STACKPTR_LCL( entry_ptr );
		FSC_Memcpy( (char *)entry + sizeof( *entry ), input, length );
		FSC_HashtableInsert( entry_ptr, hash, string_repository );
	}

	// Return string
	return entry_ptr + sizeof( fsc_hashtable_entry_t );
}

#endif	// NEW_FILESYSTEM
