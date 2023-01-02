cmake_minimum_required(VERSION 3.1)
project(OurPaint)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})

find_package(lagui REQUIRED)
find_package(PNG REQUIRED)
find_package(LCMS2 REQUIRED)

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

include_directories(
    ${LAGUI_INCLUDE_DIRS_ALL}
)

file(GLOB_RECURSE OurPaintFiles 
    ourpaint.c ouroperations.c ournodes.c ourtranslations.c
    BuildResources/data_splash.c
    BuildResources/data_splash_highdpi.c
)

add_executable(OurPaint ${OurPaintFiles})

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
