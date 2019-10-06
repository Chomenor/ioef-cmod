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

#ifdef CMOD_LOGGING
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

time_t current_ctime;
char current_date[15];
char current_time[15];

typedef struct {
	const char *name;
	int date_mode;	// 0 = none, 1 = date and time in file, 2 = date in filename and time in file
	cvar_t *active_cvar;

	fileHandle_t handle;
	qboolean log_error;
	char current_date[15];
} log_t;

#define CMLogEntry(id, name, date_mode) {name, date_mode, 0, 0, qfalse, ""},
log_t logs[] = {
	CMLogList
};
#undef CMLogEntry

#ifdef SYSTEM_NEWLINE
#define LOG_NEWLINE SYSTEM_NEWLINE
#else
#define LOG_NEWLINE "\n"
#endif

/* ******************************************************************************** */
// Support Functions
/* ******************************************************************************** */

static void update_current_time(void) {
	// Updates current_ctime, current_date, and current_time if necessary
	static int last_frametime;
	struct tm *timeinfo;

	// Abort if frametime hasn't changed
	if(com_frameTime == last_frametime) return;
	last_frametime = com_frameTime;

	// Abort if current_ctime hasn't changed
	{	time_t last_ctime = current_ctime;
		time(&current_ctime);
		if(current_ctime == last_ctime) return; }

	timeinfo = localtime(&current_ctime);

	// Get current_date
	Com_sprintf(current_date, sizeof(current_date), "%d-%02d-%02d",
			timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);

	// Get current_time
	Com_sprintf(current_time, sizeof(current_time), "%02d:%02d:%02d",
			timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec); }

static fileHandle_t get_log_handle(log_t *log) {
	char path[64];
	if(log->date_mode == 2) Com_sprintf(path, sizeof(path), "logs/%s/%s.txt", log->name, current_date);
	else Com_sprintf(path, sizeof(path), "logs/%s.txt", log->name);
	return FS_SV_FOpenFileAppend(path); }

static void update_log_handle(log_t *log) {
	// Updates the log handle if necessary
	// Handle may still be null in case of error
	if(log->log_error) return;
	if(log->handle) {
		if(log->date_mode == 2 && strcmp(log->current_date, current_date)) FS_FCloseFile(log->handle);
		else return; }
	log->handle = get_log_handle(log);
	if(!log->handle) {
		Com_Printf("Failed to open handle for logfile %s\n", log->name);
		log->log_error = qtrue; } }

/* ******************************************************************************** */
// Text conversion functions
/* ******************************************************************************** */

static const char *cmod_log_valid_char_table(void) {
	// Returns map of characters allowed in log output
	// Other chars will be converted to hex value
	static char table[256];
	static qboolean have_table = qfalse;
	if(!have_table) {
		int i;
		char valid_chars[] = " ~`!@#$%^&*_-+=;:'\",.?/()[]{}<>|";
		Com_Memset(table, 0, sizeof(table));
		for(i='a'; i<='z'; ++i) table[i] = 1;
		for(i='A'; i<='Z'; ++i) table[i] = 1;
		for(i='0'; i<='9'; ++i) table[i] = 1;
		for(i=0; i<sizeof(valid_chars)-1; ++i) table[((unsigned char *)valid_chars)[i]] = 1;
		have_table = qtrue; }
	return table; }

static void cmod_log_character_convert(const char *source, cmod_stream_t *stream) {
	const char *table = cmod_log_valid_char_table();

	while(*source && stream->position < stream->size) {
		unsigned char in = *(unsigned char *)(source++);
		if(table[in]) {
			stream->data[stream->position++] = (char)in; }
		else if(in == '\n') {
			cmod_stream_append_string(stream, "\\n"); }
		else if(in == '\\') {
			cmod_stream_append_string(stream, "\\\\"); }
		else {
			char buffer[16];
			Com_sprintf(buffer, sizeof(buffer), "\\%02x", in);
			cmod_stream_append_string(stream, buffer); } }

	// Ensure null termination
	cmod_stream_append_string(stream, ""); }

/* ******************************************************************************** */
// Logging Functions
/* ******************************************************************************** */

void cmod_logging_initialize(void) {
	int i;
	for(i=0; i<LOG_COUNT; ++i) {
		logs[i].active_cvar = Cvar_Get(va("cmod_log_%s", logs[i].name), "0", 0); } }

void cmod_logging_frame(void) {
	if(cmod_log_flush && cmod_log_flush->integer) {
		// Flush logs at end of every frame
		int i;
		for(i=0; i<LOG_COUNT; ++i) {
			if(logs[i].handle) FS_Flush(logs[i].handle); } } }

void QDECL cmLog(cmod_log_id_t log_id, int flags, const char *fmt, ...) {
	if(log_id < 0 || log_id >= LOG_COUNT) Com_Error(ERR_FATAL, "cmLog with invalid log id");
	if(!logs[log_id].active_cvar || !logs[log_id].active_cvar->integer) return;

	// Get message
	va_list argptr;
	char msg[MAXPRINTMSG];
	va_start(argptr,fmt);
	Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	// Character convert
	char buffer[MAXPRINTMSG];
	cmod_stream_t stream = {buffer, 0, sizeof(buffer), qfalse};
	cmod_log_character_convert(msg, &stream);

	if(flags & LOGFLAG_COM_PRINTF) {
		if(flags & LOGFLAG_RAW_STRING) Com_Printf("%s", msg);
		else Com_Printf("%s\n", buffer); }

	// Update log state
	update_current_time();
	update_log_handle(&logs[log_id]);
	if(!logs[log_id].handle) return;

	// Write the string
	if(flags & LOGFLAG_RAW_STRING) {
		FS_Printf(logs[log_id].handle, "%s", msg); }
	else {
		if(logs[log_id].date_mode == 1) FS_Printf(logs[log_id].handle, "%s %s ~ %s" LOG_NEWLINE,
				current_date, current_time, buffer);
		else if(logs[log_id].date_mode == 2) FS_Printf(logs[log_id].handle, "%s ~ %s" LOG_NEWLINE,
				current_time, buffer);
		else FS_Printf(logs[log_id].handle, "%s" LOG_NEWLINE, buffer); }

	if(flags & LOGFLAG_FLUSH) {
		FS_Flush(logs[log_id].handle); } }

#endif
