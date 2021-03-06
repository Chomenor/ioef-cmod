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

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

unsigned int fsc_string_hash(const char *input1, const char *input2) {
	// Only processes alphanumeric characters, so any symbol sanitizing routines don't change hash
	unsigned int hash = 5381;
	int c;

	if(input1) while ((c = *(unsigned char *)input1++)) {
		if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) hash = ((hash << 5) + hash) + c; }
	if(input2) while ((c = *(unsigned char *)input2++)) {
		if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) hash = ((hash << 5) + hash) + c; }

	return hash; }

unsigned int fsc_fs_size_estimate(fsc_filesystem_t *fs) {
	return fsc_stack_get_export_size(&fs->general_stack) +
		fsc_hashtable_get_export_size(&fs->string_repository) +
		fsc_hashtable_get_export_size(&fs->files) +
		fsc_hashtable_get_export_size(&fs->directories) +
		fsc_hashtable_get_export_size(&fs->shaders) +
		fsc_hashtable_get_export_size(&fs->crosshairs) +
		fsc_hashtable_get_export_size(&fs->pk3_hash_lookup); }

/* ******************************************************************************** */
// Data Stream
/* ******************************************************************************** */

int fsc_read_stream_data(fsc_stream_t *stream, void *output, unsigned int length) {
	// Returns 1 on error, 0 on success.
	FSC_ASSERT(stream);
	FSC_ASSERT(output);
	if(stream->position + length > stream->size || stream->position + length < stream->position) return 1;
	fsc_memcpy(output, stream->data + stream->position, length);
	stream->position += length;
	return 0; }

int fsc_write_stream_data(fsc_stream_t *stream, void *data, unsigned int length) {
	// Returns 1 on error, 0 on success.
	FSC_ASSERT(stream);
	FSC_ASSERT(data);
	if(stream->position + length > stream->size || stream->position + length < stream->position) return 1;
	fsc_memcpy(stream->data + stream->position, data, length);
	stream->position += length;
	return 0; }

void fsc_stream_append_string_substituted(fsc_stream_t *stream, const char *string, const char *substitution_table) {
	// Writes string to stream using character substitution table.
	// If stream runs out of space, output is truncated.
	// Stream data will always be null terminated.
	FSC_ASSERT(stream);
	FSC_ASSERT(stream->size > 0);
	if(!string) string = "<null>";
	while(*string) {
		if(stream->position >= stream->size-1) {
			stream->overflowed = 1;
			break; }
		if(substitution_table) {
			stream->data[stream->position++] = substitution_table[*(unsigned char *)(string++)]; }
		else {
			stream->data[stream->position++] = *(string++); } }
	if(stream->position >= stream->size) stream->position = stream->size - 1;
	stream->data[stream->position] = 0; }

void fsc_stream_append_string(fsc_stream_t *stream, const char *string) {
	// Writes string to stream.
	// If stream runs out of space, output is truncated.
	// Stream data will always be null terminated.
	fsc_stream_append_string_substituted(stream, string, 0); }

/* ******************************************************************************** */
// Standard Stack
/* ******************************************************************************** */

// This "stack" structure isn't really a stack in the technical sense, I just called
//    it that while I was still figuring out how it would work and then didn't want
//    to change the name everywhere
// Basically this is a memory structure that allocates memory but never frees anything
//    without freeing the whole structure. It also uses its own pointer format so it
//    can be written and read back from a file and keep using the same pointers.

#define STACK_INITIAL_BUCKETS 16
#define STACK_BUCKET_POSITION_BITS 20	// Determines size of each bucket

#define STACK_BUCKET_ID_BITS (32 - STACK_BUCKET_POSITION_BITS)
#define STACK_MAX_BUCKETS (1 << STACK_BUCKET_ID_BITS)
#define STACK_BUCKET_SIZE (1 << STACK_BUCKET_POSITION_BITS)
#define STACK_BUCKET_DATA_SIZE (STACK_BUCKET_SIZE - sizeof(fsc_stack_bucket_t))

static void stack_add_bucket(fsc_stack_t *stack) {
	stack->buckets_position++;
	FSC_ASSERT(stack->buckets_position >= 0);
	FSC_ASSERT(stack->buckets_position < STACK_MAX_BUCKETS);

	// Resize the bucket array if necessary
	if(stack->buckets_position >= stack->buckets_size) {
		int new_size;
		fsc_stack_bucket_t **new_allocation;

		// Try to double the buckets allocation size
		new_size = stack->buckets_size * 2;
		if(new_size > STACK_MAX_BUCKETS) new_size = STACK_MAX_BUCKETS;

		new_allocation = (fsc_stack_bucket_t **)fsc_malloc(new_size * sizeof(fsc_stack_bucket_t *));
		fsc_memcpy(new_allocation, stack->buckets, (stack->buckets_position+1)*sizeof(fsc_stack_bucket_t *));
		fsc_free(stack->buckets);
		stack->buckets = new_allocation;
		stack->buckets_size = new_size; }

	// Stack allocations are assumed to be zeroed
	// so it needs to be accounted for if you change the allocation method here
	stack->buckets[stack->buckets_position] = (fsc_stack_bucket_t *)fsc_calloc(STACK_BUCKET_SIZE);
	stack->buckets[stack->buckets_position]->position = 0; }

void fsc_stack_initialize(fsc_stack_t *stack) {
	stack->buckets_position = -1;	// stack_add_bucket will increment to 0
	stack->buckets_size = STACK_INITIAL_BUCKETS;
	stack->buckets = (fsc_stack_bucket_t **)fsc_malloc(stack->buckets_size * sizeof(fsc_stack_bucket_t *));
	stack_add_bucket(stack); }

void *fsc_stack_retrieve(const fsc_stack_t *stack, const fsc_stackptr_t pointer, int allow_null,
			const char *caller, const char *expression) {
	// Converts stackptr for a given stack to actual pointer
	if(pointer) {
		int bucket = pointer >> STACK_BUCKET_POSITION_BITS;
		unsigned int offset = pointer & ((1 << STACK_BUCKET_POSITION_BITS) - 1);

		if(bucket < 0 || bucket > stack->buckets_position || offset < sizeof(fsc_stack_bucket_t) ||
				offset - sizeof(fsc_stack_bucket_t) >= stack->buckets[bucket]->position) {
			fsc_fatal_error_tagged("stackptr out of range", caller, expression); }

		return (void *)((char *)stack->buckets[bucket] + offset); }
	else {
		if(!allow_null) fsc_fatal_error_tagged("unexpected null stackptr", caller, expression);
		return 0; } }

fsc_stackptr_t fsc_stack_allocate(fsc_stack_t *stack, unsigned int size) {
	// Returns 0 on error
	fsc_stack_bucket_t *bucket = stack->buckets[stack->buckets_position];
	unsigned int aligned_position = (bucket->position + 3) & ~3;
	char *output;

	// Add a new bucket if we are out of space
	FSC_ASSERT(size < STACK_BUCKET_DATA_SIZE);
	if(size > STACK_BUCKET_DATA_SIZE - aligned_position) {
		stack_add_bucket(stack);
		bucket = stack->buckets[stack->buckets_position];
		aligned_position = (bucket->position + 3) & ~3; }

	// Allocate the entry
	output = (char *)bucket + sizeof(*bucket) + aligned_position;
	bucket->position = aligned_position + size;

	return stack->buckets_position * STACK_BUCKET_SIZE + (unsigned int)(output - (char *)bucket); }

void fsc_stack_free(fsc_stack_t *stack) {
	// Can be called on a nulled, freed, initialized, or in some cases partially initialized stack
	int i;
	if(stack->buckets) {
		for(i=0; i<=stack->buckets_position; ++i) {
			if(stack->buckets[i]) fsc_free(stack->buckets[i]); }
		fsc_free(stack->buckets);
		stack->buckets = 0; } }

unsigned int fsc_stack_get_export_size(fsc_stack_t *stack) {
	unsigned int size = 4;	// 4 bytes for bucket count field
	int i;

	// Then add the actual length of each bucket + 4 bytes for position field
	for(i=0; i<=stack->buckets_position; ++i) {
		size += stack->buckets[i]->position + 4; }

	return size; }

int fsc_stack_export(fsc_stack_t *stack, fsc_stream_t *stream) {
	// Returns 1 on error, 0 on success
	int i;

	// Write the number of buckets
	if(fsc_write_stream_data(stream, &stack->buckets_position, 4)) return 1;

	// Write each bucket (current position followed by data)
	for(i=0; i<=stack->buckets_position; ++i) {
		if(fsc_write_stream_data(stream, &stack->buckets[i]->position, 4)) return 1;
		if(fsc_write_stream_data(stream, (char *)stack->buckets[i] + sizeof(fsc_stack_bucket_t),
				stack->buckets[i]->position)) return 1; }

	return 0; }

int fsc_stack_import(fsc_stack_t *stack, fsc_stream_t *stream) {
	// Returns 1 on error, 0 on success
	int i;

	// Read number of active buckets
	if(fsc_read_stream_data(stream, &stack->buckets_position, 4)) return 1;
	if(stack->buckets_position < 0 || stack->buckets_position >= STACK_MAX_BUCKETS) return 1;

	// Allocate bucket array
	stack->buckets_size = stack->buckets_position + 1;
	if(stack->buckets_size < STACK_INITIAL_BUCKETS) stack->buckets_size = STACK_INITIAL_BUCKETS;
	stack->buckets = (fsc_stack_bucket_t **)fsc_calloc(stack->buckets_size * sizeof(fsc_stack_bucket_t *));

	// Read data for each bucket
	for(i=0; i<=stack->buckets_position; ++i) {
		stack->buckets[i] = (fsc_stack_bucket_t *)fsc_calloc(STACK_BUCKET_SIZE);
		if(fsc_read_stream_data(stream, &stack->buckets[i]->position, 4)) goto error;
		if(stack->buckets[i]->position >= STACK_BUCKET_SIZE) goto error;
		if(fsc_read_stream_data(stream, (char *)stack->buckets[i] + sizeof(fsc_stack_bucket_t),
				stack->buckets[i]->position)) goto error; }
	return 0;

	error:
	fsc_stack_free(stack);
	return 1; }

/* ******************************************************************************** */
// Standard Hash Table
/* ******************************************************************************** */

void fsc_hashtable_initialize(fsc_hashtable_t *ht, fsc_stack_t *stack, int bucket_count) {
	if(bucket_count < 1) bucket_count = 1;
	if(bucket_count > FSC_HASHTABLE_MAX_BUCKETS) bucket_count = FSC_HASHTABLE_MAX_BUCKETS;
	ht->bucket_count = bucket_count;
	ht->buckets = (fsc_stackptr_t *)fsc_calloc(sizeof(fsc_stackptr_t) * bucket_count);
	ht->utilization = 0;
	ht->stack = stack; }

void fsc_hashtable_open(fsc_hashtable_t *ht, unsigned int hash, fsc_hashtable_iterator_t *iterator) {
	iterator->stack = ht->stack;
	iterator->next_ptr = &ht->buckets[hash % ht->bucket_count]; }

fsc_stackptr_t fsc_hashtable_next(fsc_hashtable_iterator_t *iterator) {
	fsc_stackptr_t current = *iterator->next_ptr;
	if(current) iterator->next_ptr = &(((fsc_hashtable_entry_t *)FSC_STACK_RETRIEVE(iterator->stack, current, 0))->next);
	return current; }

void fsc_hashtable_insert(fsc_stackptr_t entry_ptr, unsigned int hash, fsc_hashtable_t *ht) {
	// entry_ptr must be castable to fsc_hashtable_entry_t.
	fsc_hashtable_entry_t *entry = (fsc_hashtable_entry_t *)FSC_STACK_RETRIEVE(ht->stack, entry_ptr, 0);
	fsc_stackptr_t *bucket = &ht->buckets[hash % ht->bucket_count];
	entry->next = *bucket;
	*bucket = entry_ptr;
	++ht->utilization; }

void fsc_hashtable_free(fsc_hashtable_t *ht) {
	// Can be called on a nulled, freed, initialized, or in some cases partially initialized hashtable
	if(ht->buckets) fsc_free(ht->buckets);
	ht->buckets = 0; }

unsigned int fsc_hashtable_get_export_size(fsc_hashtable_t *ht) {
	return 8 + ht->bucket_count * sizeof(*ht->buckets); }

int fsc_hashtable_export(fsc_hashtable_t *ht, fsc_stream_t *stream) {
	// Returns 1 on error, 0 on success.
	if(fsc_write_stream_data(stream, &ht->bucket_count, 4)) return 1;
	if(fsc_write_stream_data(stream, &ht->utilization, 4)) return 1;
	if(fsc_write_stream_data(stream, ht->buckets, ht->bucket_count * sizeof(*ht->buckets))) return 1;
	return 0; }

int fsc_hashtable_import(fsc_hashtable_t *ht, fsc_stack_t *stack, fsc_stream_t *stream) {
	// Returns 1 on error, 0 on success.
	if(fsc_read_stream_data(stream, &ht->bucket_count, 4)) return 1;
	if(ht->bucket_count < 1 || ht->bucket_count > FSC_HASHTABLE_MAX_BUCKETS) return 1;
	if(fsc_read_stream_data(stream, &ht->utilization, 4)) return 1;
	ht->buckets = (fsc_stackptr_t *)fsc_malloc(ht->bucket_count * sizeof(*ht->buckets));
	if(fsc_read_stream_data(stream, ht->buckets, ht->bucket_count * sizeof(*ht->buckets))) {
		fsc_hashtable_free(ht);
		return 1; }
	ht->stack = stack;
	return 0; }

/* ******************************************************************************** */
// Standard String Repository
/* ******************************************************************************** */

fsc_stackptr_t fsc_string_repository_getentry(const char *input, int allocate, fsc_hashtable_t *string_repository, fsc_stack_t *stack) {
	// Return type is stringrepository_entry_t

	unsigned int hash = fsc_string_hash(input, 0);
	fsc_hashtable_iterator_t hti;
	fsc_stackptr_t sre_ptr;
	stringrepository_entry_t *sre;

	// Get entry
	fsc_hashtable_open(string_repository, hash, &hti);
	while((sre_ptr = fsc_hashtable_next(&hti))) {
		sre = (stringrepository_entry_t *)STACKPTR_LCL(sre_ptr);
		if(!fsc_strcmp((char *)sre + sizeof(*sre), input)) break; }

	if(!sre_ptr && allocate) {
		// Allocate new entry
		int length = fsc_strlen(input) + 1;		// Include null terminator
		sre_ptr = fsc_stack_allocate(stack, sizeof(stringrepository_entry_t) + length);
		sre = (stringrepository_entry_t *)STACKPTR_LCL(sre_ptr);
		fsc_memcpy((char *)sre + sizeof(*sre), input, length);
		fsc_hashtable_insert(sre_ptr, hash, string_repository); }

	return sre_ptr; }

fsc_stackptr_t fsc_string_repository_getstring(const char *input, int allocate,
			fsc_hashtable_t *string_repository, fsc_stack_t *stack) {
	// Return type is char *
	fsc_stackptr_t entry = fsc_string_repository_getentry(input, allocate, string_repository, stack);
	if(entry) return entry + sizeof(stringrepository_entry_t);
	return 0; }

/* ******************************************************************************** */
// Qpath Handling
/* ******************************************************************************** */

const char *fsc_get_qpath_conversion_table(void) {
	// Used to sanitize qpaths
	static char qpath_conversion_table[256];
	static int qpath_conversion_table_initialized = 0;
	int i;

	if(qpath_conversion_table_initialized) return qpath_conversion_table;

	// Default to underscore
	for(i=0; i<256; ++i) qpath_conversion_table[i] = '_';

	// Valid characters
	qpath_conversion_table[0] = 0;
	qpath_conversion_table['/'] = '/';
	qpath_conversion_table['\\'] = '/';
	qpath_conversion_table['.'] = '.';
	qpath_conversion_table['-'] = '-';
	for(i='0'; i<='9'; ++i) qpath_conversion_table[i] = i;
	for(i='a'; i<='z'; ++i) qpath_conversion_table[i] = i;
	for(i='A'; i<='Z'; ++i) qpath_conversion_table[i] = i;

	qpath_conversion_table_initialized = 1;
	return qpath_conversion_table; }

int fsc_process_qpath(const char *input, char *buffer, const char **qp_dir, const char **qp_name, const char **qp_ext) {
	// Breaks input path into directory, name, and extension sections.
	// Buffer is used to store the separated path data and should be length FSC_MAX_QPATH.
	// Output qp_dir and qp_ext may be null if no directory or extension is available.
	// Input qp_ext may be null to disable extension processing.
	// Returns number of chars written to buffer (NOT including final null terminator)
	// Buffer can be same as input for in-place processing
	int i;
	int period_pos = 0;
	int slash_pos = 0;
	FSC_ASSERT(input);
	FSC_ASSERT(buffer);
	FSC_ASSERT(qp_dir);
	FSC_ASSERT(qp_name);

	// Write buffer; get period_pos and slash_pos
	for(i=0; i<FSC_MAX_QPATH-1; ++i) {
		buffer[i] = input[i];
		if(!buffer[i]) break;
		if(buffer[i] == '\\') buffer[i] = '/';
		if(buffer[i] == '/') {
			slash_pos = i;
			period_pos = 0; }
		if(buffer[i] == '.' && qp_ext) period_pos = i; }
	buffer[i] = 0;

	// Break up output based on period and slash positions
	if(slash_pos) {
		*qp_dir = buffer;
		*qp_name = buffer + slash_pos + 1;
		buffer[slash_pos] = 0; }
	else {
		*qp_dir = 0;
		*qp_name = buffer; }

	if(period_pos) {
		*qp_ext = buffer + period_pos + 1;
		buffer[period_pos] = 0; }
	else {
		if(qp_ext) *qp_ext = 0; }

	return i; }

unsigned int fsc_get_leading_directory(const char *input, char *buffer, unsigned int buffer_length, const char **remainder) {
	// Writes leading directory (text before first slash) to buffer, truncating on overflow
	// Writes pointer to remaining (post-slash) string to remainder, or null if not found
	// Returns total number of chars in leading directory, without truncation, not counting null terminator.
	// If (return value >= buffer_length) output was truncated.
	unsigned int i;
	unsigned int chars_written;
	FSC_ASSERT(input);
	FSC_ASSERT(buffer);
	FSC_ASSERT(buffer_length > 0);

	// Start with null remainder
	if(remainder) *remainder = 0;

	// Write buffer and remainder (if slash encountered)
	for(i=0; input[i]; ++i) {
		if(input[i] == '/' || input[i] == '\\') {
			if(remainder) *remainder = (char *)(input + i + 1);
			break; }
		if(i < buffer_length) buffer[i] = input[i]; }

	chars_written = i < buffer_length ? i : buffer_length - 1;
	buffer[chars_written] = 0;
	return i; }

/* ******************************************************************************** */
// Error Handling
/* ******************************************************************************** */

void fsc_report_error(fsc_errorhandler_t *errorhandler, int id, const char *msg, void *current_element) {
	if(!errorhandler) return;
	errorhandler->handler(id, msg, current_element, errorhandler->context); }

static void (*fsc_registered_fatal_error_handler)(const char *msg) = 0;

void fsc_register_fatal_error_handler(void (*handler)(const char *msg)) {
	// Registers a handler function to call in the event fsc_fatal_error is invoked.
	fsc_registered_fatal_error_handler = handler; }

void fsc_fatal_error(const char *msg) {
	// Calls fatal error handler if registered. If not registered, or it returns, aborts the program.
	if(fsc_registered_fatal_error_handler) fsc_registered_fatal_error_handler(msg);
	fsc_error_abort(msg); }

void fsc_fatal_error_tagged(const char *msg, const char *caller, const char *expression) {
	// Calls fatal error handler with calling function and expression logging parameters
	char buffer[1024];
	fsc_stream_t stream = {buffer, 0, sizeof(buffer), 0};
	fsc_stream_append_string(&stream, msg);
	fsc_stream_append_string(&stream, " - function(");
	fsc_stream_append_string(&stream, caller);
	fsc_stream_append_string(&stream, ") expression(");
	fsc_stream_append_string(&stream, expression);
	fsc_stream_append_string(&stream, ")");
	fsc_fatal_error(buffer); }

#endif	// NEW_FILESYSTEM
