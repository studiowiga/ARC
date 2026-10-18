cmake_minimum_required(VERSION 3.22)

project(Arc VERSION 1.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# Windows : runtime statique, pas de DLL Visual C++ à installer
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

juce_add_plugin(Arc
    COMPANY_NAME                "Studio Wiga"
    PRODUCT_NAME                "Arc"
    PLUGIN_MANUFACTURER_CODE    Wiga
    PLUGIN_CODE                 ArcB
    BUNDLE_ID                   com.studiowiga.arc
    FORMATS                     ${PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

# visuel de la pédale, intégré au plugin
juce_add_binary_data(ArcAssets SOURCES background.jpg)

target_sources(Arc PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
)

target_include_directories(Arc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(Arc PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Arc
    PRIVATE
        ArcAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
