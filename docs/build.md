# Compiling

The cMod project is divided into two parts, the engine and game modules. Both are included in the release packages but currently use separate build processes.

## Engine

The following methods can be used to build the engine (main game executable). Generally the build process is the same as for [ioquake3](https://github.com/ioquake/ioq3/blob/main/README.md#compilation-and-installation).

### Building for Windows (Visual Studio)

1) Install Visual Studio Community from [https://visualstudio.microsoft.com/downloads/](https://visualstudio.microsoft.com/downloads/).
2) Clone the cMod repository using `git clone https://github.com/Chomenor/ioef-cmod.git`, or download the contents in some other way if git isn't available.
3) Launch Visual Studio, select "Open a folder", and select the cloned repository location.
4) In the "Solution Explorer" view, right click the top level folder element, and select "Switch to CMake Targets View"
5) You should now be able to locate and right click the target object (e.g. client or dedicated server) and select to build it.

Alternatively:

1) Install Visual Studio and clone the repository as above.
2) Open a console and set the current directory to the cloned repository. If you don't have standalone CMake installed, using the "Developer Command Prompt" available from the start menu may provide access to the version included in Visual Studio.
3) Run configure command, e.g. `cmake -S . -B build -G "Visual Studio 18 2026"`
4) Run build command, e.g. `cmake --build build --config Release` or open the build\cMod.sln project in Visual Studio.

### Building for Windows (MinGW)

1) Download msys2 from [https://www.msys2.org/](https://www.msys2.org/) and run the installer.
2) Launch the MINGW64 console (not the default MSYS one prompted by the installer). Can be found in the start menu.
3) Run the following commands:

```
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja git
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Building for Linux

The following commands have been tested on Ubuntu 24. Package installation may vary on other environments.

```
sudo apt install git build-essential libsdl2-dev cmake
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Building for Mac

1) Have XCode and CMake installed. Consult other guides if needed.
2) Run the following commands:

```
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Feature Selection

All changes in cMod compared to the base ioquake3 source code are listed in the [cmod_defs.h](https://github.com/Chomenor/ioef-cmod/blob/master/code/cmod/cmod_defs.h) file. Specific features can be disabled by commenting out the corresponding `#define` statement.

## Game Modules

The game modules are included in the cMod releases via the pakcmod pk3 file. If you have built the engine from source, but don't need to modify the game modules, you can simply copy the pakcmod pk3 from the latest cMod release to your baseEF folder. The pk3 is platform-neutral.

### Building QVM Modules

Currently the build process for the game modules is for Windows only. This is a holdover from the fact that the original Raven game SDK was based on a Windows toolchain.

1) Download the contents of the [cmod-game-code](https://github.com/Chomenor/cmod-game-code) repository.
2) Run the `build/qvm_build/build_all.bat` script. The `build/qvm_build/output/vm` folder should now contain 3 files: `cgame.qvm`, `qagame.qvm`, and `ui.qvm`.
3) Add the `vm` folder to a zip file, and rename it to a pk3 extention, such as `mod.pk3`. Note that vm folder must be included in the pk3, so the path inside the pk3 will be like `vm/cgame.qvm`.

### Loading Modules (Standard Method)

This method can be used to load the modules on any Elite Force engine.

1) Create a folder in your EF installation, next to baseEF. It could have any name, "testmod" for example.
2) Copy the "mod.pk3" file from the previous step into this folder.
3) Start the game, click the `MODS` button, and select the mod from the list.

### Loading Modules (Engine Selection)

In the cMod releases, the game module pk3 is located in baseEF and loaded specially by the engine. This allows the cMod modules and features to work when playing on remote servers when they would otherwise not be loaded by the standard mod method.

To enable this using a custom-built game module pk3, follow these steps:

1) Copy the module pk3 file into baseEF.
2) Start the game and run the `/path` command from the console. Locate the name of the module pk3 in the output, and note the integer hash value beneath it.
3) Edit the [fslocal.h](https://github.com/Chomenor/ioef-cmod/blob/master/code/filesystem/fslocal.h) file. Locate the `CMOD_PAKS` definition, and add the new hash value to the *bottom* of this list.
4) Recompile the engine.
