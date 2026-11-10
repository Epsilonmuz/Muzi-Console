cmake_minimum_required(VERSION 3.22)
project(MuziConsole VERSION 0.1.0 LANGUAGES C CXX)

# macOS: 12.0 target and JUCE 9.0.5 (bundled in ./JUCE). Older targets failed in the VST3 manifest helper.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(JUCE)

set(muzi_formats VST3 Standalone)
if(APPLE)
    list(APPEND muzi_formats AU)
endif()

juce_add_plugin(MuziConsole
    COMPANY_NAME "Muzi"
    BUNDLE_ID com.muzi.console
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Muzi
    PLUGIN_CODE MzCn
    VST3_CATEGORIES Fx EQ Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    FORMATS ${muzi_formats}
    PRODUCT_NAME "Muzi Console"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    DESCRIPTION "Console strip with a listening engineer"
)
juce_generate_juce_header(MuziConsole)
juce_add_binary_data(MuziAssets SOURCES
    Resources/EpsilonDial-Regular.ttf
    Resources/EpsilonDial-Bold.ttf
)
set_target_properties(MuziAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(MuziConsole PRIVATE
    Source/Engineer.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_compile_features(MuziConsole PRIVATE cxx_std_17)
target_compile_definitions(MuziConsole PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(MuziConsole PRIVATE
    MuziAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Engine + engineer tests (no JUCE needed): cmake --build build --target MuziEngineTests && ctest
option(MUZI_BUILD_TESTS "Build the DSP engine tests" ON)
if(MUZI_BUILD_TESTS)
    enable_testing()
    add_executable(MuziEngineTests Tests/EngineTests.cpp Source/Engineer.cpp)
    target_include_directories(MuziEngineTests PRIVATE Source)
    target_compile_features(MuziEngineTests PRIVATE cxx_std_17)
    add_test(NAME MuziEngineTests COMMAND MuziEngineTests)
endif()

# Headless editor snapshots + repaint timing (developer tool, off by default)
option(MUZI_BUILD_TOOLS "Build the snapshot tool" OFF)
if(MUZI_BUILD_TOOLS)
    juce_add_console_app(MuziSnapshot PRODUCT_NAME "MuziSnapshot")
    target_sources(MuziSnapshot PRIVATE Tests/Snapshot.cpp Source/Engineer.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_features(MuziSnapshot PRIVATE cxx_std_17)
    target_include_directories(MuziSnapshot PRIVATE Source)
    target_compile_definitions(MuziSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Muzi Console")
    juce_generate_juce_header(MuziSnapshot)
    target_link_libraries(MuziSnapshot PRIVATE MuziAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
