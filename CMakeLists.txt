cmake_minimum_required(VERSION 3.22)

project(OniriqueEchoBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary, and the oldest macOS the JUCE 9 runtime supports.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE is looked for next to this file first, then in the PitchBox checkout
# beside this one, so the plugins of the range can share a single copy.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(ECHOBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(ECHOBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../PitchBox/JUCE")
endif()

set(ECHOBOX_JUCE_DIR "${ECHOBOX_JUCE_DEFAULT}" CACHE PATH "Path to JUCE")

if(NOT EXISTS "${ECHOBOX_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${ECHOBOX_JUCE_DIR}.\n"
        "Run:  git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git")
endif()

add_subdirectory("${ECHOBOX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The DSP, built on its own so the offline tests can link it without dragging
# in JUCE. Everything that decides how the plugin sounds lives here.
add_library(echobox_core STATIC
    Source/dsp/Echo.cpp)

target_include_directories(echobox_core PUBLIC Source)
set_target_properties(echobox_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # MSVC's <cmath> only declares M_PI when asked. PUBLIC so the tests get it too.
    target_compile_definitions(echobox_core PUBLIC _USE_MATH_DEFINES)
    target_compile_options(echobox_core PRIVATE /W4)
else()
    target_compile_options(echobox_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The plugin.
set(ECHOBOX_FORMATS VST3)

if(APPLE)
    list(APPEND ECHOBOX_FORMATS AU)
endif()

# The system VST3 folder on Windows is under Program Files, which a normal build
# cannot write to, so installing after the build is opt-in there.
if(WIN32)
    option(ECHOBOX_COPY_AFTER_BUILD "Install the plugin after building" OFF)
else()
    option(ECHOBOX_COPY_AFTER_BUILD "Install the plugin after building" ON)
endif()

juce_add_plugin(OniriqueEchoBox
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.echobox"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Echx
    FORMATS ${ECHOBOX_FORMATS}
    PRODUCT_NAME "EchoBox"
    # What hosts list it as. The file on disk keeps the short name.
    PLUGIN_NAME "ONIRIQUE EchoBox"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    # For typing a value, and a preset's name.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Delay
    COPY_PLUGIN_AFTER_BUILD ${ECHOBOX_COPY_AFTER_BUILD})

# The Onirique sigil, as it appears in the header of the site.
juce_add_binary_data(EchoBoxAssets SOURCES Resources/sigil.png)

target_sources(OniriqueEchoBox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/PresetBrowser.cpp
    Source/ui/EchoDisplay.cpp
    Source/ui/Theme.cpp)

# The licence every Onirique plugin carries: the folder is copied as it is from
# plugin-license/ in the ONIRIQUE CENTRAL repository, and is only ever changed there.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueEchoBox)

target_compile_definitions(OniriqueEchoBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# The descriptions are in French: the sources are UTF-8, and MSVC has to be told.
if(MSVC)
    target_compile_options(OniriqueEchoBox PRIVATE /utf-8)
endif()

target_link_libraries(OniriqueEchoBox
    PRIVATE
        echobox_core
        EchoBoxAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: the whole audio path without a host.
enable_testing()

add_executable(test_echo tests/test_echo.cpp)
target_link_libraries(test_echo PRIVATE echobox_core)
target_include_directories(test_echo PRIVATE tests)
add_test(NAME echo COMMAND test_echo)

# A development tool, not part of the product: renders the editor to a PNG
# without a host. Built only when asked for by name.
add_executable(echobox_snapshot EXCLUDE_FROM_ALL tools/snapshot.cpp)

target_include_directories(echobox_snapshot PRIVATE
    tests
    $<TARGET_PROPERTY:OniriqueEchoBox,INCLUDE_DIRECTORIES>)

target_compile_definitions(echobox_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueEchoBox,COMPILE_DEFINITIONS>)

target_link_libraries(echobox_snapshot PRIVATE OniriqueEchoBox echobox_core EchoBoxAssets)
