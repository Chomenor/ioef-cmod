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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef CMOD_COMMON_STRING_FUNCTIONS
void cmod_stream_append_string(cmod_stream_t *stream, const char *string) {
	// If stream runs out of space, output is truncated.
	// Non-zero size stream will always be null terminated.
	if(stream->position >= stream->size) {
		if(stream->size) stream->data[stream->size-1] = 0;
		stream->overflowed = qtrue;
		return; }
	if(string) while(*string) {
		if(stream->position >= stream->size-1) {
			stream->overflowed = qtrue;
			break; }
		stream->data[stream->position++] = *(string++); }
	stream->data[stream->position] = 0; }

void cmod_stream_append_string_separated(cmod_stream_t *stream, const char *string, const char *separator) {
	// Appends string, adding separator prefix if both stream and input are non-empty
	if(stream->position && string && *string) cmod_stream_append_string(stream, separator);
	cmod_stream_append_string(stream, string); }

#define IS_WHITESPACE(chr) ((chr) == ' ' || (chr) == '\t' || (chr) == '\n' || (chr) == '\r')

unsigned int cmod_read_token(const char **current, char *buffer, unsigned int buffer_size, char delimiter) {
	// Returns number of characters read to output buffer (not including null terminator)
	// Null delimiter uses any whitespace as delimiter
	// Any leading and trailing whitespace characters will be skipped
	unsigned int char_count = 0;
	if(!buffer_size) return 0;

	// Skip leading whitespace
	while(**current && IS_WHITESPACE(**current)) ++(*current);

	// Read item to buffer
	while(**current && **current != delimiter) {
		if(!delimiter && IS_WHITESPACE(**current)) break;
		if(char_count < buffer_size - 1) {
			buffer[char_count++] = **current; }
		++(*current); }

	// Skip input delimiter and trailing whitespace
	if(**current) ++(*current);
	while(**current && IS_WHITESPACE(**current)) ++(*current);

	// Skip output trailing whitespace
	while(char_count && IS_WHITESPACE(buffer[char_count-1])) --char_count;

	// Add null terminator
	buffer[char_count] = 0;
	return char_count; }

unsigned int cmod_read_token_ws(const char **current, char *buffer, unsigned int buffer_size) {
	// Reads whitespace-separated token from current to buffer, and advances current pointer
	// Returns number of characters read to buffer (not including null terminator, 0 if no data remaining)
	return cmod_read_token(current, buffer, buffer_size, 0); }
#endif

#ifdef CMOD_VM_STRNCPY_FIX
// Simple strncpy function to avoid overlap check issues with some library implementations
void vm_strncpy(char *dst, char *src, int length) {
	int i;
	for(i=0; i<length; ++i) {
		dst[i] = src[i];
		if(!src[i]) break; }
	for(; i<length; ++i) {
		dst[i] = 0; } }
#endif
