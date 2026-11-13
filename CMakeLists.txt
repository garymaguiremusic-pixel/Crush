cmake_minimum_required(VERSION 3.22)

project(Crush VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(CRUSH_BUILD_PLUGIN          "Build the plugin (needs JUCE)"                                  ON)
option(CRUSH_BUILD_TESTS           "Build the unit tests and the null-test harness"                ON)
option(CRUSH_BUILD_STANDALONE      "Also build a standalone app for testing"                       OFF)
option(CRUSH_COPY_AFTER_BUILD      "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# Bit-exact float behaviour: no FMA contraction, no fast-math. The original is
# MSVC x64 SSE scalar code (/fp:precise).
function(crush_strict_float target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise)   # precise does not contract to FMA
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
endfunction()

# The DSP has no framework dependencies.
add_library(CrushDSP STATIC Source/DSP/DistortionSection.cpp)
target_include_directories(CrushDSP PUBLIC Source/DSP)
set_target_properties(CrushDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
crush_strict_float(CrushDSP)

# ------------------------------------------------------------------------------
# Tests
if(CRUSH_BUILD_TESTS)
    enable_testing()

    add_executable(crush_unit_tests tests/UnitTests.cpp)
    target_link_libraries(crush_unit_tests PRIVATE CrushDSP)
    crush_strict_float(crush_unit_tests)
    add_test(NAME unit_tests COMMAND crush_unit_tests)

    add_executable(crush_nulltest tests/NullTest.cpp)
    target_link_libraries(crush_nulltest PRIVATE CrushDSP)
    crush_strict_float(crush_nulltest)
    add_test(NAME nulltest_suite COMMAND crush_nulltest --suite ${CMAKE_CURRENT_BINARY_DIR})
endif()

# ------------------------------------------------------------------------------
# Plugin
if(CRUSH_BUILD_PLUGIN)
    if(JUCE_DIR)
        add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.4
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(CRUSH_FORMATS VST3)
    if(CRUSH_BUILD_STANDALONE)
        list(APPEND CRUSH_FORMATS Standalone)
    endif()

    # The VST3 manifest helper has to run on the build machine.
    if(CMAKE_CROSSCOMPILING)
        set(CRUSH_VST3_MANIFEST FALSE)
    else()
        set(CRUSH_VST3_MANIFEST TRUE)
    endif()

    juce_add_plugin(Crush
        PRODUCT_NAME                "CRUSH"
        COMPANY_NAME                "Crush Project"
        BUNDLE_ID                   com.crushproject.crush
        PLUGIN_MANUFACTURER_CODE    Crpj
        PLUGIN_CODE                 Crs1
        FORMATS                     ${CRUSH_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES             Fx Distortion
        VST3_AUTO_MANIFEST          ${CRUSH_VST3_MANIFEST}
        COPY_PLUGIN_AFTER_BUILD     ${CRUSH_COPY_AFTER_BUILD})

    target_sources(Crush PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/CustomLookAndFeel.cpp
        Source/UI/CrushComponents.cpp)

    target_include_directories(Crush PRIVATE Source)

    target_compile_definitions(Crush PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Crush
        PRIVATE
            CrushDSP
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
