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

#define STACKPTR(pointer) ( FSC_STACK_RETRIEVE(&fs->general_stack, pointer, 0) )	// non-null

/* ******************************************************************************** */
// Shader Indexing
/* ******************************************************************************** */

static int index_shader_file_data(fsc_filesystem_t *fs, fsc_stackptr_t source_file_ptr, char *data,
		fsc_sanity_limit_t *sanity_limit, fsc_errorhandler_t *eh) {
	// Returns number of shaders indexed from file.
	fsc_file_t *source_file = (fsc_file_t *)STACKPTR(source_file_ptr);
	int shader_count = 0;
	char *current_position = data;
	int prefix_tokens;
	char token[FSC_MAX_TOKEN_CHARS];

	char shader_name[FSC_MAX_SHADER_NAME];
	unsigned int shader_start_position;

	unsigned int hash;
	fsc_stackptr_t new_shader_ptr;
	fsc_shader_t *new_shader;

	while(1) {
		prefix_tokens = 0;

		while(1) {
			// Load next token
			shader_start_position = current_position - data;
			fsc_COM_ParseExt(token, &current_position, 1);
			if(!*token) {
				// We reached the end of the shader file.
				if(prefix_tokens) {
					fsc_report_error(eh, FSC_ERROR_SHADERFILE, "shader file has extra tokens at end", source_file); }
				return shader_count; }

			// Check for start of shader indicated by "{"
			if(token[0] == '{' && !token[1]) break;

			// Load potential shader name
			fsc_strncpy_lower(shader_name, token, sizeof(shader_name));

			++prefix_tokens; }

		if(!prefix_tokens) {
			fsc_report_error(eh, FSC_ERROR_SHADERFILE, "shader with no name", source_file);
			continue; }

		if(prefix_tokens > 1) {
			fsc_report_error(eh, FSC_ERROR_SHADERFILE, "shader with extra preceding tokens", source_file); }

		// Skip to the end of the shader
		if(fsc_SkipBracedSection(&current_position, 1)) {
			fsc_report_error(eh, FSC_ERROR_SHADERFILE, "shader with no closing brace", source_file);
			continue; }

		if(!sanity_limit || !fsc_sanity_limit(sizeof(fsc_shader_t) + fsc_strlen(shader_name),
				&sanity_limit->content_index_memory, sanity_limit, eh)) {
			// Allocate new shader
			++shader_count;
			new_shader_ptr = fsc_stack_allocate(&fs->general_stack, sizeof(fsc_shader_t));
			new_shader = (fsc_shader_t *)STACKPTR(new_shader_ptr);

			// Copy data to new shader
			new_shader->shader_name_ptr = fsc_string_repository_getstring(shader_name, 1, &fs->string_repository, &fs->general_stack);
			new_shader->source_file_ptr = source_file_ptr;
			new_shader->start_position = shader_start_position;
			new_shader->end_position = current_position - data;

			// Add shader to hash table
			hash = fsc_string_hash(shader_name, 0);
			fsc_hashtable_insert(new_shader_ptr, hash, &fs->shaders); } }

	return shader_count; }

int index_shader_file(fsc_filesystem_t *fs, fsc_stackptr_t source_file_ptr, fsc_sanity_limit_t *sanity_limit, fsc_errorhandler_t *eh) {
	// Returns number of shaders indexed from file.
	fsc_file_t *source_file = (fsc_file_t *)STACKPTR(source_file_ptr);
	unsigned int read_limit_size = source_file->filesize + 256 > source_file->filesize ? source_file->filesize + 256 : source_file->filesize;
	int shader_count = 0;

	if(!sanity_limit || !fsc_sanity_limit(read_limit_size, &sanity_limit->data_read, sanity_limit, eh)) {
		char *data = fsc_extract_file_allocated(fs, source_file, 0);
		if(!data) {
			fsc_report_error(eh, FSC_ERROR_SHADERFILE, "failed to read shader file", source_file);
			return 0; }

		shader_count = index_shader_file_data(fs, source_file_ptr, data, sanity_limit, eh);
		fsc_free(data); }

	return shader_count; }

/* ******************************************************************************** */
// Other
/* ******************************************************************************** */

int is_shader_enabled(fsc_filesystem_t *fs, const fsc_shader_t *shader) {
	return fsc_is_file_enabled((const fsc_file_t *)STACKPTR(shader->source_file_ptr), fs); }

#endif	// NEW_FILESYSTEM
