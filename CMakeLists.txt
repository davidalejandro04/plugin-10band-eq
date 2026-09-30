cmake_minimum_required(VERSION 3.22)

project(TenBandEQ VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pass -DJUCE_DIR=/path/to/JUCE to use a local checkout. Otherwise CMake
# downloads a fixed, reproducible JUCE release on first configure.
if (DEFINED JUCE_DIR)
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(TenBandEQ
    COMPANY_NAME "Open Audio"
    BUNDLE_ID "org.openaudio.tenbandeq"
    PLUGIN_MANUFACTURER_CODE OpAu
    PLUGIN_CODE TbEq
    FORMATS VST3 Standalone
    PRODUCT_NAME "Ten Band EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(TenBandEQ)

juce_add_binary_data(TenBandEQFonts HEADER_NAME UIFontData.h NAMESPACE UIFontData
    SOURCES Assets/Fonts/Inter-Regular.ttf Assets/Fonts/Inter-SemiBold.ttf)

target_sources(TenBandEQ PRIVATE
    Source/UI/Theme.cpp
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SpectrumDisplay.cpp
    Source/SpectrumDisplay.h
    Source/SpectrumAnalyzer.h
    Source/EqResponse.h
    Source/Standalone/FileRenderer.cpp
    Source/Standalone/StandaloneEngine.cpp
    Source/Standalone/SystemAudioSource.cpp)

target_include_directories(TenBandEQ PUBLIC Source)
target_sources(TenBandEQ_Standalone PRIVATE Source/Standalone/StandaloneApp.cpp)
target_compile_definitions(TenBandEQ_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

# Keep the embedded font's licence beside each distributed binary.
foreach(app_target TenBandEQ_Standalone TenBandEQ_VST3)
    add_custom_command(TARGET ${app_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/LICENSE.txt"
            "$<TARGET_FILE_DIR:${app_target}>/Inter-LICENSE.txt"
        VERBATIM)
endforeach()

target_compile_definitions(TenBandEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TenBandEQ PRIVATE
    TenBandEQFonts
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

option(TENBAND_EQ_BUILD_TESTS "Build visualization and audio regression tests" OFF)
if (TENBAND_EQ_BUILD_TESTS)
    enable_testing()
    add_executable(TenBandEQTests Tests/VisualizationTests.cpp)
    target_link_libraries(TenBandEQTests PRIVATE TenBandEQ)
    target_include_directories(TenBandEQTests PRIVATE
        Source
        $<TARGET_PROPERTY:TenBandEQ,INCLUDE_DIRECTORIES>)
    target_compile_definitions(TenBandEQTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        TENBAND_EQ_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-audio")
    add_test(NAME Visualization COMMAND TenBandEQTests)
endif()
