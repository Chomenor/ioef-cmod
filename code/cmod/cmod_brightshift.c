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

#ifdef CMOD_BRIGHTNESS_SHIFT
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/* ******************************************************************************** */
// Shift handling
/* ******************************************************************************** */

typedef struct {
	float map_overbright_target;
	float gamma_shift;
} shift_set_t;

static float shift_value(float base, float current, float target) {
	float current_delta = current - base;
	float target_delta = target - base;

	if(target_delta > 0.0) {
		if(current_delta > target_delta) target_delta = current_delta;
		else if(current_delta < 0.0) {
			target_delta += current_delta; } }
	else {
		if(current_delta < target_delta) target_delta = current_delta;
		else if(current_delta > 0.0) {
			target_delta += current_delta; } }

	return base + target_delta; }

static float default_value(cvar_t *cvar) {
	return atof(cvar->resetString); }

static float current_value(cvar_t *cvar) {
	if(cvar->latchedString) return atof(cvar->latchedString);
	return cvar->value; }

static void apply_shift_set(shift_set_t *shift_set) {
	cvar_t *r_mapOverBrightFactor = Cvar_Get("r_mapOverBrightFactor", "", 0);
	float r_mapOverBrightFactor_shifted = shift_value(default_value(r_mapOverBrightFactor),
			current_value(r_mapOverBrightFactor), shift_set->map_overbright_target);
	char shift_info[128];
	shift_info[0] = 0;

	if(r_mapOverBrightFactor_shifted != current_value(r_mapOverBrightFactor)) {
		Q_strcat(shift_info, sizeof(shift_info), va("%smapOverBrightFactor(%g => %g)",
				shift_info[0] ? " " : "", current_value(r_mapOverBrightFactor), r_mapOverBrightFactor_shifted));
		Cvar_Set("r_mapOverBrightFactorShifted", va("%g", r_mapOverBrightFactor_shifted)); }
	if(shift_set->gamma_shift != 0.0) {
		Q_strcat(shift_info, sizeof(shift_info), va("%sgammaShift(%g)", shift_info[0] ? " " : "",
				shift_set->gamma_shift));
		Cvar_Set("r_gammaShift", va("%g", shift_set->gamma_shift)); }

	if(shift_info[0]) Com_Printf("brightshift: %s\n", shift_info); }

/* ******************************************************************************** */
// Hash checks
/* ******************************************************************************** */

struct {
	int hash;
	shift_set_t shift_set;
} special_shifts[] = {
	{610817057, {2.0f, 0.1f}},		// ctf_twilight
	{-1374186326, {4.0f, 0.1f}},	// ut_subway
	{875359710, {1.0f, 0.0f}},		// pokernight
	{1006385614, {1.2f, 0.0f}},		// 1upxmas
	{-443776329, {1.0f, 0.0f}},		// crazychristmas
	{-768581189, {1.0f, 0.0f}},		// ut4_terrorism4
	{-1359736521, {1.0f, 0.0f}},	// ef_turnpike
	{1038626548, {1.0f, 0.0f}},		// ctf_becks
	{2006033781, {1.0f, 0.0f}},		// chickens
	{-4369078, {1.5f, 0.2f}},		// amenhotep
	{-301759510, {2.0f, 0.2f}},		// anubis
	{1831086714, {2.0f, 0.2f}},		// heretic
	{1535467701, {4.0f, 0.1f}},		// summer
	{-169342235, {4.0f, 0.2f}},		// winter
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
		apply_shift_set(&(shift_set_t){4.0f, 0.0f});
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

void brightshift_configure(const char *mapname) {
	//Com_Printf("Have brightshift config call: mapname(%s)\n", mapname);

	Cvar_Set("r_mapOverBrightFactorShifted", "");
	Cvar_Set("r_gammaShift", "0");

	if(mapname && *mapname) {
		char *data = 0;
		int length = FS_ReadFile(va("maps/%s.bsp", mapname), (void **)&data);
		if(data) {
			if(length >= sizeof(dheader_t)) process_bsp_data(data, length);
			FS_FreeFile((void *)data); } } }
#endif
