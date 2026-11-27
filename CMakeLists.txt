cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME RLC)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-braces>
)

# Generate DDS assets before compiling the firmware.
if(NOT Python3_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe")
    set(Python3_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(generate_dds_assets
    BYPRODUCTS
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sin_table_16.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sin_table_20.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sin_table_32.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sin_table_80.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sin_table_160.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sin_table_256.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/dds.h.inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/dds.c.inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sync.h.inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets/sync.c.inc"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/Assets"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Generators/sin-table.py"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Generators/sin-inc-gen.py"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Generators/sync-inc-gen.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Generators/sin-table.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/Generators/sin-inc-gen.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/Generators/sync-inc-gen.py"
    COMMENT "Generating DDS sine tables and sync mode include files"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME} generate_dds_assets)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
file(GLOB BSP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/BSP/*.c"
)

file(GLOB APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/APP/*.c"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${BSP_SOURCES}
    ${APP_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    m

    # Add user defined libraries
)
