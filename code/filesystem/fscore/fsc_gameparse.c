/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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

// String parsing functions adapted from qcommon.c

/*
=================
FSC_SkipRestOfLine
=================
*/
void FSC_SkipRestOfLine ( char **data ) {
	char	*p;
	int		c;

	p = *data;
	while ( (c = *p++) != 0 ) {
		if ( c == '\n' ) {
			break;
		}
	}

	*data = p;
}

/*
=================
FSC_SkipWhitespace
=================
*/
static char *FSC_SkipWhitespace( char *data, fsc_boolean *hasNewLines ) {
	int c;

	while( (c = *data) <= ' ') {
		if( !c ) {
			return FSC_NULL;
		}
		if( c == '\n' ) {
			*hasNewLines = fsc_true;
		}
		data++;
	}

	return data;
}

/*
=================
FSC_SkipWhitespace

com_token is a buffer of length MAX_TOKEN_CHARS that stores the token being returned.
=================
*/
char *FSC_ParseExt( char *com_token, char **data_p, fsc_boolean allowLineBreaks )
{
	int c = 0, len;
	fsc_boolean hasNewLines = fsc_false;
	char *data;

	data = *data_p;
	len = 0;
	com_token[0] = 0;

	// make sure incoming data is valid
	if ( !data )
	{
		*data_p = FSC_NULL;
		return com_token;
	}

	while ( 1 )
	{
		// skip whitespace
		data = FSC_SkipWhitespace( data, &hasNewLines );
		if ( !data )
		{
			*data_p = FSC_NULL;
			return com_token;
		}
		if ( hasNewLines && !allowLineBreaks )
		{
			*data_p = data;
			return com_token;
		}

		c = *data;

		// skip double slash comments
		if ( c == '/' && data[1] == '/' )
		{
			data += 2;
			while (*data && *data != '\n') {
				data++;
			}
		}
		// skip /* */ comments
		else if ( c=='/' && data[1] == '*' )
		{
			data += 2;
			while ( *data && ( *data != '*' || data[1] != '/' ) )
			{
				data++;
			}
			if ( *data )
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	// handle quoted strings
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				*data_p = ( char * ) data;
				return com_token;
			}
			if (len < FSC_MAX_TOKEN_CHARS - 1)
			{
				com_token[len] = (char)c;
				len++;
			}
		}
	}

	// parse a regular word
	do
	{
		if (len < FSC_MAX_TOKEN_CHARS - 1)
		{
			com_token[len] = (char)c;
			len++;
		}
		data++;
		c = *data;
	} while (c>32);

	com_token[len] = 0;

	*data_p = ( char * ) data;
	return com_token;
}

/*
=================
FSC_SkipBracedSection

Slightly modified from the original q_shared.c version
depth parameter is initial depth (0 if expecting first brace)
=================
*/
int FSC_SkipBracedSection(char **program, int depth) {
	char token[FSC_MAX_TOKEN_CHARS];

	do {
		FSC_ParseExt( token, program, fsc_true );
		if( token[1] == 0 ) {
			if( token[0] == '{' ) {
				depth++;
			}
			else if( token[0] == '}' ) {
				depth--;
			}
		}
	} while( depth && *program );

	return depth;
}

#endif	// NEW_FILESYSTEM
