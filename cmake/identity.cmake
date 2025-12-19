set(PROJECT_NAME ioq3)
set(PROJECT_VERSION 1.36)

set(SERVER_NAME ioq3ded)
set(CLIENT_NAME ioquake3)

set(BASEGAME baseq3)

set(CGAME_MODULE cgame)
set(GAME_MODULE qagame)
set(UI_MODULE ui)

set(WINDOWS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/windows/quake3.ico)

set(MACOS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/macos/quake3_flat.icns)
set(MACOS_BUNDLE_ID org.ioquake.${CLIENT_NAME})

set(COPYRIGHT "QUAKE III ARENA Copyright © 1999-2000 id Software, Inc. All rights reserved.")

set(CONTACT_EMAIL "info@ioquake.org")
set(PROTOCOL_HANDLER_SCHEME quake3)

if(BUILD_ELITEFORCE)
    set(PROJECT_NAME cMod)
    set(PROJECT_VERSION 1.30)

    set(BASEGAME baseEF)

    set(WINDOWS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/windows/stvoyHM.ico)

    set(MACOS_ICON_PATH ${CMAKE_SOURCE_DIR}/misc/macos/stvoyHM.icns)
    set(MACOS_BUNDLE_ID org.stvef.cmod)

    set(COPYRIGHT "This app contains code under copyright from Quake III Arena (id Software), ioquake3 (and contributors), and Star Trek Voyager Elite Force (Raven Software).")

    set(CONTACT_EMAIL "chomenor@gmail.com")
    set(PROTOCOL_HANDLER_SCHEME stvef)

    # Currently CMakeLists.txt seems to need this
    set(CMAKE_PROJECT_VERSION "${PROJECT_VERSION}")
endif()
