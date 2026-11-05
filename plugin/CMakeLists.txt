# The PhenomeEon plugin wrapper: what Logic sees (AU / VST3) and the controls.
# The sound itself lives in ../engine.

# The fonts used by the styled panel (Space Grotesk and IBM Plex Mono, both free open-licence fonts;
# the licence texts sit next to them in Fonts/). They are built into the plugin so it looks the same on every Mac.
juce_add_binary_data(PhenomeEonFonts
    HEADER_NAME PheFontData.h
    NAMESPACE   PheFontData
    SOURCES
        Fonts/SpaceGrotesk-Regular.ttf
        Fonts/SpaceGrotesk-Medium.ttf
        Fonts/SpaceGrotesk-Bold.ttf
        Fonts/IBMPlexMono-Regular.ttf
        Fonts/IBMPlexMono-Medium.ttf
)

juce_add_plugin(PhenomeEon
    COMPANY_NAME                "particledots"
    BUNDLE_ID                   com.particledots.phenomeeon
    PLUGIN_MANUFACTURER_CODE    Pdot
    PLUGIN_CODE                 Phe1
    FORMATS                     AU VST3
    PRODUCT_NAME                "PhenomeEon"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect
)

target_sources(PhenomeEon PRIVATE
    Source/Parameters.h
    Source/Parameters.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/PanelLookAndFeel.h
    Source/PanelLookAndFeel.cpp
    Source/StyledPanel.h
    Source/StyledPanel.cpp
    Source/ReelFileActions.h
    Source/ReelFileActions.cpp
)

target_compile_definitions(PhenomeEon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PhenomeEon
    PRIVATE
        phe_engine
        PhenomeEonFonts
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Optional, off by default (the GitHub build does not use it): a small program that draws the panel to picture
# files and runs automatic checks on the controls, without needing Logic. Build with -DPHE_BUILD_PREVIEW=ON.
option(PHE_BUILD_PREVIEW "Build the offline panel preview and control checks" OFF)
if(PHE_BUILD_PREVIEW)
    juce_add_console_app(PhenomeEonPreview PRODUCT_NAME "PhenomeEonPreview")
    target_sources(PhenomeEonPreview PRIVATE
        preview/preview.cpp
        Source/Parameters.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PanelLookAndFeel.cpp
        Source/StyledPanel.cpp
        Source/ReelFileActions.cpp
    )
    target_include_directories(PhenomeEonPreview PRIVATE Source)
    target_compile_definitions(PhenomeEonPreview PRIVATE
        JucePlugin_Name="PhenomeEon"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(PhenomeEonPreview
        PRIVATE phe_engine PhenomeEonFonts juce::juce_audio_processors juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
