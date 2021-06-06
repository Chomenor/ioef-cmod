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
	float map_lighting_min_clamp;
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
	if(shift_set->map_lighting_min_clamp != 0.0f) {
		Com_Printf("Setting r_autoMapLightingClampMin %g\n", shift_set->map_lighting_min_clamp);
		Cvar_Set("r_autoMapLightingClampMin", va("%g", shift_set->map_lighting_min_clamp)); } }

/* ******************************************************************************** */
// Hash checks
/* ******************************************************************************** */

struct {
	int hash;
	shift_set_t shift_set;
} special_shifts[] = {
	{-1864270671, {1.0f, 0.0f, 1}},		// matrix - quake-style environment map
	{429256076, {1.0f, 0.0f, 1}},		// dangercity - quake-style environment map
	{875359710, {0.5f, 0.0f, 1}},		// pokernight - urban terror lighting
	{1006385614, {0.6f, 0.0f, 1}},		// 1upxmas - urban terror lighting
	{-443776329, {0.5f, 0.0f, 1}},		// crazychristmas - urban terror lighting
	{-768581189, {0.5f, 0.0f, 1}},		// ut4_terrorism4 - urban terror lighting
	{-1359736521, {0.5f, 0.0f, 1}},		// ef_turnpike - urban terror lighting
	{1038626548, {0.5f, 0.0f, 0}},		// ctf_becks - unbrighten
	{2006033781, {0.5f, 0.0f, 0}},		// chickens - unbrighten
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
		apply_shift_set(&(shift_set_t){2.0f, 0.0f, 1});
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
	Cvar_Get("r_autoMapLightingClampMin", "", CVAR_ROM);
	Cvar_Set("r_autoMapLightingClampMin", "");

	if(cmod_map_adjust_enabled->integer && mapname && *mapname) {
		char *data = 0;
		int length = FS_ReadFile(va("maps/%s.bsp", mapname), (void **)&data);
		if(data) {
			if(length >= sizeof(dheader_t)) process_bsp_data(data, length);
			FS_FreeFile((void *)data); } } }
#endif
