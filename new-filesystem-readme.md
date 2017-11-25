This is an updated file handling system for the ioquake3 project. It is a near complete rewrite of files.c and related components, designed to enhance the performance, stability, and security of the game while maintaining compatibility with almost all existing maps, mods, and configurations.

Here are the key features:
- Improved resource conflict negotiation. Broken textures/shaders and other issues from conflicting pk3s are greatly reduced.
- Improved load times, especially with lots of pk3s installed. Close to clean install performance with thousands of pk3s installed!
- Broad security improvements, safer downloading options, and fixes for multiple VM-based vulnerabilities.
- Improved download handling, especially when downloading from poorly configured servers.
- Ability to use resources in mods that aren't loaded, such as missionpack maps and textures when team arena mode isn't active.
- New debugging commands to trace file/shader origins.
- Ability to customize the download and pure lists when hosting servers.
- Fixed server-side pure list overflow issues.
- Semi-pure server support.
- Numerous minor bug fixes and tweaks.

Feel free to contact me if you have any suggestions, feedback, or bug reports!

Author: Noah Metzger <chomenor@gmail.com>

# Compiling

Should be very similar to regular ioquake3.

- For make builds, run "make NEW_FILESYSTEM=1".
- If you are using one of the build scripts instead of calling make directly, add the line "NEW_FILESYSTEM=1" to "Makefile.local"
- For visual studio, set "NEW_FILESYSTEM" in the preprocessor definitions for both the main game and renderer dlls.

There are some changes to the renderer dlls in the new filesystem. If you mix a renderer dll and main application that have different filesystem versions, the game should still run, but this isn't recommended or well tested.

# ID Pak Precedence

In the original filesystem the ID paks, pak0.pk3 - pak8.pk3, are treated with the same alphabetical precedence as any other pak. This means any pak in baseq3 that happens to have a higher alphabetical rank, such as "qpak.pk3", will override the ID paks if there are any resource conflicts. This can lead to unwanted effects if any misplaced or incompatible pk3s are present in baseq3.

In the new filesystem the ID paks, or whichever core paks were defined by hash when the game was compiled, are given automatic precedence over other paks in baseq3. If you want to intentionally override the ID paks without explicitly loading a mod, you can create a directory named "basemod" alongside baseq3 and place your mod paks in it. This way they will take precedence over anything in baseq3, even the ID paks.

In general regular maps and skins will work fine in baseq3, but things that work by overriding existing game assets, such as crosshairs, will need to be moved to basemod to take effect.

# Mod Settings Option

Currently ioq3 stores a separate copy of the game settings for every mod. Whenever the mod changes, either by a user action or connection to a server, the settings are reset using the q3config.cfg from the new mod directory. This can lead to unexpected effects for users, such as graphics settings going haywire and changing resolutions when connecting to a server. This can be especially frustrating given that many mods are mostly server side and don't need any custom settings anyway.

The new filesystem introduces a command line cvar called "fs_mod_settings", with the following effects:
- fs_mod_settings 0: All settings are loaded and saved in the q3config.cfg in baseq3, regardless of the current mod. Every mod uses the same settings. This is closer to the original q3 behavior.
- fs_mod_settings 1: Existing ioquake3 behavior, with separately stored settings for each mod.

I favor fs_mod_settings 0 as the default value, because it's usually easier to manage a few settings that need to be changed between mods than to have every setting change between mods. Mods that require custom settings will be fine if they use deconflicted cvar names to store their settings. It appears most mods work under this setting without significant issues.

# Download Directory Options

There are two new settings that can be used to control automatic downloads.

- fs_saveto_dlfolder: If enabled, this will cause incoming downloads to be saved in the "downloads" folder within the target mod directory. For example, "baseq3/somefile.pk3" will be rewritten to "baseq3/downloads/somefile.pk3". Pk3s directly in the mod directory will take precedence over those in the downloads folder.

- fs_restrict_dlfolder: This setting blocks potentially less secure content from pk3s in the downloads folder. It has three modes:
    * setting 0: No restrictions.
	* setting 1: Config files and qvm files that do not match a list of known trusted mod hashes will be blocked.
	* setting 2: Config files and all qvm files will be blocked.

These settings can increase security, but may break compatibility with some servers unless you manually move files out of the downloads folder.

Based on ideas from github.com/ioquake/ioq3/issues/130

# Source Directory Options

A new cvar is introduced called "fs_dirs", which can be set from the command line to adjust which source directories the game uses to load/save files. The default is "*homepath basepath steampath gogpath". This means that homepath is the write directory, indicated by the asterisk, and the other locations are used for reading. The specific paths are still controlled by the "fs_homepath", "fs_basepath", "fs_steampath", and "fs_gogpath" cvars, respectively.

Notes:
- You can set an asterisk on multiple directories. The additional directories will be used as backup write directories if the first one fails a write test.
- The write directory selected will always be treated as the primary read directory.
- If no directory passes a write test, or no write directory was set at all, the game will run in read-only mode.

Examples:
- "fs_dirs" = "*basepath": Read and write to fs_basepath only.
- "fs_dirs" = "homepath basepath": Read-only mode with homepath taking precedence over basepath.

# Inactive Mod Support

This project supports loading files from all mod directories, not just the current active mod. This is intended to help smooth out discrepancies between server directory configurations and allows maps to work correctly even if they have dependencies that, for one reason or another, are located in the wrong mod directory.

The precedence system ensures that files located in the current mod and basegame directories always have first priority. Inactive mods are only accessed as a last resort if files cannot be found anywhere else.

There are two cvars used to control this feature:
- fs_search_inactive_mods: Applies to primary file lookups used to load models, textures, and sounds. (default: 2)
- fs_list_inactive_mods: Applies to file listing queries used by the ingame map and model menus. (default: 1)

Both cvars support the following settings:
- 0: Disabled; all files from inactive mods blocked
- 1: Enabled only for ID paks (including missionpack) and files on the connected server's pure list
- 2: Enabled; all files from inactive mods allowed

# Cache Mechanisms

The new filesystem uses a file called "fscache.dat" to store pk3 index data and reduce startup time. Cached data is matched to pk3s by filename, size, and timestamp, so it shouldn't cause problems when pk3s are modified. If you suspect the cache could be causing a problem, you can delete the cache file or disable it by setting "fs_index_cache" to 0 on the command line.

The new filesystem also uses a memory cache to keep previously accessed files in memory for faster access. This helps speed up load times between maps. The size of this buffer is controlled by the "fs_read_cache_megs" cvar. The default is currently 64 for the client and 4 for the dedicated server. This value can be set to 0 to disable the cache altogether.

# Download / Pure List Configuration

Two new cvars, "fs_download_manifest" and "fs_pure_manifest", can be used to customize which pk3s are added to the download and pure lists when running a server. Both cvars use a space-seperated list of selector rules that can be either specific pk3 names or one of the following keywords:

- *mod_paks - Selects all paks from the current active mod.
- *base_paks - Selects all paks from com_basegame (baseq3).
- *inactivemod_paks - Selects all paks that don't fit the previous two categories
- *currentmap_pak - Selects the pak containing the bsp of the current running map.
- *cgame_pak - Selects the pak containing the preferred cgame.qvm file.
- *ui_pak - Selects the pak containing the preferred ui.qvm file.
- *referenced_paks - Selects paks accessed during the loading process on the server.

The default download manifest selects all the paks from the current mod directory, as well as the current cgame and ui paks, and the current map pak. The *referenced_paks rule is currently added for consistency with original filesystem behavior, but in virtually all cases is redundant to *currentmap_pak and *mod_paks and can be dropped without issue.
```
set fs_download_manifest *mod_paks *cgame_pak *ui_pak *currentmap_pak *referenced_paks
```

Some server configurations have a lot of maps or optional mod files in the mod directory. This can lead to clients having too many files to download, since the default behavior is to place everything in the mod directory into the download list. To avoid this, you can specify only the core mod files that clients require instead of using the *mod_paks rule. For example, here is a reduced download manifest for the OSP mod.
```
set fs_download_manifest osp/zz-osp-pak3 osp/zz-osp-pak2 osp/zz-osp-pak1 osp/zz-osp-pak0 *currentmap_pak
```

The default pure manifest simply selects every pak available to the game.
```
set fs_pure_manifest *mod_paks *base_paks *inactivemod_paks
```

Pure servers with a large number of pk3s in baseq3 (300+) can run into problems with the pure list overflowing. To avoid such issues you can replace the *base_baks rule in the pure manifest with specific core paks. Note that any auxiliary paks in baseq3 required by maps or mods, as well as optional content like player models, will need be included manually as well.
```
set fs_pure_manifest *mod_paks baseq3/pak8 baseq3/pak7 baseq3/pak6 baseq3/pak5 baseq3/pak4 baseq3/pak3 baseq3/pak2 baseq3/pak1 baseq3/pak0 *currentmap_pak
```

The order of the pure list determines the precedence of files on clients. Normally the files in the pure list are sorted according to the filesystem precedence on the server, rather than the order in the pure manifest. It is possible to force a certain order in the pure manifest by separating sections with a dash. In this example baseq3/somefile.pk3 will be the first entry on the pure list and have the highest precedence, regardless of where it stands in the normal filesystem ordering.
```
set fs_pure_manifest baseq3/somefile - *mod_paks *base_paks *currentmap_pak
```

Note that if the same pk3 is selected by multiple rules, its position will be according to the first rule that selected it.

# Semi-Pure Server

This option causes clients to prioritize data from paks on the server like a normal pure server, but also allows loading content from other paks when it is not available from the pure list paks. For example, it lets clients use custom models that are not on the server. To enable, set sv_pure to 2 on the server. This setting only affects clients using the new filesystem; clients using the original filesystem will function the same as if sv_pure is 1.

In theory this option will work with servers running any filesystem version, but it is recommended to use with the new filesystem if possible.

# Shader Precedence

This section concerns map and mod developers who work with shaders, specifically regarding cases where the same shader is defined differently in different pk3 files.

To start with an example, suppose you have a shader called "textures/myshader", in the file "scripts/myshaders.shader", located in "pak0.pk3" of your mod. Now suppose you want to release an update "pak1.pk3" that replaces this shader with an updated version. The correct way is to duplicate the file "scripts/myshaders.shader" into pak1.pk3 and then update the shader within that file. Due to filesystem precedence this will override the version of the file from pak0.pk3 so it doesn't get loaded at all, ensuring only the shaders from the updated file will be used.

Now suppose you were to name the updated shader file "scripts/myshaders2.shader". Both the old and new shader files will get loaded, since they have different names, leading to behavior that can be confusing and inconsistent across different versions of Quake 3. In the original filesystem the shader precedence tends to be the opposite of the filesystem precedence, so the shader from pak0.pk3 will usually take precedence over the one from pak1.pk3. But in the new filesystem, shaders are taken from the higher precedence pk3, regardless of the .shader filename. This is a much more robust approach in general, but it means that in rare cases mods that actually depend on the old backwards precedence behavior could break. Mods doing shader replacements should apply the .shader override technique if they want consistent behavior on both the original and new filesystems.

# Debugging Cvars

There are some new cvars that can be set to 1 to enable debug prints.

- fs_debug_state: logs changes to the primary filesystem state such as the current mod dir and server pak list
- fs_debug_refresh: logs warnings that come up during file indexing phase; best set from the command line and combined with fs_index_cache 0
- fs_debug_fileio: logs file reading/writing activity
- fs_debug_lookup: logs file index lookup activity
- fs_debug_references: logs pk3 referencing activity (used for downloads and pure lists)
- fs_debug_filelist: logs directory listing calls

# Debugging Commands

The following commands can be used to get an idea of where resources will come from in the filesystem. They generate a list of resources sorted from highest to lowest precedence. The first resource is typically what will be used by the game.

- find_file <file name>: General purpose file lookup. Example: find_file maps/q3dm11.bsp
- find_shader <shader/image name>: Locates both shaders and images with this name. Example: find_shader textures/base_floor/techfloor
- find_sound <sound name>: Locate sound resources with this name. Example: find_sound sound/weapons/noammo
- find_vm <vm/dll name>: Locate vm or dll resources with this name. Example: find_vm cgame

Once you run one of the above commands, you can use the "compare" command to find why one element in the list was chosen over the other. For example, run "compare 1 2" to check why the top element was selected over the second element. The list from one previous find command is stored in memory for the purpose of supporting this command.

# Known Issues

- Little testing has been done on less common platforms and build environments, or on big endian systems. Fortunately most of the issues that could come up should be limited to a few places like fsc_os.c.

- Only the makefile and msvc12 project files are updated to use the new filesystem. Some of the other project files and build scripts aren't updated yet. I figure somebody with more experience with those components would be better suited to make the changes.

- The code conventions used for the filesystem are a bit different from the main ioq3 codebase in terms of stuff like function capitilization and brace placement.

- The server-side pure validation function SV_VerifyPaks_f is no longer supported, since it has no security value in modern conditions. All other pure server functionality is supported and cross-compatible with existing clients and servers.

- The pk3dir feature is currently not supported, as I'm not sure it's worth the complexity to include it. Since all mod dirs are loaded by default now, you can usually just place the test files in a mod dir instead. If you have a use case that this doesn't cover sufficiently, let me know and I'll look into adding the full pk3dir functionality.
