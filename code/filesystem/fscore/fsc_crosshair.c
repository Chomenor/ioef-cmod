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

// This file provides crosshair index support for potential crosshair lookup features. Supporting
// crosshair indexing in the filesystem allows the hash of each crosshair to be cached so it doesn't
// need to be recalculated each time the game is run.

#define STACKPTR( pointer ) ( FSC_STACK_RETRIEVE( &fs->general_stack, pointer, fsc_false ) ) // non-null

/*
=================
FSC_IndexCrosshair

Registers crosshair into crosshair index. Returns fsc_true on success, fsc_false otherwise.
=================
*/
fsc_boolean FSC_IndexCrosshair( fsc_filesystem_t *fs, fsc_stackptr_t source_file_ptr, fsc_sanity_limit_t *sanity_limit, fsc_errorhandler_t *eh ) {
	fsc_file_t *source_file = (fsc_file_t *)STACKPTR( source_file_ptr );
	unsigned int read_limit_size = source_file->filesize + 256 > source_file->filesize ? source_file->filesize + 256 : source_file->filesize;
	unsigned int hash;

	if ( !sanity_limit || !FSC_SanityLimit( read_limit_size, &sanity_limit->data_read, sanity_limit, eh ) ) {
		char *data = FSC_ExtractFileAllocated( fs, source_file, FSC_NULL );
		if ( !data ) {
			FSC_ReportError( eh, FSC_ERROR_CROSSHAIRFILE, "failed to extract/open crosshair file", source_file );
			return fsc_false;
		}

		hash = FSC_BlockChecksum( data, source_file->filesize );
		FSC_Free( data );

		if ( !sanity_limit || !FSC_SanityLimit( sizeof( fsc_crosshair_t ), &sanity_limit->content_index_memory, sanity_limit, eh ) ) {
			fsc_stackptr_t new_crosshair_ptr = FSC_StackAllocate( &fs->general_stack, sizeof( fsc_crosshair_t ) );
			fsc_crosshair_t *new_crosshair = (fsc_crosshair_t *)STACKPTR( new_crosshair_ptr );

			new_crosshair->hash = hash;
			new_crosshair->source_file_ptr = source_file_ptr;
			FSC_HashtableInsert( new_crosshair_ptr, hash, &fs->crosshairs );
			return fsc_true;
		}
	}

	return fsc_false;
}

/*
=================
FSC_IsCrosshairActive
=================
*/
fsc_boolean FSC_IsCrosshairActive( fsc_filesystem_t *fs, const fsc_crosshair_t *crosshair ) {
	return FSC_IsFileActive( (const fsc_file_t *)STACKPTR( crosshair->source_file_ptr ), fs );
}

#endif	// NEW_FILESYSTEM
