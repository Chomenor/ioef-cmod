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

#ifdef CMOD_CROSSHAIR
// This file is used to extend the number of crosshairs selectable in the game
// Currently uses some crazy hacks to override the UI display, to work across different mods
// Future implementations could add new VM calls to allow VMs compiled with crosshair support
//    to access the crosshair features in a more conventional way

#include "../filesystem/fslocal.h"
#include "../client/client.h"

// Crosshair Cvars
cvar_t *cmod_crosshair_enable;
cvar_t *cmod_crosshair_selection;

// Builtin Crosshairs
#define SOURCETYPE_CROSSHAIR 3
void crosshair_builtin_register(void);
qboolean crosshair_builtin_file_enabled(const fsc_file_t *file);

// Crosshair Index
typedef struct {
	fsc_file_t *file;
	unsigned int hash;
	int special_priority;
	qhandle_t handle;	// -1 = invalid, 0 = uninitialized
} crosshair_t;

crosshair_t crosshairs[256];
int crosshair_count;

/* ******************************************************************************** */
// Crosshair Index
/* ******************************************************************************** */

static unsigned int special_crosshairs[] = {
	// Helps sort crosshairs for a logical ordering in menu
	// First entry = first in crosshair menu, and default if crosshair setting is invalid
	0x076b9707,		// pak0 b
	0xd98b9513,		// pak0 c
	0x85585057,		// pak0 d
	0xf7bc361b,		// pak0 e
	0x0a3d6df9,		// pak0 f
	0x49ca88cd,		// pak0 g
	0xca445dd6,		// pak0 h
	0xeae7005d,		// pak0 i
	0xe8806c36,		// pak0 j
	0x0f5dd93d,		// pak0 k
	0x6453cfe4,		// pak0 l
	0xa0affa48,		// marksman b
	0x2d6ede50,		// marksman c
	0xb7bb746b,		// marksman d
	0xbab04a49,		// marksman e
	0xaff1e8a5,		// marksman f
	0x640460be,		// marksman g
	0x9fb736bc,		// marksman h
	0xc39a2a8d,		// marksman i
	0x9346c2db,		// marksman j
	0xc0710dd7,		// marksman k
	0x4bba8170,		// xhairsdsdn c
	0x835f47f2,		// xhairsdsdn d
	0xbdc83459,		// xhairsdsdn e
	0x70cb059a,		// xhairsdsdn g
	0xbddf5ebc,		// xhairsdsdn h
	0xddd628b8,		// xhairsdsdn i
	0xb66df595,		// xhairsdsdn j
	0xa9af4193,		// xhairsdsdn l
	0x78426651,		// xhairs b
	0xca469314,		// xhairs c
	0xc0d1265b,		// xhairs d
	0xa6e1b45a,		// xhairs e
	0x8b535601,		// xhairs f
	0xb87a7b14,		// xhairs g
	0x9f826909,		// xhairs h
	0xb053c705,		// xhairs i
	0x974dde80,		// xhairs j
	0x0d847954,		// pakhairs14 b
	0x44a8302a,		// pakhairs14 c
	0x29634e5c,		// pakhairs14 d
	0xbc044a60,		// pakhairs14 e
	0x235ebfba,		// pakhairs14 f
	0xbc4813c8,		// pakhairs14 g
	0x2a134e63,		// pakhairs14 i
	0xdcd4a326,		// pakhairs14 j
	0x027fb6d0,		// pakhairs16 a
	0x32b41930,		// pakhairs16 b
	0xc25e02d3,		// pakhairs16 c
	0x43258873,		// pakhairs16 d
	0x9a3a5892,		// pakhairs16 e
	0x962400c8,		// pakhairs16 f
	0x324b25ed,		// pakhairs16 g
	0xeda8cb55,		// pakhairs16 h
	0x7039e725,		// pakhairs16 i
	0x21a3c310,		// pakhairs16 j
};

static int get_special_priority_by_hash(unsigned int hash) {
	int i;
	for(i=0; i<ARRAY_LEN(special_crosshairs); ++i) {
		if(special_crosshairs[i] == hash) return ARRAY_LEN(special_crosshairs) - i; }
	return 0; }

static int get_crosshair_index_by_hash(unsigned int hash) {
	// Returns -1 if not found
	int i;
	for(i=0; i<crosshair_count; ++i) {
		if(crosshairs[i].hash == hash) return i; }
	return -1; }

static int compare_crosshair_file(const fsc_file_t *f1, const fsc_file_t *f2) {
	if(f1->sourcetype == SOURCETYPE_CROSSHAIR && f2->sourcetype != SOURCETYPE_CROSSHAIR) return -1;
	if(f2->sourcetype == SOURCETYPE_CROSSHAIR && f1->sourcetype != SOURCETYPE_CROSSHAIR) return 1;
	return fs_compare_file(f1, f2, qtrue); }

static int compare_crosshairs(const void *c1, const void *c2) {
	if(((crosshair_t *)c1)->special_priority > ((crosshair_t *)c2)->special_priority) return -1;
	if(((crosshair_t *)c2)->special_priority > ((crosshair_t *)c1)->special_priority) return 1;
	return compare_crosshair_file(((crosshair_t *)c1)->file, ((crosshair_t *)c2)->file); }

static qboolean fs_is_crosshair_enabled(const fsc_file_t *file) {
	if(file->sourcetype == SOURCETYPE_CROSSHAIR) return crosshair_builtin_file_enabled(file);
	if(!fsc_is_file_enabled(file, &fs)) return qfalse;

	if(fs_connected_server_pure_state() == 1) {
		// Connected to pure server
		if(file->sourcetype == FSC_SOURCETYPE_PK3) {
			unsigned int pk3_hash = ((fsc_file_direct_t *)STACKPTR(((fsc_file_frompk3_t *)file)->source_pk3))->pk3_hash;
			if(pk3_list_lookup(&connected_server_pk3_list, pk3_hash, qfalse)) return qtrue; }
		return qfalse; }

	return qtrue; }

static void build_crosshair_index(void) {
	fsc_hashtable_iterator_t hti;
	fsc_crosshair_t *entry;

	crosshair_count = 0;

	fsc_hashtable_open(&fs.crosshairs, 0, &hti);
	while((entry = STACKPTR(fsc_hashtable_next(&hti)))) {
		fsc_file_t *file = STACKPTR(entry->source_file_ptr);
		int index;
		if(!fs_is_crosshair_enabled(file)) continue;
		index = get_crosshair_index_by_hash(entry->hash);
		if(index == -1) {
			// Create new entry
			if(crosshair_count >= ARRAY_LEN(crosshairs)) return;
			crosshairs[crosshair_count++] = (crosshair_t){file, entry->hash,
					get_special_priority_by_hash(entry->hash), 0}; }
		else {
			// Use higher precedence file
			if(compare_crosshair_file(file, crosshairs[index].file) < 0) crosshairs[index].file = file; } }

	qsort(crosshairs, crosshair_count, sizeof(*crosshairs), compare_crosshairs); }

/* ******************************************************************************** */
// Crosshair Shader Registration
/* ******************************************************************************** */

static crosshair_t *registering_crosshair;

fsc_file_t *crosshair_process_lookup(const char *name) {
	// Returns crosshair file to override normal lookup handling, null otherwise
	if(*name == '#') {
		int i;
		char buffer[] = "#cmod_crosshair_";
		for(i=0; i<sizeof(buffer)-1; ++i) if(buffer[i] != name[i]) return 0;
		return registering_crosshair->file; }
	return 0; }

static qhandle_t get_crosshair_shader(crosshair_t *crosshair) {
	if(!crosshair->handle) {
		if(fs_is_crosshair_enabled(crosshair->file)) {
			char name[32];
			registering_crosshair = crosshair;
			Com_sprintf(name, sizeof(name), "#cmod_crosshair_%08x", crosshair->hash);
			crosshair->handle = re.RegisterShaderNoMip(name); }
		if(!crosshair->handle) crosshair->handle = -1; }
	return crosshair->handle == -1 ? 0 : crosshair->handle; }

/* ******************************************************************************** */
// Current Crosshair Handling
/* ******************************************************************************** */

static int get_current_crosshair_index(void) {
	// Returns -1 for no crosshair, index otherwise
	static int cached_crosshair_index;
	unsigned int hash;

	if(!crosshair_count) return -1;
	if(cmod_crosshair_selection->string[0] == '0' && cmod_crosshair_selection->string[1] == 0) return -1;

	hash = strtoul(cmod_crosshair_selection->string, NULL, 16);
	if(cached_crosshair_index >= crosshair_count || crosshairs[cached_crosshair_index].hash != hash) {
		cached_crosshair_index = get_crosshair_index_by_hash(hash);
		if(cached_crosshair_index < 0) cached_crosshair_index = 0; }

	return cached_crosshair_index; }

static void advance_current_crosshair(void) {
	int index = get_current_crosshair_index() + 1;
	if(index >= crosshair_count) {
		Cvar_Set("cmod_crosshair_selection", "0"); }
	else {
		Cvar_Set("cmod_crosshair_selection", va("%08x", crosshairs[index].hash)); } }

static crosshair_t *get_current_crosshair(void) {
	// Returns null for no crosshair, crosshair otherwise
	int index = get_current_crosshair_index();
	if(index < 0) return 0;
	return &crosshairs[index]; }

/* ******************************************************************************** */
// VM Override Handling
/* ******************************************************************************** */

qhandle_t vm_crosshairs[12];
int vm_crosshairs_registered;

qhandle_t font_shader;
float ch_scale_x;
float ch_scale_y;
int settings_menu_phase;

// Don't enable the crosshair override until the standard set of 12 crosshairs has been registered
// by at least one VM. This helps protect a bit against incompatible VMs with modified crosshair handling.

#define VM_OVERRIDE_ACTIVE (cmod_crosshair_enable->integer && font_shader > 0 && vm_crosshairs_registered == 4095)

static void crosshair_general_init(void);

void crosshair_ui_init(int vid_width, int vid_height) {
	static qboolean general_init_complete = qfalse;
	if(!general_init_complete) {
		crosshair_general_init();
		general_init_complete = qtrue; }

	build_crosshair_index();
	vm_crosshairs_registered = 0;
	font_shader = re.RegisterShaderNoMip("gfx/2d/chars_medium");

	ch_scale_x = vid_width * (1.0/640.0);
	ch_scale_y = vid_height * (1.0/480.0); }

void crosshair_vm_registering_shader(const char *name, qhandle_t result) {
	// Intercept the crosshair registration from either CG_RegisterGraphics or UI_GameOptionsMenu_Cache
	int i;
	char buffer[] = "gfx/2d/crosshair";
	int index;
	for(i=0; i<sizeof(buffer)-1; ++i) if(buffer[i] != name[i]) return;
	index = name[sizeof(buffer)-1] - 'a';
	if(index < 0 || index >= 12 || name[sizeof(buffer)]) {
		vm_crosshairs_registered = -1;
		return; }

	vm_crosshairs[index] = result;
	vm_crosshairs_registered |= (1 << index); }

void crosshair_vm_call(void) {
	// Just hook all VM calls so we don't have to worry about which ones are draw operations
	settings_menu_phase = 0; }

static qboolean coord_match(float c1, float c2) {
	float delta = c1 - c2;
	if(delta > -0.1 && delta < 0.1) return qtrue;
	return qfalse; }

#define CHECK_COORDS(xt, yt, wt, ht) (coord_match(xs, xt) && coord_match(ys, yt) \
	&& coord_match(ws, wt) && coord_match(hs, ht))

#define DSP(x, y, w, h, s1, t1, s2, t2, hShader) re.DrawStretchPic((float)(x) * ch_scale_x, \
	(float)(y) * ch_scale_y, (float)(w) * ch_scale_x, (float)(h) * ch_scale_y, s1, t1, s2, t2, hShader)

qboolean crosshair_stretchpic(float x, float y, float w, float h, float s1, float t1,
		float s2, float t2, qhandle_t hShader) {
	// Returns qtrue to suppress normal handling of command, qfalse otherwise
	if(VM_OVERRIDE_ACTIVE) {
		int i;
		float xs = x / ch_scale_x;
		float ys = y / ch_scale_y;
		float ws = w / ch_scale_x;
		float hs = h / ch_scale_y;
		qboolean vm_crosshair_match = qfalse;
		if(hShader > 0) for(i=0; i<ARRAY_LEN(vm_crosshairs); ++i) {
			if(vm_crosshairs[i] == hShader) vm_crosshair_match = qtrue; }

		/*
		if(Cvar_VariableIntegerValue("stretchpic_print")) {
			Com_Printf("stretchpic: x(%f) y(%f) w(%f)"
				" h(%f) s1(%f) t1(%f) s2(%f) t2(%f) hShader(%i)\n", x, y, w, h, s1, t1, s2, t2, hShader);
			Com_Printf("DSP(%g, %g, %g, %g, %g, %g, %g, %g, font_shader);\n", x, y, w, h, s1, t1, s2, t2); }
			*/

		if(settings_menu_phase == 0) {
			if(CHECK_COORDS(387, 245, 8, 87)) ++settings_menu_phase; }
		else if(settings_menu_phase == 1) {
			if(CHECK_COORDS(513, 245, 8, 87)) ++settings_menu_phase; }
		else if(settings_menu_phase == 2) {
			if(CHECK_COORDS(395, 335, 116, 3)) ++settings_menu_phase; }
		else if(settings_menu_phase == 3) {
			if(CHECK_COORDS(387, 332, 16, 16)) ++settings_menu_phase; }
		else if(settings_menu_phase == 4) {
			if(CHECK_COORDS(510, 332, 16, 16)) ++settings_menu_phase; }
		else if(settings_menu_phase == 5) {
			if(CHECK_COORDS(387, 224, 16, 32)) ++settings_menu_phase; }
		else if(settings_menu_phase == 6) {
			if(CHECK_COORDS(507, 224, 16, 32)) ++settings_menu_phase; }
		else if(settings_menu_phase == 7) {
			crosshair_t *current = get_current_crosshair();
			if(current) {
				// Draw crosshair
				re.SetColor((float []){1, 1, 0, 1});
				DSP(438, 270, 32, 32, 0, 0, 1, 1, get_crosshair_shader(current)); }
			else {
				// Draw "none" message
				char *language = Cvar_VariableString("g_language");
				re.SetColor((float []){0.996, 0.796, 0.398, 1});
				if(!Q_stricmp(language, "deutsch")) {
					DSP(436, 275, 6, 16, 0.539063, 0, 0.5625, 0.0625, font_shader);
					DSP(444, 275, 4, 16, 0.226563, 0, 0.242188, 0.0625, font_shader);
					DSP(450, 275, 2, 16, 0.441406, 0, 0.449219, 0.0625, font_shader);
					DSP(454, 275, 6, 16, 0.71875, 0, 0.742188, 0.0625, font_shader);
					DSP(462, 275, 4, 16, 0.226563, 0, 0.242188, 0.0625, font_shader);
					DSP(468, 275, 5, 16, 0.0078125, 0.0664063, 0.0273438, 0.128906, font_shader); }
				else if(!Q_stricmp(language, "francais")) {
					DSP(437, 275, 5, 16, 0.00390625, 0, 0.0234375, 0.0625, font_shader);
					DSP(444, 275, 5, 16, 0.117188, 0.0664063, 0.136719, 0.128906, font_shader);
					DSP(451, 275, 5, 16, 0.117188, 0, 0.136719, 0.0625, font_shader);
					DSP(458, 275, 5, 16, 0.117188, 0.0664063, 0.136719, 0.128906, font_shader);
					DSP(465, 275, 6, 16, 0.71875, 0, 0.742188, 0.0625, font_shader); }
				else {
					DSP(441, 275, 6, 16, 0.71875, 0, 0.742188, 0.0625, font_shader);
					DSP(449, 275, 5, 16, 0.777344, 0, 0.796875, 0.0625, font_shader);
					DSP(456, 275, 6, 16, 0.71875, 0, 0.742188, 0.0625, font_shader);
					DSP(464, 275, 4, 16, 0.226563, 0, 0.242188, 0.0625, font_shader); } }
			settings_menu_phase = -1;
			return qtrue; }
		else if(settings_menu_phase == -1) {
			if(xs > 395 && xs < 513 && ys > 256 && ys < 335 &&
					(vm_crosshair_match || hShader == font_shader || hShader <= 0)) return qtrue; }

		if(vm_crosshair_match) {
			crosshair_t *current = get_current_crosshair();
			if(current) re.DrawStretchPic(x, y, w, h, s1, t1, s2, t2, get_crosshair_shader(current));
			return qtrue; } }

	return qfalse; }

qboolean crosshair_cvar_update(const char *cvar_name, vmCvar_t *vm_cvar) {
	// Returns qtrue to suppress normal handling of update, qfalse otherwise
	if(VM_OVERRIDE_ACTIVE && !Q_stricmp(cvar_name, "cg_drawcrosshair")) {
		vm_cvar->modificationCount = 0;
		vm_cvar->integer = 1;
		vm_cvar->value = 1;
		Q_strncpyz(vm_cvar->string, "1", sizeof(vm_cvar->string));
		return qtrue; }
	return qfalse; }

qboolean crosshair_cvar_setvalue(const char *cvar_name, float value) {
	// Returns qtrue to suppress normal handling of set, qfalse otherwise
	if(VM_OVERRIDE_ACTIVE && !Q_stricmp(cvar_name, "cg_drawcrosshair")) {
		advance_current_crosshair();
		return qtrue; }
	return qfalse; }

/* ******************************************************************************** */
// General Init / Test Functions
/* ******************************************************************************** */

static void crosshair_debug_index_cmd(void) {
	// Print all index elements
	int i;
	for(i=0; i<crosshair_count; ++i) {
		char buffer[FS_FILE_BUFFER_SIZE];
		fs_file_to_buffer(crosshairs[i].file, buffer, sizeof(buffer), qtrue, qtrue, qtrue, qfalse);
		Com_Printf("********** crosshair index entry **********\nhash: %08x\nfile: %s\nindex: %i\nspecial_priority: %i\n",
			crosshairs[i].hash, buffer, i, crosshairs[i].special_priority); } }

static void crosshair_debug_files_cmd(void) {
	// Print all file elements
	fsc_hashtable_iterator_t hti;
	fsc_crosshair_t *entry;

	fsc_hashtable_open(&fs.crosshairs, 0, &hti);
	while((entry = STACKPTR(fsc_hashtable_next(&hti)))) {
		char buffer[FS_FILE_BUFFER_SIZE];
		fs_file_to_buffer(STACKPTR(entry->source_file_ptr), buffer, sizeof(buffer), qtrue, qtrue, qtrue, qfalse);
		Com_Printf("********** crosshair file **********\nhash: %08x\nfile: %s\n", entry->hash, buffer); } }

static void crosshair_status_cmd(void) {
	Com_Printf("cmod_crosshair_enable: %i\nfont_shader: %i\nvm_crosshairs_registered: %i\n",
			cmod_crosshair_enable->integer, font_shader, vm_crosshairs_registered);
	crosshair_t *current = get_current_crosshair();
	if(current) {
		char buffer[FS_FILE_BUFFER_SIZE];
		fs_file_to_buffer(current->file, buffer, sizeof(buffer), qtrue, qtrue, qtrue, qfalse);
		Com_Printf("current crosshair: %08x - %s\n", current->hash, buffer); }
	else {
		Com_Printf("current crosshair: none\n"); } }

static void crosshair_general_init(void) {
	crosshair_builtin_register();
	cmod_crosshair_enable = Cvar_Get("cmod_crosshair_enable", "0", CVAR_ARCHIVE);
	cmod_crosshair_selection = Cvar_Get("cmod_crosshair_selection", "", CVAR_ARCHIVE);
	Cmd_AddCommand("cmod_crosshair_status", crosshair_status_cmd);
	Cmd_AddCommand("cmod_crosshair_advance", advance_current_crosshair);
	Cmd_AddCommand("cmod_crosshair_debug_index", crosshair_debug_index_cmd);
	Cmd_AddCommand("cmod_crosshair_debug_files", crosshair_debug_files_cmd); }

#endif
