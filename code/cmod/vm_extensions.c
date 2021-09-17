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

#define VMEXT_TRAP_OFFSET 2400
#define VMEXT_TRAP_GETVALUE 700

/*
==================
VMExt_CheckGetString

Handles GetValue calls returning strings.
Returns qtrue on match and writes result to buffer, qfalse otherwise.
==================
*/
static qboolean VMExt_CheckGetString( const char *command, char *buffer, unsigned int size, vmType_t vm_type ) {
	return qfalse;
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
	*retval = 0;

	// Handle GetValue call
	if ( args[0] == VMEXT_TRAP_GETVALUE ) {
		char *buffer = VMA( 1 );
		unsigned int size = args[2];
		const char *command = VMA( 3 );

		if ( VMExt_CheckGetString( command, buffer, size, vm_type ) ) {
			*retval = 1;
		}

		return qtrue;
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
