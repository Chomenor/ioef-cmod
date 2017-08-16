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

#ifdef CMOD_COMMAND_INTERPRETER
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#define	MAX_CMD_LINE 1024

/* ******************************************************************************** */
// Tokenization Support
/* ******************************************************************************** */

static	int			cmd_argc;
static	char		*cmd_argv[MAX_STRING_TOKENS];		// points into cmd_tokenized
static	char		cmd_tokenized[BIG_INFO_STRING+MAX_STRING_TOKENS];	// will have 0 bytes inserted
static	char		cmd_cmd[BIG_INFO_STRING]; // the original command we received (no token processing)

int		Cmd_Argc( void ) {
	return cmd_argc;
}

char	*Cmd_Argv( int arg ) {
	if ( (unsigned)arg >= cmd_argc ) {
		return "";
	}
	return cmd_argv[arg];	
}

// The interpreted versions use this because
// they can't have pointers returned to them
void	Cmd_ArgvBuffer( int arg, char *buffer, int bufferLength ) {
	Q_strncpyz( buffer, Cmd_Argv( arg ), bufferLength );
}

// Returns a single string containing argv(1) to argv(argc()-1)
char	*Cmd_Args( void ) {
	static	char		cmd_args[MAX_STRING_CHARS];
	int		i;

	cmd_args[0] = 0;
	for ( i = 1 ; i < cmd_argc ; i++ ) {
		strcat( cmd_args, cmd_argv[i] );
		if ( i != cmd_argc-1 ) {
			strcat( cmd_args, " " );
		}
	}

	return cmd_args;
}

// Returns a single string containing argv(arg) to argv(argc()-1)
char *Cmd_ArgsFrom( int arg ) {
	static	char		cmd_args[BIG_INFO_STRING];
	int		i;

	cmd_args[0] = 0;
	if (arg < 0)
		arg = 0;
	for ( i = arg ; i < cmd_argc ; i++ ) {
		strcat( cmd_args, cmd_argv[i] );
		if ( i != cmd_argc-1 ) {
			strcat( cmd_args, " " );
		}
	}

	return cmd_args;
}

// The interpreted versions use this because
// they can't have pointers returned to them
void	Cmd_ArgsBuffer( char *buffer, int bufferLength ) {
	Q_strncpyz( buffer, Cmd_Args(), bufferLength );
}

// Retrieve the unmodified command string
// For rcon use when you want to transmit without altering quoting
// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
char *Cmd_Cmd(void)
{
	return cmd_cmd;
}

// Replace command separators with space to prevent interpretation
// This is a hack to protect buggy qvms
// https://bugzilla.icculus.org/show_bug.cgi?id=3593
// https://bugzilla.icculus.org/show_bug.cgi?id=4769
void Cmd_Args_Sanitize(void)
{
	int i;

	for(i = 1; i < cmd_argc; i++)
	{
		char *c = cmd_argv[i];
		
		if(strlen(c) > MAX_CVAR_VALUE_STRING - 1)
			c[MAX_CVAR_VALUE_STRING - 1] = '\0';
		
		while ((c = strpbrk(c, "\n\r;"))) {
			*c = ' ';
			++c;
		}
	}
}

// Parses the given string into command line tokens.
// The text is copied to a seperate buffer and 0 characters
// are inserted in the apropriate place, The argv array
// will point into this temporary buffer.
static void Cmd_TokenizeString2( const char *text_in, qboolean ignoreQuotes ) {
	const char	*text;
	char	*textOut;

	// Com_DPrintf("Cmd_TokenizeString: %s\n", text_in);

	// clear previous args
	cmd_argc = 0;

	if ( !text_in ) {
		return;
	}
	
	Q_strncpyz( cmd_cmd, text_in, sizeof(cmd_cmd) );

	text = text_in;
	textOut = cmd_tokenized;

	while ( 1 ) {
		if ( cmd_argc == MAX_STRING_TOKENS ) {
			return;			// this is usually something malicious
		}

		while ( 1 ) {
			// skip whitespace
			while ( *text && *text <= ' ' ) {
				text++;
			}
			if ( !*text ) {
				return;			// all tokens parsed
			}

			// skip // comments
			if ( text[0] == '/' && text[1] == '/' ) {
				return;			// all tokens parsed
			}

			// skip /* */ comments
			if ( text[0] == '/' && text[1] =='*' ) {
				while ( *text && ( text[0] != '*' || text[1] != '/' ) ) {
					text++;
				}
				if ( !*text ) {
					return;		// all tokens parsed
				}
				text += 2;
			} else {
				break;			// we are ready to parse a token
			}
		}

		// handle quoted strings
    // NOTE TTimo this doesn't handle \" escaping
		if ( !ignoreQuotes && *text == '"' ) {
			cmd_argv[cmd_argc] = textOut;
			cmd_argc++;
			text++;
			while ( *text && *text != '"' ) {
				*textOut++ = *text++;
			}
			*textOut++ = 0;
			if ( !*text ) {
				return;		// all tokens parsed
			}
			text++;
			continue;
		}

		// regular token
		cmd_argv[cmd_argc] = textOut;
		cmd_argc++;

		// skip until whitespace, quote, or command
		while ( *text > ' ' ) {
			if ( !ignoreQuotes && text[0] == '"' ) {
				break;
			}

			if ( text[0] == '/' && text[1] == '/' ) {
				break;
			}

			// skip /* */ comments
			if ( text[0] == '/' && text[1] =='*' ) {
				break;
			}

			*textOut++ = *text++;
		}

		*textOut++ = 0;

		if ( !*text ) {
			return;		// all tokens parsed
		}
	}
	
}

void Cmd_TokenizeString( const char *text_in ) {
	Cmd_TokenizeString2( text_in, qfalse );
}

void Cmd_TokenizeStringIgnoreQuotes( const char *text_in ) {
	Cmd_TokenizeString2( text_in, qtrue );
}

/* ******************************************************************************** */
// Console Command Index
/* ******************************************************************************** */

typedef struct cmd_function_s
{
	struct cmd_function_s	*next;
	char					*name;
	xcommand_t				function;
	qboolean				protected_support;
	completionFunc_t	complete;
} cmd_function_t;

static	cmd_function_t	*cmd_functions;		// possible commands to execute

cmd_function_t *Cmd_FindCommand(const char *cmd_name) {
	cmd_function_t *cmd;
	for( cmd = cmd_functions; cmd; cmd = cmd->next )
		if( !Q_stricmp( cmd_name, cmd->name ) )
			return cmd;
	return NULL; }

static cmd_function_t *Cmd_FindOrCreateCommand(const char *cmd_name) {
	cmd_function_t *cmd = Cmd_FindCommand(cmd_name);
	if(cmd) return cmd;

	cmd = S_Malloc(sizeof(cmd_function_t));
	Com_Memset(cmd, 0, sizeof(*cmd));
	cmd->name = CopyString(cmd_name);
	cmd->next = cmd_functions;
	cmd_functions = cmd;
	return cmd; }

void Cmd_AddCommand( const char *cmd_name, xcommand_t function ) {
	cmd_function_t *cmd = Cmd_FindOrCreateCommand(cmd_name);
	if(cmd->function && function) {
		Com_Printf( "Cmd_AddCommandCommon: %s already defined\n", cmd_name );
		return; }
	cmd->function = function; }

void Cmd_AddProtectableCommand( const char *cmd_name, xcommand_protected_t protected_function ) {
	cmd_function_t *cmd = Cmd_FindOrCreateCommand(cmd_name);
	if(cmd->function && protected_function) {
		Com_Printf( "Cmd_AddCommandCommon: %s already defined\n", cmd_name );
		return; }
	cmd->function = (xcommand_t)protected_function;
	cmd->protected_support = qtrue; }

void Cmd_SetCommandCompletionFunc( const char *command, completionFunc_t complete ) {
	cmd_function_t *cmd = Cmd_FindOrCreateCommand(command);
	cmd->complete = complete; }

void Cmd_RemoveCommand( const char *cmd_name ) {
	cmd_function_t	*cmd, **back;

	back = &cmd_functions;
	while( 1 ) {
		cmd = *back;
		if ( !cmd ) {
			// command wasn't active
			return;
		}
		if ( !strcmp( cmd_name, cmd->name ) ) {
			if(cmd->protected_support) {
				// Don't clear protected support flag
				cmd->function = 0;
				cmd->complete = 0;
				return; }

			// Delete the command
			*back = cmd->next;
			if (cmd->name) {
				Z_Free(cmd->name);
			}
			Z_Free (cmd);
			return;
		}
		back = &cmd->next;
	}
}

void Cmd_RemoveCommandSafe( const char *cmd_name )
{
	cmd_function_t *cmd = Cmd_FindCommand( cmd_name );
	if(!cmd) return;

	if(cmd->function) {
		Com_Error( ERR_DROP, "Restricted source tried to remove "
			"system command \"%s\"", cmd_name );
		return; }

	Cmd_RemoveCommand( cmd_name ); }

void Cmd_CommandCompletion( void(*callback)(const char *s) ) {
	cmd_function_t	*cmd;
	
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next) {
		callback( cmd->name );
	}
}

void Cmd_CompleteArgument( const char *command, char *args, int argNum ) {
	cmd_function_t	*cmd;

	// Special case for "set" command
	if(!Q_stricmpn(command, "set", 3)) {
		Cvar_CompleteCvarName(args, argNum); }

	for( cmd = cmd_functions; cmd; cmd = cmd->next ) {
		if( !Q_stricmp( command, cmd->name ) && cmd->complete ) {
			cmd->complete( args, argNum );
		}
	}
}

void Cmd_List_f (void)
{
	cmd_function_t	*cmd;
	int				i;
	char			*match;

	if ( Cmd_Argc() > 1 ) {
		match = Cmd_Argv( 1 );
	} else {
		match = NULL;
	}

	i = 0;
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next) {
		if (match && !Com_Filter(match, cmd->name, qfalse)) continue;

		Com_Printf ("%s\n", cmd->name);
		i++;
	}
	Com_Printf ("%i commands\n", i);
}

void Cmd_CompleteCfgName( char *args, int argNum ) {
	if( argNum == 2 ) {
		Field_CompleteFilename( "", "cfg", qfalse, qtrue );
	}
}

/* ******************************************************************************** */
// Individual Command String Execution
/* ******************************************************************************** */

void Cmd_ExecuteStringByMode(const char *text, cmd_mode_t mode) {	
	cmd_function_t	*cmd, **prev;

	// tokenize the command
	Cmd_TokenizeString( text );		
	if ( !Cmd_Argc() ) {
		return;		// no tokens
	}

#ifdef CMOD_CVAR_HANDLING
	// special case for "set" command
	if(!Q_stricmpn( cmd_argv[0], "set", 3)) {
		if(Cvar_Set_Command(mode)) return; }
#endif

	// check registered command functions	
	for ( prev = &cmd_functions ; *prev ; prev = &cmd->next ) {
		cmd = *prev;
		if ( !Q_stricmp( cmd_argv[0],cmd->name ) ) {
			// rearrange the links so that the command will be
			// near the head of the list next time it is used
			*prev = cmd->next;
			cmd->next = cmd_functions;
			cmd_functions = cmd;

			// perform the action if function defined
			if(cmd->function) {
				if(cmd->protected_support) {
					((xcommand_protected_t)cmd->function)(mode); }
				else if(mode == CMD_NORMAL) {
					cmd->function(); }
				return; }

			// let the cgame or game handle it
			break; } }
	
	// check cvars
#ifdef CMOD_CVAR_HANDLING
	if(cvar_command(mode)) return;
#else
	if(Cvar_Command()) return;
#endif

	// check client game commands
	if ( com_cl_running && com_cl_running->integer && CL_GameCommand() ) {
		return;
	}

	// check server game commands
	if ( com_sv_running && com_sv_running->integer && SV_GameCommand() ) {
		return;
	}

	// check ui commands
	if ( com_cl_running && com_cl_running->integer && UI_GameCommand() ) {
		return;
	}

	// send it as a server command if we are connected
	// this will usually result in a chat message
	CL_ForwardCommandToServer ( text ); }

void Cmd_ExecuteString(const char *text) {
	Cmd_ExecuteStringByMode(text, CMD_NORMAL); }

/* ******************************************************************************** */
// Command Buffer Support
/* ******************************************************************************** */

typedef struct cbuf_block_s {
	struct cbuf_block_s *next;
	cmd_mode_t mode;
	char *data;
	int position;
	int size;
} cbuf_block_t;

typedef struct {
	cbuf_block_t *first;
	cbuf_block_t *last;
} cbuf_t;

void cbuf_initialize(cbuf_t *cbuf) {
	memset(cbuf, 0, sizeof(*cbuf)); }

cbuf_block_t *cbuf_build_block(const char *text, qboolean add_newline, cmd_mode_t mode) {
	cbuf_block_t *block = Z_Malloc(sizeof(cbuf_block_t));
	int size = strlen(text);
	block->size = size + (add_newline ? 1 : 0);
	block->data = Z_Malloc(block->size);
	block->position = 0;
	block->mode = mode;
	memcpy(block->data, text, size);
	if(add_newline) block->data[size] = '\n';
	return block; }

void cbuf_insert_block(cbuf_t *cbuf, cbuf_block_t *block) {
	if(cbuf->first) block->next = cbuf->first;
	cbuf->first = block;
	if(!cbuf->last) cbuf->last = block; }

void cbuf_append_block(cbuf_t *cbuf, cbuf_block_t *block) {
	if(cbuf->last) cbuf->last->next = block;
	cbuf->last = block;
	if(!cbuf->first) cbuf->first = block; }

void cbuf_free_block(cbuf_block_t *block) {
	Z_Free(block->data);
	Z_Free(block); }

// Removes and frees first block
void cbuf_advance_block(cbuf_t *cbuf) {
	cbuf_block_t *current = cbuf->first;
	cbuf->first = current->next;
	if(!cbuf->first) cbuf->last = 0;
	cbuf_free_block(current); }

// Writes command to output and returns mode of command
cmd_mode_t cbuf_get_command(cbuf_t *cbuf, char *output, int output_size) {
	char current;
	char next;
	int output_position = 0;
	qboolean quotes = qfalse;
	qboolean in_star_comment = qfalse;
	qboolean in_slash_comment = qfalse;
	cmd_mode_t mode = cbuf->first ? cbuf->first->mode : 0;

	while(cbuf->first) {
		// Check if we need to advance to next block
		if(cbuf->first->position >= cbuf->first->size) {
			cbuf_advance_block(cbuf);
			if(!cbuf->first || cbuf->first->mode != mode) break;
			continue; }

		// Get current and next characters
		current = cbuf->first->data[cbuf->first->position++];
		next = cbuf->first->position < cbuf->first->size ? cbuf->first->data[cbuf->first->position] : 0;

		// Check for switching quote mode
		if(current == '"') quotes = quotes ? qfalse : qtrue;

		// Check for switching comment mode
		if(!quotes) {
			if(!in_star_comment && current == '/' && next == '/') {
				in_slash_comment = qtrue; }
			else if(!in_slash_comment && current == '/' && next == '*') {
				in_star_comment = qtrue; }
			else if(in_star_comment && current == '*' && next == '/') {
				in_star_comment = qfalse;
				++cbuf->first->position; } }

		// Check for command-terminating characters
		if(!in_star_comment && ((current == '\n' || current == '\r') ||
				(!in_slash_comment && !quotes && current == ';'))) break;

		// Write out the character if we aren't commented and there's enough space in the output
		if(!in_star_comment && !in_slash_comment && output_position < output_size - 1) {
			output[output_position++] = current; } }

	// Null-terminate output
	output[output_position] = 0;

	// If there's nothing left in current block, it's useful to advance it now instead of waiting
	// for the next call to this function
	if(cbuf->first && cbuf->first->position >= cbuf->first->size) {
		cbuf_advance_block(cbuf); }

	return mode; }

/* ******************************************************************************** */
// Command Buffer Execution
/* ******************************************************************************** */

cbuf_t main_cbuf;
int cmd_wait;

// Causes execution of the remainder of the command buffer to be delayed until
// next frame.  This allows commands like:
// bind g "cmd use rocket ; +attack ; wait ; -attack ; cmd use blaster"
void Cmd_Wait_f(cmd_mode_t mode) {
	if ( Cmd_Argc() == 2 ) {
		cmd_wait = atoi( Cmd_Argv( 1 ) );
		if ( cmd_wait < 0 )
			cmd_wait = 1; // ignore the argument
	} else {
		cmd_wait = 1;
	}
}

void Cbuf_Init(void) {
}

// Adds command text at the end of the buffer, does NOT add a final \n
void Cbuf_AddTextByMode(const char *text, cmd_mode_t mode) {
	cbuf_append_block(&main_cbuf, cbuf_build_block(text, qfalse, mode)); }

// Adds command text immediately after the current command
// Adds a \n to the text
void Cbuf_InsertTextByMode(const char *text, cmd_mode_t mode) {
	cbuf_insert_block(&main_cbuf, cbuf_build_block(text, qtrue, mode)); }

void Cbuf_AddText(const char *text) {
	Cbuf_AddTextByMode(text, CMD_NORMAL); }

void Cbuf_InsertText(const char *text) {
	Cbuf_InsertTextByMode(text, CMD_NORMAL); }

void Cbuf_ExecuteTextByMode(int exec_when, const char *text, cmd_mode_t mode) {
	switch (exec_when) {
	case EXEC_NOW:
		if (text && strlen(text) > 0) {
			Com_DPrintf(S_COLOR_YELLOW "EXEC_NOW %s\n", text);
			Cmd_ExecuteStringByMode(text, mode);
		} else {
			Cbuf_Execute();
			//Com_DPrintf(S_COLOR_YELLOW "EXEC_NOW %s\n", cmd_text.data);
		}
		break;
	case EXEC_INSERT:
		Cbuf_InsertTextByMode(text, mode);
		break;
	case EXEC_APPEND:
		Cbuf_AddTextByMode(text, mode);
		break;
	default:
		Com_Error(ERR_FATAL, "Cbuf_ExecuteText: bad exec_when"); } }

void Cbuf_ExecuteText(int exec_when, const char *text) {
	Cbuf_ExecuteTextByMode(exec_when, text, CMD_NORMAL); }

void Cbuf_Execute (void) {
	char cmd[MAX_CMD_LINE];
	cmd_mode_t mode;

	while(main_cbuf.first) {
		if(cmd_wait > 0) {
			cmd_wait--;
			break; }

		mode = cbuf_get_command(&main_cbuf, cmd, sizeof(cmd));
		if(*cmd) Cmd_ExecuteStringByMode(cmd, mode); } }

/* ******************************************************************************** */
// Misc Commands
/* ******************************************************************************** */

void Cmd_Exec_f(cmd_mode_t mode) {
	qboolean quiet;
#ifndef NEW_FILESYSTEM
	union {
		char	*c;
		void	*v;
	} f;
#endif
	char	filename[MAX_QPATH];

	quiet = !Q_stricmp(Cmd_Argv(0), "execq");

	if (Cmd_Argc () != 2) {
		Com_Printf ("exec%s <filename> : execute a script file%s\n",
		            quiet ? "q" : "", quiet ? " without notification" : "");
		return;
	}

	Q_strncpyz( filename, Cmd_Argv(1), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
#ifndef DEDICATED
	if(!COM_CompareExtension(filename, ".cfg")) mode = CMD_PROTECTED;
#endif
#ifdef NEW_FILESYSTEM
	fs_execute_config_file(filename,
			mode == CMD_PROTECTED ? FS_CONFIGTYPE_PROTECTED : FS_CONFIGTYPE_NONE, EXEC_INSERT, quiet);
#else
	FS_ReadFile( filename, &f.v);
	if (!f.c) {
		Com_Printf ("couldn't exec %s\n", filename);
		return;
	}
	if (!quiet)
		Com_Printf ("execing %s\n", filename);
	
	Cbuf_InsertText (f.c);

	FS_FreeFile (f.v);
#endif
}

// Inserts the current value of a variable as command text
void Cmd_Vstr_f(cmd_mode_t mode) {
#ifdef CMOD_CVAR_HANDLING
	cvar_vstr(mode);
#else
	char	*v;

	if (Cmd_Argc () != 2) {
		Com_Printf ("vstr <variablename> : execute a variable command\n");
		return;
	}

	v = Cvar_VariableString( Cmd_Argv( 1 ) );
	Cbuf_InsertTextByMode(v, mode);
#endif
}

// Just prints the rest of the line to the console
void Cmd_Echo_f(cmd_mode_t mode)
{
	Com_Printf ("%s\n", Cmd_Args());
}

/* ******************************************************************************** */
// Base Protectable Commands
/* ******************************************************************************** */

static const char *base_protectable_commands[] = {
	"cmd",
	"vid_restart",
	"disconnect",
	"globalservers",
	"ping",
	"+attack",
	"-attack",
	"map",
	"demo",
	"devmap",
	"screenshot",
	"spmap",
	"spdevmap",
	"killserver"
	"centerview",
	"cmod_crosshair_advance",
	"+moveup",
	"-moveup",
	"+movedown",
	"-movedown",
	"+left",
	"-left",
	"+right",
	"-right",
	"+forward",
	"-forward",
	"+back",
	"-back",
	"+lookup",
	"-lookup",
	"+lookdown",
	"-lookdown",
	"+strafe",
	"-strafe",
	"+moveleft",
	"-moveleft",
	"+moveright",
	"-moveright",
	"+speed",
	"-speed",
	"+attack",
	"-attack",
	"+button0",
	"-button0",
	"+button1",
	"-button1",
	"+button2",
	"-button2",
	"+button3",
	"-button3",
	"+button4",
	"-button4",
	"+altattack",
	"-altattack",
	"+use",
	"-use",
	"+button5",
	"-button5",
	"+button6",
	"-button6",
	"+button7",
	"-button7",
	"+button8",
	"-button8",
	"+button9",
	"-button9",
	"+button10",
	"-button10",
	"+button11",
	"-button11",
	"+button12",
	"-button12",
	"+button13",
	"-button13",
	"+button14",
	"-button14",
	"+mlook",
	"-mlook",
};

static void init_base_protectable_commands(void) {
	int i;
	for(i=0; i<ARRAY_LEN(base_protectable_commands); ++i) {
		cmd_function_t *cmd = Cmd_FindOrCreateCommand(base_protectable_commands[i]);
		if(cmd) cmd->protected_support = qtrue; } }

/* ******************************************************************************** */
// Initialization
/* ******************************************************************************** */

void Cmd_Init (void) {
	init_base_protectable_commands();
	Cmd_AddCommand ("cmdlist",Cmd_List_f);
	Cmd_AddProtectableCommand ("exec",Cmd_Exec_f);
	Cmd_AddProtectableCommand ("execq",Cmd_Exec_f);
	Cmd_SetCommandCompletionFunc( "exec", Cmd_CompleteCfgName );
	Cmd_SetCommandCompletionFunc( "execq", Cmd_CompleteCfgName );
	Cmd_AddProtectableCommand ("vstr",Cmd_Vstr_f);
	Cmd_SetCommandCompletionFunc( "vstr", Cvar_CompleteCvarName );
	Cmd_AddProtectableCommand ("echo",Cmd_Echo_f);
	Cmd_AddProtectableCommand ("wait", Cmd_Wait_f);
}

#endif
