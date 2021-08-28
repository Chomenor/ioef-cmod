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

#define MAX_REFERENCE_SET_ENTRIES 2048
#define MAX_PURE_CHECKSUM_CACHE 256

#define SYSTEMINFO_RESERVED_SIZE 256

#define MAX_DOWNLOAD_LIST_STRING 2048
#define MAX_PURE_LIST_STRING BIG_INFO_STRING

#define REF_DPRINTF( ... ) { if ( fs.cvar.fs_debug_references->integer ) FS_DPrintf( __VA_ARGS__ ); }

/*
###############################################################################################

Referenced Pak Tracking

The "reference_tracker" set is filled by logging game references to pk3 files
It currently serves two purposes:

1) To generate the pure validation string when fs_full_pure_validation is 1
	(Although I have never seen any server that requires this to connect)

2) As a component of the download pak list creation via '*referenced_paks' rule
	(Although in most cases it is redundant to other selector rules)

Basically this section could probably be removed with no noticable effects in normal
situations, but is kept for now just to be on the safe side for compatibility purposes.

###############################################################################################
*/

typedef struct {
	fs_hashtable_entry_t hte;
	fsc_file_direct_t *pak;
} reference_tracker_entry_t;

static fs_hashtable_t reference_tracker;

/*
=================
FS_RefTracker_Add

Returns qtrue on success, qfalse if already inserted or maximum hit.
=================
*/
static qboolean FS_RefTracker_Add( fsc_file_direct_t *pak ) {
	fs_hashtable_iterator_t it = FS_Hashtable_Iterate( &reference_tracker, pak->pk3_hash, qfalse );
	reference_tracker_entry_t *entry;

	if ( reference_tracker.element_count >= MAX_REFERENCE_SET_ENTRIES ) {
		return qfalse;
	}

	while ( ( entry = (reference_tracker_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		if ( entry->pak->pk3_hash == pak->pk3_hash ) {
			return qfalse;
		}
	}

	entry = (reference_tracker_entry_t *)S_Malloc( sizeof( reference_tracker_entry_t ) );
	entry->pak = pak;
	FS_Hashtable_Insert( &reference_tracker, (fs_hashtable_entry_t *)entry, pak->pk3_hash );
	return qtrue;
}

/*
=================
FS_RegisterReference

Adds the source pk3 of the given file to the current referenced paks set.
=================
*/
void FS_RegisterReference( const fsc_file_t *file ) {
	if ( file->sourcetype != FSC_SOURCETYPE_PK3 )
		return;

	// Don't register references for certain extensions
	{
		int i;
		static const char *special_extensions[] = { ".shader", ".txt", ".cfg", ".config", ".bot", ".arena", ".menu" };
		const char *extension = (const char *)STACKPTR( file->qp_ext_ptr );

		for ( i = 0; i < ARRAY_LEN( special_extensions ); ++i ) {
			if ( !Q_stricmp( extension, special_extensions[i] ) ) {
				return;
			}
		}
	}

	// Don't register reference in certain special cases
	if ( !Q_stricmp( (const char *)STACKPTR( file->qp_name_ptr ), "qagame" ) &&
			!Q_stricmp( (const char *)STACKPTR( file->qp_ext_ptr ), ".qvm" ) &&
			!Q_stricmp( (const char *)STACKPTR( file->qp_dir_ptr ), "vm/" ) ) {
		return;
	}
	if ( !Q_stricmp( (const char *)STACKPTR( file->qp_dir_ptr ), "levelshots/" ) ) {
		return;
	}

	// Initialize reference_tracker if it isn't already
	if ( !reference_tracker.bucket_count ) {
		FS_Hashtable_Initialize( &reference_tracker, 32 );
	}

	// Add the reference
	if ( FS_RefTracker_Add( (fsc_file_direct_t *)FSC_GetBaseFile( file, &fs.index ) ) ) {
		if ( fs.cvar.fs_debug_references->integer ) {
			char temp[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( file, temp, sizeof( temp ), qtrue, qtrue, qtrue, qfalse );
			REF_DPRINTF( "recording reference: %s\n", temp );
		}
	}
}

/*
=================
FS_ClearPakReferences
=================
*/
void FS_ClearPakReferences( int flags ) {
	REF_DPRINTF( "clearing referenced paks\n" );
	FS_Hashtable_Reset( &reference_tracker, NULL );
}

/*
=================
FS_RefTracker_GenSortKey
=================
*/
static void FS_RefTracker_GenSortKey( const fsc_file_t *file, fsc_stream_t *output ) {
	FS_WriteCoreSortKey( file, output, qtrue );
	FS_WriteSortFilename( file, output );
	FS_WriteSortValue( FS_GetSourceDirID( file ), output );
}

/*
=================
FS_RefTracker_CompareFile
=================
*/
static int FS_RefTracker_CompareFile( const fsc_file_t *file1, const fsc_file_t *file2 ) {
	char buffer1[1024];
	char buffer2[1024];
	fsc_stream_t stream1 = FSC_InitStream( buffer1, sizeof( buffer1 ) );
	fsc_stream_t stream2 = FSC_InitStream( buffer2, sizeof( buffer2 ) );
	FS_RefTracker_GenSortKey( file1, &stream1 );
	FS_RefTracker_GenSortKey( file2, &stream2 );
	return FSC_Memcmp( stream2.data, stream1.data,
			stream1.position < stream2.position ? stream1.position : stream2.position );
}

/*
=================
FS_RefTracker_Qsort
=================
*/
static int FS_RefTracker_Qsort( const void *e1, const void *e2 ) {
	return FS_RefTracker_CompareFile( *(const fsc_file_t **)e1, *(const fsc_file_t **)e2 );
}

/*
=================
FS_ReferencedPakList

Generates a sorted list of all referenced paks.

Result must be freed by Z_Free.
Result will not be valid if reference_tracker is reset.
=================
*/
static fsc_file_direct_t **FS_ReferencedPakList( int *count_out ) {
	int count = 0;
	fs_hashtable_iterator_t it = FS_Hashtable_Iterate( &reference_tracker, 0, qtrue );
	const reference_tracker_entry_t *entry;
	fsc_file_direct_t **reference_list =
	(fsc_file_direct_t **)Z_Malloc( sizeof( *reference_list ) * reference_tracker.element_count );

	// Generate reference list
	while ( ( entry = (reference_tracker_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		if ( count >= reference_tracker.element_count ) {
			Com_Error( ERR_FATAL, "FS_ReferencedPakList list overflowed" );
		}
		reference_list[count] = entry->pak;
		++count;
	}
	if ( count != reference_tracker.element_count ) {
		Com_Error( ERR_FATAL, "FS_ReferencedPakList list underflow" );
	}

	// Sort reference list
	qsort( reference_list, count, sizeof( *reference_list ), FS_RefTracker_Qsort );

	if ( count_out ) {
		*count_out = count;
	}
	return reference_list;
}

/*
=================
FS_ReferencedPakNames

This is currently just used for a certain debug command.
=================
*/
const char *FS_ReferencedPakNames( void ) {
	static char buffer[1000];
	fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );
	int i;
	int count = 0;
	fsc_file_direct_t **reference_list = FS_ReferencedPakList( &count );

	*buffer = '\0';
	for ( i = 0; i < count; ++i ) {
		if ( stream.position ) {
			FSC_StreamAppendString( &stream, " " );
		}
		FS_FileToStream( (const fsc_file_t *)reference_list[i], &stream, qfalse, qfalse, qfalse, qfalse );
	}

	Z_Free( reference_list );
	return buffer;
}

/*
###############################################################################################

Pure Validation

This section is used to generate a pure validation string to pass the SV_VerifyPaks_f
check which is needed when connecting to pure servers using the original filesystem.

###############################################################################################
*/

typedef struct {
	const fsc_file_direct_t *pk3;
	int data_size;
	int pure_checksum;
	int checksum_feed;
} pure_checksum_entry_t;

/*
=================
FS_GetPureChecksumEntryCallback
=================
*/
static void FS_GetPureChecksumEntryCallback( void *context, char *data, int size ) {
	pure_checksum_entry_t **entry = (pure_checksum_entry_t **)context;
	*entry = (pure_checksum_entry_t *)FSC_Malloc( sizeof( pure_checksum_entry_t ) + size );
	( *entry )->data_size = size;
	Com_Memcpy( (char *)&( *entry )->checksum_feed + 4, data, size );
}

/*
=================
FS_GetPureChecksumEntry
=================
*/
static pure_checksum_entry_t *FS_GetPureChecksumEntry( const fsc_file_direct_t *pk3 ) {
	pure_checksum_entry_t *entry = NULL;
	FSC_LoadPk3( STACKPTR( pk3->os_path_ptr ), &fs.index, FSC_SPNULL, NULL, FS_GetPureChecksumEntryCallback, &entry );
	if ( entry ) {
		entry->pk3 = pk3;
	}
	return entry;
}

/*
=================
FS_UpdatePureChecksumEntry
=================
*/
static void FS_UpdatePureChecksumEntry( pure_checksum_entry_t *entry, int checksum_feed ) {
	entry->checksum_feed = LittleLong( checksum_feed );
	entry->pure_checksum = FSC_BlockChecksum( &entry->checksum_feed, entry->data_size + 4 );
}


// Pure checksum cache

typedef struct pure_checksum_node_s {
	struct pure_checksum_node_s *next;
	pure_checksum_entry_t *entry;
	int rank;
} pure_checksum_node_t;

static int pure_checksum_rank = 0;
static pure_checksum_node_t *pure_checksum_cache;

/*
=================
FS_GetPureChecksumForPk3
=================
*/
static int FS_GetPureChecksumForPk3( const fsc_file_direct_t *pk3, int checksum_feed ) {
	int entry_count = 0;
	pure_checksum_node_t *node = pure_checksum_cache;
	pure_checksum_node_t *deletion_node = NULL;

	while ( node ) {
		if ( node->entry && node->entry->pk3 == pk3 ) {
			// Use existing entry
			if ( node->entry->checksum_feed != checksum_feed ) {
				FS_UpdatePureChecksumEntry( node->entry, checksum_feed );
			}
			node->rank = ++pure_checksum_rank;
			return node->entry->pure_checksum;
		}

		if ( !deletion_node || node->rank < deletion_node->rank ) {
			deletion_node = node;
		}

		++entry_count;
		node = node->next;
	}

	// Prepare new node
	if ( entry_count >= MAX_PURE_CHECKSUM_CACHE ) {
		node = deletion_node;
		if ( node->entry ) {
			FSC_Free( node->entry );
		}
	} else {
		node = (pure_checksum_node_t *)S_Malloc( sizeof( *node ) );
		node->next = pure_checksum_cache;
		pure_checksum_cache = node;
	}

	// Create new entry
	node->entry = FS_GetPureChecksumEntry( pk3 );
	if ( !node->entry ) {
		return 0;
	}
	FS_UpdatePureChecksumEntry( node->entry, checksum_feed );
	node->rank = ++pure_checksum_rank;
	return node->entry->pure_checksum;
}

/*
=================
FS_GetPureChecksumForFile
=================
*/
static int FS_GetPureChecksumForFile( const fsc_file_t *file, int checksum_feed ) {
	if ( !file )
		return 0;
	if ( file->sourcetype != FSC_SOURCETYPE_PK3 )
		return 0;
	return FS_GetPureChecksumForPk3( FSC_GetBaseFile( file, &fs.index ), checksum_feed );
}

/*
=================
FS_AddReferencedPurePk3
=================
*/
static void FS_AddReferencedPurePk3( fsc_stream_t *stream, fs_hashtable_t *reference_tracker ) {
	int i;
	int count = 0;
	fsc_file_direct_t **reference_list = FS_ReferencedPakList( &count );
	char buffer[20];
	int lump_checksum = 0;

	// Process entries
	for ( i = 0; i < count; ++i ) {
		int pure_checksum = FS_GetPureChecksumForPk3( reference_list[i], fs.checksum_feed );

		if ( fs.cvar.fs_debug_references->integer ) {
			char temp[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( (fsc_file_t *)reference_list[i], temp, sizeof( temp ), qtrue, qtrue, qtrue, qfalse );
			REF_DPRINTF( "adding pak to pure validation list: %s\n", temp );
		}

		lump_checksum ^= pure_checksum;
		Com_sprintf( buffer, sizeof( buffer ), " %i", pure_checksum );
		FSC_StreamAppendString( stream, buffer );
	}

	// Write final checksum
	Com_sprintf( buffer, sizeof( buffer ), " %i ", fs.checksum_feed ^ lump_checksum ^ count );
	FSC_StreamAppendString( stream, buffer );

	Z_Free( reference_list );
}

/*
=================
FS_BuildPureValidationString
=================
*/
static void FS_BuildPureValidationString( char *output, unsigned int output_size, fs_hashtable_t *reference_tracker ) {
	fsc_stream_t stream = FSC_InitStream( output, output_size );
	char buffer[50];
	int cgame_checksum = FS_GetPureChecksumForFile( FS_GeneralLookup( "vm/cgame.qvm", LOOKUPFLAG_IGNORE_CURRENT_MAP, qfalse ), fs.checksum_feed );
	int ui_checksum = FS_GetPureChecksumForFile( FS_GeneralLookup( "vm/ui.qvm", LOOKUPFLAG_IGNORE_CURRENT_MAP, qfalse ), fs.checksum_feed );

	Com_sprintf( buffer, sizeof( buffer ), "%i %i @", cgame_checksum, ui_checksum );
	FSC_StreamAppendString( &stream, buffer );

	if ( fs.cvar.fs_full_pure_validation->integer && fs.connected_server_sv_pure != 2 ) {
		FS_AddReferencedPurePk3( &stream, reference_tracker );
	} else {
		Com_sprintf( buffer, sizeof( buffer ), " %i %i ", cgame_checksum, fs.checksum_feed ^ cgame_checksum ^ 1 );
		FSC_StreamAppendString( &stream, buffer );
	}
}

/*
=================
FS_ReferencedPakPureChecksums

Returns a space separated string containing the pure checksums of all referenced pk3 files.
Servers with sv_pure set will get this string back from clients for pure validation
The string has a specific order, "cgame ui @ ref1 ref2 ref3 ..."
=================
*/
const char *FS_ReferencedPakPureChecksums( void ) {
	static char buffer[1000];
	FS_BuildPureValidationString( buffer, sizeof( buffer ), &reference_tracker );
	return buffer;
}

/*
###############################################################################################

Common Reference Structures

###############################################################################################
*/

// The reference query represents the input to the reference set generation functions.
typedef struct {
	// Manifest string (from manifest cvars)
	const char *manifest;

	// Enable certain special handling if query is for download list
	qboolean download;
} reference_query_t;

// Each reference list entry corresponds to one hash+name pair in the output pure/download
//   list strings.
typedef struct {
	// Primary characteristics
	char mod_dir[FSC_MAX_MODDIR];
	char name[FSC_MAX_QPATH];
	unsigned int hash;
	const fsc_file_direct_t *pak_file;	// Optional (if null, represents hash-only entry)

	// For debug print purposes
	char command_name[64];	// Name of the selector command that created this entry
	int entry_id;	// Numerical value assigned to entry to identify it in debug prints

	// Don't write to final string output
	qboolean disabled;
} reference_list_entry_t;

// Current state of a reference set / list / strings structure
typedef enum {
	REFSTATE_UNINITIALIZED,
	REFSTATE_OVERFLOWED,
	REFSTATE_VALID
} reference_state_t;

/*
###############################################################################################

Reference Set Generation

This section is used to create a reference set from a reference query.

###############################################################################################
*/

typedef struct {
	// REFSTATE_VALID: Hashtable will be initialized and iterable
	// REFSTATE_UNINITIALIZED / REFSTATE_OVERFLOWED: Hashtable not initialized
	reference_state_t state;
	fs_hashtable_t h;
} reference_set_t;

typedef struct {
	fs_hashtable_entry_t hte;
	reference_list_entry_t l;

	// Misc sorting characteristics
	int pak_file_name_match;
	unsigned int cluster;	// Indicates dash separated cluster (lower value is higher priority)

	// Sort key
	char sort_key[FSC_MAX_MODDIR+FSC_MAX_QPATH+32];
	unsigned int sort_key_length;
} reference_set_entry_t;

typedef struct {
	// General state
	const reference_query_t *query;
	reference_set_t *reference_set;
	pk3_list_t block_set;
	int cluster;
	qboolean overflowed;

	// Current command
	qboolean block_mode;

	// For debug prints
	int entry_id_counter;
	char command_name[64];
} reference_set_work_t;

/*
=================
FS_RefSet_SanitizeString

Sanitizes string to be suitable for output reference lists.
May write null string to target due to errors.
=================
*/
static void FS_RefSet_SanitizeString( const char *source, char *target, unsigned int size ) {
	char buffer[FSC_MAX_QPATH];
	char *buf_ptr = buffer;
	if ( size > sizeof( buffer ) ) {
		size = sizeof( buffer );
	}
	Q_strncpyz( buffer, source, size );

	// Underscore a couple characters that cause issues in ref strings but
	// aren't handled by FS_GeneratePath
	while ( *buf_ptr ) {
		if ( *buf_ptr == ' ' || *buf_ptr == '@' ) {
			*buf_ptr = '_';
		}
		++buf_ptr;
	}

	FS_GeneratePath( buffer, NULL, NULL, 0, 0, 0, target, size );
}

/*
=================
FS_RefSet_GenerateEntry

pak can be null; other parameters are required.
=================
*/
static void FS_RefSet_GenerateEntry( reference_set_work_t *rsw, const char *mod_dir, const char *name,
			unsigned int hash, const fsc_file_direct_t *pak_file, reference_set_entry_t *target ) {
	fsc_stream_t sort_stream = FSC_InitStream( target->sort_key, sizeof( target->sort_key ) );

	Com_Memset( target, 0, sizeof( *target ) );
	FS_RefSet_SanitizeString( mod_dir, target->l.mod_dir, sizeof( target->l.mod_dir ) );
	FS_RefSet_SanitizeString( name, target->l.name, sizeof( target->l.name ) );
	target->l.hash = hash;
	target->l.pak_file = pak_file;
	target->cluster = rsw->cluster;
	target->l.entry_id = rsw->entry_id_counter++;

	// Write command name debug string
	Q_strncpyz( target->l.command_name, rsw->command_name, sizeof( target->l.command_name ) );
	if ( FSC_Strlen( rsw->command_name ) >= sizeof( target->l.command_name ) ) {
		strcpy( target->l.command_name + sizeof( target->l.command_name ) - 4, "..." );
	}

	// Determine pak_file_name_match, which is added to the sort key to handle special cases
	//   e.g. if a pk3 is specified in the download manifest with a specific hash, and multiple pk3s exist
	//   in the filesystem with that hash, this sort value attempts to prioritize the physical pk3 closer to
	//   the user-specified name to be used as the physical download source file
	// 0 = no pak, 1 = no name match, 2 = case insensitive match, 3 = case sensitive match
	if ( pak_file ) {
		const char *pak_mod = (const char *)STACKPTR( pak_file->qp_mod_ptr );
		const char *pak_name = (const char *)STACKPTR( pak_file->f.qp_name_ptr );
		if ( !FSC_Strcmp( mod_dir, pak_mod ) && !FSC_Strcmp( name, pak_name ) ) {
			target->pak_file_name_match = 3;
		} else if ( !Q_stricmp( mod_dir, pak_mod ) && !Q_stricmp( name, pak_name ) ) {
			target->pak_file_name_match = 2;
		} else {
			target->pak_file_name_match = 1;
		}
	}

	// Write sort key
	{
		fs_modtype_t mod_type = FS_GetModType( target->l.mod_dir );
		unsigned int core_pak_priority = mod_type <= MODTYPE_BASE ? (unsigned int)FS_CorePk3Position( hash ) : 0;

		FS_WriteSortValue( ~target->cluster, &sort_stream );
		FS_WriteSortValue( mod_type > MODTYPE_BASE ? (unsigned int)mod_type : 0, &sort_stream );
		FS_WriteSortValue( core_pak_priority, &sort_stream );
		FS_WriteSortValue( (unsigned int)mod_type, &sort_stream );
		FS_WriteSortString( target->l.mod_dir, &sort_stream, qfalse );
		FS_WriteSortString( target->l.name, &sort_stream, qfalse );
		FS_WriteSortValue( target->pak_file_name_match, &sort_stream );
		target->sort_key_length = sort_stream.position;
	}
}

/*
=================
FS_RefSet_CompareEntry

Returns < 0 if e1 is higher precedence, > 0 if e2 is higher precedence.
=================
*/
static int FS_RefSet_CompareEntry( const reference_set_entry_t *e1, const reference_set_entry_t *e2 ) {
	return -FSC_Memcmp( e1->sort_key, e2->sort_key, e1->sort_key_length < e2->sort_key_length ?
			e1->sort_key_length : e2->sort_key_length );
}

/*
=================
FS_RefSet_InsertEntry

Inserts or updates reference entry into output reference set.
=================
*/
static void FS_RefSet_InsertEntry( reference_set_work_t *rsw, const char *mod_dir, const char *name,
		unsigned int hash, const fsc_file_direct_t *pak ) {
	reference_set_entry_t new_entry;
	fs_hashtable_iterator_t it;
	reference_set_entry_t *target_entry = NULL;

	// Peform some mod dir patching for download list
	if ( rsw->query->download ) {
		// Replace basemod with com_basegame since downloads aren't supposed
		// to go directly into basemod and clients may block it or have errors
		if ( !Q_stricmp( mod_dir, "basemod" ) ) {
			REF_DPRINTF( "[manifest processing] Replacing download mod directory 'basemod' with com_basegame\n" );
			mod_dir = com_basegame->string;
		}

		// Patch mod dir capitalization
		if ( !Q_stricmp( mod_dir, com_basegame->string ) ) {
			mod_dir = com_basegame->string;
		}
		if ( !Q_stricmp( mod_dir, FS_GetCurrentGameDir() ) ) {
			mod_dir = FS_GetCurrentGameDir();
		}
	}

	// Generate new entry
	FS_RefSet_GenerateEntry( rsw, mod_dir, name, hash, pak, &new_entry );

	// Print entry contents
	if ( fs.cvar.fs_debug_references->integer ) {
		REF_DPRINTF( "[manifest processing] Reference set entry created\n" );
		REF_DPRINTF( "  entry id: %i\n", new_entry.l.entry_id );
		REF_DPRINTF( "  source rule: %s\n", new_entry.l.command_name );
		REF_DPRINTF( "  path: %s/%s\n", new_entry.l.mod_dir, new_entry.l.name );
		REF_DPRINTF( "  hash: %i\n", (int)new_entry.l.hash );
		if ( new_entry.l.pak_file ) {
			char buffer[FS_FILE_BUFFER_SIZE];
			FS_FileToBuffer( (const fsc_file_t *)new_entry.l.pak_file, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
			REF_DPRINTF( "  pak file: %s\n", buffer );
			REF_DPRINTF( "  pak file name match: %u\n", new_entry.pak_file_name_match );
		} else {
			REF_DPRINTF( "  pak file: <none>\n" );
		}
		REF_DPRINTF( "  cluster: %i\n", new_entry.cluster );
	}

	// Check for invalid attributes
	if ( !*new_entry.l.mod_dir || !*new_entry.l.name || !new_entry.l.hash ) {
		REF_DPRINTF( "  result: Skipping download list entry due to invalid mod, name, or hash\n" );
		return;
	}

#ifndef STANDALONE
	// Exclude paks that fail the ID pak check from download list because clients won't download
	// them anyway and may throw an error
	if ( rsw->query->download ) {
		char buffer[256];
		Com_sprintf( buffer, sizeof( buffer ), "%s/%s", mod_dir, name );
		if ( FS_idPak( buffer, BASEGAME, FS_NODOWNLOAD_PAKS ) || FS_idPak( buffer, BASETA, FS_NODOWNLOAD_PAKS_TEAMARENA ) ) {
			REF_DPRINTF( "  result: Skipping download list entry due to ID pak name\n" );
			return;
		}
	}
#endif

	// Process block command
	if ( rsw->block_mode ) {
		if ( FS_Pk3List_Lookup( &rsw->block_set, hash ) ) {
			REF_DPRINTF( "  result: Hash already in block list\n" );
		} else {
			REF_DPRINTF( "  result: Hash added to block list\n" );
			FS_Pk3List_Insert( &rsw->block_set, hash );
		}
		return;
	}

	// Check if hash is blocked
	if ( FS_Pk3List_Lookup( &rsw->block_set, hash ) ) {
		REF_DPRINTF( "  result: Skipping entry due to hash in block list\n" );
		return;
	}

	// Look for existing entry with same hash
	it = FS_Hashtable_Iterate( &rsw->reference_set->h, hash, qfalse );
	while ( ( target_entry = (reference_set_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		if ( new_entry.l.hash == target_entry->l.hash ) {
			// Found entry; check if new entry is higher priority
			int compare_result = FS_RefSet_CompareEntry( &new_entry, target_entry );
			if ( fs.cvar.fs_debug_references->integer ) {
				if ( compare_result >= 0 ) {
					REF_DPRINTF( "  result: Duplicate hash - skipping entry due to existing %s precedence entry id %i\n",
								 compare_result > 0 ? "higher" : "equal", target_entry->l.entry_id );
				} else {
					REF_DPRINTF( "  result: Duplicate hash - overwriting existing lower precedence entry id %i\n",
								 target_entry->l.entry_id );
				}
			}
			if ( compare_result < 0 ) {
				*target_entry = new_entry;
			}
			return;
		}
	}

	// Check for excess element count
	if ( rsw->reference_set->h.element_count >= MAX_REFERENCE_SET_ENTRIES ) {
		REF_DPRINTF( "  result: Skipping entry due to MAX_REFERENCE_SET_ENTRIES hit\n" );
		rsw->overflowed = qtrue;
		return;
	}

	// Save the entry
	REF_DPRINTF( "  result: Added entry to reference set\n" );
	target_entry = (reference_set_entry_t *)Z_Malloc( sizeof( *target_entry ) );
	*target_entry = new_entry;
	FS_Hashtable_Insert( &rsw->reference_set->h, (fs_hashtable_entry_t *)target_entry, target_entry->l.hash );
}

/*
=================
FS_RefSet_InsertPak

Add a particular pak file to the reference set.
=================
*/
static void FS_RefSet_InsertPak( reference_set_work_t *rsw, const fsc_file_direct_t *pak ) {
	FS_RefSet_InsertEntry( rsw, (const char *)STACKPTR( pak->qp_mod_ptr ),
			(const char *)STACKPTR( pak->f.qp_name_ptr ), pak->pk3_hash, pak );
}

/*
=================
FS_RefSet_AddReferencedPaks

Add all current referenced paks to the reference set.
=================
*/
static void FS_RefSet_AddReferencedPaks( reference_set_work_t *rsw ) {
	fs_hashtable_iterator_t it = FS_Hashtable_Iterate( &reference_tracker, 0, qtrue );
	reference_tracker_entry_t *entry;
	while ( ( entry = (reference_tracker_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		// The #referenced_paks rule explicitly excludes paks not in basegame or mod directories,
		// regardless of fs_read_inactive_mods or servercfg directory status
		if ( FS_GetModType( FSC_GetModDir( (fsc_file_t *)entry->pak, &fs.index ) ) <= MODTYPE_INACTIVE ) {
			continue;
		}
		FS_RefSet_InsertPak( rsw, entry->pak );
	}
}

/*
=================
FS_RefSet_AddPakContainingFile

Add the pak containing the specified file to the reference set.
=================
*/
static void FS_RefSet_AddPakContainingFile( reference_set_work_t *rsw, const char *name ) {
	const fsc_file_t *file = FS_GeneralLookup( name,
			LOOKUPFLAG_IGNORE_CURRENT_MAP | LOOKUPFLAG_PK3_SOURCE_ONLY | LOOKUPFLAG_IGNORE_SERVERCFG, qfalse );
	if ( !file || file->sourcetype != FSC_SOURCETYPE_PK3 ) {
		return;
	}
	FS_RefSet_InsertPak( rsw, FSC_GetBaseFile( file, &fs.index ) );
}

typedef enum {
	PAKCATEGORY_ACTIVE_MOD,
	PAKCATEGORY_BASEGAME,
	PAKCATEGORY_INACTIVE_MOD
} pakcategory_t;

/*
=================
FS_RefSet_GetPakCategory
=================
*/
static pakcategory_t FS_RefSet_GetPakCategory( const fsc_file_direct_t *pak ) {
	int mod_type = FS_GetModType( FSC_GetModDir( (fsc_file_t *)pak, &fs.index ) );
	if ( mod_type >= MODTYPE_CURRENT_MOD )
		return PAKCATEGORY_ACTIVE_MOD;
	if ( mod_type >= MODTYPE_BASE )
		return PAKCATEGORY_BASEGAME;
	return PAKCATEGORY_INACTIVE_MOD;
}

/*
=================
FS_RefSet_AddPaksFromCategory

Add all loaded paks in specified category to the pak set.
=================
*/
static void FS_RefSet_AddPaksFromCategory( reference_set_work_t *rsw, pakcategory_t category ) {
	fsc_pk3_iterator_t it = FSC_Pk3IteratorOpenAll( &fs.index );
	while ( FSC_Pk3IteratorAdvance( &it ) ) {
		// The #inactivemod_paks rule explicitly follows the fs_read_inactive_mods setting in order for
		//    fs_read_inactive_mods to work in the expected way when using the default pure manifest
		// Note: Pure list from a previous client session should be cleared at this point in the map load process,
		//    so the potential pure list check in FD_CHECK_READ_INACTIVE_MODS should not be a factor here.
		if ( FS_CheckFileDisabled( (fsc_file_t *)it.pk3, FD_CHECK_READ_INACTIVE_MODS_IGNORE_SERVERCFG ) )
			continue;
		if ( FS_RefSet_GetPakCategory( it.pk3 ) != category )
			continue;
		FS_RefSet_InsertPak( rsw, it.pk3 );
	}
}

/*
=================
FS_RefSet_StringToHash

Converts a user-specified string (signed or unsigned) to hash value.
Returns 0 on error, hash otherwise.
=================
*/
static unsigned int FS_RefSet_StringToHash( const char *string ) {
	char test_buffer[16];
	if ( *string == '-' ) {
		unsigned int hash = (unsigned int)atoi( string );
		Com_sprintf( test_buffer, sizeof( test_buffer ), "%i", (int)hash );
		if ( FSC_Strcmp( string, test_buffer ) ) {
			return 0;
		}
		return hash;
	} else {
		unsigned int hash = strtoul( string, NULL, 10 );
		Com_sprintf( test_buffer, sizeof( test_buffer ), "%u", hash );
		if ( FSC_Strcmp( string, test_buffer ) ) {
			return 0;
		}
		return hash;
	}
}

typedef struct {
	char mod_dir[FSC_MAX_MODDIR];
	char name[FSC_MAX_QPATH];
	unsigned int hash;	// 0 if hash not manually specified
} pak_specifier_t;

/*
=================
FS_RefSet_ParseSpecifier

Converts specifier string to pak_specifier_t structure.
Returns qtrue on success, prints warning and returns qfalse on error.
=================
*/
static qboolean FS_RefSet_ParseSpecifier( const char *command_name, const char *string, pak_specifier_t *output ) {
	char buffer[FSC_MAX_MODDIR + FSC_MAX_QPATH];
	const char *hash_ptr = strchr( string, ':' );
	const char *name_ptr = NULL;

	if ( hash_ptr ) {
		// Copy section before colon to buffer
		unsigned int length = (unsigned int)( hash_ptr - string );
		if ( length >= sizeof( buffer ) ) {
			length = sizeof( buffer ) - 1;
		}
		FSC_Memcpy( buffer, string, length );
		buffer[length] = '\0';

		// Acquire hash value
		output->hash = FS_RefSet_StringToHash( hash_ptr + 1 );
		if ( !output->hash ) {
			Com_Printf( "WARNING: Error reading hash for specifier '%s'\n", command_name );
			return qfalse;
		}
	} else {
		Q_strncpyz( buffer, string, sizeof( buffer ) );
		output->hash = 0;
	}

	FSC_SplitLeadingDirectory( buffer, output->mod_dir, sizeof( output->mod_dir ), &name_ptr );
	if ( !*output->mod_dir ) {
		Com_Printf( "WARNING: Error reading mod directory for specifier '%s'\n", command_name );
		return qfalse;
	}
	if ( !name_ptr || !*name_ptr || strchr( name_ptr, '/' ) || strchr( name_ptr, '\\' ) ) {
		Com_Printf( "WARNING: Error reading pk3 name for specifier '%s'\n", command_name );
		return qfalse;
	}
	Q_strncpyz( output->name, name_ptr, sizeof( output->name ) );

	return qtrue;
}

/*
=================
FS_RefSet_ProcessSpecifierByName

Process a pak specifier in format <mod dir>/<name>
=================
*/
static void FS_RefSet_ProcessSpecifierByName( reference_set_work_t *rsw, const char *string ) {
	pak_specifier_t specifier;
	int count = 0;
	fsc_file_iterator_t it;

	if ( !FS_RefSet_ParseSpecifier( rsw->command_name, string, &specifier ) ) {
		return;
	}
	FSC_ASSERT( !specifier.hash );

	// Search for pk3s matching name
	it = FSC_FileIteratorOpen( &fs.index, "", specifier.name );
	while ( FSC_FileIteratorAdvance( &it ) ) {
		const fsc_file_direct_t *file = (const fsc_file_direct_t *)it.file;
		if ( file->f.sourcetype != FSC_SOURCETYPE_DIRECT )
			continue;
		if ( !file->pk3_hash )
			continue;
		if ( Q_stricmp( FSC_GetModDir( (const fsc_file_t *)file, &fs.index ), specifier.mod_dir ) )
			continue;
		FS_RefSet_InsertEntry( rsw, specifier.mod_dir, specifier.name, file->pk3_hash, file );
		++count;
	}

	if ( count == 0 ) {
		Com_Printf( "WARNING: Specifier '%s' failed to match any pk3s.\n", rsw->command_name );
	} else if ( count > 1 ) {
		Com_Printf( "WARNING: Specifier '%s' matched multiple pk3s.\n", rsw->command_name );
	}
}

/*
=================
FS_RefSet_ProcessSpecifierByHash

Process a pak specifier in format <mod dir>/<name>:<hash>
=================
*/
static void FS_RefSet_ProcessSpecifierByHash( reference_set_work_t *rsw, const char *string ) {
	pak_specifier_t specifier;
	int count = 0;
	fsc_pk3_iterator_t it;

	if ( !FS_RefSet_ParseSpecifier( rsw->command_name, string, &specifier ) ) {
		return;
	}
	FSC_ASSERT( specifier.hash );

	// Search for physical pk3s matching hash
	it = FSC_Pk3IteratorOpen( &fs.index, specifier.hash );
	while ( FSC_Pk3IteratorAdvance( &it ) ) {
		FS_RefSet_InsertEntry( rsw, specifier.mod_dir, specifier.name, specifier.hash, it.pk3 );
		++count;
	}

	// If no actual pak was found, create a hash-only entry
	if ( !count ) {
		FS_RefSet_InsertEntry( rsw, specifier.mod_dir, specifier.name, specifier.hash, NULL );
		++count;
	}
}

/*
=================
FS_RefSet_PatternMatch

Returns qtrue if string matches pattern containing '*' and '?' wildcards.
=================
*/
static qboolean FS_RefSet_PatternMatch( const char *string, const char *pattern ) {
	while ( 1 ) {
		if ( *pattern == '*' ) {
			// Skip asterisks; auto match if no pattern remaining
			while ( *pattern == '*' ) {
				++pattern;
			}
			if ( !*pattern ) {
				return qtrue;
			}

			// Read string looking for match with remaining pattern
			while ( *string ) {
				if ( *string == *pattern || *pattern == '?' ) {
					if ( FS_RefSet_PatternMatch( string + 1, pattern + 1 ) ) {
						return qtrue;
					}
				}
				++string;
			}

			// Leftover pattern with no match
			return qfalse;
		}

		// Check for end of string cases
		if ( !*pattern ) {
			if ( !*string ) {
				return qtrue;
			}
			return qfalse;
		}
		if ( !*string ) {
			return qfalse;
		}

		// Check for character discrepancy
		if ( *pattern != *string && *pattern != '?' && *pattern != *string ) {
			return qfalse;
		}

		// Advance strings
		++pattern;
		++string;
	}
}

/*
=================
FS_RefSet_ProcessSpecifierByWildcard

Process a pak specifier in format <mod dir>/<name> containing wildcard characters.
=================
*/
static void FS_RefSet_ProcessSpecifierByWildcard( reference_set_work_t *rsw, const char *string ) {
	int count = 0;
	fsc_pk3_iterator_t it;
	char specifier_buffer[FSC_MAX_MODDIR + FSC_MAX_QPATH];
	char file_buffer[FSC_MAX_MODDIR + FSC_MAX_QPATH];
	char *z = specifier_buffer;

	Q_strncpyz( specifier_buffer, string, sizeof( specifier_buffer ) );
	Q_strlwr( specifier_buffer );
	while ( *z ) {
		if ( *z == '\\' ) {
			*z = '/';
		}
		++z;
	}

	// Iterate all pk3s in filesystem for potential matches
	it = FSC_Pk3IteratorOpenAll( &fs.index );
	while ( FSC_Pk3IteratorAdvance( &it ) ) {
		const char *mod_dir = FSC_GetModDir( (fsc_file_t *)it.pk3, &fs.index );
		const char *name = (const char *)STACKPTR( it.pk3->f.qp_name_ptr );

		// Check pattern match
		Com_sprintf( file_buffer, sizeof( file_buffer ), "%s/%s", mod_dir, name );
		Q_strlwr( file_buffer );
		if ( !FS_RefSet_PatternMatch( file_buffer, specifier_buffer ) ) {
			continue;
		}

		// Add pk3 to reference set
		FS_RefSet_InsertEntry( rsw, mod_dir, name, it.pk3->pk3_hash, it.pk3 );
		++count;
	}

	if ( count == 0 ) {
		Com_Printf( "WARNING: Specifier '%s' failed to match any pk3s.\n", rsw->command_name );
	}
}

/*
=================
FS_RefSet_ProcessSpecifier

Process pk3 specifier of any supported type (mod/name, mod/name:hash, wildcard)
=================
*/
static void FS_RefSet_ProcessSpecifier( reference_set_work_t *rsw, const char *string ) {
	if ( strchr( string, '*' ) || strchr( string, '?' ) ) {
		FS_RefSet_ProcessSpecifierByWildcard( rsw, string );
	} else if ( strchr( string, ':' ) ) {
		FS_RefSet_ProcessSpecifierByHash( rsw, string );
	} else {
		FS_RefSet_ProcessSpecifierByName( rsw, string );
	}
}

/*
=================
FS_RefSet_ProcessManifest
=================
*/
static void FS_RefSet_ProcessManifest( reference_set_work_t *rsw, const char *string, int recursion_count ) {
	while ( 1 ) {
		const char *token = COM_ParseExt( (char **)&string, qfalse );
		if ( !*token ) {
			break;
		}

		// Process special commands
		if ( !Q_stricmp( token, "&cvar_import" ) ) {
			// Static buffer from COM_ParseExt will be overwritten, so copy out cvar name
			char cvar_name[256];
			token = COM_ParseExt( (char **)&string, qfalse );
			Q_strncpyz( cvar_name, token, sizeof( cvar_name ) );

			if ( recursion_count >= 128 ) {
				Com_Error( ERR_DROP, "Recursive overflow processing pk3 manifest" );
			}
			REF_DPRINTF( "[manifest processing] Entering import cvar '%s'\n", cvar_name );
			FS_RefSet_ProcessManifest( rsw, Cvar_VariableString( cvar_name ), recursion_count + 1 );
			REF_DPRINTF( "[manifest processing] Leaving import cvar '%s'\n", cvar_name );
			continue;
		} else if ( !Q_stricmp( token, "&block" ) ) {
			REF_DPRINTF( "[manifest processing] Blocking next selector due to 'block' command\n" );
			rsw->block_mode = qtrue;
			continue;
		} else if ( !Q_stricmp( token, "&block_reset" ) ) {
			REF_DPRINTF( "[manifest processing] Resetting blocked hash set.\n" );
			FS_Pk3List_Free( &rsw->block_set );
			FS_Pk3List_Initialize( &rsw->block_set, 64 );
			continue;
		} else if ( !Q_stricmp( token, "-" ) ) {
			++rsw->cluster;
			continue;
		}

		// Process selector commands
		Q_strncpyz( rsw->command_name, token, sizeof( rsw->command_name ) );
		REF_DPRINTF( "[manifest processing] Processing selector '%s'\n", rsw->command_name );
		if ( !Q_stricmp( token, "#mod_paks" ) ) {
			FS_RefSet_AddPaksFromCategory( rsw, PAKCATEGORY_ACTIVE_MOD );
		} else if ( !Q_stricmp( token, "#base_paks" ) ) {
			FS_RefSet_AddPaksFromCategory( rsw, PAKCATEGORY_BASEGAME );
		} else if ( !Q_stricmp( token, "#inactivemod_paks" ) ) {
			FS_RefSet_AddPaksFromCategory( rsw, PAKCATEGORY_INACTIVE_MOD );
		} else if ( !Q_stricmp( token, "#referenced_paks" ) ) {
			FS_RefSet_AddReferencedPaks( rsw );
		} else if ( !Q_stricmp( token, "#currentmap_pak" ) ) {
			FS_RefSet_AddPakContainingFile( rsw, va( "maps/%s.bsp", Cvar_VariableString( "mapname" ) ) );
		} else if ( !Q_stricmp( token, "#cgame_pak" ) ) {
			FS_RefSet_AddPakContainingFile( rsw, "vm/cgame.qvm" );
		} else if ( !Q_stricmp( token, "#ui_pak" ) ) {
			FS_RefSet_AddPakContainingFile( rsw, "vm/ui.qvm" );
		} else if ( *token == '#' || *token == '&' ) {
			Com_Printf( "WARNING: Unrecognized manifest selector '%s'\n", token );
		} else {
			FS_RefSet_ProcessSpecifier( rsw, token );
		}

		// Reset single-use modifiers
		rsw->block_mode = qfalse;
	}
}

/*
=================
FS_RefSet_GetUninitialized
=================
*/
static reference_set_t FS_RefSet_GetUninitialized( void ) {
	reference_set_t result = { REFSTATE_UNINITIALIZED };
	return result;
}

/*
=================
FS_RefSet_Generate

Generates reference set for given query.
Result must be freed by FS_RefSet_Free.
=================
*/
static reference_set_t FS_RefSet_Generate( const reference_query_t *query ) {
	reference_set_t output = FS_RefSet_GetUninitialized();
	reference_set_work_t rsw;

	// Initialize output
	output.state = REFSTATE_VALID;
	FS_Hashtable_Initialize( &output.h, MAX_REFERENCE_SET_ENTRIES );

	// Initialize rsw
	Com_Memset( &rsw, 0, sizeof( rsw ) );
	rsw.query = query;
	rsw.reference_set = &output;
	FS_Pk3List_Initialize( &rsw.block_set, 64 );

	// Invoke manifest processing
	FS_RefSet_ProcessManifest( &rsw, query->manifest, 0 );

	// Free rsw
	FS_Pk3List_Free( &rsw.block_set );

	if ( rsw.overflowed ) {
		// Clear structure in case of overflow
		FS_Hashtable_Free( &output.h, NULL );
		output = FS_RefSet_GetUninitialized();
		output.state = REFSTATE_OVERFLOWED;
	}

	return output;
}

/*
=================
FS_RefSet_Free
=================
*/
static void FS_RefSet_Free( reference_set_t *reference_set ) {
	if ( reference_set->state == REFSTATE_VALID ) {
		FS_Hashtable_Free( &reference_set->h, NULL );
	}
}

/*
###############################################################################################

Reference List Generation

This section is used to convert a reference set to a reference list.

###############################################################################################
*/

typedef struct {
	// REFSTATE_VALID: Entries will be valid pointer, entry count >= 0
	// REFSTATE_UNINITIALIZED / REFSTATE_OVERFLOWED: Entries not valid pointer, entry count == 0
	reference_state_t state;
	reference_list_entry_t *entries;
	int entry_count;
} reference_list_t;

/*
=================
FS_RefList_SortFunction
=================
*/
static int FS_RefList_SortFunction( const void *e1, const void *e2 ) {
	return FS_RefSet_CompareEntry( *(const reference_set_entry_t **)e1, *(const reference_set_entry_t **)e2 );
}

/*
=================
FS_RefList_GetUninitialized
=================
*/
static reference_list_t FS_RefList_GetUninitialized( void ) {
	reference_list_t result = { REFSTATE_UNINITIALIZED };
	return result;
}

/*
=================
FS_RefList_Generate

Converts reference set to reference list.
Result must be freed by FS_RefList_Free.
=================
*/
static reference_list_t FS_RefList_Generate( const reference_set_t *reference_set ) {
	reference_list_t reference_list = FS_RefList_GetUninitialized();

	if ( reference_set->state == REFSTATE_VALID ) {
		// Generate temp entries
		int i;
		int count = 0;
		reference_set_entry_t *entry;
		fs_hashtable_iterator_t it = FS_Hashtable_Iterate( (fs_hashtable_t *)&reference_set->h, 0, qtrue );
		reference_set_entry_t *temp_entries[MAX_REFERENCE_SET_ENTRIES];
		FSC_ASSERT( reference_set->h.element_count <= MAX_REFERENCE_SET_ENTRIES );
		while ( ( entry = (reference_set_entry_t *)FS_Hashtable_Next( &it ) ) ) {
			FSC_ASSERT( count < reference_set->h.element_count );
			temp_entries[count] = entry;
			++count;
		}
		FSC_ASSERT( count == reference_set->h.element_count );

		// Sort temp entries
		qsort( temp_entries, count, sizeof( *temp_entries ), FS_RefList_SortFunction );

		// Initialize reference list
		reference_list.state = REFSTATE_VALID;
		reference_list.entries = (reference_list_entry_t *)Z_Malloc( sizeof( *reference_list.entries ) * count );
		reference_list.entry_count = count;

		// Copy reference list entries
		for ( i = 0; i < count; ++i ) {
			reference_list.entries[i] = temp_entries[i]->l;
		}
	}

	else {
		// Just leave the list uninitialized
		reference_list.state = reference_set->state;
	}

	return reference_list;
}

/*
=================
FS_RefList_Free
=================
*/
static void FS_RefList_Free( reference_list_t *reference_list ) {
	if ( reference_list->state == REFSTATE_VALID ) {
		Z_Free( reference_list->entries );
	}
}

/*
###############################################################################################

Reference String Generation

This section is used to convert a reference list to reference strings.

###############################################################################################
*/

typedef struct {
	// REFSTATE_VALID: String will be pointer in Z_Malloc, length >= 0
	// REFSTATE_UNINITIALIZED / REFSTATE_OVERFLOWED: String equals static "", length == 0
	reference_state_t state;
	char *string;
	unsigned int length;	// strlen(string)
} reference_substring_t;

typedef struct {
	reference_substring_t name;
	reference_substring_t hash;
} reference_strings_t;

/*
=================
FS_RefStrings_GenerateSubstring

Convert source stream to reference string structure.
=================
*/
static reference_substring_t FS_RefStrings_GenerateSubstring( fsc_stream_t *source ) {
	reference_substring_t output = { REFSTATE_UNINITIALIZED, (char *)"", 0 };
	if ( source->overflowed ) {
		output.state = REFSTATE_OVERFLOWED;
	}
	if ( source->position && !source->overflowed ) {
		output.state = REFSTATE_VALID;
		output.length = source->position;
		output.string = (char *)Z_Malloc( output.length + 1 );
		FSC_Memcpy( output.string, source->data, output.length );
		output.string[output.length] = '\0';
	}
	return output;
}

/*
=================
FS_RefStrings_FreeSubstring
=================
*/
static void FS_RefStrings_FreeSubstring( reference_substring_t *substring ) {
	if ( substring->state == REFSTATE_VALID ) {
		Z_Free( substring->string );
	}
}

/*
=================
FS_RefStrings_GetUninitialized
=================
*/
static reference_strings_t FS_RefStrings_GetUninitialized( void ) {
	reference_strings_t result = {
		{ REFSTATE_UNINITIALIZED, (char *)"", 0 },
		{ REFSTATE_UNINITIALIZED, (char *)"", 0 }
	};
	return result;
}

/*
=================
FS_RefStrings_Generate

Result must be freed by FS_RefStrings_Free.
=================
*/
static reference_strings_t FS_RefStrings_Generate( reference_list_t *reference_list, unsigned int max_length ) {
	reference_strings_t output = FS_RefStrings_GetUninitialized();

	if ( reference_list->state != REFSTATE_VALID ) {
		if ( reference_list->state == REFSTATE_OVERFLOWED ) {
			// Copy overflowed state to string outputs
			output.hash.state = output.name.state = REFSTATE_OVERFLOWED;
		}
	}

	else {
		int i;
		char buffer[512];
		fsc_stream_t name_stream = FSC_InitStream( (char *)Z_Malloc( max_length ), max_length );
		fsc_stream_t hash_stream = FSC_InitStream( (char *)Z_Malloc( max_length ), max_length );

		// Generate strings
		for ( i = 0; i < reference_list->entry_count; ++i ) {
			const reference_list_entry_t *entry = &reference_list->entries[i];
			if ( entry->disabled ) {
				continue;
			}

			Com_sprintf( buffer, sizeof( buffer ), "%i", (int)entry->hash );
			if ( hash_stream.position ) {
				FSC_StreamAppendString( &hash_stream, " " );
			}
			FSC_StreamAppendString( &hash_stream, buffer );

			Com_sprintf( buffer, sizeof( buffer ), "%s/%s", entry->mod_dir, entry->name );
			if ( name_stream.position ) {
				FSC_StreamAppendString( &name_stream, " " );
			}
			FSC_StreamAppendString( &name_stream, buffer );
		}

		// Transfer strings to output structure
		output.hash = FS_RefStrings_GenerateSubstring( &hash_stream );
		Z_Free( hash_stream.data );
		output.name = FS_RefStrings_GenerateSubstring( &name_stream );
		Z_Free( name_stream.data );
	}

	return output;
}

/*
=================
FS_RefStrings_Free
=================
*/
static void FS_RefStrings_Free( reference_strings_t *reference_strings ) {
	FS_RefStrings_FreeSubstring( &reference_strings->hash );
	FS_RefStrings_FreeSubstring( &reference_strings->name );
}

/*
###############################################################################################

Download Map Handling

The download map is used to match client download requests to the actual file
on the server, since the download list name may not match the server filename.

###############################################################################################
*/

typedef fs_hashtable_t fs_download_map_t;

typedef struct {
	fs_hashtable_entry_t hte;
	char *name;
	const fsc_file_direct_t *pak;
} download_map_entry_t;

/*
=================
FS_DLMap_FreeEntry
=================
*/
static void FS_DLMap_FreeEntry( fs_hashtable_entry_t *entry ) {
	Z_Free( ( (download_map_entry_t *)entry )->name );
	Z_Free( entry );
}

/*
=================
FS_DLMap_AddEntry
=================
*/
static void FS_DLMap_AddEntry( fs_download_map_t *dlmap, const char *path, const fsc_file_direct_t *pak ) {
	download_map_entry_t *entry = (download_map_entry_t *)Z_Malloc( sizeof( *entry ) );
	entry->name = CopyString( path );
	entry->pak = pak;
	FS_Hashtable_Insert( dlmap, (fs_hashtable_entry_t *)entry, FSC_StringHash( path, NULL ) );
}

/*
=================
FS_DLMap_Free
=================
*/
static void FS_DLMap_Free( fs_download_map_t *dlmap ) {
	FS_Hashtable_Free( dlmap, FS_DLMap_FreeEntry );
	Z_Free( dlmap );
}

/*
=================
FS_DLMap_Generate
=================
*/
static fs_download_map_t *FS_DLMap_Generate( const reference_list_t *reference_list ) {
	int i;
	char buffer[512];
	fs_download_map_t *dlmap = (fs_download_map_t *)Z_Malloc( sizeof( *dlmap ) );
	FS_Hashtable_Initialize( dlmap, 16 );
	for ( i = 0; i < reference_list->entry_count; ++i ) {
		const reference_list_entry_t *entry = &reference_list->entries[i];
		if ( entry->disabled ) {
			continue;
		}
		if ( !entry->pak_file ) {
			continue;
		}
		Com_sprintf( buffer, sizeof( buffer ), "%s/%s.pk3", entry->mod_dir, entry->name );
		FS_DLMap_AddEntry( dlmap, buffer, entry->pak_file );
	}
	return dlmap;
}

/*
=================
FS_DLMap_OpenPak

Locates entry matching path in download map and opens file handle.
Returns null on error or if not found.
=================
*/
static fileHandle_t FS_DLMap_OpenPak( fs_download_map_t *dlmap, const char *path, unsigned int *size_out ) {
	fs_hashtable_iterator_t it = FS_Hashtable_Iterate( dlmap, FSC_StringHash( path, NULL ), qfalse );
	download_map_entry_t *entry;

	while ( ( entry = (download_map_entry_t *)FS_Hashtable_Next( &it ) ) ) {
		if ( !Q_stricmp( entry->name, path ) ) {
			return FS_DirectReadHandle_Open( (fsc_file_t *)entry->pak, NULL, size_out );
		}
	}

	return 0;
}

/*
###############################################################################################

Main Download / Pure List Generation

###############################################################################################
*/

// Current download map
static fs_download_map_t *download_map;

/*
=================
FS_QueryToReferenceList
=================
*/
static reference_list_t FS_QueryToReferenceList( const reference_query_t *query ) {
	reference_set_t reference_set = FS_RefSet_Generate( query );
	reference_list_t reference_list = FS_RefList_Generate( &reference_set );
	FS_RefSet_Free( &reference_set );
	return reference_list;
}

/*
=================
FS_IsHashInReferenceList
=================
*/
static qboolean FS_IsHashInReferenceList( reference_list_t *reference_list, unsigned int hash ) {
	int i;
	for ( i = 0; i < reference_list->entry_count; ++i ) {
		if ( reference_list->entries[i].hash == hash ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
FS_GenerateReferenceLists

Generate download and pure lists for server and set appropriate cvars.
=================
*/
void FS_GenerateReferenceLists( void ) {
	int i, j;
	reference_query_t download_query = { fs.cvar.fs_download_manifest->string, qtrue };
	reference_query_t pure_query = { fs.cvar.fs_pure_manifest->string, qfalse };
	reference_list_t download_list = FS_RefList_GetUninitialized();
	reference_list_t pure_list = FS_RefList_GetUninitialized();
	reference_strings_t download_strings = FS_RefStrings_GetUninitialized();
	reference_strings_t pure_strings = FS_RefStrings_GetUninitialized();
	qboolean download_valid = qtrue;
	qboolean pure_valid = qfalse;
	qboolean pure_names_valid = qfalse;

	// Need to clear cvars here for the systeminfo length checks to work properly
	Cvar_Set( "sv_paks", "" );
	Cvar_Set( "sv_pakNames", "" );
	Cvar_Set( "sv_referencedPaks", "" );
	Cvar_Set( "sv_referencedPakNames", "" );

	// Generate download list
	Com_Printf( "Generating download list...\n" );
	FS_DebugIndentStart();
	download_list = FS_QueryToReferenceList( &download_query );
	FS_DebugIndentStop();
	Com_Printf( "%i paks listed\n", download_list.entry_count );

	// Verify download list
	for ( i = 0; i < download_list.entry_count; ++i ) {
		int allowDownload = Cvar_VariableIntegerValue( "sv_allowDownload" );
		reference_list_entry_t *entry = &download_list.entries[i];

		// Check for entry with duplicate filename
		for ( j = 0; j < i; ++j ) {
			reference_list_entry_t *entry2 = &download_list.entries[j];
			if ( entry2->disabled ) {
				continue;
			}
			if ( !Q_stricmp( entry->mod_dir, entry2->mod_dir ) && !Q_stricmp( entry->name, entry2->name ) ) {
				break;
			}
		}
		if ( j < i ) {
			Com_Printf( "WARNING: Skipping download list pak '%s/%s' with same filename but"
					" different hash as another entry.\n", entry->mod_dir, entry->name );
			entry->disabled = qtrue;
			continue;
		}

		// Print warning if file is physically unavailable
		if ( !entry->pak_file && allowDownload && !( allowDownload & DLF_NO_UDP ) ) {
			Com_Printf( "WARNING: Download list pak '%s/%s' from command '%s' was not found on the server."
					" Attempts to download this file via UDP will result in an error.\n",
					entry->mod_dir, entry->name, entry->command_name );
		}

		// Print warning if pak is from an inactive mod dir
		if ( FS_GetModType( entry->mod_dir ) <= MODTYPE_INACTIVE ) {
			Com_Printf( "WARNING: Download list pak '%s/%s' from command '%s' is from an inactive mod dir."
					" This can cause problems for some clients. Consider moving this file or changing the"
					" active mod to include it.\n",
					entry->mod_dir, entry->name, entry->command_name );
		}
	}

	// Generate download strings
	download_strings = FS_RefStrings_Generate( &download_list, MAX_DOWNLOAD_LIST_STRING );

	// Check for download list overflow
	if ( download_strings.hash.state == REFSTATE_OVERFLOWED || download_strings.name.state == REFSTATE_OVERFLOWED ) {
		Com_Printf( "WARNING: Download list overflowed\n" );
		download_valid = qfalse;
	}

	if ( Cvar_VariableIntegerValue( "sv_pure" ) ) {
		int systeminfo_base_length = FSC_Strlen( Cvar_InfoString_Big( CVAR_SYSTEMINFO ) );
		int download_base_length = download_valid ? download_strings.name.length + download_strings.hash.length : 0;
		pure_valid = pure_names_valid = qtrue;

		// Generate pure list
		Com_Printf( "Generating pure list...\n" );
		FS_DebugIndentStart();
		pure_list = FS_QueryToReferenceList( &pure_query );
		FS_DebugIndentStop();
		Com_Printf( "%i paks listed\n", pure_list.entry_count );

		// Generate pure strings
		pure_strings = FS_RefStrings_Generate( &pure_list, MAX_PURE_LIST_STRING );

		// Check for pure list hash overflow
		if ( pure_strings.hash.state == REFSTATE_OVERFLOWED ) {
			Com_Printf( "WARNING: Setting sv_pure to 0 due to pure list overflow. Remove some"
					" paks from the server or adjust the pure manifest if you want to use sv_pure.\n" );
			pure_valid = pure_names_valid = qfalse;
		}

		// Check for empty pure list
		if ( pure_valid && pure_list.entry_count == 0 ) {
			Com_Printf( "WARNING: Setting sv_pure to 0 due to empty pure list.\n" );
			pure_valid = pure_names_valid = qfalse;
		}

		// Check for pure list hash systeminfo overflow
		if ( pure_valid && systeminfo_base_length + download_base_length + pure_strings.hash.length +
				SYSTEMINFO_RESERVED_SIZE >= BIG_INFO_STRING ) {
			Com_Printf( "WARNING: Setting sv_pure to 0 due to systeminfo overflow. Remove some"
					" paks from the server or adjust the pure manifest if you want to use sv_pure.\n" );
			pure_valid = pure_names_valid = qfalse;
		}

		// Check for pure list names output overflow
		if ( pure_names_valid && pure_strings.name.state == REFSTATE_OVERFLOWED ) {
			Com_Printf( "NOTE: Not writing optional sv_pakNames value due to list overflow.\n" );
			pure_names_valid = qfalse;
		}

		// Check for pure list names systeminfo overflow
		if ( pure_names_valid && systeminfo_base_length + download_base_length + pure_strings.hash.length +
				pure_strings.name.length + SYSTEMINFO_RESERVED_SIZE >= BIG_INFO_STRING ) {
			Com_Printf( "NOTE: Not writing optional sv_pakNames value due to systeminfo overflow.\n" );
			pure_names_valid = qfalse;
		}
	}

	if ( download_valid && pure_valid ) {
		// Check for download entries not on pure list
		for ( i = 0; i < download_list.entry_count; ++i ) {
			reference_list_entry_t *entry = &download_list.entries[i];
			if ( !FS_IsHashInReferenceList( &pure_list, entry->hash ) ) {
				Com_Printf( "WARNING: Download list pak '%s/%s' is missing from the pure list"
						" and may not be loaded by clients.\n", entry->mod_dir, entry->name );
			}
		}
	}

	// Write output cvars
	if ( download_valid ) {
		Cvar_Set( "sv_referencedPaks", download_strings.hash.string );
		Cvar_Set( "sv_referencedPakNames", download_strings.name.string );
	}
	if ( pure_valid ) {
		Cvar_Set( "sv_paks", pure_strings.hash.string );
	}
	if ( pure_names_valid ) {
		Cvar_Set( "sv_pakNames", pure_strings.name.string );
	}
	if ( !pure_valid ) {
		// This may not technically be necessary, since empty sv_paks should be sufficient
		// to make the server unpure, but set this as well for consistency
		Cvar_Set( "sv_pure", "0" );
	}

	// Update download map
	if ( download_map ) {
		FS_DLMap_Free( download_map );
	}
	download_map = NULL;
	if ( download_valid ) {
		download_map = FS_DLMap_Generate( &download_list );
	}

	// Free temporary structures
	FS_RefList_Free( &download_list );
	FS_RefList_Free( &pure_list );
	FS_RefStrings_Free( &download_strings );
	FS_RefStrings_Free( &pure_strings );
}

/*
###############################################################################################

Misc functions

###############################################################################################
*/

/*
=================
FS_OpenDownloadPak

Opens a pak on the server for a client UDP download.
=================
*/
fileHandle_t FS_OpenDownloadPak( const char *path, unsigned int *size_out ) {
	if ( download_map ) {
		return FS_DLMap_OpenPak( download_map, path, size_out );
	}
	return 0;
}

#endif	// NEW_FILESYSTEM
