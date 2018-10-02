/* ******************************************************************************** */
// Core Features
/* ******************************************************************************** */

// Core EF Defines
#define ELITEFORCE

// Core cMod defines
#define CMOD_COMMON
#define CMOD_CVAR_DEFS

// New filesystem
#define NEW_FILESYSTEM

// Mp3 Support
#define USE_CODEC_MP3

// New command handling
#define CMOD_COMMAND_INTERPRETER

// New cvar handling; requires NEW_FILESYSTEM and CMOD_COMMAND_INTERPRETER.
#define CMOD_CVAR_HANDLING

// New settings file system; requires NEW_FILESYSTEM, CMOD_COMMAND_INTERPRETER, and CMOD_CVAR_HANDLING.
#define CMOD_SETTINGS

#define CMOD_RESTRICT_CFG_FILES

// Disable auth stuff like in standalone mode, since EF auth server doesn't restrict anything anyway
// and sometimes goes down causing issues
#define CMOD_DISABLE_AUTH_STUFF

// Requires NEW_FILESYSTEM and CMOD_CVAR_HANDLING
#ifndef DEDICATED
	#define CMOD_CROSSHAIR
#endif

/* ******************************************************************************** */
// Graphics
/* ******************************************************************************** */

#define CMOD_BRIGHTNESS_SHIFT
#define CMOD_FULLSCREEN
#define CMOD_IGNORE_RESIZE_MESSAGES		// Avoids issues with CMOD_FULLSCREEN on some platforms/situations
#define CMOD_OVERBRIGHT
#define CMOD_FRAMEBUFFER	// Requires CMOD_OVERBRIGHT
#define CMOD_LOADINGSCREEN_FIX
#define CMOD_LIGHTMAPSTAGE_FIX
#define CMOD_GLIMP_SHUTDOWN_FIX
#define CMOD_FONT_SCALING
#define CMOD_CHAT_FIELD_SIZE
#define CMOD_FOGFACTOR_FIX

/* ******************************************************************************** */
// Version Identification
/* ******************************************************************************** */

#define CMOD_VERSION_STRING
#define CMOD_CONSOLE_COLOR
#define CMOD_USERINFO

/* ******************************************************************************** */
// Server
/* ******************************************************************************** */

#define CMOD_GETSTATUS_FIXES
#define CMOD_BOT_TWEAKS
#define CMOD_DL_PROTOCOL_FIXES
#define CMOD_PROTOCOL_MSG_FIX
#define CMOD_RECORD
#define CMOD_NO_ENGINE_NEXTMAP_SET

// Allow the "map_script" cvar to customize behavior when map command is called
#define CMOD_MAP_SCRIPT

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

#define CMOD_VMFLOATCAST
#define CMOD_REDUCE_WARNINGS
#define CMOD_EVENT_QUEUE_SIZE
#define CMOD_VM_STRNCPY_FIX
#define CMOD_CM_ALIGN_FIX
#define CMOD_DL_LASTMSG_FIX
#define CMOD_CONSOLE_KEY_FIXES
