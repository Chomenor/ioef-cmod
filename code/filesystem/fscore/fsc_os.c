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

/*
###############################################################################################

Headers & Definitions

###############################################################################################
*/

#ifdef _WIN32
// Windows defines
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#ifdef UNICODE
#define WIN_WIDECHAR
#endif
#else
// Non-windows defines
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#endif
// Common defines
#include <stdio.h>

/*
###############################################################################################

Misc

###############################################################################################
*/

/*
=================
FSC_ErrorAbort

Prints error message and aborts program. Used as a fallback if the standard error handler
is not registered.
=================
*/
void FSC_ErrorAbort( const char *msg ) {
	fprintf( stderr, "filesystem error: %s\n", msg );
	exit( 1 );
}

/*
###############################################################################################

OS Path Handling

###############################################################################################
*/

#ifdef WIN_WIDECHAR
#define FSC_CHAR wchar_t
#define FSC_UCHAR unsigned short
#else
#define FSC_CHAR char
#define FSC_UCHAR unsigned char
#endif

/*
=================
FSC_StringToOSPath

Converts UTF-8 string to OS path format. Result must be freed by caller via FSC_Free.
=================
*/
fsc_ospath_t *FSC_StringToOSPath( const char *path ) {
	FSC_ASSERT( path );
	{
		FSC_CHAR *buffer;
#ifdef WIN_WIDECHAR
		int length = MultiByteToWideChar( CP_UTF8, 0, path, -1, 0, 0 );
		FSC_ASSERT( length );
		buffer = (FSC_CHAR *)FSC_Malloc( length * sizeof( wchar_t ) );
		MultiByteToWideChar( CP_UTF8, 0, path, -1, buffer, length );
#else
		// Consider stripping non-ASCII chars for non-unicode Windows builds
		int length = FSC_Strlen( path );
		buffer = (char *)FSC_Malloc( length + 1 );
		FSC_Memcpy( buffer, path, length + 1 );
#endif
		return (fsc_ospath_t *)buffer;
	}
}

/*
=================
FSC_OSPathToString

Converts OS path format to UTF-8 string. Result must be freed by caller via FSC_Free.
=================
*/
char *FSC_OSPathToString( const fsc_ospath_t *os_path ) {
	FSC_ASSERT( os_path );
	{
		char *buffer;
#ifdef WIN_WIDECHAR
		int length = WideCharToMultiByte( CP_UTF8, 0, (LPCWSTR)os_path, -1, 0, 0, 0, 0 );
		FSC_ASSERT( length );
		buffer = (char *)FSC_Malloc( length );
		WideCharToMultiByte( CP_UTF8, 0, (LPCWSTR)os_path, -1, buffer, length, 0, 0 );
#else
		int length = strlen( (const char *)os_path );
		buffer = (char *)FSC_Malloc( length + 1 );
		FSC_Memcpy( buffer, (const char *)os_path, length + 1 );
#endif
		return buffer;
	}
}

/*
=================
FSC_OSPathSize

Returns size in bytes of an OS path object.
=================
*/
int FSC_OSPathSize( const fsc_ospath_t *os_path ) {
	FSC_ASSERT( os_path );
#ifdef WIN_WIDECHAR
	return 2 * wcslen( (const wchar_t *)os_path ) + 2;
#else
	return strlen( (const char *)os_path ) + 1;
#endif
}

/*
=================
FSC_OSPathCompare

Returns 0 if paths are equal.
=================
*/
int FSC_OSPathCompare( const fsc_ospath_t *path1, const fsc_ospath_t *path2 ) {
	FSC_ASSERT( path1 && path2 );
#ifdef WIN_WIDECHAR
	return wcscmp( (const wchar_t *)path1, (const wchar_t *)path2 );
#else
	return strcmp( (const char *)path1, (const char *)path2 );
#endif
}

/*
###############################################################################################

Basic file operations

###############################################################################################
*/

/*
=================
FSC_RenameFileRaw

Renames file in OS path format. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_RenameFileRaw( fsc_ospath_t *source_os_path, fsc_ospath_t *target_os_path ) {
	FSC_ASSERT( source_os_path && target_os_path );
#ifdef _WIN32
	if ( !MoveFile( (LPCTSTR)source_os_path, (LPCTSTR)target_os_path ) )
		return fsc_true;
#else
	if ( rename( (const char *)source_os_path, (const char *)target_os_path ) )
		return fsc_true;
#endif
	return fsc_false;
}

/*
=================
FSC_RenameFile

Renames file in standard path format. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_RenameFile( const char *source, const char *target ) {
	FSC_ASSERT( source && target );
	{
		fsc_ospath_t *source_os_path = FSC_StringToOSPath( source );
		fsc_ospath_t *target_os_path = FSC_StringToOSPath( target );
		fsc_boolean result = FSC_RenameFileRaw( source_os_path, target_os_path );
		FSC_Free( source_os_path );
		FSC_Free( target_os_path );
		return result;
	}
}

/*
=================
FSC_DeleteFileRaw

Deletes file in OS path format. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_DeleteFileRaw( fsc_ospath_t *os_path ) {
	FSC_ASSERT( os_path );
#ifdef _WIN32
	// Can we just use remove instead?
	if ( !DeleteFile( (LPCTSTR)os_path ) ) {
		return fsc_true;
	}
#else
	if ( !remove( (const char *)os_path ) ) {
		return fsc_true;
	}
#endif
	return fsc_false;
}

/*
=================
FSC_DeleteFile

Deletes file in standard path format. Returns true on error, false on success.
=================
*/
fsc_boolean FSC_DeleteFile( const char *path ) {
	FSC_ASSERT( path );
	{
		fsc_ospath_t *os_path = FSC_StringToOSPath( path );
		fsc_boolean result = FSC_DeleteFileRaw( os_path );
		FSC_Free( os_path );
		return result;
	}
}

/*
=================
FSC_MkdirRaw

Creates empty directory in OS path format. Returns true on error, false on success or if directory already exists.
=================
*/
fsc_boolean FSC_MkdirRaw( fsc_ospath_t *os_path ) {
	FSC_ASSERT( os_path );
#ifdef _WIN32
	if ( CreateDirectory( (LPCTSTR)os_path, 0 ) ) {
		return fsc_false;
	}
	return GetLastError() != ERROR_ALREADY_EXISTS;
#else
	if ( mkdir( (const char *)os_path, 0750 ) ) {
		return errno != EEXIST;
	}
#endif
	return fsc_false;
}

/*
=================
FSC_Mkdir

Creates empty directory in standard path format. Returns true on error, false on success or if directory already exists.
=================
*/
fsc_boolean FSC_Mkdir( const char *directory ) {
	fsc_ospath_t *os_path = FSC_StringToOSPath( directory );
	fsc_boolean result = FSC_MkdirRaw( os_path );
	FSC_Free( os_path );
	return result;
}

/*
=================
FSC_FOpenRaw

Opens file in OS path format. Returns file handle on success, null on error.
On success result handle must be released by FSC_FClose.
=================
*/
fsc_filehandle_t *FSC_FOpenRaw( const fsc_ospath_t *os_path, const char *mode ) {
	FSC_ASSERT( os_path );
	FSC_ASSERT( mode );
	{
#ifdef WIN_WIDECHAR
		int i;
		wchar_t mode_wide[10];
		for ( i = 0; i < 9; ++i ) {
			mode_wide[i] = mode[i];
			if ( !mode[i] )
				break;
		}
		mode_wide[9] = 0;
		return (fsc_filehandle_t *)_wfopen( (const wchar_t *)os_path, mode_wide );
#else
		return (fsc_filehandle_t *)fopen( (const char *)os_path, mode );
#endif
	}
}

/*
=================
FSC_FOpen

Opens file in standard path format. Returns file handle on success, null on error.
On success result handle must be released by FSC_FClose.
=================
*/
fsc_filehandle_t *FSC_FOpen( const char *path, const char *mode ) {
	FSC_ASSERT( path && mode );
	{
		fsc_ospath_t *os_path = FSC_StringToOSPath( path );
		fsc_filehandle_t *handle = FSC_FOpenRaw( os_path, mode );
		FSC_Free( os_path );
		return handle;
	}
}

/*
=================
FSC_FClose
=================
*/
void FSC_FClose( fsc_filehandle_t *fp ) {
	FSC_ASSERT( fp );
	fclose( (FILE *)fp );
}

/*
=================
FSC_FRead
=================
*/
unsigned int FSC_FRead( void *dest, int size, fsc_filehandle_t *fp ) {
	FSC_ASSERT( dest );
	FSC_ASSERT( fp );
	return fread( dest, 1, size, (FILE *)fp );
}

/*
=================
FSC_FWrite
=================
*/
unsigned int FSC_FWrite( const void *src, int size, fsc_filehandle_t *fp ) {
	FSC_ASSERT( src );
	FSC_ASSERT( fp );
	return fwrite( src, 1, size, (FILE *)fp );
}

/*
=================
FSC_FFlush
=================
*/
void FSC_FFlush( fsc_filehandle_t *fp ) {
	FSC_ASSERT( fp );
	fflush( (FILE *)fp );
}

/*
=================
FSC_FSeek
=================
*/
int FSC_FSeek( fsc_filehandle_t *fp, int offset, fsc_seek_type_t type ) {
	int os_type = SEEK_SET;
	FSC_ASSERT( fp );
	if ( type == FSC_SEEK_CUR )
		os_type = SEEK_CUR;
	if ( type == FSC_SEEK_END )
		os_type = SEEK_END;
	return fseek( (FILE *)fp, offset, os_type );
}

/*
=================
FSC_FTell

Returns 4294967295 on error or overflow.
=================
*/
unsigned int FSC_FTell( fsc_filehandle_t *fp ) {
	FSC_ASSERT( fp );
	{
#ifdef _WIN32
		long long value = _ftelli64( (FILE *)fp );
#else
		off_t value = ftello( (FILE *)fp );
#endif
		if ( value < 0 || value > 4294967295u )
			return 4294967295u;
		return (unsigned int)value;
	}
}

/*
###############################################################################################

String & memory functions

###############################################################################################
*/

/*
=================
FSC_Memcpy
=================
*/
void FSC_Memcpy( void *dst, const void *src, unsigned int size ) {
	FSC_ASSERT( dst && src );
	memcpy( dst, src, size );
}

/*
=================
FSC_Memcmp
=================
*/
int FSC_Memcmp( const void *str1, const void *str2, unsigned int size ) {
	FSC_ASSERT( str1 && str2 );
	return memcmp( str1, str2, size );
}

/*
=================
FSC_Memset
=================
*/
void FSC_Memset( void *dst, int value, unsigned int size ) {
	FSC_ASSERT( dst );
	memset( dst, value, size );
}

/*
=================
FSC_Strncpy

Copies string. Output will be null terminated.
=================
*/
void FSC_Strncpy( char *dst, const char *src, unsigned int size ) {
	FSC_ASSERT( dst && src );
	FSC_ASSERT( size > 0 );
	strncpy( dst, src, size );
	dst[size - 1] = '\0';
}

/*
=================
FSC_StrncpyLower

Copies string and converts to lowercase. Output will be null terminated.
=================
*/
void FSC_StrncpyLower( char *dst, const char *src, unsigned int size ) {
	FSC_ASSERT( dst && src );
	FSC_ASSERT( size > 0 );

	while ( --size && *src ) {
		*( dst++ ) = tolower( *( src++ ) );
	}
	*dst = '\0';
}

/*
=================
FSC_Strcmp
=================
*/
int FSC_Strcmp( const char *str1, const char *str2 ) {
	FSC_ASSERT( str1 && str2 );
	return strcmp( str1, str2 );
}

/*
=================
FSC_Stricmp
=================
*/
int FSC_Stricmp( const char *str1, const char *str2 ) {
	FSC_ASSERT( str1 && str2 );
#ifdef _WIN32
	return _stricmp( str1, str2 );
#else
	return strcasecmp( str1, str2 );
#endif
}

/*
=================
FSC_Strlen
=================
*/
int FSC_Strlen( const char *str ) {
	FSC_ASSERT( str );
	return strlen( str );
}

/*
=================
FSC_Malloc
=================
*/
void *FSC_Malloc( unsigned int size ) {
	void *result = malloc( size );
	FSC_ASSERT( result );
	return result;
}

/*
=================
FSC_Calloc

Returns zero-initialized memory allocation.
=================
*/
void *FSC_Calloc( unsigned int size ) {
	void *result = calloc( size, 1 );
	FSC_ASSERT( result );
	return result;
}

/*
=================
FSC_Free
=================
*/
void FSC_Free( void *allocation ) {
	FSC_ASSERT( allocation );
	free( allocation );
}

/*
###############################################################################################

Directory Iteration

###############################################################################################
*/

#define SEARCH_PATH_LIMIT 260

typedef struct {
	FSC_CHAR path[SEARCH_PATH_LIMIT];
	int path_position;
	int base_length;

	void ( *operation )( iterate_data_t *file_data, void *iterate_context );
	char qpath_buffer[FSC_MAX_QPATH];
	iterate_data_t file_data;
	void *iterate_context;
} iterate_work_t;

/*
=================
FSC_IterateAppendPath

Adds string at the end of working path. Returns true on success, false on overflow.
=================
*/
static fsc_boolean FSC_IterateAppendPath( iterate_work_t *iw, const FSC_CHAR *path ) {
	while ( iw->path_position < SEARCH_PATH_LIMIT ) {
		iw->path[iw->path_position] = *path;
		if ( !*( path++ ) ) {
			return fsc_true;
		}
		++iw->path_position;
	}

	iw->path_position = SEARCH_PATH_LIMIT - 1;
	iw->path[iw->path_position] = 0;
	return fsc_false;
}

/*
=================
FSC_IterateResetPosition

Resets working path back to specified length, erasing anything appended after that point.
=================
*/
static void FSC_IterateResetPosition( iterate_work_t *iw, int position ) {
	FSC_ASSERT( position >= 0 && position < SEARCH_PATH_LIMIT );
	FSC_ASSERT( position <= iw->path_position );
	iw->path_position = position;
	iw->path[position] = 0;
}

/*
=================
FSC_IterateDirectoryRecursive
=================
*/
static void FSC_IterateDirectoryRecursive( iterate_work_t *iw, fsc_boolean junction_allowed ) {
#ifdef _WIN32
	int old_position = iw->path_position;
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;

	if ( !FSC_IterateAppendPath( iw, TEXT( "\\*" ) ) ) {
		return;
	}
	hFind = FindFirstFile( iw->path, &FindFileData );
	//hFind = FindFirstFileEx(search, FindExInfoBasic, &FindFileData, FindExSearchNameMatch, 0, FIND_FIRST_EX_LARGE_FETCH);
	if ( hFind == INVALID_HANDLE_VALUE ) {
		return;
	}

	do {
		// Prepare path
		FSC_IterateResetPosition( iw, old_position );
		FSC_IterateAppendPath( iw, TEXT( "\\" ) );
		if ( !FSC_IterateAppendPath( iw, FindFileData.cFileName ) ) {
			continue;
		}

		if ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
			// Have directory - check validity
			if ( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) && !junction_allowed ) {
				continue;
			}
			if ( FindFileData.cFileName[0] == '.' && ( !FindFileData.cFileName[1] ||
					( FindFileData.cFileName[1] == '.' && !FindFileData.cFileName[2] ) ) ) {
				continue;
			}

			FSC_IterateDirectoryRecursive( iw, junction_allowed );
		} else {
			// Skip files greater than 4GB; they are not currently supported
			if ( FindFileData.nFileSizeHigh ) {
				continue;
			}

			// Run callback operation
			iw->file_data.os_path = (fsc_ospath_t *)iw->path;
			iw->file_data.qpath_with_mod_dir = FSC_OSPathToString( (fsc_ospath_t *)( iw->path + iw->base_length + 1 ) );
			iw->file_data.os_timestamp = FindFileData.ftLastWriteTime.dwLowDateTime;
			iw->file_data.filesize = FindFileData.nFileSizeLow;
			iw->operation( &iw->file_data, iw->iterate_context );
			FSC_Free( iw->file_data.qpath_with_mod_dir );
		}
	} while ( FindNextFile( hFind, &FindFileData ) );

	FindClose( hFind );
#else
	int old_position = iw->path_position;
	DIR *dir = opendir( iw->path );
	if ( !dir ) {
		return;
	}

	while ( 1 ) {
		struct dirent *entry;
		struct stat st;

		// Get next entry
		entry = readdir( dir );
		if ( !entry ) {
			break;
		}

		// Get path and stat
		FSC_IterateResetPosition( iw, old_position );
		FSC_IterateAppendPath( iw, "/" );
		if ( !FSC_IterateAppendPath( iw, entry->d_name ) ) {
			continue;
		}
		if ( stat( iw->path, &st ) == -1 ) {
			continue;
		}

		if ( entry->d_type & DT_DIR ) {
			// Have directory - check validity
			if ( entry->d_name[0] == '.' && ( !entry->d_name[1] || ( entry->d_name[1] == '.' && !entry->d_name[2] ) ) ) {
				continue;
			}

			FSC_IterateDirectoryRecursive( iw, junction_allowed );
		} else {
			// Skip files greater than 4GB; they are not currently supported
			if ( st.st_size > 4294967295u ) {
				continue;
			}

			// Run callback operation
			iw->file_data.os_path = (fsc_ospath_t *)iw->path;
			iw->file_data.qpath_with_mod_dir = FSC_OSPathToString( (fsc_ospath_t *)( iw->path + iw->base_length + 1 ) );
			iw->file_data.os_timestamp = (unsigned int)st.st_mtime;
			iw->file_data.filesize = (unsigned int)st.st_size;
			iw->operation( &iw->file_data, iw->iterate_context );
			FSC_Free( iw->file_data.qpath_with_mod_dir );
		}
	}

	closedir( dir );
#endif
}

/*
=================
FSC_IterateDirectory

Scans given directory recursively and executes callback operation on each file located.
=================
*/
void FSC_IterateDirectory( fsc_ospath_t *search_os_path,
		void( operation )( iterate_data_t *file_data, void *iterate_context ),	void *iterate_context ) {
	iterate_work_t iw;
	iw.path_position = 0;
	iw.operation = operation;
	iw.iterate_context = iterate_context;

	FSC_IterateAppendPath( &iw, (const FSC_CHAR *)search_os_path );
	iw.base_length = iw.path_position;

	FSC_IterateDirectoryRecursive( &iw, fsc_false );
}

#endif	// NEW_FILESYSTEM
