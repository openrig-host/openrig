cmake_minimum_required(VERSION 3.15)
project(DaveCore VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find JUCE
if(NOT DEFINED JUCE_ROOT)
    set(JUCE_ROOT "C:/JUCE" CACHE PATH "Path to JUCE root directory")
endif()

# Add JUCE
add_subdirectory(${JUCE_ROOT} JUCE)

# Define the target
juce_add_gui_app(DaveCore
    PRODUCT_NAME "DaveCore"
    COMPANY_NAME "DaveWaugh"
    BUNDLE_ID "com.davewaugh.davecore"
)

# Source files
target_sources(DaveCore PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
)

# Include directories
target_include_directories(DaveCore PRIVATE Source)

# Compile definitions
target_compile_definitions(DaveCore PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=1
    JUCE_STRICT=0
)

# Link JUCE modules
target_link_libraries(DaveCore PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

# Windows specific settings
if(WIN32)
    set_property(TARGET DaveCore PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()