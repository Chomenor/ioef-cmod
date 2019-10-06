/* ******************************************************************************** */
// Common library functions and code structure changes
// This section is intended for components that are necessary for other features to
//   work/compile but do not cause functional changes by themselves
/* ******************************************************************************** */

// [COMMON] Insert additional cMod headers into standard game includes
#define CMOD_COMMON_HEADERS

// [COMMON] Use cmod_cvar_defs.h header for declaring cmod feature cvars
#define CMOD_CVAR_DEFS

// [COMMON] Common string stream and tokenizer functions
#define CMOD_COMMON_STRING_FUNCTIONS

/* ******************************************************************************** */
// Core Features
/* ******************************************************************************** */

// [COMMON] Core EF Defines
#define ELITEFORCE

// [COMMON] Mp3 Support
#define USE_CODEC_MP3

// [FEATURE] New filesystem with various improvements
#define NEW_FILESYSTEM

// [FEATURE] New command system with various improvements
#define CMOD_COMMAND_INTERPRETER

// [FEATURE] New cvar system with various improvements
// Requires NEW_FILESYSTEM and CMOD_COMMAND_INTERPRETER.
#define CMOD_CVAR_HANDLING

// [FEATURE] New settings system with various improvements
// Changes settings location to cmod.cfg instead of original hmconfig.cfg
// and keeps same settings for different mods
// Requires NEW_FILESYSTEM, CMOD_COMMAND_INTERPRETER, and CMOD_CVAR_HANDLING.
#define CMOD_SETTINGS

/* ******************************************************************************** */
// Graphics
/* ******************************************************************************** */

// [FEATURE] Support several new brightness settings (e.g. r_overBrightFactor and r_mapLightingGamma)
#define CMOD_MAP_BRIGHTNESS_SETTINGS

// [FEATURE] Support r_textureGamma setting
#define CMOD_TEXTURE_GAMMA

// [FEATURE] Automatically adjust brightness settings for consistency on certain types of maps
#define CMOD_MAP_BRIGHTNESS_AUTO_ADJUST

// [FEATURE] Support framebuffer-based gamma, which allows r_gamma to work without changing system gamma settings
// Requires CMOD_MAP_BRIGHTNESS_SETTINGS
#define CMOD_FRAMEBUFFER

// [FEATURE] Support separate mode setting in windowed/fullscreen mode (r_mode and r_fullscreenMode)
#define CMOD_FULLSCREEN

// [COMMON] Fixes issues with CMOD_FULLSCREEN
#define CMOD_IGNORE_RESIZE_MESSAGES

// [FEATURE] Support scaling console/chat font with display resolution (controlled by cmod_font_scaling cvar)
#define CMOD_FONT_SCALING

// [FEATURE] Support modifying sky color in fastsky mode via r_fastskyColor cvar
#define CMOD_FASTSKY_COLOR

// [BUGFIX] Fix potential graphics glitch during loading screen
#define CMOD_LOADINGSCREEN_FIX

// [BUGFIX] Fix a shader caching issue that can cause low framerates on certain maps
#define CMOD_LIGHTMAPSTAGE_FIX

// [BUGFIX] Properly unintialize certain handles on renderer shutdown to prevent potential issues
// when using statically compiled renderer
#define CMOD_GLIMP_SHUTDOWN_FIX

// [BUGFIX] Increase max number of characters displayed in chat mode to match original EF default
#define CMOD_CHAT_FIELD_SIZE

// [BUGFIX] Fix potential crash issue
#define CMOD_FOGFACTOR_FIX

// [BUGFIX] Fix potential crash issue on certain maps
#define CMOD_FOGSIDE_FIX

/* ******************************************************************************** */
// Server
/* ******************************************************************************** */

// [FEATURE] Server-side game recording and admin spectator support
#define CMOD_RECORD

// [FEATURE] Allow scripts to override map command behavior via "cmod_sv_map_script" cvar
#define CMOD_MAP_SCRIPT

// [FEATURE] Additional console commands to enhance server script capabilities
#define CMOD_SERVER_CMD_TOOLS

// [FEATURE] Support for engine-based voting system
#define CMOD_VOTING

// [BUGFIX] Various server download support fixes and improvements
#define CMOD_DL_PROTOCOL_FIXES

// [BUGFIX] Various bot-related fixes and improvements
#define CMOD_BOT_TWEAKS

// [BUGFIX] Fixes for several issues that can interfere with the server being listed on
// master servers and/or server browser tools
#define CMOD_GETSTATUS_FIXES

// [BUGFIX] Fix to allow certain older ioEF versions (e.g. 1.37) to successfully negotiate
// protocol version
#define CMOD_PROTOCOL_MSG_FIX

// [BUGFIX] Prevent unnecessary engine modification to "nextmap" cvar, which can lead to breaking
// rotations and causing "stuck" maps
#define CMOD_NO_ENGINE_NEXTMAP_SET

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

// [FEATURE] Reduce unnecessary console log messages in various parts of the game
#define CMOD_REDUCE_WARNINGS

// [FEATURE] Provides cvar option (in_mouse_warping) which attempts to use the same mouse capture
// method as older EF versions
#define CMOD_MOUSE_WARPING_OPTION

// [FEATURE] New crosshair system which is meant to allow selecting any installed crosshair,
// rather than just a small fixed group. Currently rather hacky.
// Requires NEW_FILESYSTEM and CMOD_CVAR_HANDLING
#ifndef DEDICATED
#define CMOD_CROSSHAIR
#endif

// [BUGFIX] Use traditional EF float casting behavior in VM, which fixes the physics behavior
// in various mods
#define CMOD_VMFLOATCAST

// [BUGFIX] Reverse an ioef change which appears to be no longer necessary, and may potentially
// cause issues such as photons disappearing on impact
#define CMOD_NOIMPACT_TRACEFIX

// [BUGFIX] Attempt to prevent Com_QueueEvent errors by increasing queue size
#define CMOD_EVENT_QUEUE_SIZE

// [BUGFIX] Fix strncpy range check issue which caused a problem on mac platform
#define CMOD_VM_STRNCPY_FIX

// [BUGFIX] Workaround for an issue which can cause a crash on certain maps
#define CMOD_CM_ALIGN_FIX

// [BUGFIX] Fix an issue in the client which can cause it to disconnect after completing
// a download in certain situations
#define CMOD_DL_LASTMSG_FIX

// [BUGFIX] Attempt to improve detection of console opening key on non-US keyboard layouts
#define CMOD_CONSOLE_KEY_FIXES

// [BUGFIX] Disable auth system since EF auth server currently doesn't enforce any restrictions
// anyway and often goes down causing issues
#define CMOD_DISABLE_AUTH_STUFF

// [BUGFIX] Extend some ioq3 overflow checks to EF-specific sections of msg.c
#define CMOD_MSG_OVERFLOW_CHECKS

// [COMMON] New logging system
// Currently used for server-side logging, but capable of supporting client-side
// logging as well for debugging purposes
#define CMOD_LOGGING

// [COMMON] Add protections against VMs writing config files for security purposes
#define CMOD_RESTRICT_VM_CFG_WRITE

// [COMMON] Disable Quake 3-specific VM hash code in filesystem
#define CMOD_DISABLE_QUAKE_VM_HASHES

/* ******************************************************************************** */
// Version Identification
/* ******************************************************************************** */

// [COMMON] Add cMod version info to version string
#define CMOD_VERSION_STRING

// [COMMON] Add version string to userinfo and clean up which other values are included
#define CMOD_USERINFO

// [COMMON] Use green theme for console
#define CMOD_CONSOLE_COLOR

// [COMMON] Use traditional EF application icon instead of quake one
#define CMOD_EF_ICON
