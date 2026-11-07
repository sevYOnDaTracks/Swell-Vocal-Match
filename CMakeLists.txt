cmake_minimum_required(VERSION 3.22)
project(SwellVocalMatch VERSION 0.1.0 LANGUAGES C CXX)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
set(VOCAL_MATCH_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND VOCAL_MATCH_FORMATS AU)
endif()
juce_add_plugin(SwellVocalMatch
  COMPANY_NAME "Swell-audio"
  BUNDLE_ID "fr.swell-audio.vocal-match"
  PLUGIN_MANUFACTURER_CODE Swel
  PLUGIN_CODE Svmt
  FORMATS ${VOCAL_MATCH_FORMATS}
  PRODUCT_NAME "Swell Vocal Match"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Swell Vocal Match écoute ta voix pour la traiter et l'analyser."
  COPY_PLUGIN_AFTER_BUILD FALSE)
juce_add_binary_data(VocalMatchAssets SOURCES swell-mark.png)
target_sources(SwellVocalMatch PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(SwellVocalMatch PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(MSVC)
  target_compile_options(SwellVocalMatch PRIVATE /utf-8)
endif()
target_link_libraries(SwellVocalMatch PRIVATE VocalMatchAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
enable_testing()
add_executable(VocalMatchDSPTest Tests/DSPTest.cpp)
if(MSVC)
  target_compile_options(VocalMatchDSPTest PRIVATE /utf-8)
endif()
add_test(NAME VocalMatchDSP COMMAND VocalMatchDSPTest)
