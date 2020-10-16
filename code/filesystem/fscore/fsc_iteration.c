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

#define STACKPTR_LCL(pointer) ( FSC_STACK_RETRIEVE(stack, pointer, 0) )		// non-null, local stack parameter
#define STACKPTRN_LCL(pointer) ( FSC_STACK_RETRIEVE(stack, pointer, 1) )	// null allowed, local stack parameter

/* ******************************************************************************** */
// Directory iteration
/* ******************************************************************************** */

// This section is used to provide faster file iteration when starting at a certain
// directory, rather than iterating over the entire filesystem.

static int get_parent_qp_dir(const char *qp_dir, char *target) {
	// Converts qp_dir string to parent dir
	// In other words, strip everything including and after the last slash
	// Target should be size FSC_MAX_QPATH
	// Returns 0 if no parent directory, 1 otherwise
	const char *current = qp_dir;
	const char *last_slash = 0;
	unsigned int length;

	while(*current) {
		if(*current == '/') last_slash = current;
		++current; }

	// If no slash is found, parent directory is root directory. Leave output null.
	if(!last_slash) return 0;

	// Copy in everything up to the slash
	length = last_slash - qp_dir;
	if(length > FSC_MAX_QPATH - 1) length = FSC_MAX_QPATH - 1;
	fsc_memcpy(target, qp_dir, length);
	target[length] = 0;
	return 1; }

static fsc_stackptr_t get_directory(const char *qp_dir, fsc_hashtable_t *directories,
		fsc_hashtable_t *string_repository, fsc_stack_t *stack) {
	// Null qp_dir represents the root directory
	unsigned int qp_dir_hash = qp_dir ? fsc_string_hash(qp_dir, 0) : 0;

	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t directory_ptr;
	fsc_directory_t *directory;

	char parent_qp_dir[FSC_MAX_QPATH];
	int parent_path_found;
	fsc_stackptr_t parent_dir_ptr;
	fsc_directory_t *parent_dir;

	// Check if directory is already in the hash table
	fsc_hashtable_open(directories, qp_dir_hash, &hti);
	while((directory_ptr = fsc_hashtable_next(&hti))) {
		directory = (fsc_directory_t *)STACKPTR_LCL(directory_ptr);
		if(!directory->qp_dir_ptr) {
			if(!qp_dir) return directory_ptr;
			continue; }
		if(!qp_dir) continue;
		if(!fsc_stricmp((const char *)STACKPTR_LCL(directory->qp_dir_ptr), qp_dir)) return directory_ptr; }

	// It isn't, so create a new directory
	directory_ptr = fsc_stack_allocate(stack, sizeof(fsc_directory_t));
	directory = (fsc_directory_t *)STACKPTR_LCL(directory_ptr);
	directory->qp_dir_ptr = qp_dir ? fsc_string_repository_getstring(qp_dir, 1, string_repository, stack) : 0;
	fsc_hashtable_insert(directory_ptr, qp_dir_hash, directories);

	// If we are at the root directory, don't proceed any further
	if(!qp_dir) return directory_ptr;

	// Get the parent directory
	parent_path_found = get_parent_qp_dir(qp_dir, parent_qp_dir);
	parent_dir_ptr = get_directory(parent_path_found ? parent_qp_dir : 0, directories, string_repository, stack);
	parent_dir = (fsc_directory_t *)STACKPTR_LCL(parent_dir_ptr);

	// Add current directory to parent's sub_directory linked list
	directory->peer_directory = parent_dir->sub_directory;
	parent_dir->sub_directory = directory_ptr;

	return directory_ptr; }

void fsc_iteration_register_file(fsc_stackptr_t file_ptr, fsc_hashtable_t *directories,
		fsc_hashtable_t *string_repository, fsc_stack_t *stack) {
	fsc_file_t *file = (fsc_file_t *)STACKPTR_LCL(file_ptr);

	// Get directory
	fsc_stackptr_t directory_ptr = get_directory((const char *)STACKPTRN_LCL(file->qp_dir_ptr),
			directories, string_repository, stack);
	fsc_directory_t *directory = (fsc_directory_t *)STACKPTR_LCL(directory_ptr);

	// Add file to directory's sub_file linked list
	file->next_in_directory = directory->sub_file;
	directory->sub_file = file_ptr; }

/* ******************************************************************************** */
// Filesystem Iterators
/* ******************************************************************************** */

// Abstracted iterators for convenient filesystem access
// Only files that are enabled and match input criteria should be returned by these iterators

fsc_file_iterator_t fsc_file_iterator_open(fsc_filesystem_t *fs, const char *dir, const char *name) {
	// Open file iterator to iterate files matching a specific directory and name
	// 'dir' and 'name' pointers should remain valid throughout iteration
	fsc_file_iterator_t it;
	FSC_ASSERT(fs);
	FSC_ASSERT(name);
	fsc_memset(&it, 0, sizeof(it));

	fsc_hashtable_open(&fs->files, fsc_string_hash(name, dir), &it.hti);
	it.current_bucket = -1;
	it.fs = fs;
	it.dir = dir;
	it.name = name;
	return it; }

fsc_file_iterator_t fsc_file_iterator_open_all(fsc_filesystem_t *fs) {
	// Open file iterator to iterate all files in filesystem
	fsc_file_iterator_t it;
	FSC_ASSERT(fs);
	fsc_memset(&it, 0, sizeof(it));

	fsc_hashtable_open(&fs->files, 0, &it.hti);
	it.current_bucket = 0;
	it.fs = fs;
	return it; }

int fsc_file_iterator_advance(fsc_file_iterator_t *it) {
	// Returns 1 on success, 0 on end of iteration
	// Sets it->file and it->file_ptr on success
	FSC_ASSERT(it);

	while(1) {
		it->file_ptr = fsc_hashtable_next(&it->hti);
		if(it->file_ptr) {
			it->file = (fsc_file_t *)FSC_STACK_RETRIEVE(&it->fs->general_stack, it->file_ptr, 0);
			if(!fsc_is_file_enabled(it->file, it->fs)) continue;
			if(it->current_bucket == -1) {
				if(fsc_stricmp(FSC_STACK_RETRIEVE(&it->fs->general_stack, it->file->qp_name_ptr, 0), it->name)) continue;
				if(it->file->qp_dir_ptr && (!it->dir || fsc_stricmp(
						FSC_STACK_RETRIEVE(&it->fs->general_stack, it->file->qp_dir_ptr, 0), it->dir))) continue;
				if(!it->file->qp_dir_ptr && it->dir) continue; }
			return 1; }

		if(it->current_bucket >= 0 && it->current_bucket < it->fs->files.bucket_count) {
			fsc_hashtable_open(&it->fs->files, it->current_bucket++, &it->hti);
			continue; }

		it->file = 0;
		return 0; } }

fsc_pk3_iterator_t fsc_pk3_iterator_open(fsc_filesystem_t *fs, unsigned int hash) {
	// Open pk3 iterator to iterate pk3s matching specific hash
	fsc_pk3_iterator_t it;
	FSC_ASSERT(fs);
	fsc_memset(&it, 0, sizeof(it));

	fsc_hashtable_open(&fs->pk3_hash_lookup, hash, &it.hti);
	it.current_bucket = -1;
	it.fs = fs;
	it.hash = hash;
	return it; }

fsc_pk3_iterator_t fsc_pk3_iterator_open_all(fsc_filesystem_t *fs) {
	// Open pk3 iterator to iterate all pk3s in filesystem
	fsc_pk3_iterator_t it;
	FSC_ASSERT(fs);
	fsc_memset(&it, 0, sizeof(it));

	fsc_hashtable_open(&fs->pk3_hash_lookup, 0, &it.hti);
	it.current_bucket = 0;
	it.fs = fs;
	return it; }

int fsc_pk3_iterator_advance(fsc_pk3_iterator_t *it) {
	// Returns 1 on success, 0 on end of iteration
	// Sets it->pk3 and it->pk3_ptr on success
	FSC_ASSERT(it);

	while(1) {
		fsc_pk3_hash_map_entry_t *hashmap_entry = (fsc_pk3_hash_map_entry_t *)FSC_STACK_RETRIEVE(&it->fs->general_stack,
				fsc_hashtable_next(&it->hti), 1);
		if(hashmap_entry) {
			it->pk3_ptr = hashmap_entry->pk3;
			it->pk3 = (fsc_file_direct_t *)FSC_STACK_RETRIEVE(&it->fs->general_stack, it->pk3_ptr, 0);
			if(!fsc_is_file_enabled((fsc_file_t *)(it->pk3), it->fs)) continue;
			if(it->current_bucket == -1 && it->pk3->pk3_hash != it->hash) continue;
			return 1; }

		if(it->current_bucket >= 0 && it->current_bucket < it->fs->pk3_hash_lookup.bucket_count) {
			fsc_hashtable_open(&it->fs->pk3_hash_lookup, it->current_bucket++, &it->hti);
			continue; }

		it->pk3 = 0;
		it->pk3_ptr = 0;
		return 0; } }

fsc_shader_iterator_t fsc_shader_iterator_open(fsc_filesystem_t *fs, const char *name) {
	// Open shader iterator to iterate shaders matching a specific name
	// 'name' pointer should remain valid throughout iteration
	fsc_shader_iterator_t it;
	FSC_ASSERT(fs);
	FSC_ASSERT(name);
	fsc_memset(&it, 0, sizeof(it));

	fsc_hashtable_open(&fs->shaders, fsc_string_hash(name, 0), &it.hti);
	it.current_bucket = -1;
	it.fs = fs;
	it.name = name;
	return it; }

fsc_shader_iterator_t fsc_shader_iterator_open_all(fsc_filesystem_t *fs) {
	// Open shader iterator to iterate all shaders in filesystem
	fsc_shader_iterator_t it;
	FSC_ASSERT(fs);
	fsc_memset(&it, 0, sizeof(it));

	fsc_hashtable_open(&fs->shaders, 0, &it.hti);
	it.current_bucket = 0;
	it.fs = fs;
	return it; }

int fsc_shader_iterator_advance(fsc_shader_iterator_t *it) {
	// Returns 1 on success, 0 on end of iteration
	// Sets it->shader and it->shader_ptr on success
	const fsc_file_t *src_file;
	FSC_ASSERT(it);

	while(1) {
		it->shader_ptr = fsc_hashtable_next(&it->hti);
		if(it->shader_ptr) {
			it->shader = (fsc_shader_t *)FSC_STACK_RETRIEVE(&it->fs->general_stack, it->shader_ptr, 0);
			src_file = (const fsc_file_t *)FSC_STACK_RETRIEVE(&it->fs->general_stack, it->shader->source_file_ptr, 0);
			if(!fsc_is_file_enabled(src_file, it->fs)) continue;
			if(it->current_bucket == -1 &&
					fsc_stricmp(FSC_STACK_RETRIEVE(&it->fs->general_stack, it->shader->shader_name_ptr, 0), it->name)) continue;
			return 1; }

		if(it->current_bucket >= 0 && it->current_bucket < it->fs->shaders.bucket_count) {
			fsc_hashtable_open(&it->fs->shaders, it->current_bucket++, &it->hti);
			continue; }

		it->shader = 0;
		return 0; } }

#endif	// NEW_FILESYSTEM
