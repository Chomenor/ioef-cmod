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

// For misc cmod stuff, included at the end of qcommon.h

#ifdef CMOD_MAP_BRIGHTNESS_AUTO_ADJUST
void cmod_auto_brightness_configure(const char *mapname);
#endif

#ifdef CMOD_CROSSHAIR
// Client hooks
void crosshair_ui_init(int vid_width, int vid_height);
void crosshair_vm_call(void);
qboolean crosshair_stretchpic(float x, float y, float w, float h, float s1, float t1,
		float s2, float t2, qhandle_t hShader);
qboolean crosshair_cvar_setvalue(const char *cvar_name, float value);
void crosshair_vm_registering_shader(const char *name, qhandle_t result);

// Cvar hooks
qboolean crosshair_cvar_update(const char *cvar_name, vmCvar_t *vm_cvar);

// Filesystem hooks
fsc_file_t *crosshair_process_lookup(const char *name);
#endif

#ifdef CMOD_VM_STRNCPY_FIX
void vm_strncpy(char *dst, char *src, int length);
#endif
