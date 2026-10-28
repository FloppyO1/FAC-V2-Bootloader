cmake_minimum_required(VERSION 3.22)
#
# This file is generated only once, and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME FAC_V2_Bootloader)


# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# The CDC class uses single buffered endpoints: dropping the double buffer code saves flash.
# It changes PCD structures, so it must reach every file (set before the CubeMX targets).
add_compile_definitions(USE_USB_DOUBLE_BUFFER=0U)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Bootloader sources: the glob re-runs at every build, so a new .c file is picked up
file(GLOB_RECURSE FAC_BL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Core/Src/Bootloader/*.c"
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${FAC_BL_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# The linker script is not a source, so a change to it would not relink on its own
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/fac_bootloader.ld"
)

# ST's GNU Tools support this flag, the upstream Arm GCC does not
include(CheckCCompilerFlag)
check_c_compiler_flag(-fcyclomatic-complexity FAC_HAS_CYCLOMATIC)
if(FAC_HAS_CYCLOMATIC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -fcyclomatic-complexity)
endif()

# .hex and .bin next to the .elf, then the memory footprint
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
)
