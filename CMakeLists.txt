cmake_minimum_required(VERSION 3.22)

# These two must come before project(), or the plugins are built for the macOS of the build machine.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

project(Multitails VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MULTITAILS_BUILD_AAX "Also build AAX (Pro Tools; needs PACE signing to load in release Pro Tools)" OFF)
option(MULTITAILS_BUILD_TESTS "Build the headless test runner" OFF)
option(MULTITAILS_COPY_AFTER_BUILD "Install the plugins into the user plug-in folders after building" ON)
set(MULTITAILS_VST2_SDK "" CACHE PATH "Steinberg VST2 SDK folder: if set, a VST2 is built too (the SDK is no longer distributed)")
set(MULTITAILS_AAX_COPY_DIR "" CACHE PATH "Where the AAX is copied after building (empty = JUCE default, the system Avid folder, needs write access)")

# ---- JUCE: -DJUCE_DIR=<path>, or ./JUCE, or downloaded automatically (needs git)
set(MULTITAILS_JUCE_TAG "8.0.10" CACHE STRING "JUCE version fetched when no local copy is given")
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt)
    set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${MULTITAILS_JUCE_TAG}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
    set(JUCE_DIR ${juce_SOURCE_DIR})
endif()

set(MULTITAILS_FORMATS VST3)
if(APPLE)
    list(APPEND MULTITAILS_FORMATS AU)
endif()
if(MULTITAILS_VST2_SDK)
    juce_set_vst2_sdk_path(${MULTITAILS_VST2_SDK})
    list(APPEND MULTITAILS_FORMATS VST)
endif()
if(MULTITAILS_BUILD_AAX)
    if(EXISTS ${JUCE_DIR}/modules/juce_audio_plugin_client/AAX/SDK)
        juce_set_aax_sdk_path(${JUCE_DIR}/modules/juce_audio_plugin_client/AAX/SDK)
    endif()
    list(APPEND MULTITAILS_FORMATS AAX)
endif()

set(MULTITAILS_EXTRA_PLUGIN_ARGS)
if(MULTITAILS_AAX_COPY_DIR)
    list(APPEND MULTITAILS_EXTRA_PLUGIN_ARGS AAX_COPY_DIR "${MULTITAILS_AAX_COPY_DIR}")
endif()

# An audio effect that also listens to MIDI, for the MIDI map. On AU that means a music effect (aumf), not a plain aufx.
juce_add_plugin(Multitails
    ${MULTITAILS_EXTRA_PLUGIN_ARGS}
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Gianluca Sofia"
    BUNDLE_ID "com.gianlucasofia.multitails"
    PLUGIN_MANUFACTURER_CODE Gsof
    PLUGIN_CODE Mtls
    PRODUCT_NAME "MULTITAILS"
    FORMATS ${MULTITAILS_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${MULTITAILS_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    AAX_CATEGORY AAX_ePlugInCategory_Reverb)

# Embedded UI fonts (Rubik, Silkscreen; SIL Open Font License, see Resources/fonts)
juce_add_binary_data(MultitailsFonts HEADER_NAME TailsFonts.h NAMESPACE TailsFonts SOURCES
    Resources/fonts/Rubik-Medium.ttf
    Resources/fonts/Rubik-Bold.ttf
    Resources/fonts/Rubik-BlackItalic.ttf
    Resources/fonts/Silkscreen-Regular.ttf)
set_target_properties(MultitailsFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Factory impulse responses, see Resources/ir/README.md
file(GLOB MULTITAILS_IRS CONFIGURE_DEPENDS Resources/ir/*.flac)
juce_add_binary_data(MultitailsIrs HEADER_NAME IrFiles.h NAMESPACE IrFiles SOURCES ${MULTITAILS_IRS})
set_target_properties(MultitailsIrs PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Multitails PRIVATE Source/TailsProcessor.cpp)
target_include_directories(Multitails PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(Multitails PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_FLAC=1
    MULTITAILS_VERSION="${PROJECT_VERSION}")
target_link_libraries(Multitails
    PRIVATE MultitailsFonts MultitailsIrs juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(MULTITAILS_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
