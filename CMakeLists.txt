cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) on macOS.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(VoxBundle VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE --------------------------------------------------------------------
set(VOX_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(VOX_JUCE_DIR)
    add_subdirectory(${VOX_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# AU on macOS (the point of this bundle). On other platforms VST3 is built
# purely so the code can be compiled and tested there.
if(APPLE)
    set(VOX_DEFAULT_FORMATS AU)
else()
    set(VOX_DEFAULT_FORMATS VST3)
endif()
set(VOX_FORMATS ${VOX_DEFAULT_FORMATS} CACHE STRING "Plugin formats to build, e.g. \"AU;VST3\"")

set(VOX_PLUGINS VoxComp VoxEQ VoxHeat VoxSpace VoxSynth VoxDouble VoxTune VoxPitch VoxDevil VoxAdlib)
set(VOX_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

function(vox_add_plugin target)
    cmake_parse_arguments(V "MIDI" "CODE;DESCRIPTION" "SOURCES" ${ARGN})
    if(V_MIDI)
        set(midi TRUE)
    else()
        set(midi FALSE)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME "Vox Bundle"
        BUNDLE_ID "com.voxbundle.${target}"
        PLUGIN_MANUFACTURER_CODE Vxbd
        PLUGIN_CODE ${V_CODE}
        PRODUCT_NAME "${target}"
        DESCRIPTION "${V_DESCRIPTION}"
        FORMATS ${VOX_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT ${midi}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_SANDBOX_SAFE TRUE)

    target_sources(${target} PRIVATE ${V_SOURCES} plugins/${target}/Entry.cpp)
    target_include_directories(${target} PRIVATE shared plugins/${target})
    target_compile_definitions(${target} PUBLIC ${VOX_COMMON_DEFS})
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

set(VoxComp_SOURCES  plugins/VoxComp/CompProcessor.cpp  plugins/VoxComp/CompEditor.cpp)
set(VoxEQ_SOURCES    plugins/VoxEQ/EqProcessor.cpp      plugins/VoxEQ/EqEditor.cpp)
set(VoxHeat_SOURCES  plugins/VoxHeat/HeatProcessor.cpp  plugins/VoxHeat/HeatEditor.cpp)
set(VoxSpace_SOURCES plugins/VoxSpace/SpaceProcessor.cpp plugins/VoxSpace/SpaceEditor.cpp)
set(VoxSynth_SOURCES plugins/VoxSynth/SynthProcessor.cpp plugins/VoxSynth/SynthEditor.cpp)
set(VoxDouble_SOURCES plugins/VoxDouble/DoubleProcessor.cpp plugins/VoxDouble/DoubleEditor.cpp)
set(VoxTune_SOURCES  plugins/VoxTune/TuneProcessor.cpp   plugins/VoxTune/TuneEditor.cpp)
set(VoxPitch_SOURCES plugins/VoxPitch/PitchProcessor.cpp plugins/VoxPitch/PitchEditor.cpp)
set(VoxDevil_SOURCES plugins/VoxDevil/DevilProcessor.cpp plugins/VoxDevil/DevilEditor.cpp)
set(VoxAdlib_SOURCES plugins/VoxAdlib/AdlibProcessor.cpp plugins/VoxAdlib/AdlibEditor.cpp)

set(VOX_ENABLED_PLUGINS ${VOX_PLUGINS} CACHE STRING "Subset of plugins to build")

foreach(p IN LISTS VOX_ENABLED_PLUGINS)
    if(p STREQUAL "VoxComp")
        vox_add_plugin(VoxComp CODE Vcmp DESCRIPTION "Vocal compressor, de-esser and limiter" SOURCES ${VoxComp_SOURCES})
    elseif(p STREQUAL "VoxEQ")
        vox_add_plugin(VoxEQ CODE Veqz DESCRIPTION "Vocal equalizer" SOURCES ${VoxEQ_SOURCES})
    elseif(p STREQUAL "VoxHeat")
        vox_add_plugin(VoxHeat CODE Vhet DESCRIPTION "Vocal saturation" SOURCES ${VoxHeat_SOURCES})
    elseif(p STREQUAL "VoxSpace")
        vox_add_plugin(VoxSpace CODE Vspc DESCRIPTION "Vocal reverb and delay" SOURCES ${VoxSpace_SOURCES})
    elseif(p STREQUAL "VoxSynth")
        vox_add_plugin(VoxSynth CODE Vsyn DESCRIPTION "Vocoder synth" SOURCES ${VoxSynth_SOURCES})
    elseif(p STREQUAL "VoxDouble")
        vox_add_plugin(VoxDouble CODE Vdbl DESCRIPTION "Vocal doubler" SOURCES ${VoxDouble_SOURCES})
    elseif(p STREQUAL "VoxTune")
        vox_add_plugin(VoxTune CODE Vtun DESCRIPTION "Vocal pitch correction" SOURCES ${VoxTune_SOURCES})
    elseif(p STREQUAL "VoxPitch")
        vox_add_plugin(VoxPitch CODE Vpit DESCRIPTION "Vocal pitch shifter" SOURCES ${VoxPitch_SOURCES})
    elseif(p STREQUAL "VoxDevil")
        vox_add_plugin(VoxDevil CODE Vdvl DESCRIPTION "Devil compressor" SOURCES ${VoxDevil_SOURCES})
    elseif(p STREQUAL "VoxAdlib")
        vox_add_plugin(VoxAdlib CODE Vadl DESCRIPTION "Ad-lib processor" SOURCES ${VoxAdlib_SOURCES})
    endif()
endforeach()

# --- Offline self-test + screenshot tool (not part of the plugin bundle) ------
option(VOX_BUILD_SELFTEST "Build the offline DSP self-test / screenshot tool" OFF)
if(VOX_BUILD_SELFTEST)
    juce_add_console_app(VoxSelfTest PRODUCT_NAME "VoxSelfTest")
    set(test_sources tests/SelfTest.cpp)
    foreach(p IN LISTS VOX_ENABLED_PLUGINS)
        list(APPEND test_sources ${${p}_SOURCES})
        target_include_directories(VoxSelfTest PRIVATE plugins/${p})
        string(TOUPPER ${p} up)
        target_compile_definitions(VoxSelfTest PRIVATE VOX_HAS_${up}=1)
    endforeach()
    target_sources(VoxSelfTest PRIVATE ${test_sources})
    target_include_directories(VoxSelfTest PRIVATE shared)
    target_compile_definitions(VoxSelfTest PRIVATE ${VOX_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(VoxSelfTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
