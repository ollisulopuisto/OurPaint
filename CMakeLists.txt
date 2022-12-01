cmake_minimum_required(VERSION 3.1)
project(OurPaint)

find_package(lagui REQUIRED)

add_definitions(-w)

include_directories(
    ${LAGUI_INCLUDE_DIRS_ALL}
)

file(GLOB_RECURSE OurPaintFiles 
    ourpaint.c ouroperations.c
)

add_executable(OurPaint ${OurPaintFiles})

target_link_libraries(OurPaint
    ${LAGUI_SHARED_LIBS}
)
