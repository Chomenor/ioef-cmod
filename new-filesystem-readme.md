This is an updated file handling system for ioquake3 and other Quake 3 based games. It is a near complete rewrite of files.c and related components, designed to enhance the performance, stability, and security of the game while maintaining compatibility with virtually all existing servers and content.

Here are the key features:
- Improved precedence system which prevents conflicts and unwanted game changes from custom pk3s
- Ability to use missionpack maps and textures when team arena mode isn't active
- Greatly improved load times, especially with lots of pk3s installed
- Safer downloading options and other security improvements
- Improved recovery from server download errors such as broken HTTP links
- Ability to customize the download and pure lists when hosting servers
- Improved server mod loading & ability to change mods on a running server
- Fixed server-side pure list overflow issues
- Semi-pure server support
- New debugging commands to trace file/shader origins
- Various other features and improvements

Feel free to contact me if you have any suggestions, feedback, or bug reports!

Author: Noah Metzger <chomenor@gmail.com>

# Installation & Usage

If you are using Windows, you can download a binary from the Github release page and extract it to your Quake 3 directory. For other operating systems, you will need to compile a binary yourself; refer to the instructions below.

In most cases everything will work under this filesystem the same as you are familiar with from ioquake3. One exception is that there are certain types of mods which will need to be moved from baseq3 to a directory called "basemod". If you are using mods installed in baseq3 please refer to the "precedence system" section below.

# Compiling

The build process should be the same as regular ioquake3. Refer to the ioquake3 instructions at <http://wiki.ioquake3.org/Building_ioquake3>.

There are some changes to the renderer dlls in the new filesystem. If you mix a renderer dll and main application that have different filesystem versions, the game should still run, but this isn't recommended or well tested.

# Precedence System

This filesystem uses improved logic to resolve conflicts between pk3s (i.e. when multiple pk3s contain conflicting resources). It follows a logical pattern that is roughly (mod paks > core game paks > current map pak > other paks), whereas the original filesystem uses a more archaic system where the base precedence for image-like content is approximately (shaders > tga images > jpg images) regardless of the filename/directory of the pk3s.

In short what this means is that in the standard Q3 filesystem, as you add more paks (such as custom maps) manually or through ingame downloads, there is an increased risk of conflicts that cause broken textures, wrong textures, or other errors. This is because there are various means by which any pk3 can override any other pk3 and the more pk3s are installed, the higher the chance of conflicts. This filesystem is able to prioritize the current mod, core game paks, and current map over other pk3s so the game is stable like a clean install no matter how many pk3s are installed.

NOTE that certain types of content such as crosshairs, enhanced texture mods, and VMs that work by overriding the ID paks will not work out of baseq3 due to the new precedence policy. These mods are still supported in this filesystem, but they need to be manually enabled by placing their pk3s in a directory called "basemod" instead of baseq3. This system gives the user control over which pk3s are allowed to modify the game and makes the game much more stable overall.

For a more technical overview of the precedence changes refer to this [precedence chart](new-filesystem-precedence.png).

# Mod Settings Option

Currently ioquake3 stores a separate copy of the game settings for every mod. Whenever the mod changes, either by a user action or connection to a server, the settings are reset using the q3config.cfg from the new mod directory. This can lead to unexpected effects for users, such as graphics settings going haywire and changing resolutions when connecting to a server. This can be especially frustrating given that many mods are mostly server side and don't need any custom settings anyway.

This project introduces a command line cvar called "fs_mod_settings", with the following effects:
- fs_mod_settings 0: All settings are loaded and saved in the q3config.cfg in baseq3, regardless of the current mod. Every mod uses the same settings. This is closer to the original q3 behavior.
- fs_mod_settings 1: Existing ioquake3 behavior, with separately stored settings for each mod.

I favor fs_mod_settings 0 as the default value, because it's usually easier to manage a few settings that need to be changed between mods than to have every setting change between mods. Mods that require custom settings will be fine if they use deconflicted cvar names to store their settings. It appears most mods work under this setting without significant issues.

# Download Directory Support

The "fs_download_mode" setting can be used to customize pk3 download handling on the client. It supports the following values:

- 0 (default): Pk3 files are downloaded to the normal location.
- 1: Pk3 files are saved to the "downloads" folder within the target mod directory. For example, "baseq3/somefile.pk3" will be rewritten to "baseq3/downloads/somefile.pk3". Pk3s in the downloads folder are deprioritized compared to pk3s directly in the mod directory.
- 2: Same as 1, but also disables loading certain less secure content (cfg files and qvm files that don't match a list of trusted mod hashes) from the downloads folder.
- 3: Same as 2, but blocks all qvm files from the downloads folder regardless of hash.

A setting of 1 can help organize downloaded pk3s and reduce the chance of pk3 conflicts. A setting of 2 or 3 can help increase security when downloading from untrusted servers, at the expense of possible mod compatibility issues.

# Source Directory Options

A new cvar is introduced called "fs_dirs", which can be set from the command line to adjust which source directories the game uses to load/save files.

Windows / Mac:
The default is "*fs_homepath fs_basepath fs_steampath fs_gogpath". This means that homepath is the write directory, indicated by the asterisk, and the other locations are used for reading. The specific paths are controlled by the "fs_homepath", "fs_basepath", "fs_steampath", and "fs_gogpath" cvars, respectively.

Linux:
The default is "*_xdg_home fs_homepath fs_basepath fs_steampath fs_gogpath". "_xdg_home" is a special keyword that places files according to the [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/latest/). With the default fs_dirs value, xdg home is used for writing, indicated by an asterisk, the legacy homepath (fs_homepath) is used a secondary read location, and the install location (fs_basepath) is used as a third read location.

Notes:
- You can specify arbitrary cvars to use as source directories, instead of the default ones like fs_basepath and fs_homepath, but the specified cvars must be set on the command line along with fs_dirs in order to take effect.
- You can set an asterisk on multiple directories. The additional directories will be used as backup write directories if the first one fails a write test.
- The write directory selected will always be treated as the highest precedence read directory.
- If no directory passes a write test, or no write directory was set (no asterisks), the game will run in read-only mode.

Examples:
- +set fs_dirs fs_homepath fs_basepath: Read-only mode with homepath taking precedence over basepath in the event that both directories contain files with the same name.
- +set fs_dirs *fs_basepath *fs_homepath: Try to use fs_basepath as the write directory, but fall back to fs_homepath if basepath is not writable. Both basepath and homepath will be readable, with whichever directory is used as the write directory taking precedence.

# Inactive Mod Support

This filesystem supports loading files from all mod directories, not just the current active mod. This can help smooth out discrepancies between server directory configurations and allow maps to work correctly even if they have dependencies that, for one reason or another, are located in the wrong mod directory.

The precedence system ensures that files located in the current mod and basegame directories always have first priority. Inactive mods are only accessed as a last resort if files cannot be found anywhere else.

There are two cvars used to control this feature:
- fs_read_inactive_mods: Applies to file reading operations used to load content like models, textures, and sounds. (default: 1)
- fs_list_inactive_mods: Applies to file listing queries used by the ingame map and model menus. (default: 1)

Both cvars support the following settings:
- 0: Disabled; all files from inactive mods blocked
- 1: Enabled only for ID paks (including missionpack) and files on the connected server's pure list
- 2: Enabled; all files from inactive mods allowed

# Semi-Pure Server Support

This feature allows clients to load external content (particularly player models) when playing on an otherwise pure server. It retains the stability and compatibility benefits of a pure server, because the pure pk3s are prioritized over other pk3s, but is less restrictive when it comes to allowing content that doesn't exist in any of the pure pk3s.

To enable this feature, set sv_pure to 2 on the server. This will only affects clients using the new filesystem. Clients using the original filesystem will function the same as if sv_pure is 1.

# Server Features

## Download & Pure List Configuration

Two new cvars, "fs_download_manifest" and "fs_pure_manifest", are added to allow customizing which pk3s are added to the download and pure lists when running a server. Both cvars use a space-seperated list of selector rules that can be either specific pk3s in ```<mod>/<name>``` format or one of the following keywords:

- ```#mod_paks``` - Selects all paks from the current active mod.
- ```#base_paks``` - Selects all paks from com_basegame (baseq3).
- ```#inactivemod_paks``` - Selects all paks in inactive mod directories (not baseq3 or the current mod) that are enabled by the fs_read_inactive_mods setting on the server. Under the default fs_read_inactive_mods setting of 1 this will select only the missionpack paks pak0-pak3.
- ```#currentmap_pak``` - Selects the highest precedence pk3 containing the bsp of the current running map.
- ```#cgame_pak``` - Selects the highest precedence pk3 containing cgame.qvm file.
- ```#ui_pak``` - Selects the highest precedence pk3 containing the ui.qvm file.
- ```#referenced_paks``` - Selects paks accessed during the loading process on the server. Only selects paks from com_basegame (baseq3) and mod directories, not inactive mods or servercfg directories.

Note that the precedence of servercfg directories is ignored for pure/download list purposes. Servercfg directories are not explicitly blocked from pure/download lists, but are treated the same as inactive mod directories and only referenced as a last resort if there is no other pk3 matching a selector rule.

#### Default download manifest

The default download manifest selects all the paks from the current mod directory, as well as the current cgame and ui paks, and the current map pak. The #referenced_paks rule is currently added for potential mod compatibility reasons, but in most cases is redundant to the other rules and can be dropped without issue.
```
set fs_download_manifest #mod_paks #cgame_pak #ui_pak #currentmap_pak #referenced_paks
```

#### Reduced download manifest example

Some server configurations have a lot of maps or optional mod files in the mod directory. This can lead to clients having too many files to download, since the default behavior is to place everything in the mod directory into the download list. To avoid this, you can specify only the core mod files that clients require instead of using the #mod_paks rule. For example, here is a reduced download manifest for the OSP mod.
```
set fs_download_manifest osp/zz-osp-pak3 osp/zz-osp-pak2 osp/zz-osp-pak1 osp/zz-osp-pak0 #currentmap_pak
```

#### Default pure manifest

The default pure manifest selects every pak normally available to the game.
```
set fs_pure_manifest #mod_paks #base_paks #inactivemod_paks
```

#### Reduced pure manifest example

Pure servers with a large number of pk3s installed (e.g. 250+) can run into problems with the pure list overflowing. To avoid such issues you can replace the #base_baks rule in the pure manifest with specific core paks and add the #currentmap_pak to handle the current map. Note that any auxiliary paks in baseq3 required by maps or mods, as well as optional content like player models, will need be included manually as well.
```
set fs_pure_manifest #mod_paks baseq3/pak8 baseq3/pak7 baseq3/pak6 baseq3/pak5 baseq3/pak4 baseq3/pak3 baseq3/pak2 baseq3/pak1 baseq3/pak0 #currentmap_pak
```

### Advanced Features

#### Block command

In some cases it can be convenient to exclude a certain pk3 that would otherwise be selected by one or more rules within a manifest. This can be accomplished by using the "&block" command followed by a normal selector rule. All pk3s selected by the rule will be blocked, based on hash, from being selected by any subsequent rules. In this example, the "mod/somemap.pk3" file can be selected by the #currentmap_pak rule, but not subsequent rules such as #mod_paks because they come after the block command.
```
set fs_download_manifest #currentmap_pak &block mod/somemap #mod_paks #cgame_pak #ui_pak #referenced_paks
```
It is also possible to clear the block list during manifest processing by using the "&block_reset" command.

#### Pk3 specifier wildcards

Pk3 specifiers can contain wildcards to select a range of pk3s. Currently supported wildcards are '*' (matches 0 or more characters) and '?' (matches 1 character). This example shows a variation of the OSP mod download manifest using a wildcard. Warning: If your specifier happens to contain the character sequence ```/*``` you will need to enclose the manifest in quotes as shown here, or the set command will parse it as a comment.
```
set fs_download_manifest "osp/zz-osp-* #currentmap_pak"
```

#### Manual hash specifiers

It is possible to specify pk3s by hash, to support conditions where the file may not physically exist on the server or may exist under a different name. If the file does not physically exist it can't be used for UDP downloads, but it can be used for pure lists, as well as download lists on HTTP-only servers (e.g. with sv_dlURL active and sv_allowDownload set to 0). To use this feature, specify paks using the format ```<mod>/<name>:<hash>```. The hash can be specified in either signed or unsigned integer format.
```
set fs_pure_manifest #mod_paks #base_paks #inactivemod_paks baseq3/md3-bender:-722067772 baseq3/md3-laracroft:1134218139 baseq3/md3-spongebob:-871946717
```

#### Cvar importing

Manifests can import other cvars using the "&cvar_import" command followed by a cvar name. The contents of the specified cvar are parsed the same as if they were entered in the place of the &cvar_import command. This can be helpful for organization or to load certain commands in both the pure and download manifests. As a simple example the following commands are equivalent to the default pure manifest.
```
set custom_cvar #base_paks
set fs_pure_manifest #mod_paks &cvar_import custom_cvar #inactivemod_paks
```

#### Manual pure list ordering

The order of the pure list determines the precedence of files on clients. Normally the server sorts the pure list according to filesystem precedence conventions, rather than the order in the pure manifest, but there may be special conditions where it is useful to force a certain order in the pure list. This can be accomplished by separating sections in the pure manifest with a dash. In this example baseq3/somefile.pk3 will be the first entry on the pure list and have the highest precedence, regardless of where it stands in the normal filesystem ordering. Note that if the same pk3 is selected by multiple rules, its position will be determined by the first rule that selected it.
```
set fs_pure_manifest baseq3/somefile - #mod_paks #base_paks #currentmap_pak
```

#### Inactive mod directory support

Paks from inactive mod directories can be added to the pure and download manifests, but clients will need to be using this filesystem or an engine with equivalent inactive mod support in order to use them. This can be used to support special configurations involving a hybrid of multiple mods. It is currently only recommended to use inactive mod pk3s in the download manifest if you assume all clients have inactive mod support, because otherwise other clients will encounter errors attempting the download.

## Servercfg Support

The servercfg system provides some advanced features to help control the local configuration of servers, separate from client-linked settings such as fs_game. It is currently only supported for servers using the dedicated server binary.

### Basic Configuration

In the default configuration, the "servercfg" directory works like an always-active mod, similar to basemod. It overrides all other mod directories including basemod and fs_game.

The servercfg directory can be used as an alternative to baseq3 or the fs_game directory for storing server-side mod and configuration files. This has the advantage of ensuring the server-side config remains stable and has maxiumum priority on the server even if the fs_game setting and contents change. It also prevents server-side pk3s from being auto-downloaded to clients by default.

To use this feature, simply create the "servercfg" directory on the server and place server-side mods and config files in it. As long as you are using a dedicated server binary compiled with the new filesystem, the servercfg directory will be enabled and loaded by default.

### Custom Servercfg Directories

The name of the servercfg directory is controlled by the "fs_servercfg" cvar. The default is "servercfg" but a different directory can be set either at server startup or while the server is running. Server config scripts can modify this setting (typically preceding a map launch) to switch between mods on a running server.

Multiple servercfg directories can also be combined by separating them with a space in fs_servercfg. The first directory specified has highest precedence and overrides later directories in case of conflicts. Example: ```set fs_servercfg server_mod server_base```

### Custom Write Directory

The standard fs_servercfg setting does not change the directory for game module logs and other files written by the server. Files will still be written to the standard locations (e.g. fs_game or com_basegame).

The setting "fs_servercfg_writedir" can be set to force a certain write directory for the server. This could be the same directory as the primary fs_servercfg directory, but it is possible to use any directory name. Example: ```set fs_servercfg_writedir serverlogs```

## Restricted Pk3 Sources

In some cases it can be useful for pk3s to be installed on a server, but with restrictions on what the server can load from them. Currently two types of restrictions are supported:

- "nolist": The pk3 is loaded on the server, but excluded from file list operations. This generally means maps can still be loaded from the pk3, but certain scripts such as bot files are not loaded.
- "refonly": Prevents the server from loading any contents of the pk3, but it can still be used to populate pure and download lists.

To use these modes, create a folder called "nolist" or "refonly" within a mod directory, and move pk3s into it. For example, to apply the nolist restriction to a pk3 located at "baseq3/mymap.pk3", move it to "baseq3/nolist/mymap.pk3". Note that manifest rules should still refer to the short path, e.g. "baseq3/mymap".

# Developer & Debugging Notes

## Shader Precedence

In this filesystem, shader precedence follows the same rules as normal filesystem precedence, e.g. a shader in pak2.pk3 will override a shader in pak1.pk3, and a shader in a mod will override a shader in baseq3. However, the original filesystem uses the opposite precedence when shaders are defined in .shader files with different names, which can make things confusing for mod authors. Here are some tips for mod authors working with shader definitions:

- To update shaders in an existing pak from your mod to a new version, copy the .shader file from the earlier pak to the new pak, keeping the same filename. Then modify the shader(s) in the new file. The original filesystem will only load the newer file, so make sure it contains all the shaders from the original file as well.

- If you wish to override a shader from paks in baseq3, such as the ID paks, you need to copy the most recent version of every .shader file containing that shader into your mod, then modify the shader within those files. If you simply define the shader without following these steps, it will work on the new filesystem, but not the original filesystem.

- Conversely, make sure you don't accidentally define unwanted shaders that conflict with the ID paks or earlier paks from your mod in arbitrary .shader files. These will be overridden in the original filesystem but will take effect on the new filesystem, which could lead to problems.

## Cache Mechanisms

This filesystem adds two types of caches to improve load times, an index cache and a memory cache.

The index cache stores pk3 index data in a file called fscache.dat to reduce the initial game startup time. Cached data is matched to pk3s by filename, size, and timestamp, so it shouldn't cause problems when pk3s are modified. If you suspect the cache could be causing a problem, you can delete the cache file or disable it by setting "fs_index_cache" to 0 on the command line.

The memory cache is used to keep previously accessed files in memory for faster access and reduce load times between levels. The size of this buffer is controlled by the "fs_read_cache_megs" cvar. The default is currently 64 for the client and 4 for the dedicated server. This value can be set to 0 to disable the cache altogether.

## Debugging Cvars

This project introduces some new cvars that can be set to 1 to enable debug prints.

- fs_debug_state: logs changes to the primary filesystem state such as the current mod dir and server pure list
- fs_debug_refresh: logs warnings that come up during file indexing phase; best set from the command line and combined with fs_index_cache 0
- fs_debug_fileio: logs file reading/writing activity
- fs_debug_lookup: logs file index lookup activity
- fs_debug_references: logs pk3 referencing activity (used for downloads and pure lists)
- fs_debug_filelist: logs directory listing calls

## Debugging Commands

The following commands can be used to get an idea of where resources will come from in the filesystem. They generate a list of resources sorted from highest to lowest precedence. The first resource is typically what will be used by the game.

- find_file <file name>: General purpose file lookup. Example: find_file maps/q3dm11.bsp
- find_shader <shader/image name>: Locates both shaders and images with this name. Example: find_shader textures/base_floor/techfloor
- find_sound <sound name>: Locate sound resources with this name. Example: find_sound sound/weapons/noammo
- find_vm <vm/dll name>: Locate vm or dll resources with this name. Example: find_vm cgame

Once you run one of the above commands, you can use the "fs_compare" command to find why one element in the list was chosen over the other. For example, run "fs_compare 1 2" to check why the top element was selected over the second element. The list from one previous find command is stored in memory for the purpose of supporting this command.

# Known Issues

This project is fairly well developed at this point, so there are few significant known issues. If you find any new issues, feel free to email me or open an issue on GitHub.

- This is currently an engine only modification, and does not remove limits in the UI on the number of maps and models that can appear in the menus. The UI code will need to be changed in mods to raise these limits. You can still load maps and models through the console or by connecting to a server even if they don't appear in the menu.

- Limited testing has been done on less common platforms and build environments or on big endian systems. Testing help and feedback for such platforms is appreciated.

- Only the makefile and msvc12 project files are updated to use the new filesystem. Some of the other project files and build scripts aren't updated yet. It appears most of these files are in an unmaintained state in ioquake3 anyway.
