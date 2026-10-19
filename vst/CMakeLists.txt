cmake_minimum_required(VERSION 3.22)
project(EgretEq VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG master # ponytail: pin an exact release tag once one is chosen, for reproducible builds
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# The plugin target pulls in JUCE's GUI/windowing modules, which on Linux
# need dev packages (freetype, X11, ALSA, ...) that a plain DSP-only build
# doesn't require. Flip this off to configure just the EngineTest target.
option(EGRET_BUILD_PLUGIN "Build the VST3/Standalone plugin (needs GUI dev libs on Linux)" ON)

if(EGRET_BUILD_PLUGIN)
    juce_add_plugin(EgretEq
        COMPANY_NAME "Egret"
        PRODUCT_NAME "Egret EQ"
        PLUGIN_MANUFACTURER_CODE Egrt
        PLUGIN_CODE Eeq1
        FORMATS VST3 Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        VST3_CATEGORIES Fx EQ
        # SVG so JUCE renders every icon size crisply (16-256 px on Windows).
        ICON_BIG Resources/icon.svg
        ICON_SMALL Resources/icon-small.svg
    )

    target_sources(EgretEq PRIVATE
        Source/EqDsp.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    )

    target_compile_definitions(EgretEq PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(EgretEq PRIVATE
        juce::juce_audio_utils
        juce_recommended_config_flags
        juce_recommended_lto_flags
        juce_recommended_warning_flags
    )
endif()

# DSP self-test: plain C++, no JUCE, so it builds anywhere.
add_executable(EngineTest
    Tests/EngineTest.cpp
    Source/EqDsp.cpp
)
target_include_directories(EngineTest PRIVATE Source)
