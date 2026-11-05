#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

PhenomeEonProcessor::PhenomeEonProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", PheParams::createLayout())
{
    startTimerHz (20);
}

PhenomeEonProcessor::~PhenomeEonProcessor()
{
    stopTimer();
}

void PhenomeEonProcessor::switchOff (const char* parameterId)
{
    if (auto* p = apvts.getParameter (parameterId))
    {
        if (p->getValue() > 0.5f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (0.0f);
            p->endChangeGesture();
        }
    }
}

void PhenomeEonProcessor::timerCallback()
{
    if (engine.takeRecordStopped())
        switchOff (PheParams::rec);

    if (engine.takeNewSpliceStopped())
        switchOff (PheParams::recNewSplice);

    if (engine.takeClearHandled())
        switchOff (PheParams::clearReel);

    // Splice, Shift, Delete Splice Marker and Delete All Markers act the moment they go on; then they go back off.
    const int handled = engine.takeHandledButtons();
    if ((handled & phe::Engine::kButtonSplice) != 0)           switchOff (PheParams::splice);
    if ((handled & phe::Engine::kButtonShift) != 0)            switchOff (PheParams::shift);
    if ((handled & phe::Engine::kButtonDeleteMarker) != 0)     switchOff (PheParams::deleteMarker);
    if ((handled & phe::Engine::kButtonDeleteAllMarkers) != 0) switchOff (PheParams::deleteAllMarkers);

    // A project saved while one of these was on must not record, clear or cut when it is opened.
    if (switchTransientButtonsOff.exchange (false))
    {
        switchOff (PheParams::rec);
        switchOff (PheParams::recNewSplice);
        switchOff (PheParams::clearReel);
        switchOff (PheParams::splice);
        switchOff (PheParams::shift);
        switchOff (PheParams::deleteMarker);
        switchOff (PheParams::deleteAllMarkers);
    }
}

void PhenomeEonProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
}

bool PhenomeEonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono in / mono out, or stereo in / stereo out.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

phe::Params PhenomeEonProcessor::readParams() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto on  = [&get] (const char* id) { return get (id) > 0.5f; };

    phe::Params p;
    p.inputLevel   = get (PheParams::inputLevel);
    p.soundOnSound = get (PheParams::soundOnSound);
    p.geneSize     = get (PheParams::geneSize);
    p.slide        = get (PheParams::slide);
    p.variSpeed    = get (PheParams::variSpeed);
    p.morph        = get (PheParams::morph);
    p.organize     = get (PheParams::organize);
    p.outputLevel  = get (PheParams::outputLevel);

    p.inputGainStep = juce::jlimit (0, phe::kNumInputGainSteps - 1, (int) std::lround (get (PheParams::inputGain)));
    p.pitchSnap     = (int) std::lround (get (PheParams::pitchSnap));
    p.pitchScale    = (int) std::lround (get (PheParams::pitchScale));

    p.rec              = on (PheParams::rec);
    p.recNewSplice     = on (PheParams::recNewSplice);
    p.splice           = on (PheParams::splice);
    p.shift            = on (PheParams::shift);
    p.play             = on (PheParams::play);
    p.deleteMarker     = on (PheParams::deleteMarker);
    p.deleteAllMarkers = on (PheParams::deleteAllMarkers);
    p.eraseSplice      = on (PheParams::eraseSplice);
    p.clearReel        = on (PheParams::clearReel);
    p.undo             = on (PheParams::undo);
    return p;
}

void PhenomeEonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Silence any output channels that have no matching input (not expected with the layouts above).
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()),
                    buffer.getNumSamples());
}

juce::AudioProcessorEditor* PhenomeEonProcessor::createEditor()
{
    return new PhenomeEonEditor (*this);
}

void PhenomeEonProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PhenomeEonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            switchTransientButtonsOff.store (true);
        }
}

// This is what Logic calls to make the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhenomeEonProcessor();
}
