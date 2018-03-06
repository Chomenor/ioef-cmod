This file provides additional documentation for the design and technical details of this project. It assumes you are already familiar with the content of the main readme.md file. If you need an overview of how file handling works in ioquake3 in general, skip to the appendix at the end of this document.

# Project Changes

This is an overview of some of the main design changes in this project compared to the original filesystem.

**original filesystem:** Primarily based in a single source file, files.c.  
**new filesystem:** Divided into multiple source files under the filesystem and filesystem/fscore directories. The core component handles basic file indexing and access and can be compiled separately from the game. The main component serves as an interface between the core and the game.  
**reason for change:** The modular design and separate source files make the code easier to work with as new features and capabilities are added. Allowing the filesystem core to be compiled separately makes it useful for standalone utilities and tests.

**original filesystem:** Files inside and outside pk3s are handled separately, and most code that iterates over files has separate cases for both types of files.  
**new filesystem:** All files are abstracted behind the fsc_file_t type, so for most purposes they can be treated the same regardless of how they are located.  
**reason for change:** Easier to add support for new file storage methods and easier code maintenance in general.

**original filesystem:** The ordered priority of files is determined when the filesystem is refreshed.  
**new filesystem:** The file index is unordered, and it is up to the file lookup, listing, and reference modules to resolve conflicts at call time.  
**reason for change:** It separates the indexing and precedence handling code, allows the precedence logic to be customized for each module, and makes it easier to implement file-level precedence debugging features. It also improves load times because it is no longer necessary to resort the entire filesystem on every level change or server connection.

**original filesystem:** Shaders are indexed by the renderer.  
**new filesystem:** Shaders are indexed by the filesystem, and new API calls are added to allow the renderer to access shaders by name.  
**reason for change:** It allows shaders to be sorted with the same precedence logic used by the filesystem and compared alongside images instead of always overriding them. It also improves load times significantly because shaders don't have to be reindexed on every map change and can take advantage of the index cache file.

# Source Files

The source code of this project is divided into the following files and sections, under the code/filesystem directory:

- fscore/*.c: Contains the core file indexing and reading functionality, OS interface functions, and some utility functions.

- fs_main.c: Handles the filesystem initialization and shared state data (file index, current mod, pure list, cvars, etc.)

- fs_lookup.c: Locates game resources (files and shaders) using the file index, resolving precedence when necessary.

- fs_fileio.c: Handles reading and writing of files on the disk.

- fs_download.c: Handles parts of the client-side download process, such as determining which files need to be downloaded and performing the final save operation after the download completes. The download itself is still handled in the client code.

- fs_filelist.c: Supports directory listing functions. Primarily used to populate map/model menus in the UI.

- fs_reference.c: Used to generate the pure list and download list when hosting a server. Also handles pure validation requirements when connecting to legacy pure servers.

- fs_commands.c: Supports user-entered console commands, e.g. "find_file" and "dir".

- fs_misc.c: Utility functions used by the rest of the filesystem.

# Filesystem Core

The primary role of this component is file indexing, which allows iterating all the files accessible to the game both on the disk and in pk3s, as well as additional resources such as shaders. The file index is stored in the fsc_filesystem_t structure.

```
typedef struct fsc_filesystem_s {
	// Support
	fsc_stack_t general_stack;
	fsc_hashtable_t string_repository;

	// Main Filesystem
	fsc_hashtable_t files;
	int refresh_count;

	// Iteration
	fsc_hashtable_t directories;

	// Shaders
	fsc_hashtable_t shaders;

	// Crosshairs
	fsc_hashtable_t crosshairs;

	// PK3 Hash Lookup - Useful to determine files needed to download
	fsc_hashtable_t pk3_hash_lookup;

	// Custom Sourcetypes - Can be used for special applications
	fsc_sourcetype_t custom_sourcetypes[FSC_CUSTOM_SOURCETYPE_COUNT];

	// Stats
	fsc_stats_t total_stats;
	fsc_stats_t active_stats;
	fsc_stats_t new_stats;
} fsc_filesystem_t;
```

- general_stack: Handles the memory allocation for the filesystem index. All fsc_stackptr_t pointers used throughout the index need to be dereferenced against this structure by calling the fsc_stack_retrieve function. This is usually abbreviated via the STACKPTR macro, which under fscore typically references an "fs" variable local to the calling function, and in the main filesystem references the global "fs" variable defined in fs_main.c.

- string_repository: Used to allocate string storage from the general_stack, but in a deduplicated fashion so the same string is only stored once.

- files: Main hashtable for all the files in the filesystem. The hash is based on the directory and name of the file, but not the extension. Note that files are never removed from this structure, so pointers stay valid, but they can be "disabled" if they are no longer available on the disk. When you iterate files you typically need to call fsc_is_file_enabled to check which files are actually available.

- refresh_count: Controls which files are disabled. A refresh count value is stored for each file in the index; when it matches this value the file is considered enabled, otherwise it is considered disabled. To "reset" the filesystem and disable all files you can increment this value, although in practice you should use fsc_filesystem_reset which also updates the stat counters.

- directories: Used to optimize file listing operations. For example, if you wanted to list just the files under "models/players/sarge" using the files hashtable, you would have to iterate over the entire filesystem to find them. This structure allows the file listing code to look up a specific directory and only iterate files under that directory.

- shaders: Stores an index of all shaders available in the game. The hash is based on the name of the shader.

- crosshairs: Stores a list of crosshairs available in the game. This structure is currently not used, but may be useful for future features or derived projects.

- pk3_hash_lookup: Stores an index of pk3 files based on the pk3 hash. Pk3 files are listed in the regular files hashtable as well, but this structure makes it faster and more convenient to find them by hash.

- custom_sourcetypes: Rudimentary support for adding files from custom sources, such as from a custom download manager system. May be useful for future features or derived projects.

- total_stats, active_stats, new_stats: File tallies that can be used for things like info messages and to optimize hashtable sizes.

You should be able to find plenty of examples of how these fields are used and the hashtable iteration syntax throughout the filesystem code.

## File Objects

Files are represented by the fsc_file_t structure, defined in fscore.h. It contains the qpath, which is divided into directory, name, and extension components to save memory and make it easier to work with.

There are two basic types of files, those directly on the disk and those inside pk3s. The sourcetype field in fsc_file_t indicates the type. FSC_SOURCETYPE_DIRECT files can be cast to fsc_file_direct_t to access more fields and FSC_SOURCETYPE_PK3 files can be cast to fsc_file_frompk3_t. Note that many attributes are not defined directly in fsc_file_frompk3_t since they can to be obtained from the parent pk3 using the source_pk3 field.

The filesystem core provides a number of utility functions that work on files of either sourcetype (or custom sourcetypes if they are set up). These include fsc_extract_file, fsc_is_file_enabled, fsc_get_mod_dir, and fsc_file_to_stream.

## Initialization

The simplest way to initialize the index and populate it with files is to use these steps:

1) Allocate an fsc_filesystem_t structure
2) Call fsc_filesystem_initialize on it
3) Call fsc_load_directory on each source directory

For example, this code loads files from the "source1" and "source2" directories.

```
void filesystem_test(void) {
	void *source1_path = fsc_string_to_os_path("source1");
	void *source2_path = fsc_string_to_os_path("source2");
	fsc_filesystem_t fs;

	fsc_filesystem_initialize(&fs);
	fsc_load_directory(&fs, source1_path, 0, 0);
	fsc_load_directory(&fs, source2_path, 1, 0);

	fsc_free(source1_path);
	fsc_free(source2_path);

	// Do something with filesystem...
}
```

Note the 0 and 1 values to the source_dir_id parameter in fsc_load_directory. These values are not used internally by the filesystem core, but they get stored in the file structures so you can tell which source directory the file came from later.

## File Refresh

Once the filesystem is loaded, you can refresh it to update files that are changed or added on the disk. Simply call fsc_filesystem_reset to "clear" the filesystem and disable all files, then repeat the calls to fsc_load_directory that were used in the initialization.

```
fsc_filesystem_reset(&fs);
fsc_load_directory(&fs, source1_path, 0, 0);
fsc_load_directory(&fs, source2_path, 1, 0);
```

This performs very quickly because pk3s already in the index, matched by name, size and timestamp, will simply be re-enabled rather than reindexed from scratch.

## Index Cache

The index cache system works by creating a memory image of all the filesystem structures which can be written out to a file and loaded back on subsequent startups. Files are then matched to the cache data using the normal refresh process. This approach has very good performance on startup, since it is simply a direct dump from the cache file into memory.

To use the index cache, follow these steps:

1) Create the cache file using fsc_cache_export_file. A good time to do this is usually right after the filesystem has been initialized and fsc_load_directory has been called on each source directory. The output filesystem will be reconstructed to only contain active files, so you don't have to worry about the cache being cluttered with old files.

2) On subsequent startups, import the cache file using fsc_cache_import_file, in place of fsc_filesystem_initialize. This initializes the filesystem with all the cached elements starting in a disabled state. If this call fails (return value 1), fall back to calling fsc_filesystem_initialize instead to get an empty filesystem.

3) Call fsc_load_directory on each source directory like normal. If the files successfully match the cached elements, the cached elements will be enabled, otherwise new elements will be generated.

The cache file is not considered secure against malicious tampering, so it is important to store it in a location such as base source directory where running VMs don't have write access to it.

# Filesystem Main (fs_main.c)

This component handles the filesystem initialization and refresh process, and holds the primary filesystem state including the cvars, source directories, index, current mod, and pure list from the connected server. By convention, the rest of the filesystem code can access the variables defined in fs_main, but they should only be modified from within fs_main.

## Initialization Process

The initialization function, fs_startup, is called only once when the game starts. It initializes the filesystem cvars and configures the source directories, initializes the index using the cache file if possible, performs an initial refresh, and writes an updated cache file if enough new files were added to justify it.

## Refresh Process

The refresh function, fs_refresh, can be called at any time to check for new files on the disk and update them into the index. It uses the process described in the filesystem core section, which is to call fsc_filesystem_reset followed by fsc_load_directory on each source directory. It does not change any part of the filesystem state other than adding new files to the index.

## Source Directory Handling

The source directory names and paths are stored in the "fs_sourcedirs" array which is initialized in fs_startup and remains constant afterwards. The source directories are ordered by precedence with index 0 being highest priority. Each fs_source_directory_t entry contains the name of the source dir (used to identify it in fs_dirs), the path cvar, and whether the source directory is active.

You can tell which source directory a file came from by checking its "source_dir_id" field, which represents the index to the fs_sourcedirs array.

The write directory is always fs_sourcedirs[0], unless the filesystem is in read-only mode. Before doing write operations check the fs_read_only value and abort if it is true, or use the fs_generate_path_writedir function which includes the check automatically.

## Mod Directory Handling

Forms of the mod directory are stored in 3 locations:

- fs_main.c->current_mod_dir: This is the functioning active mod directory used by all filesystem code and the FS_GetCurrentGameDir function. Note the conventions that when no mod is set, current_mod_dir is an empty string but FS_GetCurrentGameDir returns com_basegame.

- fs_game cvar: This cvar is set by CL_SystemInfoChanged and by VMs. It is mainly a feeder value to current_mod_dir rather than used directly, and is transferred to current_mod_dir (with sanity checks applied) when fs_set_mod_dir is called.

- cl_main.c->cl_oldGame: This is used to revert a server-set fs_game value when disconnecting from a server.

When fs_mod_settings is enabled, settings are loaded from the config file in the current mod dir. When the mod dir changes, Com_GameRestart must be called to clear the old settings and load the new config file. Care must be taken to avoid running fs_set_mod_dir separately from Com_GameRestart, as this could lead to the wrong config file being overwritten with the wrong settings. There are currently two ways for current_mod_dir to change:

- Through Com_GameRestart->fs_set_mod_dir.

- Through FS_ConditionalRestart->fs_set_mod_dir, but only if fs_mod_settings is disabled. If fs_mod_settings is enabled it will go through Com_GameRestart instead of calling fs_set_mod_dir directly.

# File Lookup (fs_lookup.c)

The file lookup system handles most requests for game content. It uses two main steps, a "selection" phase to locate elements matching the query, and a "precedence" phase to determine the best element to use if the selection phase returned more than one element. The code is divided into the following sections, albeit in a slightly different order than presented here.

- Wrapper functions: These functions handle a request from the game code for a specific type of resource, and construct a lookup_query_t (or two in the case of a combined vm/game dll lookup). The query processing functions are then called to either produce an output resource (normal mode) or print debug data (debug mode).

- Query processing functions: These functions take one or more lookup_query_t inputs and call perform_selection on each of them to generate a list of lookup resources. They then either identify and return the best resource using the precedence functions (normal mode) or sort and print the list of resources to the console (debug mode).

- Resource construction: This section is used to convert a file or shader to a "lookup resource" (lookup_resource_t) which contains extra data used to sort the element.

- Selection: The perform_selection function takes a single lookup_query_t as an input, finds all the elements that match the criteria, converts them to lookup resources, and adds them to the target selection_output_t. The selection_output_t is basically an auto-expanding array of lookup resources.

- Precedence: This section provides the comparison and sorting functions to select the best element from a list of lookup resources.

## Precedence Rules

This is a list of the precedence rules ordered from highest to lowest priority. The first rule that has a non-neutral result determines the result of a comparison between two resources.

- resource_disabled: There are several conditions, such as a file not on the pure list, where resources are deemed to unusable in the selection process. Instead of just skipping these resources outright they are assigned a "disabled" string in the lookup resource, which allows them to go through the precedence process and appear in the debug outputs. This rule ensures such resources get sorted to the bottom of the resource list.

- special_shaders: Prioritizes "special" shaders (those from a system pak, server pak list, current mod dir, or basemod dir). Resources in these locations normally override resources outside them anyway, but this rule causes explicit shaders to override images when they are both in one of these locations. This is helpful in some special cases. For example, suppose the system paks provide a shader and image both with the same name, in which the shader invokes the image, and a mod provides a modified version of the image only. By itself, the basemod_or_current_mod_dir rule would cause the mod image to override the shader instead of just the image, which is probably not the mod author's intention nor the original filesystem behavior. This rule increases the areas where shaders override images to better handle these kind of conditions.

- server_pak_position: Prioritizes paks according to the order of the server pak list (sv_paks) when connected to a pure or semi-pure server.

- basemod_or_current_mod_dir: Prioritizes current_mod_dir (which generally corresponds to fs_game) and basemod over other mods.

- system_paks: Prioritizes the system paks (e.g. pak0-pak8.pk3 in the case of Quake 3) which are defined by hash in fspublic.h.

- current_map_pak: Prioritizes the pak, if any, where the current map was loaded from. That value is stored under current_map_pk3 in fs_main.c.

- inactive_mod_dir: De-prioritizes resources from inactive mod dirs; i.e. random mod dirs that are not com_basegame, basemod, or the current mod dir.

- downloads_folder: De-prioritizes resources from paks within a "downloads" folder.

- shader_over_image: Prioritizes explicit shaders over default shader images. Note the placement behind the system_paks, current_map_pak, inactive_mod_dir, and downloads_folder rules, as those are cases where it is desirable to let images override shaders.

- dll_over_qvm: Prioritizes game dlls over qvms. Note that this rule is only relevant if vm_game, vm_cgame, or vm_ui is set to 0 (VMI_NATIVE), as otherwise dlls will not be part of the query.

- direct_over_pk3: Prioritizes resources directly on disk (i.e. not in a pk3) over ones in a pk3.

- pk3_name_precedence: Handles the alphabetical precedence of pk3s. Paks with names starting with z have higher precedence than those starting with a. The exact character precedence is defined in get_string_sort_table in fs_misc.c.

- extension_precedence: Prioritizes tga files over jpg, wav over mp3, etc. The actual order is determined by the order in the query, so refer to the order the extensions are listed in shader_or_image_lookup and fs_sound_lookup.

- source_dir_precedence: Prioritizes the source dirs (e.g. homepath, basepath, etc.) according to their position in fs_sourcedirs.

- intra_pk3_position: Prioritizes resources with a higher offset in the pk3 file, to support existing conventions for shaders defined multiple times in different shader files within the same pk3.

- intra_shaderfile_position: Prioritizes shaders with a lower offset in the shader file, to support existing conventions for shaders defined multiple times within the same shader file.

- case_match: Prioritizes files that match the case of the query over others, e.g. baseq3/q3config.cfg is prioritized over baseq3/q3conFig.cfg or baSeq3/q3config.cfg (on case-sensitive filesystems).

## Downloaded VM Restrictions

The fs_restrict_dlfolder setting of 2 is used to restrict the loading of VMs from the downloads folder to those that match a set of known trusted hashes. If a VM fails to match a verified hash, the current behavior is to throw an ERR_DROP when connected to a pure server, and fall back to the best available qvm when not on a pure server. This helps provide an informative error message in the pure server scenario (where a specific qvm is likely required) but also avoids a single nonessential pk3 causing a mod to permanently error out on non-pure servers.

The verification is handled in the query processing functions, after the resources have already been sorted by the normal precedence rules. This allows the hash calculation to only be done on the highest precedence VM and working down if necessary, instead of having to calculate the hash for every potential VM.

# File Reading / Writing (fs_fileio.c)

This component handles file read/write operations based on a specific path on the disk.

## Path Handling Functions

The fs_generate_path function provides a standardized method for generating paths anywhere in the game code. It takes up to 3 path inputs, separates them with a slash character, and writes them to the output buffer. Sanity checks for overflows, special characters, relative paths, and unsafe extensions are performed, but can be suppressed for individual path components using the flag parameters. If the path creation fails, the function returns 0.

Creation of new directories on the disk is handled through fs_generate_path and is enabled by passing the FS_CREATE_DIRECTORIES or FS_CREATE_DIRECTORIES_FOR_FILE flags.

There are two other generate path variants: fs_generate_path_sourcedir, which creates a path starting with a specific source directory, and fs_generate_path_writedir, which creates a path starting with the write source directory and fails if the filesystem is in read-only mode.

## Read Cache

The read cache is a circular buffer used to store file contents and reuse them when possible. The size of the buffer is controlled by the fs_read_cache_megs cvar. To avoid repeatedly accessed files rolling off the buffer, the concept of "stages" is used. The stage is incremented on each map load, and files are recopied to the front of the buffer once per stage.

## Data Reading

The fs_read_data function takes either an fsc_file_t or an exact path and reads the entire file into memory. The allocation will be from the cache if possible, otherwise it will use system malloc to avoid any overflow risk to other memory systems. Either way, fs_free_data is used to free the result.

## Handle Management

The filesystem uses its own file handle type, fileHandle_t, which can represent several types of handles. Cache read handles use file data from fs_read_data internally. This form of read handle supports any type of file, including files inside pk3s. Direct read, write, and pipe handles are abstractions to the OS library functions which are only valid for files directly on the disk.

An ownership value can be set on file handles which is used to prevent VMs from accessing file handles they didn't create or leaving file handles open when they shutdown.

# Downloads (fs_download.c)

When connecting to a server, the download list is received in CL_SystemInfoChanged which calls fs_register_download_list. Each hash/name value is converted to a download_entry_t, which contains some extra path format data, and placed in the next_download linked list. Once the download entries are set up, each potential download is executed by the client as follows:

1) cl_main.c->CL_NextDownload calls fs_advance_next_needed_download, which pops download entries from next_download to current_download until either current_download represents a needed file or no download entries remain.

2) cl_main.c->CL_NextDownload calls fs_get_current_download_info to get the current download filename. If a download is returned, CL_NextDownload can either start the download or if there is an error, skip it and loop back to calling fs_advance_next_needed_download.

3) If the client successfully completes a download, fs_finalize_download is called to perform final sanity checks and move the download to the save location.

The main rationale for the download handling being split between the filesystem and client is for ease of integration with new projects. The file-related aspects of the download are handled in the filesystem, which makes it easy to pull in without any extra integration work. Meanwhile the code in CL_NextDownload retains much of its original structure, so if a given project's implementation deviates from ioquake3, it will be easier to map those changes onto the new filesystem.

## Attempted Download Tracking

The download system stores two sets of hashes, attempted_downloads and attempted_downloads_http, which are used to prevent trying to redownload the same file twice in the same session. Attempted downloads are handled in the following manner:

- UDP download attempted: Download will be skipped in fs_advance_next_needed_download.

- http download attempted only, currently in cURL disconnected mode: Download will be skipped in fs_advance_next_needed_download. Once a normal connection to the server is reestablished an entirely new download list will be received through fs_register_download_list and a UDP download can be attempted.

- http download attempted only, currently not in cURL disconnected mode: Download will be accepted in fs_advance_next_needed_download, and the output parameter curl_already_attempted from fs_get_current_download_info will inform CL_NextDownload to only attempt a UDP download for that file.

## Download Scenarios

Here are some download situations/test cases that should be handled by the download system.

**Scenario:** Downloads enabled on client but not server.  
**Original FS:** Attempt UDP download anyway, resulting in error code from server and ERR_DROP  
**New FS:** Skip download and continue with connection.

**Scenario:** cURL download fails.  
**Original FS:** Throws ERR_DROP  
**New FS:** Attempt UDP download if available, and continue with connection.

**Scenario:** cURL download has wrong hash.  
**Original FS:** Throws ERR_DROP, or possible download loop  
**New FS:** Save file if and only if it doesn't match an existing hash, attempt UDP download if available, and continue with connection.

**Scenario:** VM/default.cfg missing until download completes due to pure server settings.  
**Original FS:** Throws ERR_DROP  
**New FS:** Works because there is no restart or default.cfg check until after downloads complete.

Keep in mind that downloads are not always essential, so it's usually desirable to keep trying to connect even if downloads fail. Even on a pure server you do not necessarily need to have every pak on the server, you just can't load paks that aren't on the server.

# File Listing (fs_filelist.c)

This component handles file list requests, which are used primarily by UI menus and debug commands. The file listing process works in roughly these steps:

- A shared function such as FS_ListFilteredFiles or FS_GetFileList is called, which creates a filelist_query_t object and calls list_files.

- The "start directory" in the directory index is determined. This allows only files and subfiles under the directory specified in the query to be iterated, instead of having to iterate every file in the index like the original filesystem.

- The file depths are calculated. This is to emulate the behavior of the original filesystem, which typically only lists files 2 directories deep from the base directory.

- A temporary file set is initialized and temp_file_set_populate is called. It iterates every file under the determined start directory, converts the file (and subdirectories) to strings, checks if the string matches the query criteria, and if so adds it to the file set. A sort key is stored with each file set entry, and duplicate entries are resolved to use the highest precedence sort key.

- temp_file_set_to_file_list is called to sort the file set using the included sort keys and convert it to the array format used by the game.

# Pk3 Reference Handling (fs_reference.c)

This component handles constructing the pure and download lists when hosting a server, and generating the pure validation string which is necessary when connecting to an original filesystem pure server. The main sections are as follows:

- Reference set: This is a hashtable-based temporary structure for storing paks. Only a single pk3 is stored for a given hash, and conflicts are resolved by selecting the pk3 with the higher filesystem precedence. A position field is also stored, which is used by the dash separator feature in manifest strings to alter the reference list sort order.

- Reference list: The reference list is a sortable pak list structure that is generated from a reference set.

- Reference string building: Used to convert a reference list to the hash/filename pure/download strings that are sent to clients.

- Pure validation: The FS_ReferencedPakPureChecksums function is used to satisfy the SV_VerifyPaks_f check on a remote pure server. By default, only the cgame and ui checksums are sent, which is usually faster and more reliable than sending all the referenced paks. If you want the old behavior of sending all the referenced paks, perhaps because you suspect a server may use some nonstandard validation behavior, you can enable it with the fs_full_pure_validation setting.

- Referenced paks: This section is used to record which paks have been accessed by the game. It is used for pure validation when fs_full_pure_validation is set to 1, and for the *referenced_paks selector rule for the download list. Neither feature is essential, so referenced pak tracking could be considered for deprecation in the future.

- Download / pure list building: This section is used to convert both download and pure list manifest strings into reference sets.

- Server download list handling: The fs_set_download_list function is called from SV_SpawnServer during server initialization, which populates the download_paks structure and sets the "sv_referencedPaks" and "sv_referencedPakNames" cvars accordingly. When a download request is received from the client, fs_open_download_pak is called to open the read handle. It searches the download_paks reference set for a file matching the client request and retrieves the real path from the reference set entry. This is safer than opening the path directly and it correctly handles cases where the pk3 name was changed by path sanitization or the pk3 is located in the downloads folder on the server.

- Server pure list handling: The fs_set_pure_list function is called from SV_SpawnServer during server initialization, which sets the "sv_paks" and "sv_pakNames" cvars. If an overflow occurs it will first try skipping sv_pakNames, since it is only used for informational purposes. If that isn't sufficient it will set sv_pure to 0.

# FAQ

**Q:** Why is the new filesystem so much larger (in terms of lines/source files)?  
**A:** Some of the increase is due to new features, performance optimizations, and bugfixes. Other increases are due to design decisions that make the code more modular and maintainable at the expense of line count.

**Q:** Could the improvements of this project be added in ioquake3 incrementally?  
**A:** Most of the changes need to be done at the same time because they require changing the underlying data structures of the file index.

**Q:** Since some new filesystem features can bypass problems with maps, will it cause map developers to release broken maps?  
**A:** Map developers should already test maps before release using a clean install of both ioquake3 and original Quake 3. As long as map developers follow existing good practices there should be no problems.

**Q:** Will the index cache reduce the stability of the game, and can it be corrupted?  
**A:** So far the cache has been extremely stable. At the time of writing I haven't found a single bug or problem that traced to it in over a year since the initial release of this project. Still, if this project were to be integrated in ioquake3 it would make sense to default fs_index_cache to 0 during the initial testing phase just to rule out the cache as a source of problems.

**Q:** How hard is this project to merge with other ioquake3-based projects?  
**A:** It will be some degree of a task, but for projects using a files.c relatively close to ioquake3 it shouldn't be too hard. Most of the code is under the filesystem directory and can just be copied in. I think the improvements are well worth the trouble, especially if people tend to run your game with downloads enabled or a lot of pk3s installed.

**Q:** Why is SV_VerifyPaks_f removed?  
**A:** This function was used as an extra check to make certain kinds of hacks a little bit more difficult in the early days of Quake 3, when it was still closed source. It's not necessary or useful anymore so has been removed to reduce complexity.

**Q:** Is it a good idea to change resource/shader precedence when the current system has been accepted for so long?  
**A:** Compatibility is an important factor here, but I believe the changes are well worthwhile in this case. The original precedence system is outdated and has a lot of weird quirks that cause many unnecessary conflicts. The new system achieves much lower conflict error rates with almost no compatibility impact on existing content. It also has much clearer code, new debug commands, and support for features like download folder restriction. 

# Conclusion

If you have any questions feel free to email me at chomenor@gmail.com.

This project is dedicated to the creators, mapping, and modding communities of Quake 3, Elite Force, and similar games. Thank you for all your amazing work!

Thank you to everybody who has helped with testing and feedback!

# Appendix: File Handling Review

This is a review of how the standard ioquake3 filesystem works and some of the terminology used in this project.

File handling in ioquake3 is based on one or more "source directories". This includes the basepath, which is typically the location containing the game executable, and the homepath, which is typically located in the user's home directory. The specific locations can be modified from the command line by setting cvars such as fs_homepath and fs_basepath. Only one directory (typically homepath) is used for writing things like config files and automatic downloads; the others are read-only.

Each source directory can contain any number of "mod directories". The original implementation sometimes uses the term "game directory" instead; this refers to the same thing. A specific directory ("baseq3" by default in ioquake3) will be loaded by default. One additional directory can be specified by the client or server as the "current mod" via the fs_game cvar, which will be loaded on top of baseq3 and take precedence over it.

Game asset files can be placed directly on the disk within each mod directory, or in a pk3 file (a zip file with .pk3 extension) at the root level of the mod directory. When the game code queries a path (sometimes called a "qpath"), the filesystem will attempt to locate the file both inside pk3s and directly on the disk, under both the current mod and baseq3 directories, under each source directory. The game code normally doesn't specify a mod, source directory, or pk3 for a file; it is up to the filesystem to locate it.

Each pk3 has a 32-bit hash used to identify it. "Pure" servers will send a list of hashes (sv_paks) which tells clients to only load pk3s on the list, and in the order specified by the list (first pk3 in the list has highest priority). This is used for both security and compatibility purposes, although the security aspect is usually not relevant by itself anymore. Download-enabled servers also send a list of pk3s that clients should download if they don't have them already, where sv_referencedPaks represents the hashes of the needed files and sv_referencedPakNames represents the names of the files to use for the download query.

Shaders are defined in files with qpaths of the form "scripts/*.shader", which usually contain multiple shaders per file. In the original filesystem these files are not handled specially by the filesystem and the renderer indexes and parses these files itself. However the new filesystem has special shader handling support which enables improvements over the old system.
