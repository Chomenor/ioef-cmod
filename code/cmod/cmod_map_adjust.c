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

#ifdef CMOD_MAP_AUTO_ADJUST
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/* ******************************************************************************** */
// Shift handling
/* ******************************************************************************** */

typedef struct {
	float map_lighting_factor;
	float map_lighting_gamma;
	int envMapMode;
	float overbright_factor_max;
	float overbright_factor_shift;
} shift_set_t;

static void apply_shift_set(shift_set_t *shift_set) {
	if(shift_set->map_lighting_factor != 1.0f) {
		Com_Printf("Setting r_autoMapLightingFactor %g\n", shift_set->map_lighting_factor);
		Cvar_Set("r_autoMapLightingFactor", va("%g", shift_set->map_lighting_factor)); }
	if(shift_set->map_lighting_gamma != 0.0f) {
		Com_Printf("Setting r_autoMapLightingGammaMod %g\n", shift_set->map_lighting_gamma);
		Cvar_Set("r_autoMapLightingGammaMod", va("%g", shift_set->map_lighting_gamma)); }
	if(shift_set->envMapMode != 0) {
		Com_Printf("Setting r_autoEnvMapMode %i\n", shift_set->envMapMode);
		Cvar_Set("r_autoEnvMapMode", va("%i", shift_set->envMapMode)); }
	if(shift_set->overbright_factor_max != 0.0f) {
		Com_Printf("Setting r_autoOverBrightFactorMax %g\n", shift_set->overbright_factor_max);
		Cvar_Set("r_autoOverBrightFactorMax", va("%g", shift_set->overbright_factor_max)); }
	if(shift_set->overbright_factor_shift != 0.0f) {
		Com_Printf("Setting r_autoOverBrightFactorShift %g\n", shift_set->overbright_factor_shift);
		Cvar_Set("r_autoOverBrightFactorShift", va("%g", shift_set->overbright_factor_shift)); }
}

/* ******************************************************************************** */
// Hash checks
/* ******************************************************************************** */

#define URBAN_TERROR_STANDARD {0.5f, 0.0f, 1}
#define QUAKE3_STANDARD {2.0f, 0.0f, 1}

struct {
	int hash;
	shift_set_t shift_set;
} special_shifts[] = {
	{-1864270671, {1.0f, 0.0f, 1}},		// matrix - quake-style environment map
	{429256076, {1.0f, 0.0f, 1}},		// dangercity - quake-style environment map
	{875359710, URBAN_TERROR_STANDARD},		// pokernight - urban terror lighting
	{1006385614, {0.6f, 0.0f, 1}},			// 1upxmas - urban terror lighting
	{-443776329, URBAN_TERROR_STANDARD},	// crazychristmas - urban terror lighting
	{-768581189, URBAN_TERROR_STANDARD},	// ut4_terrorism4 - urban terror lighting
	{-1359736521, URBAN_TERROR_STANDARD},	// ef_turnpike - urban terror lighting
	{1038626548, {0.5f, 0.0f, 0}},		// ctf_becks - darken
	{2006033781, {0.5f, 0.0f, 0}},		// chickens - darken
	{1948057473, {1.0f, 0.0f, 0, 1.4f}},	// longgone - darken
	{-1571214409, {0.7f, 0.0f, 0}},		// otc - darken
	{-101413010, {1.0f, 0.0f, 0, 1.5f}},	// bod_lunchroom - darken
	{-1316605387, {1.0f, 0.0f, 0, 1.5f}},	// whitemeat - darken
	{138603980, {1.0f, 0.0f, 0, 1.5f}},		// ctf_crossroads_z - darken
	{-825917568, {1.0f, 0.0f, 0, 1.5f}},	// pinballarena2 - darken
	{1034758439, {1.0f, 0.0f, 0, 1.4f}},	// pinballarena_ii - darken
	{-338180026, {1.0f, 0.0f, 0, 1.5f}},	// ctf_akilo - darken
	{1678180441, {1.0f, 0.0f, 0, 1.5f}},	// ctf_akilo_f4g - darken
	{-389292666, {1.0f, 0.0f, 0, 1.5f}},	// ctf_akilo2 - darken
	{-1510930769, {1.0f, 0.0f, 0, 1.5f}},	// ctf_akilo3 - darken
	{-790481733, {1.0f, 0.0f, 0, 1.5f}},	// pro_akilo2 - darken
	{1262130506, {1.0f, 0.0f, 0, 1.5f}},	// pro_akilo3 - darken
	{519839263, {1.0f, 0.0f, 0, 1.0f}},		// leafland (ef version) - darken
	{-658192787, {1.0f, 0.0f, 0, 1.0f}},	// skunkysbitch - darken
	{723156790, {1.0f, 0.0f, 0, 1.0f}},		// danger_christmas - darken
	{1599589538, {1.0f, 0.0f, 0, 1.0f}},	// snowcity - darken
	{1736560496, {1.0f, 0.0f, 0, 1.1f}},	// ctf_gen_xmas - darken
	{1701618430, {1.0f, 0.0f, 0, 1.0f}},	// dm_ic - darken
	{1818880400, {1.0f, 0.0f, 0, 1.0f}},	// ctf_ic - darken
	{2108385997, {1.0f, 0.0f, 1, 1.3f}},	// ef_abbey2 - darken
	{-1695979, {1.0f, 0.0f, 0, 1.2f}},		// ef_algiers - darken
	{-424018281, {1.0f, 0.0f, 0, 1.2f}},	// ef_algiersroofs - darken
	{-2096164947, {1.0f, 0.0f, 1, 1.3f}},	// ef_kingdom - darken
	{1671051894, {1.0f, 0.0f, 1, 1.3f}},	// rtcw_ice - darken
	{-162049488, {2.0f, 0.0f, 1, 1.0f}},	// perramses - darken
	{-1026364727, {1.0f, 0.2f, 0, 1.2f}},	// sd6 - adjust
	{-1374186326, {2.0f, 0.1f, 1}},		// ut_subway - brighten
	{610817057, {1.0f, 0.2f, 0}},		// ctf_twilight - brighten
	{-4369078, {1.0f, 0.2f, 0}},		// amenhotep - brighten
	{-301759510, {1.0f, 0.3f, 0}},		// anubis - brighten
	{1831086714, {1.0f, 0.2f, 0}},		// heretic - brighten
	{1535467701, {2.0f, 0.1f, 1}},		// summer - brighten
	{-169342235, {2.0f, 0.5f, 1}},		// winter - brighten
	{-834364908, {2.0f, 0.5f, 1}},		// ethora - brighten
	{-1862613250, {2.0f, 0.5f, 1}},		// goththang - brighten
	{-383639493, {1.0f, 0.4f, 0}},		// helmsdeep - brighten
	{-1201980974, {2.5f, 0.5f, 0}},		// ctf_kln4 - brighten
	{-993374657, {2.0f, 0.0f, 0}},		// ctf_finalhour - brighten
	{-1935206618, {2.0f, 0.0f, 0}},		// ctf_rg2_e - brighten
	{-485373179, {2.0f, 0.0f, 0}},		// ctf_rg2_h - brighten
	{-1267516348, QUAKE3_STANDARD},		// leaks2 (ef version) - brighten
};

static qboolean check_brightshift_hash(int hash) {
	// Returns qtrue if settings were applied, qfalse otherwise
	int i;
	//Com_Printf("Have hash: %i\n", hash);

	for(i=0; i<ARRAY_LEN(special_shifts); ++i) {
		if(special_shifts[i].hash == hash) {
			apply_shift_set(&special_shifts[i].shift_set);
			return qtrue; } }

	return qfalse; }

/* ******************************************************************************** */
// Quake 3 Entity checks
/* ******************************************************************************** */

static qboolean check_quake3_entities(char *entities) {
	// Returns qtrue if settings were applied, qfalse otherwise
	int i;
	char *token;
	int entity_hits = 0;
	struct {
		char *name;
		int count;
	} quake_entities[] = {
		{"item_health_small", 0},
		{"item_health", 0},
		{"item_health_large", 0},
		{"item_health_mega", 0},
		{"weapon_shotgun", 0},
		{"weapon_rocketlauncher", 0},
		{"weapon_lightning", 0},
		{"weapon_plasmagun", 0},
		{"weapon_bfg", 0},
		{"weapon_nailgun", 0},
		{"weapon_prox_launcher", 0},
		{"weapon_chaingun", 0},
		{"ammo_shells", 0},
		{"ammo_bullets", 0},
		{"ammo_rockets", 0},
		{"ammo_lightning", 0},
		{"ammo_slugs", 0},
		{"ammo_cells", 0},
		{"ammo_bfg", 0},
		{"ammo_nails", 0},
		{"ammo_mines", 0},
		{"ammo_belt", 0},
	};

	while(1) {
		token = COM_ParseExt(&entities, qtrue);
		if(!*token || *token != '{') break;

		while(1) {
			token = COM_ParseExt(&entities, qtrue);
			if(!*token || *token == '}') break;

			if(!Q_stricmp(token, "classname")) {
				token = COM_ParseExt(&entities, qtrue);
				//Com_Printf("Have classname: %s\n", token);
				for(i=0; i<ARRAY_LEN(quake_entities); ++i) {
					if(!Q_stricmp(token, quake_entities[i].name)) {
						++quake_entities[i].count;
						break; } } }
			else token = COM_ParseExt(&entities, qtrue); } }

	for(i=0; i<ARRAY_LEN(quake_entities); ++i) {
		if(quake_entities[i].count) ++entity_hits; }
	//Com_Printf("quake entity hits: %i\n", entity_hits);

	if(entity_hits >= 3) {
		apply_shift_set(&(shift_set_t)QUAKE3_STANDARD);
		return qtrue; }
	return qfalse; }

/* ******************************************************************************** */
// Process function
/* ******************************************************************************** */

static void process_bsp_data(char *data, int length) {
	dheader_t *header = (dheader_t *)data;
	lump_t *entity_lump = header->lumps + LUMP_ENTITIES;
	int entity_offset = LittleLong(entity_lump->fileofs);
	int entity_length = LittleLong(entity_lump->filelen);
	int hash = LittleLong (Com_BlockChecksum ((void *)data, length));
	char *entities;

	if(check_brightshift_hash(hash)) return;

	if(entity_offset < 0 || entity_offset > length) return;
	if(entity_offset + entity_length < 0 || entity_offset + entity_length > length) return;

	entities = malloc(entity_length + 1);
	memcpy(entities, data + entity_offset, entity_length);
	entities[entity_length] = 0;
	check_quake3_entities(entities);
	free(entities);

	//Com_Printf("Have brightshift data: length(%i) hash(%i) entity_offset(%i) entity_length(%i)\n",
	//		length, hash, entity_offset, entity_length);
}

void cmod_map_adjust_configure(const char *mapname) {
	cvar_t *cmod_map_adjust_enabled = Cvar_Get("cmod_map_adjust_enabled", "1", CVAR_ARCHIVE|CVAR_LATCH);
	Cvar_Get("r_autoMapLightingFactor", "", CVAR_ROM);
	Cvar_Set("r_autoMapLightingFactor", "");
	Cvar_Get("r_autoMapLightingGammaMod", "", CVAR_ROM);
	Cvar_Set("r_autoMapLightingGammaMod", "");
	Cvar_Get("r_autoEnvMapMode", "0", CVAR_ROM);
	Cvar_Set("r_autoEnvMapMode", "0");
	Cvar_Get("r_autoOverBrightFactorMax", "", CVAR_ROM);
	Cvar_Set("r_autoOverBrightFactorMax", "");
	Cvar_Get("r_autoOverBrightFactorShift", "", CVAR_ROM);
	Cvar_Set("r_autoOverBrightFactorShift", "");

	if(cmod_map_adjust_enabled->integer && mapname && *mapname) {
		char *data = 0;
		int length = FS_ReadFile(va("maps/%s.bsp", mapname), (void **)&data);
		if(data) {
			if(length >= sizeof(dheader_t)) process_bsp_data(data, length);
			FS_FreeFile((void *)data); } } }
#endif
