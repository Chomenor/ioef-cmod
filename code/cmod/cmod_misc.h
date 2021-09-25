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

#ifdef CMOD_VM_EXTENSIONS
typedef enum {
	VM_GAME,
	VM_CGAME,
	VM_UI,
} vmType_t;

qboolean VMExt_HandleVMSyscall( intptr_t *args, vmType_t vm_type, vm_t *vm,
		void *( *VM_ArgPtr )( intptr_t intValue ), intptr_t *retval );
void VMExt_Init( void );
#endif

#ifdef CMOD_COMMON_STRING_FUNCTIONS
typedef struct {
	char *data;
	unsigned int position;
	unsigned int size;
	qboolean overflowed;
} cmod_stream_t;
void cmod_stream_append_string(cmod_stream_t *stream, const char *string);
void cmod_stream_append_string_separated(cmod_stream_t *stream, const char *string, const char *separator);
void cmod_stream_append_data(cmod_stream_t *stream, const char *data, unsigned int length);
unsigned int cmod_read_token(const char **current, char *buffer, unsigned int buffer_size, char delimiter);
unsigned int cmod_read_token_ws(const char **current, char *buffer, unsigned int buffer_size);
#endif

#ifdef CMOD_LOGGING_SYSTEM
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

#ifdef CMOD_MAP_AUTO_ADJUST
void cmod_map_adjust_configure(const char *mapname);
#endif

#ifdef CMOD_CROSSHAIR
// Client hooks
int CMCrosshair_VMAdvanceCurrentCrosshair( void );
qhandle_t CMCrosshair_GetCurrentShader( void );
void CMCrosshair_UIInit(void);
void CMCrosshair_CGameInit( void );
void CMCrosshair_RegisterVMSupport( vmType_t vm_type );

// Filesystem hooks
fsc_file_t *CMCrosshair_FileLookupHook(const char *name);
#endif

#ifdef CMOD_VM_STRNCPY_FIX
void vm_strncpy(char *dst, char *src, int length);
#endif

#ifdef CMOD_ANTI_BURNIN
float cmod_anti_burnin_shift(float val);
#endif

#ifdef CMOD_COPYDEBUG_CMD
void cmod_copydebug_cmd(void);
void cmod_debug_get_console(cmod_stream_t *stream);
#endif

#ifdef CMOD_ENGINE_ASPECT_CORRECT
void AspectCorrect_OnVmCreate( const char *module, const fsc_file_t *sourceFile );
qboolean AspectCorrect_OnCgameSyscall( intptr_t *args, intptr_t *retval );
qboolean AspectCorrect_OnUISyscall( intptr_t *args, intptr_t *retval );
#endif
