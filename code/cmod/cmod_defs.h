/* ******************************************************************************** */
// Major Features
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
#if defined(NEW_FILESYSTEM) && defined(CMOD_COMMAND_INTERPRETER)	// required
#define CMOD_CVAR_HANDLING
#endif

// [FEATURE] New settings system with various improvements
// Changes settings location to cmod.cfg instead of original hmconfig.cfg
// and keeps same settings for different mods
#if defined(NEW_FILESYSTEM) && defined(CMOD_COMMAND_INTERPRETER) && defined(CMOD_CVAR_HANDLING)		// required
#define CMOD_SETTINGS
#endif

/* ******************************************************************************** */
// Graphics
/* ******************************************************************************** */

// [FEATURE] Support several new brightness settings (e.g. r_overBrightFactor and r_mapLightingGamma)
#define CMOD_MAP_BRIGHTNESS_SETTINGS

// [FEATURE] Support r_textureGamma setting
#define CMOD_TEXTURE_GAMMA

// [FEATURE] Automatically adjust graphics settings for consistency on certain types of maps
#define CMOD_MAP_AUTO_ADJUST

// [FEATURE] Support framebuffer-based gamma, which allows r_gamma to work without changing system gamma settings
#if defined(CMOD_MAP_BRIGHTNESS_SETTINGS)	// required
#define CMOD_FRAMEBUFFER
#endif

// [FEATURE] Support separate mode setting in windowed/fullscreen mode (r_mode and r_fullscreenMode)
// Also support directly specifying custom resolutions in r_mode and r_fullscreenMode
#define CMOD_RESOLUTION_HANDLING

// [FEATURE] Support scaling console/chat font with display resolution (controlled by cmod_font_scaling cvar)
#define CMOD_FONT_SCALING

// [FEATURE] Support modifying sky color in fastsky mode via r_fastskyColor cvar
#define CMOD_FASTSKY_COLOR

// [FEATURE] Support fading HUD graphics to reduce potential burn-in on OLED displays
#define CMOD_ANTI_BURNIN

// [BUGFIX] Restore original EF environment mapped shader behavior
// Controlled by "r_envMapMode" cvar: -1=auto, 0=EF style, 1=Q3 style
#define CMOD_EF_ENVIRONMENT_MAP_MODE

// [BUGFIX] Fixes potential vid restart loop issues (potentially related to r_fullscreenMode support)
#define CMOD_IGNORE_RESIZE_MESSAGES

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

// [FEATURE] Allow scripts to override map command behavior via "sv_mapscript" cvar
#define CMOD_MAP_SCRIPT

// [FEATURE] Additional console commands to enhance server script capabilities
#define CMOD_SERVER_CMD_TOOLS

// [FEATURE] Console command event trigger support (requires CMOD_SERVER_CMD_TOOLS)
#define CMOD_SERVER_CMD_TRIGGERS

// [FEATURE] Support for engine-based voting system
#define CMOD_VOTING

// [FEATURE] Support for map index system
#define CMOD_MAPTABLE

// [FEATURE] Support minimium snaps value. This prevents older clients with low snaps
// defaults from having impaired connections on servers with higher sv_fps settings.
#define CMOD_MIN_SNAPS

// [FEATURE] Support cvars to specify custom server bsp file, server aas file,
// and server entity file (replaces the normal entities from the bsp)
#define CMOD_MAP_SOURCE_OVERRIDE

// [FEATURE] Support cvars to override client map name and mod directory
// Requires CMOD_COMMON_SERVER_INFOSTRING_HOOKS
#define CMOD_SERVER_INFOSTRING_OVERRIDE

// [FEATURE] Support loading aas files even if they don't match bsp file checksum
// Aas files are often valid to use even if this checksum doesn't match
#define CMOD_IGNORE_AAS_CHECKSUM

// [FEATURE] Ignore VM entity startsolid errors
// Ignores error generated in g_items.c->FinishSpawningItem which can occur on Q3 maps
#define CMOD_IGNORE_STARTSOLID_ERROR

// [BUGFIX] Various server download support fixes and improvements
#define CMOD_DOWNLOAD_PROTOCOL_FIXES

// [BUGFIX] Various bot-related fixes and improvements
#define CMOD_BOT_TWEAKS

// [BUGFIX] Experimental - allow bots to select random goal areas when no items are available
// Should help increase bot roaming activity especially on sniper modes
// Has some potential for for CPU load spikes, but seems to be generally okay on most maps in testing
#define CMOD_BOT_RANDOM_GOALS

// [BUGFIX] Workaround to allow bots to join password-protected server
#define CMOD_BOT_PASSWORD_FIX

// [BUGFIX] Fixes for several issues that can interfere with the server being listed on
// master servers and/or server browser tools
#define CMOD_GETSTATUS_FIXES

// [BUGFIX] Fix to allow certain older ioEF versions (e.g. 1.37) to successfully negotiate
// protocol version
#define CMOD_PROTOCOL_MSG_FIX

// [BUGFIX] Prevent unnecessary engine modification to "nextmap" cvar, which can lead to breaking
// rotations and causing "stuck" maps
#define CMOD_NO_ENGINE_NEXTMAP_SET

// [BUGFIX] Use alternative to changing serverid during map restarts
// This avoids the need for a systeminfo update during map restarts and potentially fixes
// some intermittent buggy behavior seen in the Elite Force 1.20 client
#define CMOD_MAP_RESTART_STATIC_SERVERID

// [BUGFIX] Prevent gamestate overflows by dropping entity baselines
// This fixes issues that can cause errors on certain maps in certain circumstances
// Dropping some entity baselines should not normally cause any issues except perhaps a
// negligible drop in network efficiency, and is much better than the map failing to load
#define CMOD_GAMESTATE_OVERFLOW_FIX

// [BUGFIX] Support per-client download map storage, to avoid potential errors if map
// changes during a multi-pk3 UDP download sequence
#ifdef NEW_FILESYSTEM
#define CMOD_PER_CLIENT_DOWNLOAD_MAP
#endif

// [BUGFIX] Improved server ping calculation, enabled by sv_pingFix cvar
#define CMOD_SV_PINGFIX

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

// [FEATURE] Load cMod QVM module releases in place of stock game QVMS
#define CMOD_QVM_LOADING

// [FEATURE] Reduce unnecessary console log messages in various parts of the game
#define CMOD_REDUCE_WARNINGS

// [FEATURE] Provides cvar option (in_mouse_warping) which attempts to use the same mouse capture
// method as older EF versions
#define CMOD_MOUSE_WARPING_OPTION

// [FEATURE] New crosshair system to allow selecting any installed crosshair, rather
// than just a small fixed group, when combined with QVM support.
#if !defined(DEDICATED) && defined(NEW_FILESYSTEM)	// required
#define CMOD_CROSSHAIR
#endif

// [FEATURE] Support "s_noOverlap" option to emulate 1.20 behavior of only playing one copy
// of the same sound effect at the same time
#define CMOD_FILTER_OVERLAPPING_SOUNDS

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

// [BUGFIX] Increase concurrent sound limit to reduce rapidfire weapon stuttering
#define CMOD_CONCURRENT_SOUNDS

// [BUGFIX] Try to fix some issues with UI popup after errors
#define CMOD_ERROR_POPUP_FIXES

// [BUGFIX] Prevent overwriting argv[0] (can cause problems with server scripts accessing
//   process name, for example)
#define CMOD_ARGV_PATCH

// [COMMON] New logging system
// Currently used for server-side logging, but capable of supporting client-side
// logging as well for debugging purposes
#if defined(NEW_FILESYSTEM)		// required
#define CMOD_LOGGING_SYSTEM
#define CMOD_LOGGING_MESSAGES
#endif

// [COMMON] Add protections against VMs writing config files for security purposes
#define CMOD_RESTRICT_VM_CFG_WRITE

// [COMMON] Add console command to copy debug info to clipboard
#define CMOD_COPYDEBUG_CMD

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

// [COMMON] Restructure server serverinfo and systeminfo handling to use common access functions
#define CMOD_COMMON_SERVER_INFOSTRING_HOOKS

// [COMMON] Support extra VM interface functions for compatible VMs
#define CMOD_VM_EXTENSIONS

/* ******************************************************************************** */
// Setup
/* ******************************************************************************** */

#if defined(CMOD_COPYDEBUG_CMD) && defined(_WIN32) && !defined(DEDICATED) && defined(CMOD_SETTINGS) \
	&& defined(NEW_FILESYSTEM)
#define CMOD_COPYDEBUG_CMD_SUPPORTED
#endif
