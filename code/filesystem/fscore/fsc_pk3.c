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

#define STACKPTR(pointer) ( fsc_stack_retrieve(&fs->general_stack, pointer) )
#define STACKPTRL(pointer) ( fsc_stack_retrieve(stack, pointer) )	// stack is a local parameter

// Somewhat arbitrary limit to avoid overflow issues
#define FSC_MAX_PK3_SIZE 4240000000

/* ******************************************************************************** */
// PK3 File Processing
/* ******************************************************************************** */

typedef struct {
	char *data;
	int cd_length;
	unsigned int zip_offset;
	int entry_count;
} central_directory_t;

static int fsc_endian_check(void) {
	// Returns 1 on little endian system, 0 on big endian system
	// WARNING: Currently untested on big endian system
	static volatile int test = 1;
	if (*(char *)&test) return 1;
	return 0; }

static unsigned int fsc_endian_convert_int(unsigned int value) {
	if(fsc_endian_check()) return value;
    value = ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0xFF00FF);
    return (value << 16) | (value >> 16); }

static unsigned short fsc_endian_convert_short(unsigned short value) {
	if(fsc_endian_check()) return value;
	return (value << 8) | (value >> 8); }

static int get_pk3_central_directory_fp(void *fp, unsigned int file_length, central_directory_t *output) {
	// Returns 1 on error, 0 on success
	char buffer[66000];
	int buffer_read_size = 0;	// Offset from end of file/buffer of data that has been successfully read to buffer
	int eocd_position = 0;	// Offset from end of file/buffer of the EOCD record
	unsigned int cd_position;	// Offset from beginning of file of central directory

	// End Of Central Directory Record (EOCD) can start anywhere in the last 65KB or so of the zip file,
	// determined by the presence of a magic number. For performance purposes first scan the last 4KB of the file,
	// and if the magic number isn't found do a second pass for the whole 65KB.
	int pass;
	for(pass=0; pass<2; ++pass) {
		// Get buffer_read_target, which is the offset from end of file/buffer that we are tring to read to buffer
		int i;
		int buffer_read_target = pass == 0 ? 4096 : sizeof(buffer);
		if((unsigned int)buffer_read_target > file_length) buffer_read_target = file_length;
		if(buffer_read_target <= buffer_read_size) return 1;

		// Read the data
		fsc_fseek(fp, file_length - buffer_read_target, FSC_SEEK_SET);
		fsc_fread(buffer + sizeof(buffer) - buffer_read_target, buffer_read_target - buffer_read_size, fp);

		// Search for magic number
		for(i=22; i<buffer_read_target; ++i) {	// EOCD cannot start less than 22 bytes from end of file, because it is 22 bytes long
			char *string = buffer + sizeof(buffer) - i;
			if(string[0] == 0x50 && string[1] == 0x4b && string[2] == 0x05 && string[3] == 0x06) {
				eocd_position = i;
				break; } }

		buffer_read_size = buffer_read_target;
		if(eocd_position) break; }

	if(!eocd_position) return 1;

	#define EOCD_SHORT(offset) fsc_endian_convert_short(*(unsigned short *)(buffer + sizeof(buffer) - eocd_position + offset))
	#define EOCD_INT(offset) fsc_endian_convert_int(*(unsigned int *)(buffer + sizeof(buffer) - eocd_position + offset))

	output->entry_count = EOCD_SHORT(8);
	output->cd_length = EOCD_INT(12);

	// No reason for central directory to be over 100 MB
	if(output->cd_length < 0 || output->cd_length > 100<<20) return 1;

	// Must be space for central directory between the beginning of the file and the EOCD start
	if((unsigned int)output->cd_length > file_length - eocd_position) return 1;

	// EOCD sanity checks that were in the original code: something to do with ensuring this is not a spanned archive
	if(EOCD_INT(4) != 0) return 1;	// "this file's disk number" and "disk number containing central directory" should both be 0
	if(EOCD_SHORT(8) != EOCD_SHORT(8)) return 1;	// "cd entries on this disk" and "cd entries total" should be equal

	// Determine real central directory position
	cd_position = file_length - eocd_position - output->cd_length;

	// Determine zip offset from error in reported cd position - all file offsets need to be adjusted by this value
	{	unsigned int cd_position_reported = EOCD_INT(16);
		if(cd_position_reported > cd_position) return 1;	// cd_position is already the maximum valid position
		output->zip_offset = cd_position - cd_position_reported; }

	output->data = fsc_malloc(output->cd_length);

	{	unsigned int buffer_file_position = file_length - buffer_read_size;		// Position in file where buffered data begins
		unsigned int unbuffered_read_length = 0;	// Amount of non-buffered data read
		if(cd_position < buffer_file_position) {
			// Since central directory starts before buffer, read unbuffered part of central directory into output
			unbuffered_read_length = buffer_file_position - cd_position;
			if(unbuffered_read_length > (unsigned int)output->cd_length) unbuffered_read_length = output->cd_length;
			fsc_fseek(fp, cd_position, FSC_SEEK_SET);
			fsc_fread(output->data, unbuffered_read_length, fp); }
		if(unbuffered_read_length < (unsigned int)output->cd_length) {
			// Read remaining data from buffer into output
			char *buffer_data = buffer + sizeof(buffer) - buffer_read_size;
			if(cd_position > buffer_file_position) buffer_data += cd_position - buffer_file_position;
			fsc_memcpy(output->data + unbuffered_read_length, buffer_data, output->cd_length - unbuffered_read_length); } }

	return 0; }

static int get_pk3_central_directory_path(void *os_path, central_directory_t *output,
			fsc_file_direct_t *source_file, fsc_errorhandler_t *eh) {
	// Returns 1 on error, 0 on success
	void *fp = 0;
	unsigned int length;

	// Open file
	fp = fsc_open_file(os_path, "rb");
	if(!fp) {
		fsc_report_error(eh, FSC_ERROR_PK3FILE, "error opening pk3", source_file);
		return 1; }

	// Get size
	fsc_fseek(fp, 0, FSC_SEEK_END);
	length = fsc_ftell(fp);
	if(!length) {
		fsc_fclose(fp);
		fsc_report_error(eh, FSC_ERROR_PK3FILE, "zero size pk3", source_file);
		return 1; }
	if(length > FSC_MAX_PK3_SIZE) {
		fsc_fclose(fp);
		fsc_report_error(eh, FSC_ERROR_PK3FILE, "excessively large pk3", source_file);
		return 1; }

	// Get central directory
	if(get_pk3_central_directory_fp(fp, length, output)) {
		fsc_fclose(fp);
		fsc_report_error(eh, FSC_ERROR_PK3FILE, "error retrieving pk3 central directory", source_file);
		return 1; }
	fsc_fclose(fp);

	return 0; }

void register_pk3_hash_lookup_entry(fsc_stackptr_t pk3_file_ptr, fsc_hashtable_t *pk3_hash_lookup, fsc_stack_t *stack) {
	fsc_file_direct_t *pk3_file = STACKPTRL(pk3_file_ptr);
	fsc_stackptr_t hash_map_entry_ptr = fsc_stack_allocate(stack, sizeof(fsc_pk3_hash_map_entry_t));
	fsc_pk3_hash_map_entry_t *hash_map_entry = STACKPTRL(hash_map_entry_ptr);
	hash_map_entry->pk3 = pk3_file_ptr;
	fsc_hashtable_insert(hash_map_entry_ptr, pk3_file->pk3_hash, pk3_hash_lookup); }

static void register_file_from_pk3(fsc_filesystem_t *fs, char *filename, int filename_length,
			fsc_stackptr_t sourcefile_ptr, unsigned int header_position, unsigned int compressed_size,
			unsigned int uncompressed_size, short compression_method, fsc_errorhandler_t *eh) {
	fsc_file_direct_t *sourcefile = STACKPTR(sourcefile_ptr);
	fsc_stackptr_t file_ptr = fsc_stack_allocate(&fs->general_stack, sizeof(fsc_file_frompk3_t));
	fsc_file_frompk3_t *file = STACKPTR(file_ptr);

	char buffer[FSC_MAX_QPATH];
	char *qp_dir, *qp_name, *qp_ext;
	unsigned int fs_hash;
	int filename_contents;

	// Copy filename into null-terminated buffer for process_qpath
	// Also convert to lowercase to match behavior of old filesystem
	if(filename_length > FSC_MAX_QPATH - 1) filename_length = FSC_MAX_QPATH - 1;
	fsc_strncpy_lower(buffer, filename, filename_length+1);

	// Call process_qpath
	fsc_process_qpath(buffer, buffer, &qp_dir, &qp_name, &qp_ext);
	fs_hash = fsc_string_hash(qp_name, qp_dir);

	// Write qpaths to file structure
	file->f.qp_dir_ptr = qp_dir ? fsc_string_repository_getstring(qp_dir, 1, &fs->string_repository, &fs->general_stack) : 0;
	file->f.qp_name_ptr = fsc_string_repository_getstring(qp_name, 1, &fs->string_repository, &fs->general_stack);
	file->f.qp_ext_ptr = qp_ext ? fsc_string_repository_getstring(qp_ext, 1, &fs->string_repository, &fs->general_stack) : 0;

	// Load the rest of the fields
	file->f.sourcetype = FSC_SOURCETYPE_PK3;
	file->source_pk3 = sourcefile_ptr;
	file->header_position = header_position;
	file->compressed_size = compressed_size;
	file->compression_method = compression_method;
	file->f.filesize = uncompressed_size;

	// Register file and contents
	fsc_hashtable_insert(file_ptr, fs_hash, &fs->files);
	fsc_iteration_register_file(file_ptr, &fs->directories, &fs->string_repository, &fs->general_stack);
	++sourcefile->pk3_subfile_count;

	filename_contents = fsc_filename_contents(qp_dir, qp_name, qp_ext);
	if(filename_contents & FSC_CONTENTS_SHADER) {
		++sourcefile->shader_file_count;
		sourcefile->shader_count += index_shader_file(fs, file_ptr, eh); }
	if(filename_contents & FSC_CONTENTS_CROSSHAIR) {
		index_crosshair(fs, file_ptr, eh); } }

void fsc_load_pk3(void *os_path, fsc_filesystem_t *fs, fsc_stackptr_t sourcefile_ptr, fsc_errorhandler_t *eh,
				void (receive_hash_data)(void *context, char *data, int size), void *receive_hash_data_context ) {
	// In normal usage, this is used to load pk3 files into the index. It can also be called with
	//    receive_hash_data set to generate pk3 hash data without indexing anything.
	fsc_file_direct_t *sourcefile = STACKPTR(sourcefile_ptr);
	central_directory_t cd;
	int entry_position = 0;		// Position of current entry relative to central directory data
	int entry_counter = 0;		// Number of current entry

	int filename_length;
	int entry_length;
	unsigned int uncompressed_size;
	unsigned int compressed_size;
	unsigned int header_position;

	int *crcs_for_hash;
	int crcs_for_hash_buffer[1024];
	int crcs_for_hash_count = 0;

	// Load central directory
	if(get_pk3_central_directory_path(os_path, &cd, sourcefile, eh)) return;

	// Try to use the stack buffer, but if it's not big enough resort to malloc
	if(cd.entry_count > sizeof(crcs_for_hash_buffer) / sizeof(*crcs_for_hash_buffer)) {
		crcs_for_hash = fsc_malloc((cd.entry_count + 1) * 4); }
	else crcs_for_hash = crcs_for_hash_buffer;

	// Process each file
	while(1) {
		// Make sure there is enough space to read the entry (minimum 47 bytes if filename is 1 byte)
		if(entry_position + 47 > cd.cd_length) {
			fsc_report_error(eh, FSC_ERROR_PK3FILE, "invalid file cd entry position", sourcefile);
			goto freemem; }

		// Verify magic number
		if(cd.data[entry_position] != 0x50 || cd.data[entry_position+1] != 0x4b
				|| cd.data[entry_position+2] != 0x01 || cd.data[entry_position+3] != 0x02) {
			fsc_report_error(eh, FSC_ERROR_PK3FILE, "file cd entry does not have correct signature", sourcefile);
			goto freemem; }

		#define CD_ENTRY_SHORT(offset) fsc_endian_convert_short(*(unsigned short *)(cd.data + entry_position + offset))
		#define CD_ENTRY_INT(offset) fsc_endian_convert_int(*(unsigned int *)(cd.data + entry_position + offset))
		#define CD_ENTRY_INT_LE(offset) (*(unsigned int *)(cd.data + entry_position + offset))

		// Get filename_length and entry_length
		filename_length = (int)CD_ENTRY_SHORT(28);
		{	int extrafield_length = (int)CD_ENTRY_SHORT(30);
			int comment_length = (int)CD_ENTRY_SHORT(32);
			entry_length = 46 + filename_length + extrafield_length + comment_length;
			if(entry_position + entry_length > cd.cd_length) {
				fsc_report_error(eh, FSC_ERROR_PK3FILE,
					"invalid file cd entry position 2", sourcefile);
				goto freemem; } }

		// Get compressed_size and uncompressed_size
		compressed_size = CD_ENTRY_INT(20);
		uncompressed_size = CD_ENTRY_INT(24);

		// Get local header_position (which is indicated by CD header, but needs to be modified by zip offset)
		header_position = CD_ENTRY_INT(42) + cd.zip_offset;

		// Sanity checks
		if(header_position + compressed_size < header_position) {
			fsc_report_error(eh, FSC_ERROR_PK3FILE, "invalid file local entry position 1", sourcefile); goto freemem; }
		if(header_position + compressed_size > FSC_MAX_PK3_SIZE) {
			fsc_report_error(eh, FSC_ERROR_PK3FILE, "invalid file local entry position 2", sourcefile); goto freemem; }

		if(uncompressed_size) {
			crcs_for_hash[crcs_for_hash_count++] = CD_ENTRY_INT_LE(16); }

		if(!(void *)receive_hash_data && !(!uncompressed_size && *(cd.data+entry_position+46+filename_length-1) == '/')) {
			// Not in hash mode and not a directory entry - load the file
			register_file_from_pk3(fs, cd.data+entry_position+46, filename_length, sourcefile_ptr,
					header_position, compressed_size, CD_ENTRY_INT(24), CD_ENTRY_SHORT(10), eh); }

		++entry_counter;
		entry_position += entry_length;
		if(entry_counter >= cd.entry_count) break; }		// Abort if that was the last file

	if((void *)receive_hash_data) {
		receive_hash_data(receive_hash_data_context, (char *)crcs_for_hash, crcs_for_hash_count * 4);
		goto freemem; }

	sourcefile->pk3_hash = fsc_block_checksum(crcs_for_hash, crcs_for_hash_count * 4);

	// Add the pk3 to the hash lookup table
	register_pk3_hash_lookup_entry(sourcefile_ptr, &fs->pk3_hash_lookup, &fs->general_stack);

	freemem:
	fsc_free(cd.data);
	if(crcs_for_hash != crcs_for_hash_buffer) fsc_free(crcs_for_hash); }

/* ******************************************************************************** */
// PK3 Extraction Support
/* ******************************************************************************** */

static int zlib_extract(void *fp, unsigned int source_pos, unsigned int source_size, unsigned int output_size, void *output, fsc_errorhandler_t *eh) {
	// Returns 0 on success, 1 on failure

	z_stream stream;
	void *source_data;
	unsigned int result;

	// Read all the source data
	source_data = fsc_malloc(source_size);
	fsc_fseek(fp, source_pos, FSC_SEEK_SET);
	result = fsc_fread(source_data, source_size, fp);
	if(result != source_size) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "zlib_extract - failed to read all source data", 0);
		return 1; }

	// Set up the stream
	fsc_memset(&stream, 0, sizeof(stream));
	if(inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "zlib_extract - zlib inflateInit failed", 0);
		return 1; }

	stream.next_in = source_data;
	stream.avail_in = source_size;

	stream.next_out = output;
	stream.avail_out = output_size;

	inflate(&stream, Z_SYNC_FLUSH);

	if(stream.total_out != output_size) {
		inflateEnd(&stream);
		fsc_free(source_data);
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "zlib_extract - failed to reach correct output size", 0);
		return 1; }

	inflateEnd(&stream);
	fsc_free(source_data);
	return 0; }

static int direct_pk3_extract(void *fp, unsigned int source_pos, unsigned int source_size,
		unsigned int output_size, char *output, fsc_errorhandler_t *eh) {
	// Returns 0 on success, 1 on failure
	unsigned int result;

	if(source_size != output_size) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "direct_pk3_extract - source_size and output_size should be the same", 0);
		return 1; }

	fsc_fseek(fp, source_pos, FSC_SEEK_SET);
	result = fsc_fread(output, source_size, fp);
	if(result != source_size) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "direct_pk3_extract - failed to read all data", 0);
		return 1; }

	return 0; }

/* ******************************************************************************** */
// PK3 Sourcetype Operations
/* ******************************************************************************** */

static int fsc_pk3_is_file_active(const fsc_file_t *file, const fsc_filesystem_t *fs) {
	const fsc_file_direct_t *source_pk3 = STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3);
	return source_pk3->refresh_count == fs->refresh_count; }

static const char *fsc_pk3_get_mod_dir(const fsc_file_t *file, const fsc_filesystem_t *fs) {
	const fsc_file_direct_t *source_pk3 = STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3);
	return STACKPTR(source_pk3->qp_mod_ptr); }

static int fsc_pk3_extract_data(const fsc_file_t *file, char *buffer, const fsc_filesystem_t *fs, fsc_errorhandler_t *eh) {
	// Returns 0 on success, 1 on failure
	const fsc_file_frompk3_t *frompk3 = (const fsc_file_frompk3_t *)file;
	const fsc_file_direct_t *source_pk3 = STACKPTR(frompk3->source_pk3);
	int retval = 1;
	void *fp;
	char localheader[30];
	unsigned int result;

	unsigned int data_start;

	// Open the file
	fp = fsc_open_file(STACKPTR(source_pk3->os_path_ptr), "rb");
	if(!fp) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "extract_pk3_file - failed to open pk3 file", 0);
		return 1; }

	// Read the local header to get data position
	fsc_fseek(fp, frompk3->header_position, FSC_SEEK_SET);
	result = fsc_fread(localheader, 30, fp);
	if(result != 30) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "extract_pk3_file - failed to read local header", 0);
		goto close; }
	if(localheader[0] != 0x50 || localheader[1] != 0x4b || localheader[2] != 0x03 || localheader[3] != 0x04) {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "extract_pk3_file - incorrect signature in local header", 0);
		goto close; }

	#define LH_SHORT(offset) fsc_endian_convert_short(*(unsigned short *)(localheader + offset))
	data_start = frompk3->header_position + LH_SHORT(26) + LH_SHORT(28) + 30;

	// Read the data
	if(frompk3->compression_method == 8) {
		if(zlib_extract(fp, data_start, frompk3->compressed_size, file->filesize, buffer, eh)) {
			goto close; } }
	else if(frompk3->compression_method == 0) {
		if(direct_pk3_extract(fp, data_start, frompk3->compressed_size, file->filesize, buffer, eh)) {
			goto close; } }
	else {
		fsc_report_error(eh, FSC_ERROR_EXTRACT, "extract_pk3_file - unknown compression type", 0);
		goto close; }

	retval = 0;

	close:
	fsc_fclose(fp);
	return retval; }

fsc_sourcetype_t pk3_sourcetype = {FSC_SOURCETYPE_PK3, fsc_pk3_is_file_active,
		fsc_pk3_get_mod_dir, fsc_pk3_extract_data};

#endif	// NEW_FILESYSTEM
