cmake_minimum_required(VERSION 3.17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(ourpaint-android)

if(${LAGUI_ANDROID})
    add_definitions(-DLAGUI_ANDROID)
endif()
if(${LAGUI_USE_GLES})
    add_definitions(-DLA_USE_GLES)
endif()

INCLUDE_DIRECTORIES(${LCMS_SRC_DIR}/../include)
INCLUDE_DIRECTORIES(${FREETYPE_SRC_DIR}/include)
INCLUDE_DIRECTORIES(${LAGUI_SRC_DIR})
INCLUDE_DIRECTORIES(${LAGUI_SRC_DIR}/nanovg)
INCLUDE_DIRECTORIES(${PNG_SRC_DIR})
INCLUDE_DIRECTORIES(${ANDROID_NDK}/sources/android/native_app_glue)

if(${LAGUI_USE_PNG})
    add_definitions(-DLA_WITH_PNG)
endif()

LINK_DIRECTORIES(../build-android/install/${ANDROID_ABI}/lib)
LINK_DIRECTORIES(../build-android/install/${ANDROID_ABI}/lib/lagui)

add_compile_options(-fpermissive
    -Wno-error=incompatible-pointer-types
    -Wno-error=incompatible-function-pointer-types
    -Wno-error=int-conversion
    -w
)

SET(OPS ../../)

add_library(${PROJECT_NAME} SHARED

${OPS}ourpaint.c ${OPS}ouroperations.c ${OPS}ournodes.c ${OPS}ourtranslations.c ${OPS}ourshader.cpp ${OPS}ourstrings.cpp
${OPS}BuildResources/data_splash.c
${OPS}BuildResources/data_splash_highdpi.c

${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_link_libraries(${PROJECT_NAME} PUBLIC log lagui GLESv3 EGL android z png freetype lcms2)

#target_link_options(${PROJECT_NAME} BEFORE PUBLIC "-v")
target_link_options(${PROJECT_NAME} BEFORE PUBLIC "-Wl,--start-group")
target_link_options(${PROJECT_NAME} PUBLIC "-Wl,--end-group")

install(TARGETS ${PROJECT_NAME})