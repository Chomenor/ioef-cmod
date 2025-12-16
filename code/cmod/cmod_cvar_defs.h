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

#ifdef CMOD_SAFE_AUTOEXEC
CVAR_DEF(cmod_restrict_autoexec, "1", CVAR_ARCHIVE)
#endif

#ifdef CMOD_FONT_SCALING
CVAR_DEF(cmod_font_scaling, "0.5", CVAR_ARCHIVE)
#endif

#ifdef CMOD_CONSOLE_KEY_FIXES
CVAR_DEF(cmod_console_mode, "1", CVAR_ARCHIVE)
#endif

#ifdef CMOD_MOUSE_WARPING_OPTION
CVAR_DEF(in_mouse_warping, "0", CVAR_ARCHIVE);
#endif

#ifdef CMOD_ANTI_BURNIN
CVAR_DEF(cmod_anti_burnin, "0", CVAR_ARCHIVE);
#endif

#ifdef CMOD_FILTER_OVERLAPPING_SOUNDS
// 0 = no filtering (original ioEF behavior)
// 1 = filter weapon channel only (default)
// 2 = filter all non-auto channels (original EF behavior)
CVAR_DEF(s_noOverlap, "1", CVAR_ARCHIVE);
#endif

#ifdef CMOD_LOGGING_SYSTEM
CVAR_DEF(cmod_log_flush, "1", 0)
#endif

#ifdef CMOD_MIN_SNAPS
CVAR_DEF(sv_minSnaps, "50", CVAR_ARCHIVE);
#endif

#ifdef CMOD_MAP_SCRIPT
CVAR_DEF(sv_mapscript, "", 0)
CVAR_DEF(sv_mapscript_bsp_check, "1", 0)
CVAR_DEF(sv_mapscript_mapcmd, "", CVAR_ROM)
CVAR_DEF(sv_mapscript_mapname, "", CVAR_ROM)
#endif

#ifdef CMOD_MAPTABLE
CVAR_DEF(sv_maptable_source_dirs, "maps", 0)
CVAR_DEF(sv_maptable_entry_count, "-1", CVAR_ROM)
CVAR_DEF(sv_maptable_loaded, "false", CVAR_ROM)
#endif

#ifdef CMOD_MAP_SOURCE_OVERRIDE
CVAR_DEF(sv_override_ent_file, "", 0)
CVAR_DEF(sv_override_aas_file, "", 0)
CVAR_DEF(sv_override_bsp_file, "", 0)
#endif

#ifdef CMOD_SERVER_INFOSTRING_OVERRIDE
CVAR_DEF(sv_override_client_map, "", 0)
CVAR_DEF(sv_override_client_mod, "", 0)
#endif

#ifdef CMOD_SERVER_CMD_TRIGGERS
CVAR_DEF(cmod_trigger_debug, "0", 0)
CVAR_DEF(cmod_in_trigger, "0", 0)
#endif

#ifdef CMOD_SV_PINGFIX
CVAR_DEF(sv_pingFix, "2", 0)
#endif

#ifdef CMOD_ENGINE_ASPECT_CORRECT
CVAR_DEF(cl_engineAspectCorrect, "1", CVAR_ARCHIVE)	// 1 = enable for known compatible mods, 2 = enable always (FOR TESTING ONLY!)
CVAR_DEF(cg_fov, "85*", CVAR_ARCHIVE)
CVAR_DEF(cg_aspectCorrect, "1", CVAR_ARCHIVE)
CVAR_DEF(cg_aspectCorrectGunPos, "-1", 0)
CVAR_DEF(ui_aspectCorrect, "-1", 0)
#endif

#ifdef CMOD_CONSOLE_AUTO_RETURN
CVAR_DEF(con_autoReturn, "1", CVAR_ARCHIVE)
#endif

#ifdef CMOD_MULTI_MASTER_QUERY
// 0 = original master server query mode (select ipv4 or ipv6 but not both),
// 1 = ipv4 only, 2 = ipv6 only, 3 = both ipv4 and ipv6
CVAR_DEF(cl_masterMultiQuery, "3", CVAR_ARCHIVE)
#endif

#ifdef CMOD_UI_MOUSE_SENSITIVITY
// Positive value sets a specific UI mouse sensitivity (5 = original sensitivity).
// Value of -1 (default) uses value of main "sensitivity" cvar if it is below 5, but
// does not go above 5. This way low sensitivity values will lower menu sensitivity,
// but high values don't increase it, to be safe and ensure menu stays navigable.
CVAR_DEF(cl_menuSensitivity, "-1", CVAR_ARCHIVE)
#endif

#ifdef CMOD_MODEL_NAME_LENGTH_LIMIT
// Maximum allowed length of player model string.
CVAR_DEF( sv_maxModelLength, "48", 0 )
#endif

#ifdef CMOD_CONSOLE_KEY_DEBUG
CVAR_DEF( in_keyboardDebug, "0", 0 )
#endif
