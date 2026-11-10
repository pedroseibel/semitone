cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(Semitone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8: use a local checkout in ./JUCE if there is one, otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Set SEMITONE_INSTALL=ON to copy the built plug-ins into the user's plug-in folders.
option(SEMITONE_INSTALL "Copy plug-ins into the user's plug-in folders after building" OFF)

if(APPLE)
    set(SEMITONE_FORMATS VST3 AU Standalone)
    set(SEMITONE_PLATFORM_SOURCES Source/ReducedMotion.mm)
else()
    set(SEMITONE_FORMATS VST3 Standalone)
    set(SEMITONE_PLATFORM_SOURCES Source/ReducedMotion.cpp)
endif()

juce_add_plugin(Semitone
    COMPANY_NAME "Semitone"
    BUNDLE_ID "audio.semitone.Semitone"
    PLUGIN_MANUFACTURER_CODE Smtn
    PLUGIN_CODE Smt1
    PRODUCT_NAME "Semitone"
    FORMATS ${SEMITONE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${SEMITONE_INSTALL})

target_sources(Semitone PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${SEMITONE_PLATFORM_SOURCES})

target_compile_definitions(Semitone PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Semitone
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- tests (not part of the plug-in) -----------------------------------------------
enable_testing()

# The pitch engine on its own: pitch accuracy, residual noise, transients, stereo, speed.
add_executable(SemitoneEngineTest Tests/engine_test.cpp)
target_include_directories(SemitoneEngineTest PRIVATE Source)
add_test(NAME engine COMMAND SemitoneEngineTest)

# The plug-in end to end: runs audio through the processor, renders the UI to PNGs, and
# loads the built VST3 the way a DAW would.
juce_add_console_app(SemitoneCheck PRODUCT_NAME "SemitoneCheck")
target_sources(SemitoneCheck PRIVATE
    Tests/plugin_check.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${SEMITONE_PLATFORM_SOURCES})
target_compile_definitions(SemitoneCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(SemitoneCheck PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
add_dependencies(SemitoneCheck Semitone_VST3)

add_test(NAME plugin
    COMMAND SemitoneCheck ui.png "${CMAKE_BINARY_DIR}/Semitone_artefacts/$<CONFIG>/VST3/Semitone.vst3"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(engine plugin PROPERTIES TIMEOUT 600)
