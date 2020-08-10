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

#ifdef CMOD_SETTINGS
#ifndef DEDICATED
CVAR_DEF(cmod_restrict_autoexec, "1", CVAR_ARCHIVE)
#endif
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

#ifdef CMOD_LOGGING
CVAR_DEF(cmod_log_flush, "1", 0)
#endif

#ifdef CMOD_MIN_SNAPS
CVAR_DEF(sv_min_snaps, "50", CVAR_ARCHIVE);
#endif

#ifdef CMOD_MAP_SCRIPT
CVAR_DEF(cmod_sv_mapscript_script, "", 0)
CVAR_DEF(cmod_sv_mapscript_bsp_check, "1", 0)
CVAR_DEF(cmod_sv_mapscript_mapcmd, "", CVAR_ROM)
CVAR_DEF(cmod_sv_mapscript_mapname, "", CVAR_ROM)
#endif

#ifdef CMOD_VOTING
CVAR_DEF(cmod_sv_voting_enabled, "0", 0)
CVAR_DEF(cmod_sv_voting_debug, "0", 0)
CVAR_DEF(cmod_sv_voting_duration, "20", 0)
CVAR_DEF(cmod_sv_voting_mode, "0", 0)		// 0 = normal, 1 = default no
CVAR_DEF(cmod_sv_voting_max_voters_per_ip, "1", 0)
CVAR_DEF(cmod_sv_voting_option_list, "", 0)
CVAR_DEF(cmod_sv_voting_preoption_script, "", 0)
CVAR_DEF(cmod_sv_voting_postoption_script, "", 0)
#endif

#ifdef CMOD_MAPTABLE
CVAR_DEF(cmod_sv_maptable_source_dirs, "maps", 0)
CVAR_DEF(cmod_sv_maptable_entry_count, "-1", CVAR_ROM);
CVAR_DEF(cmod_sv_maptable_loaded, "false", CVAR_ROM);
#endif

#ifdef CMOD_MAP_SOURCE_OVERRIDE
CVAR_DEF(cmod_sv_override_ent_file, "", 0)
CVAR_DEF(cmod_sv_override_aas_file, "", 0)
CVAR_DEF(cmod_sv_override_bsp_file, "", 0)
#endif

#ifdef CMOD_SERVER_INFOSTRING_OVERRIDE
CVAR_DEF(cmod_sv_override_client_map, "", 0)
CVAR_DEF(cmod_sv_override_client_mod, "", 0)
#endif

#ifdef CMOD_SERVER_CMD_TRIGGERS
CVAR_DEF(cmod_trigger_debug, "0", 0)
CVAR_DEF(cmod_in_trigger, "0", 0)
#endif
