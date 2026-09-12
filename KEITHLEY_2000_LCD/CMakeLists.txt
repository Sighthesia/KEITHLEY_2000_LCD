cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
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
set(CMAKE_PROJECT_NAME KEITHLEY_2000_LCD)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

add_compile_options(-Wall -Wextra -Werror)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/hal_board.c
    Core/Src/lt7680_bus.c
    Core/Src/lt7680_gfx.c
    Core/Src/font_digits.c
    Core/Src/font_half.c
    Core/Src/font_text.c
    Core/Src/keypad.c
    Core/Src/k2000_proto.c
    Core/Src/host_snapshot.c
    Core/Src/raw_reading_snapshot.c
    Core/Src/main_display.c
    Core/Src/panel_transform.c
    Core/Src/reading_split.c
    Core/Src/render_scheduler.c
    Core/Src/rif_reader.c
    Core/Src/rif_tile_cache.c
    Core/Src/scene.c
    Core/Src/status_bar.c
    Core/Src/trend_axis.c
    Core/Src/trend_buffer.c
    Core/Src/uart_rx_queue.c
    Core/Src/sht3x.c
    Core/Src/spi_timeout.c
    Core/Src/ui_model.c
    Core/Src/ui_layout.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
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

    # Add user defined libraries
)
