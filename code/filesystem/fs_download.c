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

#define MAX_DOWNLOAD_NAME 64	// Max length of the pk3 filename

typedef struct download_entry_s {
	struct download_entry_s *next;
	unsigned int hash;
	char *local_name;
	char *remote_name;
	char *filename;
	char *mod_dir;
} download_entry_t;

/*
###############################################################################################

Download List Handling

###############################################################################################
*/

static download_entry_t *current_download;
static download_entry_t *next_download;

/*
=================
FS_FreeDownloadEntry
=================
*/
static void FS_FreeDownloadEntry( download_entry_t *entry ) {
	Z_Free( entry->local_name );
	Z_Free( entry->remote_name );
	Z_Free( entry->filename );
	Z_Free( entry->mod_dir );
	Z_Free( entry );
}

/*
=================
FS_AdvanceDownload

Pops a download entry from next_download into current_download.
=================
*/
void FS_AdvanceDownload( void ) {
	if ( current_download ) {
		FS_FreeDownloadEntry( current_download );
	}
	current_download = next_download;
	if ( next_download ) {
		next_download = next_download->next;
	}
}

/*
=================
FS_AddNextDownload

Push a download entry into next_download.
=================
*/
static void FS_AddNextDownload( download_entry_t *entry ) {
	entry->next = next_download;
	next_download = entry;
}

/*
=================
FS_FreeDownloadList

Free all downloads in list.
=================
*/
static void FS_FreeDownloadList( void ) {
	while ( current_download || next_download ) {
		FS_AdvanceDownload();
	}
}

/*
###############################################################################################

Attempted Download Tracking

This section is used to prevent trying to unsuccessfully download the same file over
and over again in the same session.

###############################################################################################
*/

pk3_list_t attempted_downloads_http;
pk3_list_t attempted_downloads;

/*
=================
FS_RegisterAttemptedDownload
=================
*/
static void FS_RegisterAttemptedDownload( unsigned int hash, qboolean http ) {
	pk3_list_t *target = http ? &attempted_downloads_http : &attempted_downloads;
	if ( !target->ht.bucket_count ) {
		FS_Pk3List_Initialize( target, 20 );
	}
	FS_Pk3List_Insert( target, hash );
}

/*
=================
FS_CheckAttemptedDownload

Returns qtrue if download already attempted.
=================
*/
static qboolean FS_CheckAttemptedDownload( unsigned int hash, qboolean http ) {
	pk3_list_t *target = http ? &attempted_downloads_http : &attempted_downloads;
	return FS_Pk3List_Lookup( target, hash ) ? qtrue : qfalse;
}

/*
=================
FS_RegisterCurrentDownloadAttempt

Register that an HTTP or UDP download is being attempted for the current active download entry.
=================
*/
void FS_RegisterCurrentDownloadAttempt( qboolean http ) {
	FSC_ASSERT( current_download );
	FS_RegisterAttemptedDownload( current_download->hash, http );
}

/*
=================
FS_ClearAttemptedDownloads

Clear attempted download records when disconnecting from remote server.
=================
*/
void FS_ClearAttemptedDownloads( void ) {
	FS_Pk3List_Free( &attempted_downloads_http );
	FS_Pk3List_Free( &attempted_downloads );
}

/*
###############################################################################################

Needed Download Checks

###############################################################################################
*/

/*
=================
FS_DownloadCandidateAlreadyExists

Returns qtrue if download entry matches an existing file in filesystem.
=================
*/
static qboolean FS_DownloadCandidateAlreadyExists( download_entry_t *entry, const fsc_file_direct_t **different_moddir_match_out ) {
	fsc_pk3_iterator_t it = FSC_Pk3IteratorOpen( &fs.index, entry->hash );
	while ( FSC_Pk3IteratorAdvance( &it ) ) {
		if ( fs.cvar.fs_redownload_across_mods->integer &&
				Q_stricmp( FSC_GetModDir( (fsc_file_t *)it.pk3, &fs.index ), entry->mod_dir ) ) {
			// If "fs_redownload_across_mods" is set, ignore match from different mod dir,
			// but record it so FS_IsValidDownload can display a warning later
			if ( different_moddir_match_out ) {
				*different_moddir_match_out = it.pk3;
			}
			continue;
		}
		return qtrue;
	}

	return qfalse;
}

/*
=================
FS_DownloadCandidateIsIDPak

Returns qtrue if download entry meets the criteria of an ID pak which shouldn't be downloaded.
=================
*/
static qboolean FS_DownloadCandidateIsIDPak( download_entry_t *entry ) {
	char test_path[FS_MAX_PATH];
	Com_sprintf( test_path, sizeof( test_path ), "%s/%s", entry->mod_dir, entry->filename );
#ifndef STANDALONE
	if ( FS_idPak( test_path, BASEGAME, FS_NODOWNLOAD_PAKS ) ) {
		return qtrue;
	}
	if ( FS_idPak( test_path, BASETA, FS_NODOWNLOAD_PAKS_TEAMARENA ) ) {
		return qtrue;
	}
#endif
	return qfalse;
}

/*
=================
FS_IsValidDownload

Returns qtrue if file is valid to download, qfalse otherwise.
recheck_hash can be set to retest a file that was downloaded and has an unexpected hash.
=================
*/
static qboolean FS_IsValidDownload( download_entry_t *entry, unsigned int recheck_hash, qboolean curl_disconnected ) {
	unsigned int hash = recheck_hash ? recheck_hash : entry->hash;
	const fsc_file_direct_t *different_moddir_match = NULL;

	if ( fs.read_only ) {
		Com_Printf( "WARNING: Ignoring download %s because filesystem is in read-only state.\n",
				entry->local_name );
		return qfalse;
	}

	if ( !Q_stricmp( entry->mod_dir, "basemod" ) ) {
		Com_Printf( "WARNING: Ignoring download %s because downloads to basemod directory are not allowed.\n",
				entry->local_name );
		return qfalse;
	}

	if ( FS_DownloadCandidateAlreadyExists( entry, &different_moddir_match ) ) {
		if ( recheck_hash ) {
			Com_Printf( "WARNING: Downloaded pk3 %s has unexpected hash which already exists in index."
					" Download not saved.\n", entry->local_name );
		}
		return qfalse;
	}

	if ( !recheck_hash ) {
		if ( FS_CheckAttemptedDownload( hash, qfalse ) ) {
			Com_Printf( "WARNING: Ignoring download %s because a download with the same hash has already been"
					" attempted in this session.\n", entry->local_name );
			return qfalse;
		}
		if ( curl_disconnected && FS_CheckAttemptedDownload( hash, qtrue ) ) {
			// Wait for the reconnect to attempt this as a UDP download
			return qfalse;
		}
	}

	// NOTE: Consider using hash-based check instead of the old filename check?
	if ( FS_DownloadCandidateIsIDPak( entry ) ) {
		Com_Printf( "WARNING: Ignoring download %s as possible ID pak.\n", entry->local_name );
		return qfalse;
	}

	if ( different_moddir_match ) {
		char buffer[FS_FILE_BUFFER_SIZE];
		FS_FileToBuffer( (fsc_file_t *)different_moddir_match, buffer, sizeof( buffer ),
				qfalse, qtrue, qfalse, qfalse );
		Com_Printf( "WARNING: %s %s, even though the file already appears to exist at %s."
				" Set fs_redownload_across_mods to 0 to disable this behavior.\n",
				recheck_hash ? "Saving" : "Downloading", entry->local_name, buffer );
	}

	return qtrue;
}

/*
###############################################################################################

Download List Creation

###############################################################################################
*/

/*
=================
FS_CreateDownloadEntry

Returns new entry on success, null on error.
Download entries should be freed by FS_FreeDownloadEntry.
=================
*/
static download_entry_t *FS_CreateDownloadEntry( const char *name, unsigned int hash ) {
	download_entry_t *entry;
	char temp_mod_dir[FSC_MAX_MODDIR];
	const char *temp_filename = NULL;
	char mod_dir[FSC_MAX_MODDIR];
	char filename[MAX_DOWNLOAD_NAME + 1];

	// Generate mod_dir and filename
	if ( !FSC_SplitLeadingDirectory( name, temp_mod_dir, sizeof( temp_mod_dir ), &temp_filename ) ) {
		return NULL;
	}
	if ( !temp_filename ) {
		return NULL;
	}
	FS_SanitizeModDir( temp_mod_dir, mod_dir );
	if ( !*mod_dir ) {
		return NULL;
	}
	if ( !FS_GeneratePath( temp_filename, NULL, NULL, 0, 0, 0, filename, sizeof( filename ) ) ) {
		return NULL;
	}

	// Patch mod dir capitalization
	if ( !Q_stricmp( mod_dir, com_basegame->string ) ) {
		Q_strncpyz( mod_dir, com_basegame->string, sizeof( mod_dir ) );
	}
	if ( !Q_stricmp( mod_dir, FS_GetCurrentGameDir() ) ) {
		Q_strncpyz( mod_dir, FS_GetCurrentGameDir(), sizeof( mod_dir ) );
	}

	// Set the download entry strings
	entry = (download_entry_t *)Z_Malloc( sizeof( *entry ) );
	entry->local_name = CopyString( va( "%s/%s%s.pk3", mod_dir, fs.cvar.fs_download_mode->integer > 0 ? "downloads/" : "", filename ) );
	entry->remote_name = CopyString( va( "%s.pk3", name ) );
	entry->mod_dir = CopyString( mod_dir );
	entry->filename = CopyString( filename );
	entry->hash = hash;
	return entry;
}

/*
=================
FS_PrintDownloadList

Prints predicted needed pak list to console.
=================
*/
void FS_PrintDownloadList( void ) {
	download_entry_t *entry = next_download;
	qboolean have_entry = qfalse;
	while ( entry ) {
		if ( !FS_DownloadCandidateAlreadyExists( entry, NULL ) ) {
			if ( !have_entry ) {
				Com_Printf( "Need paks: %s", entry->remote_name );
				have_entry = qtrue;
			} else {
				Com_Printf( ", %s", entry->remote_name );
			}
		}
		entry = entry->next;
	}
	if ( have_entry ) {
		Com_Printf( "\n" );
	}
}

/*
=================
FS_RegisterDownloadList

Generates download entries for list of referenced pk3s received from server.
=================
*/
void FS_RegisterDownloadList( const char *hash_list, const char *name_list ) {
	int i;
	int count;
	int hashes[1024];

	FS_FreeDownloadList();

	Cmd_TokenizeString( hash_list );
	count = Cmd_Argc();
	if ( count > ARRAY_LEN( hashes ) ) {
		count = ARRAY_LEN( hashes );
	}
	for ( i = 0; i < count; ++i ) {
		hashes[i] = atoi( Cmd_Argv( i ) );
	}

	Cmd_TokenizeString( name_list );
	if ( Cmd_Argc() < count ) {
		count = Cmd_Argc();
	}
	for ( i = count - 1; i >= 0; --i ) {
		download_entry_t *entry = FS_CreateDownloadEntry( Cmd_Argv( i ), hashes[i] );
		if ( !entry ) {
			Com_Printf( "WARNING: Ignoring download %s due to invalid name.\n", Cmd_Argv( i ) );
			continue;
		}
		FS_AddNextDownload( entry );
	}
}

/*
###############################################################################################

Download List Advancement

###############################################################################################
*/

/*
=================
FS_AdvanceToNextNeededDownload

Advances through download queue until the current download is either null
or valid to download (from the filesystem perspectiveat at least; CL_NextDownload
may skip downloads for other reasons by calling FS_AdvanceDownload)
=================
*/
void FS_AdvanceToNextNeededDownload( qboolean curl_disconnected ) {
	if ( !current_download ) {
		FS_AdvanceDownload();
	}

	while ( current_download ) {
		if ( FS_IsValidDownload( current_download, 0, curl_disconnected ) ) {
			break;
		}
		FS_AdvanceDownload();
	}
}

/*
=================
FS_GetCurrentDownloadInfo

Returns qtrue and writes info if current_download is available, qfalse if current_download is null.
=================
*/
qboolean FS_GetCurrentDownloadInfo( char **local_name_out, char **remote_name_out, qboolean *curl_already_attempted_out ) {
	if ( !current_download ) {
		return qfalse;
	}
	*local_name_out = current_download->local_name;
	*remote_name_out = current_download->remote_name;
	*curl_already_attempted_out = FS_CheckAttemptedDownload( current_download->hash, qtrue );
	return qtrue;
}

/*
###############################################################################################

Download Completion

###############################################################################################
*/

/*
=================
FS_TempDownloadHashCallback
=================
*/
static void FS_TempDownloadHashCallback( void *context, char *data, int size ) {
	*(unsigned int *)context = FSC_BlockChecksum( data, size );
}

/*
=================
FS_GetTempDownloadPk3Hash
=================
*/
static unsigned int FS_GetTempDownloadPk3Hash( const char *tempfile_path ) {
	void *os_path = FSC_StringToOSPath( tempfile_path );
	unsigned int result = 0;
	FSC_LoadPk3( os_path, &fs.index, FSC_SPNULL, FS_TempDownloadHashCallback, &result );
	FSC_Free( os_path );
	return result;
}

/*
=================
FS_FinalizeDownload

Does some final verification and moves the download, which hopefully has been written to
the temporary file, to its final location.
=================
*/
void FS_FinalizeDownload( void ) {
	char tempfile_path[FS_MAX_PATH];
	char target_path[FS_MAX_PATH];
	unsigned int actual_hash;

	if ( !current_download ) {
		// Shouldn't happen
		Com_Printf( "^3WARNING: FS_FinalizeDownload called with no current download\n" );
		return;
	}

	if ( !FS_GeneratePathWritedir( "download.temp", NULL, 0, 0, tempfile_path, sizeof( tempfile_path ) ) ) {
		Com_Printf( "ERROR: Failed to get tempfile path for download\n" );
		return;
	}

	if ( !FS_GeneratePathWritedir( current_download->local_name, NULL, FS_ALLOW_PK3 | FS_ALLOW_DIRECTORIES | FS_CREATE_DIRECTORIES_FOR_FILE,
				0, target_path, sizeof( target_path ) ) ) {
		Com_Printf( "ERROR: Failed to get target path for download\n" );
		return;
	}

	actual_hash = FS_GetTempDownloadPk3Hash( tempfile_path );
	if ( !actual_hash ) {
		Com_Printf( "WARNING: Downloaded pk3 %s appears to be missing or corrupt. Download not saved.\n",
				current_download->local_name );
		return;
	}

	if ( actual_hash != current_download->hash ) {
		// Wrong hash - this could be a malicious attempt to spoof a core pak or maybe a corrupt
		//    download, but probably is just a server configuration issue mixing up pak versions.
		//    Run the file needed check with the new hash to see if it still passes.
		if ( !FS_IsValidDownload( current_download, actual_hash, qfalse ) ) {
			// Error should already be printed
			return;
		} else {
			Com_Printf( "WARNING: Downloaded pk3 %s has unexpected hash.\n", current_download->local_name );
		}
	}

	if ( FS_FileInPathExists( target_path ) ) {
		const char *new_name = va( "%s/%s%s.%08x.pk3", current_download->mod_dir, fs.cvar.fs_download_mode->integer > 0 ? "downloads/" : "",
				current_download->filename, actual_hash );
		Com_Printf( "WARNING: Downloaded pk3 %s conflicts with existing file. Using name %s instead.\n",
				current_download->local_name, new_name );
		if ( !FS_GeneratePathWritedir( new_name, NULL, FS_ALLOW_DIRECTORIES | FS_ALLOW_PK3, 0, target_path, sizeof( target_path ) ) ) {
			Com_Printf( "ERROR: Failed to get nonconflicted target path for download\n" );
			return;
		}

		FSC_DeleteFile( target_path );
	}

	FSC_RenameFile( tempfile_path, target_path );
	if ( FS_FileInPathExists( tempfile_path ) ) {
		Com_Printf( "ERROR: There was a problem moving downloaded pk3 %s from temporary file to target"
				" location. Download may not be saved.\n", current_download->local_name );
	} else {
		// Download appears successful; refresh filesystem to make sure it is properly registered
		FS_Refresh( qtrue );
	}
}

#endif	// NEW_FILESYSTEM
