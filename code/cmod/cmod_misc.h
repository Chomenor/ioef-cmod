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

#ifdef CMOD_COMMON_STRING_FUNCTIONS
typedef struct {
	char *data;
	unsigned int position;
	unsigned int size;
	qboolean overflowed;
} cmod_stream_t;
void cmod_stream_append_string(cmod_stream_t *stream, const char *string);
void cmod_stream_append_string_separated(cmod_stream_t *stream, const char *string, const char *separator);
unsigned int cmod_read_token(const char **current, char *buffer, unsigned int buffer_size, char delimiter);
unsigned int cmod_read_token_ws(const char **current, char *buffer, unsigned int buffer_size);
#endif

#ifdef CMOD_LOGGING
// id, name, date mode
#define CMLogList \
	CMLogEntry(LOG_SERVER, "server", 1) \
	CMLogEntry(LOG_RECORD, "record", 1) \
	CMLogEntry(LOG_VOTING, "voting", 1)

#define CMLogEntry(id, name, date_mode) id,
typedef enum {
	CMLogList
	LOG_COUNT
} cmod_log_id_t;
#undef CMLogEntry

// Flags
#define LOGFLAG_COM_PRINTF 1
#define LOGFLAG_FLUSH 2
#define LOGFLAG_RAW_STRING 4

void cmod_logging_initialize(void);
void cmod_logging_frame(void);
void QDECL cmLog(cmod_log_id_t log_id, int flags, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
#endif

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
