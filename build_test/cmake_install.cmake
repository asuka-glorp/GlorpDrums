# Install script for directory: /home/servandoc/Documents/DEV/GlorpDrums

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/servandoc/Documents/DEV/GlorpDrums/build_test/libs/JUCE/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}/home/servandoc/.local/share/vst3/GlorpDrums.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/servandoc/.local/share/vst3/GlorpDrums.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}/home/servandoc/.local/share/vst3/GlorpDrums.so"
         RPATH "")
  endif()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/servandoc/.local/share/vst3/GlorpDrums.so")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/servandoc/.local/share/vst3" TYPE MODULE FILES "/home/servandoc/Documents/DEV/GlorpDrums/build_test/GlorpDrums_artefacts/Release/VST3/GlorpDrums.vst3/Contents/x86_64-linux/GlorpDrums.so")
  if(EXISTS "$ENV{DESTDIR}/home/servandoc/.local/share/vst3/GlorpDrums.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/servandoc/.local/share/vst3/GlorpDrums.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}/home/servandoc/.local/share/vst3/GlorpDrums.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  
            file(MAKE_DIRECTORY "/home/servandoc/.vst3")
            if(NOT EXISTS "/home/servandoc/.vst3/GlorpDrums.vst3")
                execute_process(COMMAND ${CMAKE_COMMAND} -E
                    create_symlink "/home/servandoc/.local/share/vst3/GlorpDrums.vst3"
                    "/home/servandoc/.vst3/GlorpDrums.vst3")
            endif()
        
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpDrums" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpDrums")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpDrums"
         RPATH "")
  endif()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/servandoc/.local/share/bin/GlorpDrums")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/servandoc/.local/share/bin" TYPE EXECUTABLE FILES "/home/servandoc/Documents/DEV/GlorpDrums/build_test/GlorpDrums_artefacts/Release/Standalone/GlorpDrums")
  if(EXISTS "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpDrums" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpDrums")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpDrums")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpKitEditor" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpKitEditor")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpKitEditor"
         RPATH "")
  endif()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/servandoc/.local/share/bin/GlorpKitEditor")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/servandoc/.local/share/bin" TYPE EXECUTABLE FILES "/home/servandoc/Documents/DEV/GlorpDrums/build_test/GlorpKitEditor_artefacts/Release/GlorpKitEditor")
  if(EXISTS "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpKitEditor" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpKitEditor")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}/home/servandoc/.local/share/bin/GlorpKitEditor")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/servandoc/.local/share/icons/glorpdrums_icon.png;/home/servandoc/.local/share/icons/glorpkit_studio_icon.png")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/servandoc/.local/share/icons" TYPE FILE FILES
    "/home/servandoc/Documents/DEV/GlorpDrums/assets/glorpdrums_icon.png"
    "/home/servandoc/Documents/DEV/GlorpDrums/assets/glorpkit_studio_icon.png"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  
        file(MAKE_DIRECTORY "/home/servandoc/.local/share/icons/hicolor/256x256/apps")
        file(MAKE_DIRECTORY "/home/servandoc/.local/share/applications")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glorpdrums_icon.png"
             DESTINATION "/home/servandoc/.local/share/icons/hicolor/256x256/apps")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glorpkit_studio_icon.png"
             DESTINATION "/home/servandoc/.local/share/icons/hicolor/256x256/apps")
        file(WRITE "/home/servandoc/.local/share/applications/glorpdrums.desktop"
"[Desktop Entry]
Name=GlorpDrums
Comment=Acoustic Drum Sampler
Exec=GlorpDrums
Icon=glorpdrums_icon
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
StartupWMClass=GlorpDrums
")
        file(WRITE "/home/servandoc/.local/share/applications/glorpkit_studio.desktop"
"[Desktop Entry]
Name=GlorpKit Studio
Comment=.glorpkit Drum Kit Creator & Editor
Exec=GlorpKitEditor
Icon=glorpkit_studio_icon
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
StartupWMClass=GlorpKitEditor
")
    
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/servandoc/.config/GlorpDrums/")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/servandoc/.config/GlorpDrums" TYPE DIRECTORY FILES "")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/servandoc/.config/GlorpDrums/presets/")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/servandoc/.config/GlorpDrums/presets" TYPE DIRECTORY FILES "/home/servandoc/Documents/DEV/GlorpDrums/presets/")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/servandoc/Documents/DEV/GlorpDrums/build_test/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/servandoc/Documents/DEV/GlorpDrums/build_test/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
