cmake_minimum_required(VERSION 3.1)
project(OurPaint)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})

find_package(lagui REQUIRED)
find_package(PNG REQUIRED)
find_package(LCMS2 REQUIRED)

add_definitions(-w)

include_directories(
    ${LAGUI_INCLUDE_DIRS_ALL}
)

file(GLOB_RECURSE OurPaintFiles 
    ourpaint.c ouroperations.c ournodes.c ourtranslations.c
)

add_executable(OurPaint ${OurPaintFiles})

target_link_libraries(OurPaint
    ${LAGUI_SHARED_LIBS}
    ${PNG_LIBRARY}
    ${LCMS2_LIBRARIES}
)
