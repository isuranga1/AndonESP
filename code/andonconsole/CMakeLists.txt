# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --save-temps")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Console specific variables
# Default values and conditions
if(NOT DEFINED CONSOLE_ID)
    set(CONSOLE_ID "00000")
endif()

if(NOT DEFINED BUILDMETHOD)
    set(BUILDMETHOD "DEMO")
endif()

if(${BUILDMETHOD} STREQUAL "DEMO")
    add_compile_definitions(BUILDMETHOD_DEMO=1)
elseif(${BUILDMETHOD} STREQUAL "PRODUCTION")
    add_compile_definitions(BUILDMETHOD_PRODUCTION=1)
endif()

add_compile_definitions(
    CONSOLE_ID=${CONSOLE_ID}
    BUILDMETHOD=${BUILDMETHOD}
)

project(andonconsole)
