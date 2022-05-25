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

typedef struct {
	const char *extension;
	const char *filter;
	int flags;
} filelist_query_t;

typedef struct {
	const char *extension;
	const char *filter;
	int flags;

	int crop_length;
	fsc_stack_t temp_stack;

	// Depth is max number of slash-separated sections allowed in output
	// i.e. depth=0 suppresses any output, depth=1 allows "file" and "dir1/", depth=2 allows "dir1/file" and "dir1/dir2/"
	// Direct depth applies to files on disk (outside of pk3s), general applies to files in pk3s
	int general_file_depth;
	int general_directory_depth;
	int direct_file_depth;
	int direct_directory_depth;
} filelist_work_t;

// Treat pk3dirs the same as pk3s here
#define DIRECT_NON_PK3DIR( file ) ( file->sourcetype == FSC_SOURCETYPE_DIRECT && !( (fsc_file_direct_t *)file )->pk3dir_ptr )

/*
###############################################################################################

Sort key functions

###############################################################################################
*/

typedef struct {
	int length;
	// data appended to end of structure
} file_list_sort_key_t;

/*
=================
FS_FileList_GenerateSortKey
=================
*/
static file_list_sort_key_t *FS_FileList_GenerateSortKey( const fsc_file_t *file, fsc_stack_t *stack, const filelist_work_t *flw ) {
	char buffer[1024];
	fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );
	file_list_sort_key_t *key;

	FS_WriteCoreSortKey( file, &stream, ( flw->flags & LISTFLAG_IGNORE_PURE_LIST ) ? qfalse : qtrue );

	key = (file_list_sort_key_t *)FSC_STACK_RETRIEVE( stack, FSC_StackAllocate( stack, sizeof( *key ) + stream.position ), fsc_false );
	key->length = stream.position;
	FSC_Memcpy( (char *)key + sizeof( *key ), stream.data, stream.position );
	return key;
}

/*
=================
FS_FileList_CompareSortKey
=================
*/
static int FS_FileList_CompareSortKey( file_list_sort_key_t *key1, file_list_sort_key_t *key2 ) {
	return FSC_Memcmp( (char *)key2 + sizeof( *key2 ), (char *)key1 + sizeof( *key1 ),
			key1->length < key2->length ? key1->length : key2->length );
}

/*
###############################################################################################

String processing functions

###############################################################################################
*/

/*
=================
FS_FileList_PatternMatch

Returns qtrue if string matches pattern containing '*' and '?' wildcards.
Set initial_wildcard to qtrue to process pattern as if the first character was an asterisk.
=================
*/
static qboolean FS_FileList_PatternMatch( const char *string, const char *pattern, qboolean initial_wildcard ) {
	while ( 1 ) {
		if ( *pattern == '*' || initial_wildcard ) {
			// Skip asterisks; auto match if no pattern remaining
			char lwr, upr;
			while ( *pattern == '*' )
				++pattern;
			if ( !*pattern )
				return qtrue;

			// Get 'lwr' and 'upr' versions of next char in pattern for fast comparison
			lwr = tolower( *pattern );
			upr = toupper( *pattern );

			// Read string looking for match with remaining pattern
			while ( *string ) {
				if ( *string == lwr || *string == upr || *pattern == '?' ) {
					if ( FS_FileList_PatternMatch( string + 1, pattern + 1, qfalse ) )
						return qtrue;
				}
				++string;
			}

			// Leftover pattern with no match
			return qfalse;
		}

		// Check for end of string cases
		if ( !*pattern ) {
			if ( !*string )
				return qtrue;
			return qfalse;
		}
		if ( !*string )
			return qfalse;

		// Check for character discrepancy
		if ( *pattern != *string && *pattern != '?' && tolower( *pattern ) != tolower( *string ) )
			return qfalse;

		// Advance strings
		++pattern;
		++string;
	}
}

/*
=================
FS_FileList_NormalizeSeparators

Normalize OS-specific path separator content (like ./ or //) out of the path string.
=================
*/
static void FS_FileList_NormalizeSeparators( const char *source, char *target, int target_size ) {
	int target_index = 0;
	qboolean slash_mode = qfalse;

	while ( *source ) {
		char current = *( source++ );

		// Convert backslashes to forward slashes
		if ( current == '\\' )
			current = '/';

		// Defer writing slashes until a valid character is encountered
		if ( current == '/' ) {
			slash_mode = qtrue;
			continue;
		}

		// Ignore periods that are followed by slashes or end of string
		if ( current == '.' && ( *source == '/' || *source == '\\' || *source == '\0' ) )
			continue;

		// Write out deferred slashes unless at the beginning of path
		if ( slash_mode ) {
			slash_mode = qfalse;
			if ( target_index ) {
				if ( target_index + 2 >= target_size )
					break;
				target[target_index++] = '/';
			}
		}

		// Write character
		if ( target_index + 1 >= target_size )
			break;
		target[target_index++] = current;
	}

	target[target_index] = '\0';
}

/*
=================
FS_FileList_StripTrailingSlash

Removes single trailing slash from path; returns qtrue if found.
=================
*/
static qboolean FS_FileList_StripTrailingSlash( const char *source, char *target, int target_size ) {
	int length;
	Q_strncpyz( target, source, target_size );
	length = strlen( target );
	if ( length && ( target[length - 1] == '/' || target[length - 1] == '\\' ) ) {
		target[length - 1] = '\0';
		return qtrue;
	}
	return qfalse;
}

/*
###############################################################################################

File list generation

###############################################################################################
*/

typedef struct {
	fs_hashtable_entry_t hte;
	char *string;
	const fsc_file_t *file;
	qboolean directory;
	file_list_sort_key_t *file_sort_key;
} temp_file_set_entry_t;

/*
=================
FS_FileList_AllocateString

Allocate string using Z_Malloc, instead of the normal S_Malloc used by CopyString, to avoid overflows
when generating really large file lists.
=================
*/
static char *FS_FileList_AllocateString( const char *string ) {
	int length = strlen( string );
	char *copy = (char *)Z_Malloc( length + 1 );
	Com_Memcpy( copy, string, length );
	copy[length] = '\0';
	return copy;
}

/*
=================
FS_FileList_MaxFilesForCategory

Apply different cutoffs depending on source category, for better handling of overflow conditions.
=================
*/
static unsigned int FS_FileList_MaxFilesForCategory( const fsc_file_t *file ) {
	fs_modtype_t mod_type = FS_GetModType( FSC_GetModDir( file, &fs.index ) );
	const fsc_file_direct_t *base_file = FSC_GetBaseFile( file, &fs.index );
	unsigned int cutoff = 25000;

	if ( base_file && FS_CorePk3Position( base_file->pk3_hash ) )
		cutoff = 35000;
	if ( mod_type >= MODTYPE_CURRENT_MOD )
		cutoff = 30000;
	if ( mod_type < MODTYPE_BASE )
		cutoff = 20000;
	if ( base_file && base_file->f.flags & FSC_FILEFLAG_DLPK3 )
		cutoff = 15000;

	if ( file->sourcetype != FSC_SOURCETYPE_PK3 )
		cutoff += 2500;

	return cutoff;
}

/*
=================
FS_FileList_TempFileSetInsert

Inserts a path/file combination into the file set, displacing lower precedence entries if needed.
=================
*/
static void FS_FileList_TempFileSetInsert( fs_hashtable_t *ht, const fsc_file_t *file, const char *path,
		qboolean directory, file_list_sort_key_t **sort_key_ptr, filelist_work_t *flw ) {
	static qboolean cutoff_warned = qfalse;
	unsigned int hash = FSC_StringHash( path, NULL );
	fs_hashtable_iterator_t it = FS_Hashtable_Iterate( ht, hash, qfalse );
	temp_file_set_entry_t *entry;

	if ( ht->element_count >= FS_FileList_MaxFilesForCategory( file ) ) {
		if ( !cutoff_warned ) {
			Com_Printf( "^3WARNING: File list operation skipping files due to overflow.\n" );
		}
		cutoff_warned = qtrue;
		return;
	}

	// Generate sort key if one was not already created for this file
	if ( !*sort_key_ptr )
		*sort_key_ptr = FS_FileList_GenerateSortKey( file, &flw->temp_stack, flw );

	while ( ( entry = (temp_file_set_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		// Check if file is match
		if ( strcmp( path, entry->string ) ) {
			continue;
		}

		// Matching file found - replace it if new file is higher precedence
		if ( FS_FileList_CompareSortKey( *sort_key_ptr, entry->file_sort_key ) < 0 ) {
			entry->file = file;
			entry->directory = directory;
			entry->file_sort_key = *sort_key_ptr;
		}

		return;
	}

	// No matching file - create new entry
	entry = (temp_file_set_entry_t *)Z_Malloc( sizeof( *entry ) );
	entry->file = file;
	entry->directory = directory;
	entry->file_sort_key = *sort_key_ptr;
	entry->string = FS_FileList_AllocateString( path );
	FS_Hashtable_Insert( ht, &entry->hte, hash );
}

/*
=================
FS_FileList_CheckPathListable

Returns qtrue if path stored in stream matches filter/extension criteria, qfalse otherwise.
=================
*/
static qboolean FS_FileList_CheckPathListable( fsc_stream_t *stream, const filelist_work_t *flw ) {
	if ( !stream->position ) {
		return qfalse;
	}
	if ( flw->extension ) {
		if ( !FS_FileList_PatternMatch( stream->data, flw->extension, qtrue ) ) {
			return qfalse;
		}
	}
	if ( flw->filter ) {
		if ( !Com_FilterPath( (char *)flw->filter, stream->data, qfalse ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
=================
FS_FileList_CheckFileListable

Returns qtrue if file is valid to list, qfalse otherwise.
=================
*/
static qboolean FS_FileList_CheckFileListable( const fsc_file_t *file, const filelist_work_t *flw ) {
	int disabled_checks = FD_CHECK_LIST_INACTIVE_MODS;
	if ( !( flw->flags & LISTFLAG_IGNORE_PURE_LIST ) &&
			!( ( flw->flags & LISTFLAG_PURE_ALLOW_DIRECT_SOURCE ) && file->sourcetype == FSC_SOURCETYPE_DIRECT ) ) {
		disabled_checks |= FD_CHECK_PURE_LIST;
	}
	if ( FS_CheckFileDisabled( file, disabled_checks ) ) {
		return qfalse;
	}
	if ( file->sourcetype == FSC_SOURCETYPE_PK3 && FSC_GetBaseFile( file, &fs.index )->f.flags & FSC_FILEFLAG_NOLIST_PK3 ) {
		return qfalse;
	}
	if ( ( flw->flags & LISTFLAG_IGNORE_TAPAK0 ) && file->sourcetype == FSC_SOURCETYPE_PK3 &&
			FSC_GetBaseFile( file, &fs.index )->pk3_hash == 2430342401u ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
FS_FileList_CutStream

Reads string from source within the specified range (inclusive), and writes it to target.
=================
*/
static void FS_FileList_CutStream( const fsc_stream_t *source, fsc_stream_t *target, int start_pos, int end_pos ) {
	target->position = 0;
	if ( start_pos < end_pos ) {
		FSC_StreamWriteData( target, source->data + start_pos, end_pos - start_pos );
	}
	FSC_StreamAppendString( target, "" );
}

/*
=================
FS_FileList_TempFileSetPopulate

Recursively searches for files matching listing criteria and adds them to file set.
=================
*/
static void FS_FileList_TempFileSetPopulate( const fsc_directory_t *base, fs_hashtable_t *output, filelist_work_t *flw ) {
	char path_buffer[FS_FILE_BUFFER_SIZE];
	fsc_stream_t path_stream = FSC_InitStream( path_buffer, sizeof( path_buffer ) );
	char string_buffer[FS_FILE_BUFFER_SIZE];
	fsc_stream_t string_stream = FSC_InitStream( string_buffer, sizeof( string_buffer ) );
	fsc_file_t *file;
	fsc_directory_t *directory;

	file = (fsc_file_t *)STACKPTRN( base->sub_file );
	while ( file ) {
		if ( FSC_IsFileActive( file, &fs.index ) && FS_FileList_CheckFileListable( file, flw ) ) {
			int directory_depth = DIRECT_NON_PK3DIR( file ) ? flw->direct_directory_depth : flw->general_directory_depth;
			int file_depth = DIRECT_NON_PK3DIR( file ) ? flw->direct_file_depth : flw->general_file_depth;
			int i, j;
			file_list_sort_key_t *sort_key = NULL;
			int depth = 0;

			// Generate file and directory strings for each file, and call FS_FileList_TempFileSetInsert
			// For example, a file with post-crop_length string "abc/def/temp.txt" will generate:
			// - file string "abc/def/temp.txt" if file depth >= 3
			// - if the file is in a pk3, "abc/" if dir depth >= 1, and "abc/def/" if dir depth >= 2
			// - if file is on disk, ["abc", ".", ".."] if dir depth >= 1, ["abc/def", "abc/.", "abc/.."]
			//       if dir depth >= 2, and ["abc/def/.", "abc/def/.."] if dir depth >= 3

			path_stream.position = 0;
			FS_FileToStream( file, &path_stream, qfalse, qfalse, qfalse, qfalse );
			for ( i = flw->crop_length; i < path_stream.position; ++i ) {
				if ( path_stream.data[i] == '/' ) {
					depth++;
					if ( depth <= directory_depth ) {
						// Process directory
						FS_FileList_CutStream( &path_stream, &string_stream, flw->crop_length, i );
						// Include trailing slash unless directory is from disk, as per original filesystem behavior
						if ( !DIRECT_NON_PK3DIR( file ) ) {
							FSC_StreamAppendString( &string_stream, "/" );
						}
						if ( FS_FileList_CheckPathListable( &string_stream, flw ) ) {
							FS_FileList_TempFileSetInsert( output, file, string_stream.data, qtrue, &sort_key, flw );
						}
					}
				}

				// Generate "." and ".." entries for directories from disk
				if ( DIRECT_NON_PK3DIR( file ) && ( i == flw->crop_length || path_stream.data[i] == '/' ) && depth < directory_depth ) {
					FS_FileList_CutStream( &path_stream, &string_stream, flw->crop_length, i );
					if ( i != flw->crop_length ) {
						FSC_StreamAppendString( &string_stream, "/" );
					}
					for ( j = 0; j < 2; ++j ) {
						FSC_StreamAppendString( &string_stream, "." );
						if ( FS_FileList_CheckPathListable( &string_stream, flw ) ) {
							FS_FileList_TempFileSetInsert( output, file, string_stream.data, qtrue, &sort_key, flw );
						}
					}
				}
			}

			if ( depth < file_depth ) {
				// Process file
				FS_FileList_CutStream( &path_stream, &string_stream, flw->crop_length, path_stream.position );
				if ( FS_FileList_CheckPathListable( &string_stream, flw ) ) {
					FS_FileList_TempFileSetInsert( output, file, string_stream.data, qfalse, &sort_key, flw );
				}
			}
		}

		file = (fsc_file_t *)STACKPTRN( file->next_in_directory );
	}

	// Process subdirectories
	directory = (fsc_directory_t *)STACKPTRN( base->sub_directory );
	while ( directory ) {
		FS_FileList_TempFileSetPopulate( directory, output, flw );
		directory = (fsc_directory_t *)STACKPTRN( directory->peer_directory );
	}
}

/*
=================
FS_FileList_CompareString

Sorts file list results by the text of the path string. Only used as a final criteria
if the underlying files were sorted as equivalent.
=================
*/
static int FS_FileList_CompareString( const temp_file_set_entry_t *element1, const temp_file_set_entry_t *element2 ) {
	char buffer1[FS_FILE_BUFFER_SIZE];
	char buffer2[FS_FILE_BUFFER_SIZE];
	fsc_stream_t stream1 = FSC_InitStream( buffer1, sizeof( buffer1 ) );
	fsc_stream_t stream2 = FSC_InitStream( buffer2, sizeof( buffer2 ) );

	// Use shorter-path-first mode for sorting directories, as it is generally better and more consistent
	//    with original filesystem behavior
	FS_WriteSortString( element1->string, &stream1, element1->directory ? qtrue : qfalse );
	FS_WriteSortString( element2->string, &stream2, element2->directory ? qtrue : qfalse );
	return FSC_Memcmp( stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position );
}

/*
=================
FS_FileList_CompareElement

Comparator to sort elements in temporary file list structure.
=================
*/
static int FS_FileList_CompareElement( const temp_file_set_entry_t *element1, const temp_file_set_entry_t *element2 ) {
	if ( element1->file != element2->file ) {
		int sort_result = FS_FileList_CompareSortKey( element1->file_sort_key, element2->file_sort_key );
		if ( sort_result ) {
			return sort_result;
		}
	}
	return FS_FileList_CompareString( element1, element2 );
}

/*
=================
FS_FileList_CompareElementQsort
=================
*/
static int FS_FileList_CompareElementQsort( const void *element1, const void *element2 ) {
	return FS_FileList_CompareElement( *(const temp_file_set_entry_t **)element1,
			*(const temp_file_set_entry_t **)element2 );
}

/*
=================
FS_FileList_TempSetToList

Converts temporary file set structure to sorted file list structure.
=================
*/
static char **FS_FileList_TempSetToList( fs_hashtable_t *file_set, int *numfiles_out, filelist_work_t *flw ) {
	int i;
	fs_hashtable_iterator_t it;
	temp_file_set_entry_t *entry;
	int position = 0;
	char **output = (char **)Z_Malloc( sizeof( *output ) * ( file_set->element_count + 1 ) );
	temp_file_set_entry_t **temp_list = (temp_file_set_entry_t **)Z_Malloc( sizeof( *temp_list ) * file_set->element_count );

	// Transfer entries from file set hashtable to temporary list
	it = FS_Hashtable_Iterate( file_set, 0, qtrue );
	while ( ( entry = (temp_file_set_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		if ( position >= file_set->element_count ) {
			// shouldn't happen
			Com_Error( ERR_FATAL, "fs_filelist.c->FS_FileList_TempSetToList element_count overflow" );
		}
		temp_list[position++] = entry;
	}
	if ( position != file_set->element_count ) {
		// shouldn't happen
		Com_Error( ERR_FATAL, "fs_filelist.c->FS_FileList_TempSetToList element_count underflow" );
	}

	// Sort the list
	qsort( temp_list, file_set->element_count, sizeof( *temp_list ), FS_FileList_CompareElementQsort );

	// Transfer strings from list to output array
	for ( i = 0; i < file_set->element_count; ++i ) {
		output[i] = temp_list[i]->string;
	}
	output[i] = NULL;

	if ( numfiles_out ) {
		*numfiles_out = file_set->element_count;
	}
	Z_Free( temp_list );
	return output;
}

/*
=================
FS_FileList_GetStartDirectory

Get the directory entry for a given path in the fsc directory index system.
Path can be empty to start at base directory. Returns null if not found.
=================
*/
static fsc_directory_t *FS_FileList_GetStartDirectory( const char *path ) {
	char buffer[FSC_MAX_QPATH];
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t directory_ptr;
	fsc_directory_t *directory = NULL;

	// Add trailing slash to match fsc directory format
	if ( path && *path ) {
		Com_sprintf( buffer, sizeof( buffer ), "%s/", path );
	} else {
		buffer[0] = '\0';
	}

	// Look for directory entry
	FSC_HashtableIterateBegin( &fs.index.directories, FSC_StringHash( buffer, NULL ), &hti );
	while ( ( directory_ptr = FSC_HashtableIterateNext( &hti ) ) ) {
		directory = (fsc_directory_t *)STACKPTR( directory_ptr );
		if ( !Q_stricmp( (const char *)STACKPTR( directory->qp_dir_ptr ), buffer ) ) {
			break;
		}
	}

	if ( !directory_ptr ) {
		return NULL;
	}
	return directory;
}

/*
=================
FS_FileList_PrintDebugFlags
=================
*/
static void FS_FileList_PrintDebugFlags( int flags ) {
	if ( flags ) {
		char buffer[256];
		fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );
		const char *flag_strings[3] = { NULL };

		flag_strings[0] = ( flags & LISTFLAG_IGNORE_TAPAK0 ) ? "ignore_tapak0" : NULL;
		flag_strings[1] = ( flags & LISTFLAG_IGNORE_PURE_LIST ) ? "ignore_pure_list" : NULL;
		flag_strings[2] = ( flags & LISTFLAG_PURE_ALLOW_DIRECT_SOURCE ) ? "pure_allow_direct_source" : NULL;
		FS_CommaSeparatedList( flag_strings, ARRAY_LEN( flag_strings ), &stream );

		FS_DPrintf( "flags: %i (%s)\n", flags, buffer );
	} else {
		FS_DPrintf( "flags: <none>\n" );
	}
}

/*
=================
FS_FileList_GenerateList

Returns file list array for given input query.
=================
*/
static char **FS_FileList_GenerateList( const char *path, int *numfiles_out, filelist_query_t *query ) {
	char path_buffer1[FSC_MAX_QPATH];
	char path_buffer2[FSC_MAX_QPATH];
	fsc_directory_t *start_directory = NULL;
	fs_hashtable_t temp_file_set;
	filelist_work_t flw;
	char **result;
	int start_time = 0;
	int special_depth = 0;	// Account for certain depth-increasing quirks in original filesystem

	if ( fs.cvar.fs_debug_filelist->integer ) {
		start_time = Sys_Milliseconds();
		FS_DPrintf( "********** file list query **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "path: %s\n", path );
		FS_DPrintf( "extension: %s\n", query->extension );
		FS_DPrintf( "filter: %s\n", query->filter );
		FS_FileList_PrintDebugFlags( query->flags );
	}

	// Initialize temp structures
	Com_Memset( &flw, 0, sizeof( flw ) );
	flw.extension = query->extension;
	flw.filter = query->filter;
	flw.flags = query->flags;
	FSC_StackInitialize( &flw.temp_stack );
	FS_Hashtable_Initialize( &temp_file_set, 32768 );

	// Determine start directory
	if ( path ) {
		if ( FS_FileList_StripTrailingSlash( path, path_buffer1, sizeof( path_buffer1 ) ) ) {
			++special_depth;
		}
		FS_FileList_NormalizeSeparators( path_buffer1, path_buffer2, sizeof( path_buffer2 ) );
		if ( *path_buffer2 ) {
			start_directory = FS_FileList_GetStartDirectory( path_buffer2 );
		} else {
			start_directory = FS_FileList_GetStartDirectory( NULL );
			++special_depth;
		}
	}

	if ( start_directory ) {
		// Determine depths
		int extension_length;
		if ( flw.filter ) {
			// Unlimited depth in filter mode
			flw.general_file_depth = flw.general_directory_depth = 256;
			flw.direct_file_depth = flw.direct_directory_depth = 256;
		} else if ( !Q_stricmp( flw.extension, "/" ) ) {
			// This extension is handled specially by the original filesystem (via Sys_ListFiles)
			// Do a directory-only query, and skip the extension check because directories in this
			//    mode can be generated without the trailing slash
			flw.general_directory_depth = 1 + special_depth;
			flw.direct_directory_depth = 1;
			flw.extension = NULL;
		} else {
			// Roughly emulate original filesystem depth behavior
			flw.general_file_depth = 2 + special_depth;
			flw.general_directory_depth = 1 + special_depth;
			flw.direct_file_depth = 1;
		}

		// Optimization to skip processing path types blocked by extension anyway
		extension_length = flw.extension ? strlen( flw.extension ) : 0;
		if ( extension_length ) {
			if ( flw.extension[extension_length - 1] == '/' ) {
				flw.general_file_depth = flw.direct_file_depth = 0;
			} else if ( flw.extension[extension_length - 1] != '?' && flw.extension[extension_length - 1] != '*' ) {
				flw.general_directory_depth = flw.direct_directory_depth = 0;
			}
		}

		// Disable non-direct files when emulating OS-specific behavior that would restrict
		//    output to direct files on original filesystem
		// NOTE: Consider restricting general depths to match direct depths in these cases instead of
		//    disabling them entirely?
		if ( Q_stricmp( path_buffer1, path_buffer2 ) ) {
			if ( fs.cvar.fs_debug_filelist->integer ) {
				FS_DPrintf( "NOTE: Restricting to direct files only due to OS-specific"
						" path separator conversion: original(%s) converted(%s)\n",
						path_buffer1, path_buffer2 );
			}
			flw.general_file_depth = flw.general_directory_depth = 0;
		}
		if ( flw.extension && ( strchr( flw.extension, '*' ) || strchr( flw.extension, '?' ) ) ) {
			if ( fs.cvar.fs_debug_filelist->integer ) {
				FS_DPrintf( "NOTE: Restricting to direct files only due to OS-specific"
						" extension wildcards\n" );
			}
			flw.general_file_depth = flw.general_directory_depth = 0;
		}

		// Debug print depths
		if ( fs.cvar.fs_debug_filelist->integer ) {
			FS_DPrintf( "depths: gf(%i) gd(%i) df(%i) dd(%i)\n", flw.general_file_depth, flw.general_directory_depth,
					flw.direct_file_depth, flw.direct_directory_depth );
		}

		// Determine prefix length
		if ( !flw.filter ) {
			flw.crop_length = strlen( (const char *)STACKPTR( start_directory->qp_dir_ptr ) );
		}

		// Populate file set
		FS_FileList_TempFileSetPopulate( start_directory, &temp_file_set, &flw );
	} else if ( fs.cvar.fs_debug_filelist->integer ) {
		FS_DPrintf( "NOTE: Failed to match start directory.\n" );
	}

	// Generate file list
	result = FS_FileList_TempSetToList( &temp_file_set, numfiles_out, &flw );

	if ( fs.cvar.fs_debug_filelist->integer ) {
		FS_DPrintf( "result: %i elements\n", temp_file_set.element_count );
		FS_DPrintf( "temp stack usage: %u\n", FSC_StackExportSize( &flw.temp_stack ) );
		FS_DPrintf( "time: %i\n", Sys_Milliseconds() - start_time );
		FS_DebugIndentStop();
	}

	FS_Hashtable_Free( &temp_file_set, NULL );
	FSC_StackFree( &flw.temp_stack );
	return result;
}

/*
=================
FS_FreeFileList
=================
*/
void FS_FreeFileList( char **list ) {
	int i;
	if ( !list ) {
		return;
	}

	for ( i = 0; list[i]; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}

/*
###############################################################################################

Mod directory listing

###############################################################################################
*/

#define MAX_MOD_DIRS 128

typedef struct {
	char *mod_dirs[MAX_MOD_DIRS];
	int count;
} mod_dir_list_t;

/*
=================
FS_AddModDirToList
=================
*/
static void FS_AddModDirToList( mod_dir_list_t *list, const char *mod_dir ) {
	int i;
	if ( list->count >= MAX_MOD_DIRS ) {
		return;
	}
	for ( i = 0; i < list->count; ++i ) {
		if ( !Q_stricmp( list->mod_dirs[i], mod_dir ) ) {
			return;
		}
	}
	list->mod_dirs[list->count++] = CopyString( mod_dir );
}

/*
=================
FS_PopulateModDirList
=================
*/
static void FS_PopulateModDirList( fsc_directory_t *base, mod_dir_list_t *list ) {
	fsc_file_t *file;
	fsc_directory_t *directory;
	const char *last_mod_dir = NULL;

	file = (fsc_file_t *)STACKPTRN( base->sub_file );
	while ( file ) {
		if ( file->sourcetype == FSC_SOURCETYPE_DIRECT && FSC_IsFileActive( file, &fs.index ) ) {
			const char *mod_dir = FSC_GetModDir( file, &fs.index );
			// optimization to avoid calling FS_AddModDirToList over and over with same directory
			if ( mod_dir != last_mod_dir ) {
				FS_AddModDirToList( list, FSC_GetModDir( file, &fs.index ) );
				last_mod_dir = mod_dir;
			}
		}

		file = (fsc_file_t *)STACKPTRN( file->next_in_directory );
	}

	directory = (fsc_directory_t *)STACKPTRN( base->sub_directory );
	while ( directory ) {
		FS_PopulateModDirList( directory, list );
		directory = (fsc_directory_t *)STACKPTRN( directory->peer_directory );
	}
}

/*
=================
FS_ModDirListQsort
=================
*/
static int FS_ModDirListQsort( const void *element1, const void *element2 ) {
	return Q_stricmp( *(const char **)element1, *(const char **)element2 );
}

/*
=================
FS_GenerateModDirList
=================
*/
static void FS_GenerateModDirList( mod_dir_list_t *list ) {
	list->count = 0;
	FS_PopulateModDirList( FS_FileList_GetStartDirectory( NULL ), list );
	qsort( list->mod_dirs, list->count, sizeof( *list->mod_dirs ), FS_ModDirListQsort );
}

/*
=================
FS_FreeModDirList
=================
*/
static void FS_FreeModDirList( mod_dir_list_t *list ) {
	int i;
	for ( i = 0; i < list->count; ++i ) {
		Z_Free( list->mod_dirs[i] );
	}
}

/*
=================
FS_GetModList
=================
*/
static int FS_GetModList( char *listbuf, int bufsize ) {
	int i;
	int nTotal = 0; // Amount of buffer used so far
	int nMods = 0; // Number of mods
	mod_dir_list_t list;
	char description[49];
	int mod_name_length;
	int description_length;

	FS_GenerateModDirList( &list );

	for ( i = 0; i < list.count; ++i ) {
		char *mod_name = list.mod_dirs[i];

		// skip standard directories
		if ( !Q_stricmp( mod_name, com_basegame->string ) ) {
			continue;
		}
		if ( !Q_stricmp( mod_name, "basemod" ) ) {
			continue;
		}

		FS_GetModDescription( mod_name, description, sizeof( description ) );
		mod_name_length = strlen( mod_name ) + 1;
		description_length = strlen( description ) + 1;

		if ( nTotal + mod_name_length + description_length < bufsize ) {
			strcpy( listbuf, mod_name );
			listbuf += mod_name_length;
			strcpy( listbuf, description );
			listbuf += description_length;
			nTotal += mod_name_length + description_length;
			++nMods;
		}
	}

	FS_FreeModDirList( &list );
	return nMods;
}

/*
###############################################################################################

Exported functions

###############################################################################################
*/

/*
=================
FS_ListFilteredFiles_Flags

Result must be freed by FS_FreeFileList.
path, extension, filter, and numfiles_out may be null.
=================
*/
char **FS_ListFilteredFiles_Flags( const char *path, const char *extension,
		const char *filter, int *numfiles_out, int flags ) {
	filelist_query_t query = { extension, filter, flags };
	return FS_FileList_GenerateList( path, numfiles_out, &query );
}

/*
=================
FS_ListFiles

Result must be freed by FS_FreeFileList.
path, extension, and numfiles may be null.
=================
*/
char **FS_ListFiles( const char *path, const char *extension, int *numfiles ) {
	return FS_ListFilteredFiles_Flags( path, extension, NULL, numfiles, 0 );
}

/*
=================
FS_GetFileList

Returns file list in buffer format used by VMs.
path and extension may be null.
=================
*/
int FS_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
	int i;
	int flags = 0;
	int nFiles = 0;
	int nTotal = 0;
	int nLen;
	char **pFiles = NULL;
	FSC_ASSERT( listbuf );

	*listbuf = '\0';

	if ( !Q_stricmp( path, "$modlist" ) ) {
		return FS_GetModList( listbuf, bufsize );
	}

	if ( !Q_stricmp( path, "demos" ) ) {
		// Check for new demos before displaying the UI demo menu
		FS_AutoRefresh();
	}

	if ( !Q_stricmp( path, "models/players" ) && extension && !Q_stricmp( extension, "/" ) && Q_stricmp( fs.current_mod_dir, BASETA ) ) {
		// Special case to block missionpack pak0.pk3 models from the standard non-TA model list
		// which doesn't handle their skin setting correctly
		flags |= LISTFLAG_IGNORE_TAPAK0;
	}

	{
		filelist_query_t query = { extension, NULL, flags };
		pFiles = FS_FileList_GenerateList( path, &nFiles, &query );
	}

	for ( i = 0; i < nFiles; i++ ) {
		nLen = strlen( pFiles[i] ) + 1;
		if ( nTotal + nLen + 1 < bufsize ) {
			strcpy( listbuf, pFiles[i] );
			listbuf += nLen;
			nTotal += nLen;
		} else {
			nFiles = i;
			break;
		}
	}

	FS_FreeFileList( pFiles );
	return nFiles;
}

#endif	// NEW_FILESYSTEM
