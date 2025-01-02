cmake_minimum_required(VERSION 3.1)
project(OurPaint)

if(${ANDROID})
    include(android/apk_build.cmake)
    return()
endif()

if(${LAGUI_USE_GLES})
    add_definitions(-DLA_USE_GLES)
endif()


IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_INSTALL_PREFIX $ENV{HOME}/OurPaint CACHE PATH "Where to install Our Paint" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
include (InstallRequiredSystemLibraries)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-no-pie")
set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-no-pie")

find_package(lagui REQUIRED)
find_package(PNG REQUIRED)
find_package(LCMS2 REQUIRED)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/std:c11>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_definitions(-w)

# embed_resource("data.dat" "data.h" "DATA")
function(embed_resource resource_file_name source_file_name variable_name)
    if(EXISTS "${source_file_name}")
        if("${source_file_name}" IS_NEWER_THAN "${resource_file_name}")
            return()
        endif()
    endif()
    file(READ "${resource_file_name}" hex_content HEX)
    string(REPEAT "[0-9a-f]" 32 pattern)
    string(REGEX REPLACE "(${pattern})" "\\1\n" content "${hex_content}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " content "${content}")
    string(REGEX REPLACE ", $" "" content "${content}")
    set(array_definition "unsigned char ${variable_name}[] =\n{\n${content}\n};")
    set(source "// Auto generated file.\n${array_definition}\n")
    file(WRITE "${source_file_name}" "${source}")
endfunction()

file(MAKE_DIRECTORY "BuildResources")
embed_resource("Resources/splash.png" "BuildResources/data_splash.c" "DATA_SPLASH")
embed_resource("Resources/splash_highdpi.png" "BuildResources/data_splash_highdpi.c" "DATA_SPLASH_HIGHDPI")

if(WIN32)
endif()

include_directories(
    ${LAGUI_INCLUDE_DIRS_ALL}
)

file(GLOB_RECURSE OurPaintFiles
    ourpaint.c ouroperations.c ournodes.c ourshader.cpp ourstrings.cpp
    ourtranslations_zh-hans.c
    ourtranslations_es-ES.c
    BuildResources/data_splash.c
    BuildResources/data_splash_highdpi.c
    Resources/icon.rc
)

add_executable(OurPaint ${OurPaintFiles})

target_link_options(OurPaint PUBLIC "-Wl,--start-group")


execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE OURPAINT_GIT_BRANCH
	OUTPUT_STRIP_TRAILING_WHITESPACE)
	
execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE OURPAINT_GIT_HASH
	OUTPUT_STRIP_TRAILING_WHITESPACE)

target_compile_definitions(OurPaint PRIVATE "-DOURPAINT_GIT_BRANCH=\"${OURPAINT_GIT_BRANCH}\"")
target_compile_definitions(OurPaint PRIVATE "-DOURPAINT_GIT_HASH=\"${OURPAINT_GIT_HASH}\"")

target_link_libraries(OurPaint
    ${LAGUI_SHARED_LIBS}
    ${PNG_LIBRARY}
    ${LCMS2_LIBRARIES}
)

SET(INSTALL_EXTRAS
    README.md default_brushes.udf default_pallettes.udf COPYING COPYING_CC_BY_NC)

SET(LAGUI_FONT_CUSTOM_PATH  $ENV{HOME}/.local/share/fonts/lagui CACHE STRING "Where to find lagui fonts")

if (NOT DEFINED ${LAGUI_FONT_CUSTOM_PATH})
    set(LAGUI_FONT_CUSTOM_PATH $ENV{HOME}/.local/share/fonts/lagui)
endif()

set(INSTALL_PATH_BIN ".")

if(${CMAKE_VERSION} VERSION_LESS "3.21")
install(TARGETS OurPaint DESTINATION ${INSTALL_PATH_BIN})
else()
install(TARGETS OurPaint RUNTIME_DEPENDENCY_SET runtime_deps DESTINATION ${INSTALL_PATH_BIN})
LIST(APPEND pre_exclude_regexes "api-ms-.*")
LIST(APPEND pre_exclude_regexes "ext-ms-.*")
list(APPEND post_exclude_regexes ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\/][Ss][Yy][Ss][Tt][Ee][Mm]32.*")
list(APPEND post_exclude_regexes ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\/][Ss][Yy][Ss][Ww][Oo][Ww]64.*")
LIST(APPEND post_exclude_regexes "^/lib" "^/usr" "^/bin")
install(RUNTIME_DEPENDENCY_SET runtime_deps
    PRE_EXCLUDE_REGEXES ${pre_exclude_regexes}
    POST_EXCLUDE_REGEXES ${post_exclude_regexes}
    DESTINATION ${INSTALL_PATH_BIN}
)
endif()
install(DIRECTORY "${LAGUI_FONT_CUSTOM_PATH}/" DESTINATION ${INSTALL_PATH_BIN}/fonts PATTERN "*.*")
install(FILES ${INSTALL_EXTRAS} DESTINATION ${INSTALL_PATH_BIN})
