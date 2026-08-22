cmake_minimum_required(VERSION 3.22)
project(Plotter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared JUCE clone. Override with -DJUCE_PATH=/somewhere/else if needed.
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE repository")

# CI has no plug-in folders to install into, and on Windows the copy needs admin.
option(PLOTTER_COPY_AFTER_BUILD "Install the built plugin into the local plug-in folders" ON)
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

# CI builds shouldn't install into the runner's system plug-in folders.
option(PLOTTER_COPY_AFTER_BUILD "Install the plug-in to the user plug-in folders after building" ON)

juce_add_plugin(Plotter
    COMPANY_NAME            "Sungam"
    PLUGIN_MANUFACTURER_CODE Sngm
    PLUGIN_CODE              Plt1
    PRODUCT_NAME            "Plotter"

    # An INSTRUMENT that also emits MIDI. This is the combination that actually
    # routes MIDI out in Live/Reaper/Cubase. A MIDI-effect build (IS_MIDI_EFFECT)
    # is a Logic/AU MIDI FX thing and Live will not load it as one.
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        TRUE
    IS_MIDI_EFFECT           FALSE

    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD  ${PLOTTER_COPY_AFTER_BUILD}
    FORMATS                  VST3 AU Standalone)

target_sources(Plotter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PlotterCanvas.cpp)

target_compile_definitions(Plotter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Plotter PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
