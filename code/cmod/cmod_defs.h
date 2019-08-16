/* ******************************************************************************** */
// Core Features
/* ******************************************************************************** */

// Core EF Defines
#define ELITEFORCE

// Core cMod defines
#define CMOD_COMMON_CORE
#define CMOD_CVAR_DEFS

// New filesystem
#define NEW_FILESYSTEM

// Mp3 Support
#define USE_CODEC_MP3

// New command system with various improvements
#define CMOD_COMMAND_INTERPRETER

// New cvar system with various improvements
// Requires NEW_FILESYSTEM and CMOD_COMMAND_INTERPRETER.
#define CMOD_CVAR_HANDLING

// New settings system with various improvements
// Changes settings location to cmod.cfg instead of original hmconfig.cfg,
// keeps same settings for different mods, and restricts VM modifications
// Requires NEW_FILESYSTEM, CMOD_COMMAND_INTERPRETER, and CMOD_CVAR_HANDLING.
#define CMOD_SETTINGS

/* ******************************************************************************** */
// Graphics
/* ******************************************************************************** */

// Support several new brightness settings (e.g. r_overBrightFactor and r_mapLightingGamma)
#define CMOD_MAP_BRIGHTNESS_SETTINGS

// Support r_textureGamma setting
#define CMOD_TEXTURE_GAMMA

// Automatically adjust brightness settings for consistency on certain types of maps
#define CMOD_MAP_BRIGHTNESS_AUTO_ADJUST

// Support framebuffer-based gamma, which allows r_gamma to work without changing system gamma settings
// Requires CMOD_MAP_BRIGHTNESS_SETTINGS
#define CMOD_FRAMEBUFFER

// Support separate mode setting in windowed/fullscreen mode (r_mode and r_fullscreenMode)
#define CMOD_FULLSCREEN

// Fixes issues with CMOD_FULLSCREEN
#define CMOD_IGNORE_RESIZE_MESSAGES

// Fix potential graphics glitch during loading screen
#define CMOD_LOADINGSCREEN_FIX

// Fix a shader caching issue that can cause low framerates on certain maps
#define CMOD_LIGHTMAPSTAGE_FIX

// Properly unintialize certain handles on renderer shutdown to prevent potential issues
// when using statically compiled renderer
#define CMOD_GLIMP_SHUTDOWN_FIX

// Support scaling console/chat font with display resolution (controlled by cmod_font_scaling cvar)
#define CMOD_FONT_SCALING

// Increase max number of characters displayed in chat mode to match original EF default
#define CMOD_CHAT_FIELD_SIZE

// Fix potential crash issue
#define CMOD_FOGFACTOR_FIX

// Fix potential crash issue on certain maps
#define CMOD_FOGSIDE_FIX

// Support modifying sky color in fastsky mode via r_fastskyColor cvar
#define CMOD_FASTSKY_COLOR

/* ******************************************************************************** */
// Version Identification
/* ******************************************************************************** */

// Add cMod version info to version string
#define CMOD_VERSION_STRING

// Add version string to userinfo and clean up which other values are included
#define CMOD_USERINFO

// Use green theme for console
#define CMOD_CONSOLE_COLOR

/* ******************************************************************************** */
// Server
/* ******************************************************************************** */

// Server-side game recording and admin spectator support
#define CMOD_RECORD

// Various server download support fixes and improvements
#define CMOD_DL_PROTOCOL_FIXES

// Various bot-related fixes and improvements
#define CMOD_BOT_TWEAKS

// Fixes for several issues that can interfere with the server being listed on
// master servers and/or server browser tools
#define CMOD_GETSTATUS_FIXES

// Fix to allow certain older ioEF versions (e.g. 1.37) to successfully negotiate
// protocol version
#define CMOD_PROTOCOL_MSG_FIX

// Prevent unnecessary engine modification to "nextmap" cvar, which can lead to breaking
// rotations and causing "stuck" maps
#define CMOD_NO_ENGINE_NEXTMAP_SET

// Allow scripts to override map command behavior via "cmod_sv_map_script" cvar
#define CMOD_MAP_SCRIPT

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

// Reduce unnecessary console log messages in various parts of the game
#define CMOD_REDUCE_WARNINGS

// Use traditional EF float casting behavior in VM, which fixes the physics behavior
// in various mods
#define CMOD_VMFLOATCAST

// Reverse an ioef change which appears to be no longer necessary, and may potentially
// cause issues such as photons disappearing on impact
#define CMOD_NOIMPACT_TRACEFIX

// Attempt to prevent Com_QueueEvent errors by increasing queue size
#define CMOD_EVENT_QUEUE_SIZE

// Fix strncpy range check issue which caused a problem on mac platform
#define CMOD_VM_STRNCPY_FIX

// Workaround for an issue which can cause a crash on certain maps
#define CMOD_CM_ALIGN_FIX

// Fix an issue in the client which can cause it to disconnect after completing
// a download in certain situations
#define CMOD_DL_LASTMSG_FIX

// Attempt to improve detection of console opening key on non-US keyboard layouts
#define CMOD_CONSOLE_KEY_FIXES

// Provides cvar option (in_mouse_warping) which attempts to use the same mouse capture
// method as older EF versions
#define CMOD_MOUSE_WARPING_OPTION

// New crosshair system which is meant to allow selecting any installed crosshair,
// rather than just a small fixed group. Currently rather hacky.
// Requires NEW_FILESYSTEM and CMOD_CVAR_HANDLING
#ifndef DEDICATED
#define CMOD_CROSSHAIR
#endif

// Disable auth system since EF auth server currently doesn't enforce any restrictions
// anyway and often goes down causing issues
#define CMOD_DISABLE_AUTH_STUFF

// Add protections against VMs writing config files for security purposes
#define CMOD_RESTRICT_VM_CFG_WRITE
