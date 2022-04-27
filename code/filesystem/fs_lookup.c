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

#define ADD_STRING(string) FSC_StreamAppendString(stream, string)
#define ADD_STRINGL(string) FSC_StreamAppendString(&stream, string)

typedef struct {
	// File lookup
	const char *qp_name;	// null to disable file lookup
	const char *qp_dir;
	const char **qp_exts;
	int extension_count;

	// Shader lookup
	const char *shader_name;	// null to disable shader lookup

	// Lookup flags
	int lookup_flags;

	// Special
	qboolean dll_query;
#ifdef CMOD_QVM_SELECTION
	qboolean cmod_qvm_query;
#endif
} lookup_query_t;

#define RESFLAG_IN_DOWNLOAD_PK3 1
#define RESFLAG_IN_CURRENT_MAP_PAK 2
#define RESFLAG_FROM_DLL_QUERY 4
#define RESFLAG_CASE_MISMATCH 8

typedef struct {
	// This should contain only static data, i.e. no pointers that expire when the query
	// finishes, since it gets saved for debug queries
	const fsc_file_t *file;
	const fsc_shader_t *shader;
	int server_pure_position;
#ifdef FS_SERVERCFG_ENABLED
	unsigned int servercfg_priority;
#endif
	int core_pak_priority;
	int extension_position;
	fs_modtype_t mod_type;
	int flags;
#ifdef CMOD_QVM_SELECTION
	int cmod_pak_priority;
#endif

	// Can be set to an error explanation to disable the resource during selection but still
	// have it show up in the precedence debug listings.
	const char *disabled;
} lookup_resource_t;

typedef struct {
	lookup_resource_t *resources;
	int resource_count;
	int resource_allocation;
} selection_output_t;

typedef struct {
	const fsc_file_t *file;
	const fsc_shader_t *shader;
} query_result_t;

/*
###############################################################################################

Resource construction - Converts file or shader to lookup resource

###############################################################################################
*/

/*
=================
FS_ConfigureLookupResource
=================
*/
static void FS_ConfigureLookupResource( const lookup_query_t *query, lookup_resource_t *resource ) {
	const char *resource_mod_dir = FSC_GetModDir( resource->file, &fs.index );
	const fsc_file_direct_t *base_file = FSC_GetBaseFile( resource->file, &fs.index );

	// Determine mod dir match level
	resource->mod_type = FS_GetModType( resource_mod_dir );

#ifdef FS_SERVERCFG_ENABLED
	// Determine servercfg priority
	if ( !( query->lookup_flags & LOOKUPFLAG_IGNORE_SERVERCFG ) ) {
		resource->servercfg_priority = FS_Servercfg_Priority( resource_mod_dir );
	}
#endif

	// Configure pk3-specific properties
	if ( resource->file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		if ( !( query->lookup_flags & LOOKUPFLAG_IGNORE_PURE_LIST ) ) {
			resource->server_pure_position = FS_Pk3List_Lookup( &fs.connected_server_pure_list, base_file->pk3_hash );
		}
		if ( base_file->f.flags & FSC_FILEFLAG_DLPK3 ) {
			resource->flags |= RESFLAG_IN_DOWNLOAD_PK3;
		}

		// Sort core paks and the current map pak specially, unless they are part of an active mod directory
#ifdef FS_SERVERCFG_ENABLED
		if ( !resource->servercfg_priority )
#endif
		if ( resource->mod_type < MODTYPE_OVERRIDE_DIRECTORY ) {
			resource->core_pak_priority = FS_CorePk3Position( base_file->pk3_hash );
			if ( !( query->lookup_flags & LOOKUPFLAG_IGNORE_CURRENT_MAP ) && base_file == fs.current_map_pk3 )
				resource->flags |= RESFLAG_IN_CURRENT_MAP_PAK;
		}
	}

#ifdef CMOD_QVM_SELECTION
	// Special priority for cmod qvm lookups
	if ( query->cmod_qvm_query && resource->file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		resource->cmod_pak_priority = FS_CmodPk3Position( base_file->pk3_hash );
	}
#endif

	// Check mod dir for case mismatched current or basegame directory
	if ( ( !Q_stricmp( resource_mod_dir, FS_GetCurrentGameDir() ) && strcmp( resource_mod_dir, FS_GetCurrentGameDir() ) )
			|| ( !Q_stricmp( resource_mod_dir, com_basegame->string ) && strcmp( resource_mod_dir, com_basegame->string ) ) ) {
		resource->flags |= RESFLAG_CASE_MISMATCH;
	}

	// Restrict source locations for settings (e.g. q3config.cfg, autoexec.cfg, or default.cfg) query
	if ( query->lookup_flags & LOOKUPFLAG_SETTINGS_FILE ) {
		if ( fs.cvar.fs_mod_settings->integer && resource->mod_type != MODTYPE_BASE && resource->mod_type != MODTYPE_CURRENT_MOD ) {
			resource->disabled = "settings config file can only be loaded from com_basegame or current mod dir";
		}
		if ( !fs.cvar.fs_mod_settings->integer && resource->mod_type != MODTYPE_BASE ) {
			resource->disabled = "settings config file can only be loaded from com_basegame dir";
		}
	}

	// Dll query handling
	if ( query->dll_query ) {
		if ( resource->file->sourcetype != FSC_SOURCETYPE_DIRECT ) {
			resource->disabled = "dll files can only be loaded directly from disk";
		}
		resource->flags |= RESFLAG_FROM_DLL_QUERY;
	}

	// Disable files according to lookupflag sourcetype restrictions
	if ( ( query->lookup_flags & LOOKUPFLAG_DIRECT_SOURCE_ONLY ) && resource->file->sourcetype != FSC_SOURCETYPE_DIRECT ) {
		resource->disabled = "blocking file due to direct_source_only flag";
	}
	if ( ( query->lookup_flags & LOOKUPFLAG_PK3_SOURCE_ONLY ) && resource->file->sourcetype != FSC_SOURCETYPE_PK3 ) {
		resource->disabled = "blocking file due to pk3_source_only flag";
	}

	// Disable files according to download folder restrictions
	if ( ( query->lookup_flags & LOOKUPFLAG_NO_DOWNLOAD_FOLDER ) && ( resource->flags & RESFLAG_IN_DOWNLOAD_PK3 ) ) {
		resource->disabled = "blocking file in download folder due to no_download_folder flag";
	}

	// Disable files blocked by fs_read_inactive_mods setting
	if ( FS_CheckFileDisabled( resource->file, FD_CHECK_READ_INACTIVE_MODS ) ) {
		resource->disabled = "blocking file from inactive mod dir due to fs_read_inactive_mods setting";
	}

	// Disable files not on pure list if connected to a pure server
#ifdef CMOD_QVM_SELECTION
	if ( !resource->cmod_pak_priority )
#endif
	if ( !resource->server_pure_position && FS_ConnectedServerPureState() == 1 && !( query->lookup_flags & LOOKUPFLAG_IGNORE_PURE_LIST ) &&
			!( ( query->lookup_flags & LOOKUPFLAG_PURE_ALLOW_DIRECT_SOURCE ) && resource->file->sourcetype == FSC_SOURCETYPE_DIRECT ) ) {
		resource->disabled = "connected to pure server and file is not on pure list";
	}
}

/*
=================
FS_FileToLookupResource
=================
*/
static void FS_FileToLookupResource( const lookup_query_t *query, const fsc_file_t *file,
		int extension_index, qboolean case_mismatch, lookup_resource_t *resource ) {
	FSC_Memset( resource, 0, sizeof( *resource ) );
	resource->file = file;
	resource->shader = NULL;
	resource->extension_position = extension_index;
	if ( case_mismatch ) {
		resource->flags |= RESFLAG_CASE_MISMATCH;
	}
	FS_ConfigureLookupResource( query, resource );
}

/*
=================
FS_ShaderToLookupResource
=================
*/
static void FS_ShaderToLookupResource( const lookup_query_t *query, const fsc_shader_t *shader,
		lookup_resource_t *resource ) {
	FSC_Memset( resource, 0, sizeof( *resource ) );
	resource->file = (const fsc_file_t *)STACKPTR( shader->source_file_ptr );
	resource->shader = shader;
	FS_ConfigureLookupResource( query, resource );
}

/*
###############################################################################################

Selection - Generates set of lookup resources for given query

###############################################################################################
*/

/* *** Selection Output Structure *** */

/*
=================
FS_InitializeSelectionOutput
=================
*/
static void FS_InitializeSelectionOutput( selection_output_t *output ) {
	FSC_Memset( output, 0, sizeof( *output ) );
	output->resources = (lookup_resource_t *)Z_Malloc( 20 * sizeof( *output->resources ) );
	output->resource_count = 0;
	output->resource_allocation = 20;
}

/*
=================
FS_FreeSelectionOutput
=================
*/
static void FS_FreeSelectionOutput( selection_output_t *output ) {
	Z_Free( output->resources );
}

/*
=================
FS_AllocateLookupResource
=================
*/
static lookup_resource_t *FS_AllocateLookupResource( selection_output_t *output ) {
	if ( output->resource_count >= output->resource_allocation ) {
		// No more slots - double the allocation size
		lookup_resource_t *new_resources = (lookup_resource_t *)Z_Malloc( output->resource_count * 2 * sizeof( *output->resources ) );
		FSC_Memcpy( new_resources, output->resources, output->resource_count * sizeof( *output->resources ) );
		Z_Free( output->resources );
		output->resources = new_resources;
		output->resource_allocation = output->resource_count * 2;
	}
	return &output->resources[output->resource_count++];
}

/* *** File Selection *** */

/*
=================
FS_LookupStringMatch
=================
*/
static qboolean FS_LookupStringMatch( const char *string1, const char *string2, qboolean *case_mismatch_out ) {
	if ( !Q_stricmp( string1, string2 ) ) {
		if ( !*case_mismatch_out && strcmp( string1, string2 ) ) {
			*case_mismatch_out = qtrue;
		}
		return qtrue;
	}
	return qfalse;
}

/*
=================
FS_IsFileSelected

Returns qtrue if file is selected, qfalse otherwise.
=================
*/
static qboolean FS_IsFileSelected( const fsc_file_t *file, const lookup_query_t *query,
		int *extension_index_out, qboolean *case_mismatch_out ) {
	int i;

	if ( !FS_LookupStringMatch( query->qp_name, (const char *)STACKPTR( file->qp_name_ptr ), case_mismatch_out ) ) {
		return qfalse;
	}

	if ( !FS_LookupStringMatch( query->qp_dir, (const char *)STACKPTR( file->qp_dir_ptr ), case_mismatch_out ) ) {
		return qfalse;
	}

	for ( i = 0; i < query->extension_count; ++i ) {
		if ( FS_LookupStringMatch( query->qp_exts[i], (const char *)STACKPTR( file->qp_ext_ptr ), case_mismatch_out ) ) {
			*extension_index_out = i + 1;
			return qtrue;
		}
	}

	return qfalse;
}

/*
=================
FS_PerformFileSelection

Adds files matching criteria to output.
=================
*/
static void FS_PerformFileSelection( const lookup_query_t *query, selection_output_t *output ) {
	fsc_file_iterator_t it = FSC_FileIteratorOpen( &fs.index, query->qp_dir, query->qp_name );
	while ( FSC_FileIteratorAdvance( &it ) ) {
		int extension_index = 0;
		qboolean case_mismatch = qfalse;
		if ( FS_IsFileSelected( it.file, query, &extension_index, &case_mismatch ) ) {
			lookup_resource_t *resource = FS_AllocateLookupResource( output );
			FS_FileToLookupResource( query, it.file, extension_index, case_mismatch, resource );
		}
	}
}

/* *** Shader Selection *** */

/*
=================
FS_PerformShaderSelection

Adds shaders matching criteria to output.
=================
*/
static void FS_PerformShaderSelection( const lookup_query_t *query, selection_output_t *output ) {
	fsc_shader_iterator_t it = FSC_ShaderIteratorOpen( &fs.index, query->shader_name );
	while ( FSC_ShaderIteratorAdvance( &it ) ) {
		FS_ShaderToLookupResource( query, it.shader, FS_AllocateLookupResource( output ) );
	}
}

/* *** Selection Main *** */

/*
=================
FS_PerformSelection

Adds resources matching query to output.

Caller is responsible for calling FS_InitializeSelectionOutput beforehand,
and FS_FreeSelectionOutput afterwards.
=================
*/
static void FS_PerformSelection( const lookup_query_t *query, selection_output_t *output ) {
	if ( query->shader_name ) {
		FS_PerformShaderSelection( query, output );
	}
	if ( query->qp_name ) {
		FS_PerformFileSelection( query, output );
	}
}

/*
###############################################################################################

Precedence - Selects best lookup resource from set of lookup resources

###############################################################################################
*/

/* *** Support Functions *** */

/*
=================
FS_ResourceToStream

Creates a string representation of selection resource for debug print purposes.
=================
*/
static void FS_ResourceToStream( const lookup_resource_t *resource, fsc_stream_t *stream ) {
	FS_FileToStream( resource->file, stream, qtrue, qtrue, qtrue, resource->shader ? qfalse : qtrue );

	if ( resource->shader ) {
		ADD_STRING( "->" );
		ADD_STRING( (const char *)STACKPTR( resource->shader->shader_name_ptr ) );
	}
}

/* *** Precedence Rule Definitions *** */

#define PC_COMPARE( check_name ) static int pc_cmp_##check_name( const lookup_resource_t *r1, const lookup_resource_t *r2 )

#define PC_DEBUG( check_name ) static void pc_dbg_##check_name( const lookup_resource_t *high, const lookup_resource_t *low, \
		int high_num, int low_num, fsc_stream_t *stream )

// Return -1 if r1 has higher precedence, 1 if r2 has higher precedence, 0 if neither

/*
=================
(CMP) resource_disabled
=================
*/

PC_COMPARE( resource_disabled ) {
	if ( r1->disabled && !r2->disabled )
		return 1;
	if ( r2->disabled && !r1->disabled )
		return -1;
	return 0;
}

PC_DEBUG( resource_disabled ) {
	ADD_STRING( va( "Resource %i was selected because resource %i is disabled: %s", high_num, low_num, low->disabled ) );
}

#ifdef CMOD_QVM_SELECTION
/*
=================
(CMP) cmod_paks
=================
*/

PC_COMPARE( cmod_paks ) {
	if ( r1->cmod_pak_priority > r2->cmod_pak_priority )
		return -1;
	if ( r2->cmod_pak_priority > r1->cmod_pak_priority )
		return 1;
	return 0;
}

PC_DEBUG( cmod_paks ) {
	ADD_STRING( va( "Resource %i was selected due to special cMod pak precedence criteria.", high_num ) );
}
#endif

/*
=================
(CMP) special_shaders
=================
*/

PC_COMPARE( special_shaders ) {
	qboolean r1_special = ( r1->shader && ( r1->mod_type >= MODTYPE_OVERRIDE_DIRECTORY ||
			r1->core_pak_priority || r1->server_pure_position ) ) ? qtrue : qfalse;
	qboolean r2_special = ( r2->shader && ( r2->mod_type >= MODTYPE_OVERRIDE_DIRECTORY ||
			r2->core_pak_priority || r2->server_pure_position ) ) ? qtrue : qfalse;

	if ( r1_special && !r2_special )
		return -1;
	if ( r2_special && !r1_special )
		return 1;

	return 0;
}

PC_DEBUG( special_shaders ) {
	ADD_STRING( va( "Resource %i was selected because it is classified as a special shader (from a core pak, the server pure list,"
			" the current mod dir, or the basemod dir) and resource %i is not.", high_num, low_num ) );
}

/*
=================
(CMP) server_pure_position
=================
*/

PC_COMPARE( server_pure_position ) {
	if ( r1->server_pure_position && !r2->server_pure_position )
		return -1;
	if ( r2->server_pure_position && !r1->server_pure_position )
		return 1;

	if ( r1->server_pure_position < r2->server_pure_position )
		return -1;
	if ( r2->server_pure_position < r1->server_pure_position )
		return 1;

	return 0;
}

PC_DEBUG( server_pure_position ) {
	if ( !low->server_pure_position ) {
		ADD_STRING( va( "Resource %i was selected because it is on the server pure list and resource %i is not.", high_num, low_num ) );
	} else {
		ADD_STRING( va( "Resource %i was selected because it has a lower server pure list position (%i) than resource %i (%i).",
				high_num, high->server_pure_position, low_num, low->server_pure_position ) );
	}
}

/*
=================
(CMP) servercfg_directory
=================
*/

#ifdef FS_SERVERCFG_ENABLED
PC_COMPARE( servercfg_directory ) {
	if ( r1->servercfg_priority > r2->servercfg_priority )
		return -1;
	if ( r2->servercfg_priority > r1->servercfg_priority )
		return 1;
	return 0;
}

PC_DEBUG( servercfg_directory ) {
	if ( !low->servercfg_priority ) {
		ADD_STRING( va( "Resource %i was selected because it is in a servercfg directory (%s) and resource %i is not.",
				high_num, FSC_GetModDir( high->file, &fs.index ), low_num ) );
	} else {
		ADD_STRING( va( "Resource %i was selected because it is in a higher priority servercfg directory (%s) than resource %i (%s)."
				" The earlier directory listed in fs_servercfg has higher priority.",
				high_num, FSC_GetModDir( high->file, &fs.index ), low_num, FSC_GetModDir( low->file, &fs.index ) ) );
	}
}
#endif

/*
=================
(CMP) basemod_or_current_mod_dir
=================
*/

PC_COMPARE( basemod_or_current_mod_dir ) {
	if ( r1->mod_type >= MODTYPE_OVERRIDE_DIRECTORY || r2->mod_type >= MODTYPE_OVERRIDE_DIRECTORY ) {
		if ( r1->mod_type > r2->mod_type )
			return -1;
		if ( r2->mod_type > r1->mod_type )
			return 1;
	}
	return 0;
}

PC_DEBUG( basemod_or_current_mod_dir ) {
	ADD_STRING( va( "Resource %i was selected because it is from ", high_num ) );
	if ( high->mod_type == MODTYPE_CURRENT_MOD ) {
		ADD_STRING( va( "the current mod directory (%s)", FSC_GetModDir( high->file, &fs.index ) ) );
	} else {
		ADD_STRING( "the 'basemod' directory" );
	}
	ADD_STRING( va( " and resource %i is not. ", low_num ) );
}

/*
=================
(CMP) core_paks
=================
*/

PC_COMPARE( core_paks ) {
	if ( r1->core_pak_priority > r2->core_pak_priority )
		return -1;
	if ( r2->core_pak_priority > r1->core_pak_priority )
		return 1;
	return 0;
}

PC_DEBUG( core_paks ) {
	ADD_STRING( va( "Resource %i was selected because it has a higher core pak rank than resource %i.", high_num, low_num ) );
}

/*
=================
(CMP) current_map_pak
=================
*/

PC_COMPARE( current_map_pak ) {
	if ( ( r1->flags & RESFLAG_IN_CURRENT_MAP_PAK ) && !( r2->flags & RESFLAG_IN_CURRENT_MAP_PAK ) )
		return -1;
	if ( ( r2->flags & RESFLAG_IN_CURRENT_MAP_PAK ) && !( r1->flags & RESFLAG_IN_CURRENT_MAP_PAK ) )
		return 1;
	return 0;
}

PC_DEBUG( current_map_pak ) {
	ADD_STRING( va( "Resource %i was selected because it is from the same pk3 as the current map and %i is not.", high_num, low_num ) );
}

/*
=================
(CMP) inactive_mod_dir
=================
*/

PC_COMPARE( inactive_mod_dir ) {
	if ( r1->mod_type > MODTYPE_INACTIVE && r2->mod_type <= MODTYPE_INACTIVE )
		return -1;
	if ( r2->mod_type > MODTYPE_INACTIVE && r1->mod_type <= MODTYPE_INACTIVE )
		return 1;
	return 0;
}

PC_DEBUG( inactive_mod_dir ) {
	ADD_STRING( va( "Resource %i was selected because resource %i is from an inactive mod directory "
			"(not basegame, basemod, or current mod).", high_num, low_num ) );
}

/*
=================
(CMP) downloads_folder
=================
*/

PC_COMPARE( downloads_folder ) {
	if ( !( r1->flags & RESFLAG_IN_DOWNLOAD_PK3 ) && ( r2->flags & RESFLAG_IN_DOWNLOAD_PK3 ) )
		return -1;
	if ( !( r2->flags & RESFLAG_IN_DOWNLOAD_PK3 ) && ( r1->flags & RESFLAG_IN_DOWNLOAD_PK3 ) )
		return 1;
	return 0;
}

PC_DEBUG( downloads_folder ) {
	ADD_STRING( va( "Resource %i was selected because resource %i is in the downloads folder and resource %i is not.",
			high_num, low_num, high_num ) );
}

/*
=================
(CMP) shader_over_image
=================
*/

PC_COMPARE( shader_over_image ) {
	if ( r1->shader && !r2->shader )
		return -1;
	if ( r2->shader && !r1->shader )
		return 1;
	return 0;
}

PC_DEBUG( shader_over_image ) {
	ADD_STRING( va( "Resource %i was selected because it is a shader and resource %i is not a shader.", high_num, low_num ) );
}

/*
=================
(CMP) dll_over_qvm
=================
*/

PC_COMPARE( dll_over_qvm ) {
#define QUALIFYING_DLL( resource ) ( resource->flags & RESFLAG_FROM_DLL_QUERY )
	if ( QUALIFYING_DLL( r1 ) && !QUALIFYING_DLL( r2 ) )
		return -1;
	if ( QUALIFYING_DLL( r2 ) && !QUALIFYING_DLL( r1 ) )
		return 1;
	return 0;
}

PC_DEBUG( dll_over_qvm ) {
	ADD_STRING( va( "Resource %i was selected because it is a dll and resource %i is not a dll.", high_num, low_num ) );
}

/*
=================
(CMP) direct_over_pk3
=================
*/

PC_COMPARE( direct_over_pk3 ) {
#define PK3_LIKE_FILE( file ) ( file->sourcetype == FSC_SOURCETYPE_PK3 || \
								( file->sourcetype == FSC_SOURCETYPE_DIRECT && ( (fsc_file_direct_t *)file )->pk3dir_ptr ) )
	if ( !PK3_LIKE_FILE( r1->file ) && PK3_LIKE_FILE( r2->file ) )
		return -1;
	if ( !PK3_LIKE_FILE( r2->file ) && PK3_LIKE_FILE( r1->file ) )
		return 1;
	return 0;
}

PC_DEBUG( direct_over_pk3 ) {
	ADD_STRING( va( "Resource %i was selected because it is a file directly on the disk, while resource %i is inside a pk3.",
			high_num, low_num ) );
}

/*
=================
(CMP) pk3_name_precedence
=================
*/

PC_COMPARE( pk3_name_precedence ) {
	return FS_ComparePk3Source( r1->file, r2->file );
}

PC_DEBUG( pk3_name_precedence ) {
	ADD_STRING( va( "Resource %i was selected because the pk3 containing it has lexicographically higher precedence "
			"than the pk3 containing resource %i.", high_num, low_num ) );
}

/*
=================
(CMP) extension_precedence
=================
*/

PC_COMPARE( extension_precedence ) {
	if ( r1->extension_position < r2->extension_position )
		return -1;
	if ( r2->extension_position < r1->extension_position )
		return 1;
	return 0;
}

PC_DEBUG( extension_precedence ) {
	ADD_STRING( va( "Resource %i was selected because its extension has a higher precedence than "
			"the extension of resource %i.", high_num, low_num ) );
}

/*
=================
(CMP) source_dir_precedence
=================
*/

PC_COMPARE( source_dir_precedence ) {
	int r1_id = FS_GetSourceDirID( r1->file );
	int r2_id = FS_GetSourceDirID( r2->file );
	if ( r1_id < r2_id )
		return -1;
	if ( r2_id < r1_id )
		return 1;
	return 0;
}

PC_DEBUG( source_dir_precedence ) {
	ADD_STRING( va( "Resource %i was selected because it is from a higher precedence source directory (%s) than resource %i (%s)",
			high_num, FS_GetSourceDirString( high->file ), low_num, FS_GetSourceDirString( low->file ) ) );
}

/*
=================
(CMP) intra_pk3_position
=================
*/

PC_COMPARE( intra_pk3_position ) {
	if ( r1->file->sourcetype != FSC_SOURCETYPE_PK3 || r2->file->sourcetype != FSC_SOURCETYPE_PK3 )
		return 0;
	if ( ( (fsc_file_frompk3_t *)r1->file )->header_position > ( (fsc_file_frompk3_t *)r2->file )->header_position )
		return -1;
	if ( ( (fsc_file_frompk3_t *)r2->file )->header_position > ( (fsc_file_frompk3_t *)r1->file )->header_position )
		return 1;
	return 0;
}

PC_DEBUG( intra_pk3_position ) {
	ADD_STRING( va( "Resource %i was selected because it has a later position within the pk3 file than resource %i.",
			high_num, low_num ) );
}

/*
=================
(CMP) intra_shaderfile_position
=================
*/

PC_COMPARE( intra_shaderfile_position ) {
	if ( !r1->shader || !r2->shader )
		return 0;
	if ( r1->shader->start_position < r2->shader->start_position )
		return -1;
	if ( r2->shader->start_position < r1->shader->start_position )
		return 1;
	return 0;
}

PC_DEBUG( intra_shaderfile_position ) {
	ADD_STRING( va( "Resource %i was selected because it has an earlier position within the shader file than resource %i.",
			high_num, low_num ) );
}

/*
=================
(CMP) case_match
=================
*/

PC_COMPARE( case_match ) {
	if ( !( r1->flags & RESFLAG_CASE_MISMATCH ) && ( r2->flags & RESFLAG_CASE_MISMATCH ) )
		return -1;
	if ( !( r2->flags & RESFLAG_CASE_MISMATCH ) && ( r1->flags & RESFLAG_CASE_MISMATCH ) )
		return 1;
	return 0;
}

PC_DEBUG( case_match ) {
	ADD_STRING( va( "Resource %i was selected because resource %i has a case discrepancy from the query and resource %i does not.",
			high_num, low_num, high_num ) );
}

/* *** Precedence List & Sorting *** */

typedef struct {
	const char *identifier;
	int ( *comparator )( const lookup_resource_t *r1, const lookup_resource_t *r2 );
	void ( *debugfunction )( const lookup_resource_t *high, const lookup_resource_t *low,
			int high_num, int low_num, fsc_stream_t *stream );
} precedence_check_t;

#define ADD_CHECK( check ) { #check, pc_cmp_##check, pc_dbg_##check }

static const precedence_check_t precedence_checks[] = {
	ADD_CHECK( resource_disabled ),
#ifdef CMOD_QVM_SELECTION
	ADD_CHECK( cmod_paks ),
#endif
	ADD_CHECK( special_shaders ),
	ADD_CHECK( server_pure_position ),
#ifdef FS_SERVERCFG_ENABLED
	ADD_CHECK( servercfg_directory ),
#endif
	ADD_CHECK( basemod_or_current_mod_dir ),
	ADD_CHECK( core_paks ),
	ADD_CHECK( current_map_pak ),
	ADD_CHECK( inactive_mod_dir ),
	ADD_CHECK( downloads_folder ),
	ADD_CHECK( shader_over_image ),
	ADD_CHECK( dll_over_qvm ),
	ADD_CHECK( direct_over_pk3 ),
	ADD_CHECK( pk3_name_precedence ),
	ADD_CHECK( extension_precedence ),
	ADD_CHECK( source_dir_precedence ),
	ADD_CHECK( intra_pk3_position ),
	ADD_CHECK( intra_shaderfile_position ),
	ADD_CHECK( case_match )
};

/*
=================
FS_PrecedenceComparator
=================
*/
static int FS_PrecedenceComparator( const lookup_resource_t *resource1, const lookup_resource_t *resource2 ) {
	int i;
	int result;

	for ( i = 0; i < ARRAY_LEN( precedence_checks ); ++i ) {
		result = precedence_checks[i].comparator( resource1, resource2 );
		if ( result ) {
			return result;
		}
	}

	// Use memory address as comparison of last resort
	return resource1 < resource2 ? -1 : 1;
}

/*
=================
FS_PrecedenceComparatorQsort
=================
*/
static int FS_PrecedenceComparatorQsort( const void *e1, const void *e2 ) {
	return FS_PrecedenceComparator( (const lookup_resource_t *)e1, (const lookup_resource_t *)e2 );
}

/*
=================
FS_SelectionSort
=================
*/
static void FS_SelectionSort( selection_output_t *selection_output ) {
	qsort( selection_output->resources, selection_output->resource_count, sizeof( *selection_output->resources ),
		FS_PrecedenceComparatorQsort );
}

/*
###############################################################################################

Query processing - Runs selection/precedence oprations for a given lookup query

###############################################################################################
*/

/* *** Standard Lookup *** */

/*
=================
FS_PerformLookup
=================
*/
static void FS_PerformLookup( const lookup_query_t *queries, int query_count, qboolean protected_vm_lookup, query_result_t *output ) {
	int i;
	selection_output_t selection_output;
	lookup_resource_t *best_resource = NULL;

	// Start with empty output
	Com_Memset( output, 0, sizeof( *output ) );

	// Perform selection
	FS_InitializeSelectionOutput( &selection_output );
	for ( i = 0; i < query_count; ++i ) {
		FS_PerformSelection( &queries[i], &selection_output );
	}

	// If selection didn't receive anything, leave the output file null to indicate failure
	if ( !selection_output.resource_count ) {
		FS_FreeSelectionOutput( &selection_output );
		return;
	}

	if ( protected_vm_lookup && fs.cvar.fs_download_mode->integer >= 2 ) {
		// Select the first resource that meets download folder restriction requirements
		FS_SelectionSort( &selection_output );
		for ( i = 0; i < selection_output.resource_count; ++i ) {
			if ( selection_output.resources[i].flags & RESFLAG_IN_DOWNLOAD_PK3 ) {
				char buffer[FS_FILE_BUFFER_SIZE];
				qboolean trusted_hash = FS_CheckTrustedVMFile( selection_output.resources[i].file );
				if ( !trusted_hash ) {
					// Skip unstrusted hash
					FS_FileToBuffer( selection_output.resources[i].file, buffer, sizeof( buffer ),
							qtrue, qtrue, qtrue, qfalse );
					Com_Printf( "^3WARNING: QVM file %s has an untrusted hash and was blocked due to your"
							" fs_download_mode setting. You may need to move this pk3 out of the"
							" downloads folder or set fs_download_mode to 0 or 1 to play on this server."
							" Note that these measures may reduce security.\n", buffer );
					continue;
				} else if ( fs.cvar.fs_download_mode->integer >= 3 ) {
					// Skip trusted hash as well with extra-restrictive fs_download_mode setting
					FS_FileToBuffer( selection_output.resources[i].file, buffer, sizeof( buffer ),
							qtrue, qtrue, qtrue, qfalse );
					Com_Printf( "^3WARNING: QVM file %s has a trusted hash but was blocked due to your"
							" fs_download_mode setting. You may need to move this pk3 out of the"
							" downloads folder or set fs_download_mode to 0, 1, or 2 to play on this server."
							" Note that these measures may reduce security.\n", buffer );
					continue;
				}
			}

			// Have non-blocked file
			best_resource = &selection_output.resources[i];
			break;
		}
	} else {
		// Standard lookup; just pick the top resource
		best_resource = &selection_output.resources[0];
		for ( i = 1; i < selection_output.resource_count; ++i ) {
			if ( FS_PrecedenceComparator( best_resource, &selection_output.resources[i] ) > 0 ) {
				best_resource = &selection_output.resources[i];
			}
		}
	}

	if ( best_resource && !best_resource->disabled ) {
		output->file = best_resource->file;
		output->shader = best_resource->shader;
	}

	FS_FreeSelectionOutput( &selection_output );
}

/* *** Debug Query Storage *** */

static qboolean have_debug_selection = qfalse;
static selection_output_t debug_selection;

/* *** Debug Lookup *** */

/*
=================
FS_LookupFlagsToStream

Add lookup flags to stream for debug print purposes.
=================
*/
static void FS_LookupFlagsToStream( int flags, fsc_stream_t *stream ) {
	const char *flag_strings[9] = { NULL };
	flag_strings[0] = ( flags & LOOKUPFLAG_ENABLE_DDS ) ? "enable_dds" : NULL;
	flag_strings[1] = ( flags & LOOKUPFLAG_IGNORE_PURE_LIST ) ? "ignore_pure_list" : NULL;
	flag_strings[2] = ( flags & LOOKUPFLAG_PURE_ALLOW_DIRECT_SOURCE ) ? "pure_allow_direct_source" : NULL;
	flag_strings[3] = ( flags & LOOKUPFLAG_IGNORE_CURRENT_MAP ) ? "ignore_current_map" : NULL;
	flag_strings[4] = ( flags & LOOKUPFLAG_DIRECT_SOURCE_ONLY ) ? "direct_source_only" : NULL;
	flag_strings[5] = ( flags & LOOKUPFLAG_PK3_SOURCE_ONLY ) ? "pk3_source_only" : NULL;
	flag_strings[6] = ( flags & LOOKUPFLAG_SETTINGS_FILE ) ? "settings_file" : NULL;
	flag_strings[7] = ( flags & LOOKUPFLAG_NO_DOWNLOAD_FOLDER ) ? "no_download_folder" : NULL;
	flag_strings[8] = ( flags & LOOKUPFLAG_IGNORE_SERVERCFG ) ? "ignore_servercfg" : NULL;
	FS_CommaSeparatedList( flag_strings, ARRAY_LEN( flag_strings ), stream );
}

/*
=================
FS_DebugPrintLookupQuery
=================
*/
static void FS_DebugPrintLookupQuery( const lookup_query_t *query ) {
	char buffer[256];
	fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );
	Com_Printf( "  path: %s%s\n", query->qp_dir, query->qp_name );
	stream.position = 0;
	FS_CommaSeparatedList( query->qp_exts, query->extension_count, &stream );
	Com_Printf( "  extensions: %s\n", stream.data );
	Com_Printf( "  shader: %s\n", query->shader_name ? query->shader_name : "<none>" );
	if ( query->lookup_flags ) {
		stream.position = 0;
		FS_LookupFlagsToStream( query->lookup_flags, &stream );
		Com_Printf( "  flags: %i (%s)\n", query->lookup_flags, stream.data );
	} else {
		Com_Printf( "  flags: <none>\n" );
	}
	Com_Printf( "  dll_query: %s\n", query->dll_query ? "yes" : "no" );
}

/*
=================
FS_DebugLookup
=================
*/
#ifdef CMOD_QVM_SELECTION
// Support output parameter
#define FS_DebugLookup( queries, query_count, protected_vm_lookup ) FS_DebugLookup2( queries, query_count, protected_vm_lookup, NULL )
static void FS_DebugLookup2( const lookup_query_t *queries, int query_count, qboolean protected_vm_lookup, query_result_t *output ) {
#else
static void FS_DebugLookup( const lookup_query_t *queries, int query_count, qboolean protected_vm_lookup ) {
#endif
	int i;

	// Print source queries
	if ( fs.cvar.fs_debug_lookup->integer ) {
		for ( i = 0; i < query_count; ++i ) {
			Com_Printf( "Query %i\n", i + 1 );
			FS_DebugPrintLookupQuery( &queries[i] );
			Com_Printf( "\n" );
		}
	}

	// Set global state
	if ( have_debug_selection ) {
		FS_FreeSelectionOutput( &debug_selection );
	}
	FS_InitializeSelectionOutput( &debug_selection );

	// Perform selection
	for ( i = 0; i < query_count; ++i ) {
		FS_PerformSelection( &queries[i], &debug_selection );
	}
	FS_SelectionSort( &debug_selection );
	have_debug_selection = qtrue;

	// Print element data
	for ( i = 0; i < debug_selection.resource_count; ++i ) {
		char buffer[2048];
		fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );

		ADD_STRINGL( va( "  ^3Element %i: ^7", i + 1 ) );
		FS_ResourceToStream( &debug_selection.resources[i], &stream );

		if ( protected_vm_lookup ) {
			// Print extra hash data
			unsigned char hash[32];
			FS_CalculateFileSha256( debug_selection.resources[i].file, hash );
			ADD_STRINGL( "\nhash: " );
			FS_Sha256ToStream( hash, &stream );

			if ( !Q_stricmp( (const char *)STACKPTR( debug_selection.resources[i].file->qp_ext_ptr ), ".qvm" ) ) {
				ADD_STRINGL( va( "\ntrusted: %s", FS_CheckTrustedVMHash( hash ) ? "yes" :
						"no; blocked in download folder if fs_restrict_dlfolder set" ) );
			}
		}

		Com_Printf( "%s\n\n", buffer );
	}

	if ( !debug_selection.resource_count ) {
		Com_Printf( "No matching resources found.\n" );
	} else if ( debug_selection.resources[0].disabled ) {
		Com_Printf( "No resource was selected because element 1 is disabled: %s\n", debug_selection.resources[0].disabled );
	}
#ifdef CMOD_QVM_SELECTION
	if ( output ) {
		Com_Memset( output, 0, sizeof( *output ) );
		if ( debug_selection.resource_count && !debug_selection.resources[0].disabled ) {
			output->file = debug_selection.resources[0].file;
			output->shader = debug_selection.resources[0].shader;
		}
	}
#endif
}

/* *** Debug Comparison - Compares two resources, lists each test and result, and debug info for decisive test *** */

/*
=================
FS_ResourceComparisonToStream
=================
*/
static void FS_ResourceComparisonToStream( const lookup_resource_t *resource1, const lookup_resource_t *resource2,
		int resource1_num, int resource2_num, fsc_stream_t *stream ) {
	int i;
	int result;
	int decisive_result = 0;
	int decisive_test = -1;
	int length;

	ADD_STRING( "Check                           Result\n" );
	ADD_STRING( "------------------------------- ---------\n" );

	for ( i = 0; i < ARRAY_LEN( precedence_checks ); ++i ) {
		// Write check name and spaces
		ADD_STRING( precedence_checks[i].identifier );
		length = strlen( precedence_checks[i].identifier );
		while ( length++ < 32 ) {
			ADD_STRING( " " );
		}

		// Get result string
		result = precedence_checks[i].comparator( resource1, resource2 );
		if ( result && !decisive_result ) {
			decisive_result = result;
			decisive_test = i;
		}

		// Write result identifier
		if ( result < 0 ) {
			ADD_STRING( va( "resource %i", resource1_num ) );
		} else if ( result > 0 ) {
			ADD_STRING( va( "resource %i", resource2_num ) );
		} else {
			ADD_STRING( "---" );
		}
		ADD_STRING( "\n" );
	}

	if ( decisive_result ) {
		const lookup_resource_t *high_resource, *low_resource;
		int high_num, low_num;
		if ( decisive_result < 0 ) {
			high_resource = resource1;
			low_resource = resource2;
			high_num = resource1_num;
			low_num = resource2_num;
		} else {
			high_resource = resource2;
			low_resource = resource1;
			high_num = resource2_num;
			low_num = resource1_num;
		}

		ADD_STRING( "\n" );
		precedence_checks[decisive_test].debugfunction( high_resource, low_resource, high_num, low_num, stream );
	}
}

/*
=================
FS_DebugCompareResources

Uses data from a previous lookup command (stored in debug_selection global variable).
Input corresponds to the index (resource #) of two resources from that lookup.
=================
*/
void FS_DebugCompareResources( int resource1_position, int resource2_position ) {
	char data[65000];
	fsc_stream_t stream = FSC_InitStream( data, sizeof( data ) );

	if ( !have_debug_selection ) {
		Com_Printf( "This command must be preceded by a 'find_file', 'find_shader', 'find_sound', or 'find_vm' command.\n" );
		return;
	}

	if ( resource1_position == resource2_position || resource1_position < 1 || resource2_position < 1 ||
			resource1_position > debug_selection.resource_count || resource2_position > debug_selection.resource_count ) {
		Com_Printf( "Resource numbers out of range.\n" );
		return;
	}

	FS_ResourceComparisonToStream( &debug_selection.resources[resource1_position - 1], &debug_selection.resources[resource2_position - 1],
			resource1_position, resource2_position, &stream );

	Com_Printf( "%s\n", stream.data );
}

/*
###############################################################################################

Wrapper functions - Generates query and calls query handling functions

###############################################################################################
*/

/*
=================
FS_Lookup_DebugPrintFile
=================
*/
static void FS_Lookup_DebugPrintFile( const fsc_file_t *file ) {
	if ( file ) {
		char buffer[FS_FILE_BUFFER_SIZE];
		FS_FileToBuffer( file, buffer, sizeof( buffer ), qtrue, qtrue, qtrue, qfalse );
		FS_DPrintf( "result: %s\n", buffer );
	} else {
		FS_DPrintf( "result: <not found>\n" );
	}
}

/*
=================
FS_Lookup_DebugPrintFlags
=================
*/
static void FS_Lookup_DebugPrintFlags( int flags ) {
	if ( flags ) {
		char buffer[256];
		fsc_stream_t stream = FSC_InitStream( buffer, sizeof( buffer ) );
		FS_LookupFlagsToStream( flags, &stream );
		FS_DPrintf( "flags: %i (%s)\n", flags, buffer );
	} else {
		FS_DPrintf( "flags: <none>\n" );
	}
}

/*
=================
FS_GeneralLookup
=================
*/
const fsc_file_t *FS_GeneralLookup( const char *name, int lookup_flags, qboolean debug ) {
	lookup_query_t query;
	fsc_qpath_buffer_t qpath_split;
	query_result_t lookup_result;
	FSC_ASSERT( name );

#ifdef CMOD_CROSSHAIR
	{
		fsc_file_t *crosshair = CMCrosshair_FileLookupHook( name );
		if ( crosshair ) {
			return crosshair;
		}
	}
#endif

	Com_Memset( &query, 0, sizeof( query ) );
	query.lookup_flags = lookup_flags;

	// For compatibility purposes, support dropping one leading slash from qpath
	// as per FS_FOpenFileReadDir in original filesystem
	if ( *name == '/' || *name == '\\' ) {
		++name;
	}

	FSC_SplitQpath( name, &qpath_split, fsc_false );
	query.qp_dir = qpath_split.dir;
	query.qp_name = qpath_split.name;
	query.qp_exts = (const char **)&qpath_split.ext;
	query.extension_count = 1;

	if ( debug ) {
		FS_DebugLookup( &query, 1, qfalse );
		return NULL;
	}

	FS_PerformLookup( &query, 1, qfalse, &lookup_result );
	if ( fs.cvar.fs_debug_lookup->integer ) {
		FS_DPrintf( "********** general lookup **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", name );
		FS_Lookup_DebugPrintFlags( lookup_flags );
		FS_Lookup_DebugPrintFile( lookup_result.file );
		FS_DebugIndentStop();
	}

	return lookup_result.file;
}

/*
=================
FS_ShaderOrImageLookup

Input name should be extension-free (call COM_StripExtension first).
=================
*/
static void FS_ShaderOrImageLookup( const char *name, qboolean image_only, int lookup_flags,
		query_result_t *output, qboolean debug ) {
	lookup_query_t query;
	fsc_qpath_buffer_t qpath_split;
	const char *exts[] = { ".dds", ".png", ".tga", ".jpg", ".jpeg", ".pcx", ".bmp" };

	Com_Memset( &query, 0, sizeof( query ) );
	query.lookup_flags = lookup_flags;
	if ( !image_only ) {
		query.shader_name = name;
	}

	// For compatibility purposes, support dropping one leading slash from qpath
	// as per FS_FOpenFileReadDir in original filesystem
	if ( *name == '/' || *name == '\\' ) {
		++name;
	}

	FSC_SplitQpath( name, &qpath_split, fsc_true );
	query.qp_dir = qpath_split.dir;
	query.qp_name = qpath_split.name;
	query.qp_exts = ( lookup_flags & LOOKUPFLAG_ENABLE_DDS ) ? exts : exts + 1;
	query.extension_count = ( lookup_flags & LOOKUPFLAG_ENABLE_DDS ) ? ARRAY_LEN( exts ) : ARRAY_LEN( exts ) - 1;

	if ( debug ) {
		FS_DebugLookup( &query, 1, qfalse );
	} else {
		FS_PerformLookup( &query, 1, qfalse, output );
	}
}

/*
=================
FS_ShaderLookup

Input name should be extension-free (call COM_StripExtension first).
Returns null if shader not found or image took precedence.
=================
*/
const fsc_shader_t *FS_ShaderLookup( const char *name, int lookup_flags, qboolean debug ) {
	query_result_t lookup_result;
	FSC_ASSERT( name );

#ifdef CMOD_CROSSHAIR
	if ( CMCrosshair_FileLookupHook( name ) ) {
		return NULL;
	}
#endif

	FS_ShaderOrImageLookup( name, qfalse, lookup_flags, &lookup_result, debug );
	if ( debug ) {
		return NULL;
	}

	if ( fs.cvar.fs_debug_lookup->integer ) {
		FS_DPrintf( "********** shader lookup **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", name );
		FS_Lookup_DebugPrintFlags( lookup_flags );
		FS_Lookup_DebugPrintFile( lookup_result.file );
		FS_DebugIndentStop();
	}

	return lookup_result.shader;
}

/*
=================
FS_ImageLookup

Input name should be extension-free (call COM_StripExtension first).
=================
*/
const fsc_file_t *FS_ImageLookup( const char *name, int lookup_flags, qboolean debug ) {
	query_result_t lookup_result;
	FSC_ASSERT( name );

#ifdef CMOD_CROSSHAIR
	{
		fsc_file_t *crosshair = CMCrosshair_FileLookupHook( name );
		if ( crosshair ) {
			return crosshair;
		}
	}
#endif

	FS_ShaderOrImageLookup( name, qtrue, lookup_flags, &lookup_result, debug );
	if ( debug ) {
		return NULL;
	}

	if ( fs.cvar.fs_debug_lookup->integer ) {
		FS_DPrintf( "********** image lookup **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", name );
		FS_Lookup_DebugPrintFlags( lookup_flags );
		FS_Lookup_DebugPrintFile( lookup_result.file );
		FS_DebugIndentStop();
	}

	return lookup_result.file;
}

/*
=================
FS_SoundLookup

Input name should be extension-free (call COM_StripExtension first).
=================
*/
const fsc_file_t *FS_SoundLookup( const char *name, int lookup_flags, qboolean debug ) {
	lookup_query_t query;
	fsc_qpath_buffer_t qpath_split;
	const char *exts[] = {
		".wav",
#ifdef USE_CODEC_MP3
		".mp3",
#endif
#ifdef USE_CODEC_VORBIS
		".ogg",
#endif
#ifdef USE_CODEC_OPUS
		".opus",
#endif
	};
	query_result_t lookup_result;
	FSC_ASSERT( name );

	// For compatibility purposes, support dropping one leading slash from qpath
	// as per FS_FOpenFileReadDir in original filesystem
	if ( *name == '/' || *name == '\\' ) {
		++name;
	}

	Com_Memset( &query, 0, sizeof( query ) );
	FSC_SplitQpath( name, &qpath_split, fsc_true );
	query.qp_dir = qpath_split.dir;
	query.qp_name = qpath_split.name;
	query.qp_exts = exts;
	query.extension_count = ARRAY_LEN( exts );

	if ( debug ) {
		FS_DebugLookup( &query, 1, qfalse );
		return NULL;
	}

	FS_PerformLookup( &query, 1, qfalse, &lookup_result );
	if ( fs.cvar.fs_debug_lookup->integer ) {
		FS_DPrintf( "********** sound lookup **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", name );
		FS_Lookup_DebugPrintFlags( lookup_flags );
		FS_Lookup_DebugPrintFile( lookup_result.file );
		FS_DebugIndentStop();
	}

	return lookup_result.file;
}

#ifdef CMOD_QVM_SELECTION
// The following pk3s contain QVMs which are functionally interchangeable with the standard game
// qvms, and can be safely replaced with the most up to date cMod qvms if available.
static const int stock_qvms[] = {
	-334095706,		// pak2.pk3
	-982121719,		// pak92.pk3
	1445632735,		// pakext2b.pk3
	2099203013,		// pakcmod-stable-2021-07-16.pk3
	732565402,		// pakcmod-dev-2021-07-16.pk3
	401438010,		// pakcmod-current-2021-09-18.pk3
	-749739206,		// pakcmod-current-2021-09-25.pk3
	-1518584883,	// pakcmod-current-2021-10-15.pk3
	34943118,		// pakcmod-current-2021-11-11.pk3
	1803491023,		// pakcmod-current-2021-12-03.pk3
	1289620810,		// pakcmod-current-2021-12-28.pk3
	278974329,		// pakcmod-current-2022-04-03.pk3
};

/*
=================
FS_IsStockQvm
=================
*/
static qboolean FS_IsStockQvm( const fsc_file_t *file ) {
	if ( file && file->sourcetype == FSC_SOURCETYPE_PK3 ) {
		int i;
		const fsc_file_direct_t *base_file = FSC_GetBaseFile( file, &fs.index );
		for ( i = 0; i < ARRAY_LEN( stock_qvms ); ++i ) {
			if ( (int)base_file->pk3_hash == stock_qvms[i] ) {
				return qtrue;
			}
		}
	}

	return qfalse;
}

/*
=================
FS_CmodVmLookup2

Returns qtrue if a valid match was found.
=================
*/
static void FS_CmodVmLookup2( const char *name, qboolean qvm_only, qboolean prioritize_cmod, qboolean debug,
		lookup_query_t *queries, int query_count, query_result_t *lookup_result ) {
	queries[0].cmod_qvm_query = prioritize_cmod;

	if ( debug )  {
		FS_DebugLookup2( queries, query_count, qtrue, lookup_result );
	} else {
		FS_PerformLookup( queries, query_count, qtrue, lookup_result );
	}

	if ( debug || fs.cvar.fs_debug_lookup->integer ) {
		FS_DPrintf( "********** dll/qvm lookup **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", name );
		FS_DPrintf( "qvm only: %s\n", qvm_only ? "yes" : "no" );
		FS_DPrintf( "prioritize cmod: %s\n", prioritize_cmod ? "yes" : "no" );
		FS_Lookup_DebugPrintFile( lookup_result->file );
		FS_DebugIndentStop();
	}
}

/*
=================
FS_CmodVmLookup
=================
*/
static void FS_CmodVmLookup( const char *name, qboolean qvm_only, qboolean debug,
		lookup_query_t *queries, int query_count, query_result_t *lookup_result ) {
	int native = 0;
	if ( !Q_stricmp( name, "ui" ) ) {
		native = ModcfgHandling_CurrentValues.nativeUI;
	} else if ( !Q_stricmp( name, "cgame" ) ) {
		native = ModcfgHandling_CurrentValues.nativeCgame;
	}

	if ( native == 2 ) {
		Com_Printf( "Prioritizing cMod module for '%s' due to server native VM mode 2.\n", name );
		FS_CmodVmLookup2( name, qvm_only, qtrue, debug, queries, query_count, lookup_result );
		return;
	}

	FS_CmodVmLookup2( name, qvm_only, qfalse, debug, queries, query_count, lookup_result );
	if ( !lookup_result->file ) {
		return;
	}

	if ( FS_IsStockQvm( lookup_result->file ) ) {
		Com_Printf( "Prioritizing cMod module for '%s' due to compatible configuration.\n", name );
		FS_CmodVmLookup2( name, qvm_only, qtrue, debug, queries, query_count, lookup_result );
		return;
	}

#ifdef CMOD_VM_PERMISSIONS
	// native level 1 prefers cMod module only if the normally selected one is untrusted
	if ( native == 1 && !VMPermissions_CheckTrustedVMFile( lookup_result->file, NULL ) ) {
		Com_Printf( "Prioritizing cMod module for '%s' due to server native VM mode 1.\n", name );
		FS_CmodVmLookup2( name, qvm_only, qtrue, debug, queries, query_count, lookup_result );
		return;
	}
#endif
}
#endif

/*
=================
FS_VMLookup

Returns a qvm or game dll file, or null if not found.
May throw ERR_DROP due to fs_restrict_dlfolder checks.
=================
*/
const fsc_file_t *FS_VMLookup( const char *name, qboolean qvm_only, qboolean debug, qboolean *is_dll_out ) {
	const char *qvm_exts[] = { ".qvm" };
	const char *dll_exts[] = { DLL_EXT };
	lookup_query_t queries[2];
	int query_count = qvm_only ? 1 : 2;
	fsc_qpath_buffer_t qpath_splits[2];
	query_result_t lookup_result;
	FSC_ASSERT( name );

	Com_Memset( queries, 0, sizeof( queries ) );
	queries[0].lookup_flags = LOOKUPFLAG_IGNORE_CURRENT_MAP;
	FSC_SplitQpath( va( "vm/%s", name ), &qpath_splits[0], fsc_true );
	queries[0].qp_dir = qpath_splits[0].dir;
	queries[0].qp_name = qpath_splits[0].name;
	queries[0].qp_exts = qvm_exts;
	queries[0].extension_count = ARRAY_LEN( qvm_exts );

	if ( !qvm_only ) {
		queries[1].lookup_flags = LOOKUPFLAG_IGNORE_CURRENT_MAP;
		FSC_SplitQpath( va( "%s" ARCH_STRING, name ), &qpath_splits[1], fsc_true );
		queries[1].qp_dir = qpath_splits[1].dir;
		queries[1].qp_name = qpath_splits[1].name;
		queries[1].qp_exts = dll_exts;
		queries[1].extension_count = ARRAY_LEN( dll_exts );
		queries[1].dll_query = qtrue;
	}

#ifdef CMOD_QVM_SELECTION
	FS_CmodVmLookup( name, qvm_only, debug, queries, query_count, &lookup_result );
#else
	if ( debug ) {
		FS_DebugLookup( queries, query_count, qtrue );
		return NULL;
	}

	FS_PerformLookup( queries, query_count, qtrue, &lookup_result );
	if ( fs.cvar.fs_debug_lookup->integer ) {
		FS_DPrintf( "********** dll/qvm lookup **********\n" );
		FS_DebugIndentStart();
		FS_DPrintf( "name: %s\n", name );
		FS_DPrintf( "qvm only: %s\n", qvm_only ? "yes" : "no" );
		FS_Lookup_DebugPrintFile( lookup_result.file );
		FS_DebugIndentStop();
	}
#endif

	// Not elegant but should be okay
	if ( is_dll_out ) {
		*is_dll_out = lookup_result.file &&
				!Q_stricmp( (const char *)STACKPTR( lookup_result.file->qp_ext_ptr ), DLL_EXT ) ? qtrue : qfalse;
	}

	return lookup_result.file;
}

#endif	// NEW_FILESYSTEM
