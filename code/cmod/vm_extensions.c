/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2020 Noah Metzger (chomenor@gmail.com)

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

#ifdef CMOD_VM_EXTENSIONS
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef CMOD_SERVER_BROWSER_SUPPORT
int CL_ServerStatusExt( char *serverAddress, char *serverStatusString, int maxLen, char *extString, int extLen );
#endif

#define VMEXT_TRAP_OFFSET 2400
#define VMEXT_TRAP_GETVALUE 700

typedef enum {
#ifdef CMOD_SERVER_BROWSER_SUPPORT
	VMEXT_LAN_SERVERSTATUS_EXT,
#endif
#ifdef CMOD_CLIENT_ALT_SWAP_SUPPORT
	VMEXT_ALTSWAP_SET_STATE,
#endif

	VMEXT_FUNCTION_COUNT
} vmext_function_id_t;

/*
==================
VMExt_CheckGetString

Handles GetValue calls returning strings.
Returns qtrue on match and writes result to buffer, qfalse otherwise.
==================
*/
static qboolean VMExt_CheckGetString( const char *command, char *buffer, unsigned int size, vmType_t vm_type ) {
#ifdef CMOD_CROSSHAIR
	if ( !Q_stricmp( command, "crosshair_get_current_shader" ) ) {
		// Returns 0 to display no crosshair, >0 for shdader handle value, or -1 if engine crosshair mode is inactive.
		Com_sprintf( buffer, size, "%i", CMCrosshair_GetCurrentShader() );
		return qtrue;
	}
	if ( !Q_stricmp( command, "crosshair_advance_current" ) ) {
		// Returns 1 if successful, 0 if engine crosshair mode is inactive.
		Com_sprintf( buffer, size, "%i", CMCrosshair_VMAdvanceCurrentCrosshair() );
		return qtrue;
	}
	if ( !Q_stricmp( command, "crosshair_register_support" ) ) {
		CMCrosshair_RegisterVMSupport( vm_type );
		Q_strncpyz( buffer, "1", size );
		return qtrue;
	}
#endif
#ifdef CMOD_VM_CONFIG_VALUES
	if ( !Q_stricmp( command, "ui_version_string" ) ) {
		Q_strncpyz( buffer, "cMod HM v1.15", size );
		return qtrue;
	}
	if ( !Q_stricmp( command, "ui_no_cd_key" ) ) {
		Q_strncpyz( buffer, "1", size );
		return qtrue;
	}
#endif

	return qfalse;
}

/*
==================
VMExt_CheckGetFunction

Handles GetValue calls returning extended functions.
Returns trap id on success, -1 otherwise.
==================
*/
static int VMExt_CheckGetFunction( const char *command ) {
#ifdef CMOD_SERVER_BROWSER_SUPPORT
	if ( !Q_stricmp( command, "trap_lan_serverstatus_ext" ) )
		return VMEXT_LAN_SERVERSTATUS_EXT;
#endif
#ifdef CMOD_CLIENT_ALT_SWAP_SUPPORT
	if ( !Q_stricmp( command, "trap_altswap_set_state" ) )
		return VMEXT_ALTSWAP_SET_STATE;
#endif

	return -1;
}

/*
==================
VMExt_HandleVMSyscall

Handles VM system calls for GetValue or other extended functions.
Returns qtrue to abort standard syscall handling, qfalse otherwise.
==================
*/
qboolean VMExt_HandleVMSyscall( intptr_t *args, vmType_t vm_type, vm_t *vm,
		void *( *VM_ArgPtr )( intptr_t intValue ), intptr_t *retval ) {
	int function_id;
	*retval = 0;

	// Handle GetValue call
	if ( args[0] == VMEXT_TRAP_GETVALUE ) {
		char *buffer = VMA( 1 );
		unsigned int size = args[2];
		const char *command = VMA( 3 );

		if ( VMExt_CheckGetString( command, buffer, size, vm_type ) ) {
			*retval = 1;
		} else {
			function_id = VMExt_CheckGetFunction( command );
			if ( function_id >= 0 ) {
				Com_sprintf( buffer, size, "%i", VMEXT_TRAP_OFFSET + function_id );
				*retval = 1;
			}
		}

		return qtrue;
	}

	// Handle extension function calls
	function_id = args[0] - VMEXT_TRAP_OFFSET;
	if ( function_id >= 0 && function_id < VMEXT_FUNCTION_COUNT ) {

#ifdef CMOD_SERVER_BROWSER_SUPPORT
		if ( function_id == VMEXT_LAN_SERVERSTATUS_EXT ) {
			*retval = CL_ServerStatusExt( VMA(1), VMA(2), args[3], VMA(4), args[5] );
			return qtrue;
		}
#endif
#ifdef CMOD_CLIENT_ALT_SWAP_SUPPORT
		if ( function_id == VMEXT_ALTSWAP_SET_STATE ) {
			ClientAltSwap_SetState( args[1] );
			*retval = qtrue;
			return qtrue;
		}
#endif

		Com_Error( ERR_DROP, "Unsupported VM extension function call: %i", function_id );
	}

	return qfalse;
}

/*
==================
VMExt_Init
==================
*/
void VMExt_Init( void ) {
	Cvar_Get( "//trap_GetValue", va( "%i", VMEXT_TRAP_GETVALUE ), CVAR_PROTECTED | CVAR_ROM );
}

#endif
