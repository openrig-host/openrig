cmake_minimum_required(VERSION 3.15)
project(OpenRig VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to a JUCE checkout (cloned by CI, or C:/JUCE locally).
if(NOT DEFINED JUCE_ROOT)
    set(JUCE_ROOT "C:/JUCE" CACHE PATH "Path to JUCE root directory")
endif()

# Pull in JUCE's CMake helpers (juce_add_gui_app, the module targets, etc.).
add_subdirectory(${JUCE_ROOT} JUCE)

# Standalone GUI application target.
juce_add_gui_app(OpenRig
    PRODUCT_NAME    "OpenRig"
    COMPANY_NAME    "OpenRig"
    BUNDLE_ID       "com.openrig.app"
    ICON_BIG        "${CMAKE_CURRENT_SOURCE_DIR}/DaveCoreProject/Source/Resources/OpenRig_Icon.png"
)

# All compilation units (must match the Projucer source list).
target_sources(OpenRig PRIVATE
    DaveCoreProject/Source/Main.cpp
    DaveCoreProject/Source/MainComponent.cpp
    DaveCoreProject/Source/RackSlotComponent.cpp
    DaveCoreProject/Source/BoutiqueLookAndFeel.cpp
    DaveCoreProject/Source/ConfigModalOverlay.cpp
    DaveCoreProject/Source/LibraryPanel.cpp
    DaveCoreProject/Source/RigSerializer.cpp
    DaveCoreProject/Source/RigBuilder.cpp
    DaveCoreProject/Source/RigTransitioner.cpp
    DaveCoreProject/Source/AssetLoader.cpp
)

target_include_directories(OpenRig PRIVATE
    DaveCoreProject/Source
    DaveCoreProject/JuceLibraryCode
)

# Module configuration. Mirrors the Projurer AppConfig.h, EXCEPT ASIO is left
# off — it needs Steinberg's proprietary SDK which CI cannot obtain. WASAPI /
# DirectSound cover a release build.
target_compile_definitions(OpenRig PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
)

target_link_libraries(OpenRig PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_processors_headless
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

if(WIN32)
    set_property(TARGET OpenRig PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/DaveCoreProject")
endif()
