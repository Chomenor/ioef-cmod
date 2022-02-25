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

Directory Iteration

This system is used to provide faster file iteration when starting at a certain
directory, rather than iterating over the entire filesystem.

The primary purpose is to prevent lag when opening the player model menu when there
are very large numbers of models / pk3s installed, because the UI can make hundreds
of file list queries in succession while populating this menu.

###############################################################################################
*/

/*
=================
FSC_StripTrailingDirectory

Converts qp_dir string to parent dir, e.g. "abc/def/" converts to "abc/", "abc/" converts to "".
Source qp_dir MUST be non-empty and contain trailing slash. Target should be size FSC_MAX_QPATH.
=================
*/
static void FSC_StripTrailingDirectory( const char *qp_dir, char *target ) {
	const char *current = qp_dir;
	const char *end_slash = FSC_NULL;
	unsigned int length;
	FSC_ASSERT( qp_dir );
	FSC_ASSERT( *qp_dir );

	while ( 1 ) {
		if ( current[0] == '/' ) {
			if ( current[1] ) {
				end_slash = current;
			} else {
				break;
			}
		}

		// Path should only end with a slash (via above check)
		FSC_ASSERT( current[0] );
		++current;
	}

	// Copy path up to and including the ending slash
	length = end_slash ? end_slash - qp_dir + 1 : 0;
	FSC_ASSERT( length < FSC_MAX_QPATH );
	FSC_Memcpy( target, qp_dir, length );
	target[length] = '\0';
}

/*
=================
FSC_DirectoryForPath

Returns directory object in directories table corresponding to path, creating it if it doesn't already exist.
Input path must be either empty to represent root directory, or include a trailing slash, as per qpath directory conventions.
=================
*/
static fsc_stackptr_t FSC_DirectoryForPath( const char *qp_dir, fsc_hashtable_t *directories,
		fsc_hashtable_t *string_repository, fsc_stack_t *stack ) {
	unsigned int qp_dir_hash = FSC_StringHash( qp_dir, FSC_NULL );
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t directory_ptr;
	fsc_directory_t *directory;

	// Check if directory is already in the hash table
	FSC_HashtableIterateBegin( directories, qp_dir_hash, &hti );
	while ( ( directory_ptr = FSC_HashtableIterateNext( &hti ) ) ) {
		directory = (fsc_directory_t *)STACKPTR_LCL( directory_ptr );
		if ( !FSC_Stricmp( (const char *)STACKPTR_LCL( directory->qp_dir_ptr ), qp_dir ) ) {
			return directory_ptr;
		}
	}

	// It isn't, so create a new directory
	directory_ptr = FSC_StackAllocate( stack, sizeof( fsc_directory_t ) );
	directory = (fsc_directory_t *)STACKPTR_LCL( directory_ptr );
	directory->qp_dir_ptr = FSC_StringRepositoryGetString( qp_dir, string_repository );
	FSC_HashtableInsert( directory_ptr, qp_dir_hash, directories );

	// Link new directory to parent directory (unless already at root directory)
	if ( *qp_dir ) {
		char parent_qp_dir[FSC_MAX_QPATH];
		fsc_stackptr_t parent_dir_ptr;
		fsc_directory_t *parent_dir;

		// Get the parent directory
		FSC_StripTrailingDirectory( qp_dir, parent_qp_dir );
		parent_dir_ptr = FSC_DirectoryForPath( parent_qp_dir, directories, string_repository, stack );
		parent_dir = (fsc_directory_t *)STACKPTR_LCL( parent_dir_ptr );

		// Add current directory to parent's sub_directory linked list
		directory->peer_directory = parent_dir->sub_directory;
		parent_dir->sub_directory = directory_ptr;
	}

	return directory_ptr;
}

/*
=================
FSC_IterationRegisterFile

Adds a file to be visible to iteration system. Should only be called once per file.
=================
*/
void FSC_IterationRegisterFile( fsc_stackptr_t file_ptr, fsc_hashtable_t *directories,
		fsc_hashtable_t *string_repository, fsc_stack_t *stack ) {
	fsc_file_t *file = (fsc_file_t *)STACKPTR_LCL( file_ptr );

	// Get directory
	fsc_stackptr_t directory_ptr = FSC_DirectoryForPath( (const char *)STACKPTR_LCL( file->qp_dir_ptr ),
		directories, string_repository, stack );
	fsc_directory_t *directory = (fsc_directory_t *)STACKPTR_LCL( directory_ptr );

	// Add file to directory linked list
	file->next_in_directory = directory->sub_file;
	directory->sub_file = file_ptr;
}

/*
###############################################################################################

Filesystem Iterators

Abstracted iterators for convenient filesystem access.
Only files that are active and match input criteria should be returned by these iterators.

###############################################################################################
*/

/*
=================
FSC_FileIteratorOpen

Opens file iterator to iterate files matching a specific directory and name.
Input 'dir' and 'name' pointers should remain valid throughout iteration.
=================
*/
fsc_file_iterator_t FSC_FileIteratorOpen( fsc_filesystem_t *fs, const char *dir, const char *name ) {
	fsc_file_iterator_t it;
	FSC_ASSERT( fs );
	FSC_ASSERT( dir );
	FSC_ASSERT( name );
	FSC_Memset( &it, 0, sizeof( it ) );

	FSC_HashtableIterateBegin( &fs->files, FSC_StringHash( name, dir ), &it.hti );
	it.next_bucket = -1;
	it.fs = fs;
	it.dir = dir;
	it.name = name;
	return it;
}

/*
=================
FSC_FileIteratorOpenAll

Opens file iterator to iterate all files in filesystem.
=================
*/
fsc_file_iterator_t FSC_FileIteratorOpenAll( fsc_filesystem_t *fs ) {
	fsc_file_iterator_t it;
	FSC_ASSERT( fs );
	FSC_Memset( &it, 0, sizeof( it ) );

	FSC_HashtableIterateBegin( &fs->files, 0, &it.hti );
	it.next_bucket = 1;
	it.fs = fs;
	return it;
}

/*
=================
FSC_FileIteratorAdvance

Returns true on success, false on end of iteration.
Sets 'it->file' and 'it->file_ptr' on success.
=================
*/
fsc_boolean FSC_FileIteratorAdvance( fsc_file_iterator_t *it ) {
	FSC_ASSERT( it );

	while ( 1 ) {
		it->file_ptr = FSC_HashtableIterateNext( &it->hti );
		if ( it->file_ptr ) {
			it->file = (fsc_file_t *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->file_ptr, fsc_false );
			if ( !FSC_IsFileActive( it->file, it->fs ) ) {
				continue;
			}
			if ( it->next_bucket == -1 ) {
				// looking for a specific file, not a global iteration
				if ( FSC_Stricmp( (const char *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->file->qp_name_ptr, fsc_false ), it->name ) ) {
					continue;
				}
				if ( FSC_Stricmp( (const char *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->file->qp_dir_ptr, fsc_false ), it->dir ) ) {
					continue;
				}
			}
			return fsc_true;
		}

		if ( it->next_bucket >= 0 && it->next_bucket < it->fs->files.bucket_count ) {
			// global iteration, so go through all the buckets
			FSC_HashtableIterateBegin( &it->fs->files, it->next_bucket++, &it->hti );
			continue;
		}

		it->file = FSC_NULL;
		return fsc_false;
	}
}

/*
=================
FSC_Pk3IteratorOpen

Opens pk3 iterator to iterate pk3s matching specific hash.
=================
*/
fsc_pk3_iterator_t FSC_Pk3IteratorOpen( fsc_filesystem_t *fs, unsigned int hash ) {
	fsc_pk3_iterator_t it;
	FSC_ASSERT( fs );
	FSC_Memset( &it, 0, sizeof( it ) );

	FSC_HashtableIterateBegin( &fs->pk3_hash_lookup, hash, &it.hti );
	it.next_bucket = -1;
	it.fs = fs;
	it.hash = hash;
	return it;
}

/*
=================
FSC_Pk3IteratorOpenAll

Opens pk3 iterator to iterate all pk3s in the filesystem.
=================
*/
fsc_pk3_iterator_t FSC_Pk3IteratorOpenAll( fsc_filesystem_t *fs ) {
	fsc_pk3_iterator_t it;
	FSC_ASSERT( fs );
	FSC_Memset( &it, 0, sizeof( it ) );

	FSC_HashtableIterateBegin( &fs->pk3_hash_lookup, 0, &it.hti );
	it.next_bucket = 1;
	it.fs = fs;
	return it;
}

/*
=================
FSC_Pk3IteratorAdvance

Returns true on success, false on end of iteration.
Sets 'it->pk3' and 'it->pk3_ptr' on success.
=================
*/
fsc_boolean FSC_Pk3IteratorAdvance( fsc_pk3_iterator_t *it ) {
	FSC_ASSERT( it );

	while ( 1 ) {
		fsc_pk3_hash_map_entry_t *hashmap_entry = (fsc_pk3_hash_map_entry_t *)FSC_STACK_RETRIEVE(
				&it->fs->general_stack, FSC_HashtableIterateNext( &it->hti ), fsc_true );

		if ( hashmap_entry ) {
			it->pk3_ptr = hashmap_entry->pk3;
			it->pk3 = (fsc_file_direct_t *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->pk3_ptr, fsc_false );
			if ( !FSC_IsFileActive( (fsc_file_t *)( it->pk3 ), it->fs ) ) {
				continue;
			}
			// check if looking for a specific hash
			if ( it->next_bucket == -1 && it->pk3->pk3_hash != it->hash ) {
				continue;
			}
			return fsc_true;
		}

		if ( it->next_bucket >= 0 && it->next_bucket < it->fs->pk3_hash_lookup.bucket_count ) {
			// global iteration, so go through all the buckets
			FSC_HashtableIterateBegin( &it->fs->pk3_hash_lookup, it->next_bucket++, &it->hti );
			continue;
		}

		it->pk3 = FSC_NULL;
		it->pk3_ptr = FSC_SPNULL;
		return fsc_false;
	}
}

/*
=================
FSC_ShaderIteratorOpen

Opens shader iterator to iterate shaders matching a specific name.
Input 'name' pointer should remain valid throughout iteration.
=================
*/
fsc_shader_iterator_t FSC_ShaderIteratorOpen( fsc_filesystem_t *fs, const char *name ) {
	fsc_shader_iterator_t it;
	FSC_ASSERT( fs );
	FSC_ASSERT( name );
	FSC_Memset( &it, 0, sizeof( it ) );

	FSC_HashtableIterateBegin( &fs->shaders, FSC_StringHash( name, FSC_NULL ), &it.hti );
	it.next_bucket = -1;
	it.fs = fs;
	it.name = name;
	return it;
}

/*
=================
FSC_ShaderIteratorOpenAll

Opens shader iterator to iterate all shaders in filesystem.
=================
*/
fsc_shader_iterator_t FSC_ShaderIteratorOpenAll( fsc_filesystem_t *fs ) {
	fsc_shader_iterator_t it;
	FSC_ASSERT( fs );
	FSC_Memset( &it, 0, sizeof( it ) );

	FSC_HashtableIterateBegin( &fs->shaders, 0, &it.hti );
	it.next_bucket = 1;
	it.fs = fs;
	return it;
}

/*
=================
FSC_ShaderIteratorAdvance

Returns true on success, false on end of iteration.
Sets 'it->shader' and 'it->shader_ptr' on success.
=================
*/
fsc_boolean FSC_ShaderIteratorAdvance( fsc_shader_iterator_t *it ) {
	const fsc_file_t *src_file;
	FSC_ASSERT( it );

	while ( 1 ) {
		it->shader_ptr = FSC_HashtableIterateNext( &it->hti );
		if ( it->shader_ptr ) {
			it->shader = (const fsc_shader_t *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->shader_ptr, fsc_false );
			src_file = (const fsc_file_t *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->shader->source_file_ptr, fsc_false );
			if ( !FSC_IsFileActive( src_file, it->fs ) ) {
				continue;
			}
			// check if looking for a specific shader
			if ( it->next_bucket == -1 &&
					FSC_Stricmp( (const char *)FSC_STACK_RETRIEVE( &it->fs->general_stack, it->shader->shader_name_ptr, fsc_false ), it->name ) ) {
				continue;
			}
			return fsc_true;
		}

		if ( it->next_bucket >= 0 && it->next_bucket < it->fs->shaders.bucket_count ) {
			// global iteration, so go through all the buckets
			FSC_HashtableIterateBegin( &it->fs->shaders, it->next_bucket++, &it->hti );
			continue;
		}

		it->shader = FSC_NULL;
		return fsc_false;
	}
}

#endif	// NEW_FILESYSTEM
