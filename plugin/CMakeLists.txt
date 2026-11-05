# The PhenomeEon plugin wrapper: what Logic sees (AU / VST3) and the controls.
# The sound itself lives in ../engine.

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
)

target_compile_definitions(PhenomeEon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PhenomeEon
    PRIVATE
        phe_engine
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
