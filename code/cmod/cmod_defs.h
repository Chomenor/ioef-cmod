/* ******************************************************************************** */
// Major Features
/* ******************************************************************************** */

// [COMMON] Core EF Defines
#define ELITEFORCE

// [COMMON] Mp3 Support
#define USE_CODEC_MP3

// [FEATURE] New filesystem with various improvements
#if !defined( __EMSCRIPTEN__ )
#define NEW_FILESYSTEM
#endif

/* ******************************************************************************** */
// Settings & Security
/* ******************************************************************************** */

// [FEATURE] New command system with various improvements
#if !defined( __EMSCRIPTEN__ )
#define CMOD_COMMAND_INTERPRETER
#endif

// [FEATURE] New settings system with various improvements
// Moves settings storage to cmod.cfg instead of hmconfig.cfg
#if defined( NEW_FILESYSTEM ) && defined( CMOD_COMMAND_INTERPRETER )	// required
#define CMOD_SETTINGS
#endif

// [FEATURE] When starting the game, if regular cmod.cfg doesn't exist, search
// for config files in other locations written in the last 30 days. If one or
// more is found, prompt user if they want to import the most recent one.
#if !defined( DEDICATED ) && defined( CMOD_SETTINGS )
#define CMOD_IMPORT_SETTINGS
#endif

// [FEATURE] Restrict settings that can be changed by autoexec.cfg, to avoid
// issues with prepackaged config files in certain old EF installer packages
#if !defined( DEDICATED ) && defined( CMOD_SETTINGS )
#define CMOD_SAFE_AUTOEXEC
#endif

// [FEATURE] Support determining allowed permissions for game modules based on factors
// such as download folder status and trusted hash.
#if !defined( DEDICATED ) && defined( NEW_FILESYSTEM )
#define CMOD_VM_PERMISSIONS
#endif

// [FEATURE] Restrict untrusted VMs from making persistent changes to binds
// Binds set by untrusted VMs are allowed temporarily for compatibility purposes
// but reset when disconnecting from server.
#if defined( CMOD_VM_PERMISSIONS ) && defined( CMOD_COMMAND_INTERPRETER )
#define CMOD_BIND_PROTECTION
#endif

// [FEATURE] Restrict untrusted VMs from writing files to disk
#if defined( CMOD_VM_PERMISSIONS )
#define CMOD_WRITE_PROTECTION
#endif

/* ******************************************************************************** */
// Graphics
/* ******************************************************************************** */

// [FEATURE] Support several new brightness settings (e.g. r_overBrightFactor and r_mapLightingGamma)
#define CMOD_MAP_BRIGHTNESS_SETTINGS

// [FEATURE] Perform lighting adjustments on external lightmaps so lighting settings are applied
// consistently regardless of which type of lightmap the map uses
#define CMOD_EXTERNAL_LIGHTMAP_PROCESSING

// [FEATURE] Support r_textureGamma setting
#define CMOD_TEXTURE_GAMMA

// [FEATURE] Support applying r_intensity to only a fraction of image values
// Specified by adding ':' character and fraction after r_intensity value, such as "1.5:0.5"
#define CMOD_FRACTIONAL_INTENSITY

// [FEATURE] Automatically adjust graphics settings for consistency on certain types of maps
#define CMOD_MAP_AUTO_ADJUST

// [FEATURE] Support framebuffer-based gamma, which allows r_gamma to work without changing system gamma settings
#if defined(CMOD_MAP_BRIGHTNESS_SETTINGS)	// required
#define CMOD_FRAMEBUFFER
#endif

// [FEATURE] Support separate mode setting in windowed/fullscreen mode (r_mode and r_fullscreenMode)
// Also support directly specifying custom resolutions in r_mode and r_fullscreenMode
#if !defined( __EMSCRIPTEN__ )
#define CMOD_RESOLUTION_HANDLING
#endif

// [FEATURE] Support scaling console/chat font with display resolution (controlled by cmod_font_scaling cvar)
#define CMOD_FONT_SCALING

// [FEATURE] Support modifying sky color in fastsky mode via r_fastskyColor cvar
#define CMOD_FASTSKY_COLOR

// [FEATURE] Support engine-based aspect correction for mods with no native support
#if !defined(DEDICATED) && defined(NEW_FILESYSTEM)	// required
#define CMOD_ENGINE_ASPECT_CORRECT
#endif

// [FEATURE] Support fading HUD graphics to reduce potential burn-in on OLED displays
#define CMOD_ANTI_BURNIN

// [BUGFIX] Restore original EF environment mapped shader behavior
// Controlled by "r_envMapMode" cvar: -1=auto, 0=EF style, 1=Q3 style
#define CMOD_EF_ENVIRONMENT_MAP_MODE

// [BUGFIX] Fixes potential vid restart loop issues (potentially related to r_fullscreenMode support)
#if !defined( __EMSCRIPTEN__ )
#define CMOD_IGNORE_RESIZE_MESSAGES
#endif

// [BUGFIX] Fix potential graphics glitch during loading screen
#define CMOD_LOADINGSCREEN_FIX

// [BUGFIX] Fix a shader caching issue that can cause low framerates on certain maps
#define CMOD_LIGHTMAPSTAGE_FIX

// [BUGFIX] Hacky workaround for opengl2 renderer error seen on map 'ctf_storm'
#define CMOD_GL2_MAX_FACE_POINTS_FIX

// [BUGFIX] Properly unintialize certain handles on renderer shutdown to prevent potential issues
// when using statically compiled renderer
#define CMOD_GLIMP_SHUTDOWN_FIX

// [BUGFIX] Increase max number of characters displayed in chat mode to match original EF default
#define CMOD_CHAT_FIELD_SIZE

// [BUGFIX] Fix potential crash issue
#define CMOD_FOGFACTOR_FIX

// [BUGFIX] Fix potential crash issue
// Also fixed in Quake3e: https://github.com/ec-/Quake3e/commit/d2d1dc4d715da5c609bf2c643fce4a0579cc9dec
#define CMOD_FRCL_ITERATION_FIX

// [BUGFIX] Fix renderer bug which may cause potential crash issue
// Also fixed in Quake3e: https://github.com/ec-/Quake3e/commit/fdc6e858be40be2a447820f1d76d74b6c9042491
#define CMOD_FRCL_ENTITYNUM_FIX

// [BUGFIX] Fix potential crash issue on certain maps
#define CMOD_FOGSIDE_FIX

// [BUGFIX] Workaround potential mac compiler bug affecting tcGen shaders
// Original issue involved broken console shader on release builds using clang v13.0 x86_64
#define CMOD_TCGEN_FIX

// [BUGFIX] Support increased texture size in ResampleTexture to avoid errors with
// r_roundImagesDown set to 0.
#define CMOD_INCREASE_MAX_TEXTURE_SIZE

// [TWEAK] Allow increasing r_lodCurveError value which would otherwise be blocked by cheat
// protection. Unlike low values, which could potentially expose cracks in walls, allowing
// higher values shouldn't present any cheating risk.
#define CMOD_ALLOW_INCREASED_LOD_CURVE_ERROR

// [TWEAK] Experimental support for loading non power of two textures without resizing,
// for slight graphical improvements on some maps.
// Enabled by "r_ext_texture_non_power_of_two" cvar.
#define CMOD_SUPPORT_NON_POWER_OF_TWO_TEXTURES

/* ******************************************************************************** */
// Sound
/* ******************************************************************************** */

#ifndef DEDICATED

// [FEATURE] Support "s_noOverlap" option to emulate 1.20 behavior of only playing one copy
// of the same sound effect at the same time.
#define CMOD_FILTER_OVERLAPPING_SOUNDS

// [BUGFIX] Reset sound system on map changes to avoid getting cached sounds from a previous
// map or mod.
#define CMOD_SOUND_RESET

// [TWEAK] Optimization of sound reset above to avoid load time impact.
#if defined(CMOD_SOUND_RESET) && defined(NEW_FILESYSTEM)
#define CMOD_FAST_SOUND_RESET
#endif

// [BUGFIX] Remove apparently unnecessary abort condition in MP3 decoder, which breaks the
// music in certain maps such as 'amenhotep' and 'hm_stronghold'.
#define CMOD_MP3_LAYER_CHECK_FIX

// [BUGFIX] Disable passing "allow change" flags to SDL_OpenAudioDevice by default.
// This fixes a crash issue seen when connecting/disconnecting USB audio devices, and
// could have other stability or sound configuration benefits.
#define CMOD_SOUND_DISABLE_SDL_FORMAT_CHANGES

// [TWEAK] Increase concurrent sound limit to reduce rapidfire weapon stuttering.
#define CMOD_CONCURRENT_SOUNDS

// [BUGFIX] Fix for possible sound buffer synchronization issues due to time overflow condition.
// Not sure if this is actually necessary, but it shouldn't hurt.
#define CMOD_SOUND_DMA_BUFFER_TWEAK

#endif	// !DEDICATED

/* ******************************************************************************** */
// Client
/* ******************************************************************************** */

#ifndef DEDICATED

// [FEATURE] Support VM calls to extend server querying features for the UI server browser.
#define CMOD_SERVER_BROWSER_SUPPORT

// [FEATURE] Support asynchronous host name resolution when querying master servers, and
// automatically query for both ipv4 and ipv6 servers when possible.
#define CMOD_MULTI_MASTER_QUERY

// [FEATURE] New crosshair system to allow selecting any installed crosshair, rather
// than just a small fixed group, when combined with QVM support.
#if defined(NEW_FILESYSTEM)	// required
#define CMOD_CROSSHAIR
#endif

// [FEATURE] Support VM calls to enable cgame alt fire button swapping features without server support.
#define CMOD_CLIENT_ALT_SWAP_SUPPORT

// [FEATURE] Provides cvar option (in_mouse_warping) which attempts to use the same mouse capture
// method as older EF versions.
#define CMOD_MOUSE_WARPING_OPTION

// [FEATURE] Improved UI mouse sensitivity handling. By default, the cursor speed in UI will be reduced
// if ingame sensitivity is set below the default of 5. A specific UI menu sensitivity that is higher or
// lower than ingame sensitivity can also be set using "cl_menuSensitivity" cvar.
#define CMOD_UI_MOUSE_SENSITIVITY

// [FEATURE] Support "URI" console command to register/deregister URI handler on Windows.
// On other platforms it will currently only display an error message.
// Based on https://github.com/stefansundin/ioq3/commit/13060022021866704167aa28a086fd5a7043350a
#define CMOD_URI_REGISTER_COMMAND

// [BUGFIX] Attempt to improve detection of console opening key on non-US keyboard layouts.
#define CMOD_CONSOLE_KEY_FIXES

// [BUGFIX] Fix an issue in the client which can cause it to disconnect after completing
// a download in certain situations.
#define CMOD_DL_LASTMSG_FIX

// [TWEAK] Increase some map/graphics limits for better compatibility.
#define CMOD_INCREASE_LIMITS

// [TWEAK] Restore console scroll position to end when reopening. Enabled by "con_autoReturn" cvar.
#define CMOD_CONSOLE_AUTO_RETURN

// [TWEAK] Disable motd server contact which currently does not appear to have any working
// functionality attached.
#define CMOD_DISABLE_MOTD

// [TWEAK] Defer loading HTTP library until needed for download, like earlier ioq3 versions.
// Saves ~10ms startup time.
#define CMOD_DEFER_HTTP

#endif	// !DEDICATED

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

// [FEATURE] Allow mods to set custom player score values that are sent in response to
// status queries, instead of using the playerstate score field. Especially useful for
// Elimination mode in cases where the score field is needed for round indicator features.
#define CMOD_SUPPORT_STATUS_SCORES_OVERRIDE

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

// [BUGFIX] Workaround for game code bug when creating EV_SHIELD_HIT event
// This fixes an issue with the original game code in which EV_SHIELD_HIT events are created
// with r.origin set to vec3_origin instead of the origin of the player being hit. Due to
// entity visibility checks, this prevented the event from being sent to clients reliably.
#define CMOD_SHIELD_EFFECT_FIX

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

// [BUGFIX] Workaround for "Super Mario Mod" mod, to allow players to connect to server
// without loading the mod first
#if defined( NEW_FILESYSTEM )
#define CMOD_MARIO_MOD_FIX
#endif

// [BUGFIX] Limit player model length as workaround for EF-specific game code bug in
// which long model names can cause client errors in certain cases
#define CMOD_MODEL_NAME_LENGTH_LIMIT

// [TWEAK] Enable ipv6 support by default for dedicated server builds
#define CMOD_DEDICATED_SERVER_IPV6_DEFAULT

/* ******************************************************************************** */
// Misc
/* ******************************************************************************** */

// [FEATURE] Load cMod QVM module releases in place of stock game QVMS
#define CMOD_QVM_SELECTION

// [FEATURE] Reduce unnecessary console log messages in various parts of the game
#define CMOD_REDUCE_WARNINGS

// [FEATURE] Support loading files from "Resources" directory in Mac app bundle
// Workaround for notarization issues if unsigned resource files are placed in MacOS directory
#define CMOD_MAC_APP_RESOURCES

// [FEATURE] Make certain engine information and setting preferences available to VMs
// Used to enable features in the cMod VMs that are too specialized to enable by default for all engines
#define CMOD_VM_CONFIG_VALUES

// [BUGFIX] Use traditional EF float casting behavior in VM, which fixes the physics behavior
// in various mods
#define CMOD_VMFLOATCAST

// [BUGFIX] Reverse an ioef change which appears to be no longer necessary, and may potentially
// cause issues such as photons disappearing on impact
#define CMOD_NOIMPACT_TRACEFIX

// [BUGFIX] Attempt to prevent Com_QueueEvent errors by increasing queue size
#define CMOD_EVENT_QUEUE_SIZE

// [BUGFIX] Attempt to handle invalid memory addresses on VM block copy operations by wrapping
// the addresses rather than triggering an error, similar to old ioq3/ioef versions prior to
// around 2011. Potential fix for issue with Super FFA mod.
#define CMOD_VM_TOLERANT_BLOCKCOPY

// [BUGFIX] Fix strncpy range check issue which caused a problem on mac platform
#define CMOD_VM_STRNCPY_FIX

// [BUGFIX] Workaround for an issue which can cause a crash on certain maps
#define CMOD_CM_ALIGN_FIX

// [BUGFIX] Disable auth system since EF auth server currently doesn't enforce any restrictions
// anyway and often goes down causing issues
#define CMOD_DISABLE_AUTH_STUFF

// [BUGFIX] Extend some ioq3 overflow checks to EF-specific sections of msg.c
#define CMOD_MSG_OVERFLOW_CHECKS

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

// [COMMON] Add console command to copy debug info to clipboard
#define CMOD_COPYDEBUG_CMD

/* ******************************************************************************** */
// Version Identification
/* ******************************************************************************** */

// [COMMON] Set cMod version identifier strings and window title
#define CMOD_VERSION_STRING

// [COMMON] Add version string to userinfo and clean up which other values are included
#define CMOD_USERINFO

// [COMMON] Use green theme for console
#define CMOD_CONSOLE_COLOR

// [COMMON] Use traditional EF application icon instead of quake one
#define CMOD_EF_ICON

/* ******************************************************************************** */
// Error Handling
/* ******************************************************************************** */

// [BUGFIX] Workaround for ioquake3 issues related to longjmp being called during
// QVM trap calls, especially on Windows 11. Fix ported from Quake3e.
#define CMOD_LONGJMP_FIX

// [BUGFIX] Try to fix some issues with UI popup after errors.
#define CMOD_ERROR_POPUP_FIXES

// [TWEAK] Disable signal handlers
// This disables the ioquake3 error signal handling, so the standard OS error handling
// is invoked instead in the case of crashes. In most cases this results in more informative
// error messages, and it also allows features like crash dumps to work correctly.
#define CMOD_NO_ERROR_SIGNAL_HANDLER

// [TWEAK] Disable the pid file/safe mode prompt when restarting after error,
// since it currently doesn't reset enough settings to fix most types of errors.
#define CMOD_NO_SAFE_SETTINGS_PROMPT

/* ******************************************************************************** */
// Common library functions and code structure changes
// This section is intended for components that are necessary for other features to
//   work/compile but do not cause functional changes by themselves
/* ******************************************************************************** */

// [COMMON] Misc defines shared by multiple features
#define CMOD_COMMON_DEFINES

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

// [COMMON] Stub functions for VM permissions, to support compiling even if
// CMOD_VM_PERMISSIONS is disabled
#define CMOD_CORE_VM_PERMISSIONS

/* ******************************************************************************** */
// Setup and implied settings - should not be enabled/disabled directly.
/* ******************************************************************************** */

#if defined(CMOD_COPYDEBUG_CMD) && defined(_WIN32) && !defined(DEDICATED) && defined(CMOD_SETTINGS) \
	&& defined(NEW_FILESYSTEM)
#define CMOD_COPYDEBUG_CMD_SUPPORTED
#endif

#if defined(CMOD_SETTINGS)
#define CMOD_CVAR_HANDLING
#endif

#if defined(CMOD_QVM_SELECTION)
// Support for loading values from modcfg configstrings from remote server
#define CMOD_CLIENT_MODCFG_HANDLING
#endif

#if defined(USE_CODEC_MP3)
#if !defined( FPM_64BIT ) && !defined( FPM_INTEL ) && !defined( FPM_ARM ) && !defined( FPM_MIPS ) \
		&& !defined( FPM_SPARC ) && !defined ( FPM_PPC ) && !defined( FPM_DEFAULT )
#define FPM_64BIT
#endif
#endif
