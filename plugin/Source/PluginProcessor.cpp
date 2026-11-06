#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <phe/core/Morph.h>
#include <phe/core/HostClock.h>

namespace
{
    // Remembered per computer (not per project): the reel folder, where saved reels go and where moved files are looked for.
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "PhenomeEon";
        o.folderName = "particledots";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return std::make_unique<juce::PropertiesFile> (o);
    }

    juce::File defaultReelFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PhenomeEon");
    }
}

juce::File PhenomeEonProcessor::getReelFolder() const
{
    if (auto s = openSettings())
    {
        const auto v = s->getValue ("reelFolder");
        if (v.isNotEmpty())
            return juce::File (v);
    }
    return defaultReelFolder();
}

void PhenomeEonProcessor::setReelFolder (const juce::File& folder)
{
    if (auto s = openSettings())
    {
        s->setValue ("reelFolder", folder.getFullPathName());
        s->saveIfNeeded();
    }
    library.setHomeFolder (folder.getFullPathName().toStdString());
}

PhenomeEonProcessor::PhenomeEonProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", PheParams::createLayout())
{
    library.setHomeFolder (getReelFolder().getFullPathName().toStdString());
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
    library.tick();

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
    if ((handled & phe::Engine::kButtonErase) != 0)            switchOff (PheParams::eraseSplice);
    if ((handled & phe::Engine::kButtonUndo) != 0)             switchOff (PheParams::undo);

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
        switchOff (PheParams::eraseSplice);
        switchOff (PheParams::undo);
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

    auto choiceOf = [&get] (const char* id, int maxIndex) { return juce::jlimit (0, maxIndex, (int) std::lround (get (id))); };
    p.variSpeedMode = choiceOf (PheParams::variSpeedMode, 2);
    p.recordSource  = choiceOf (PheParams::recordSource, 1);
    p.organizeMode  = choiceOf (PheParams::organizeMode, 1);
    p.geneWindow    = choiceOf (PheParams::geneWindow, 1);
    p.playMode      = choiceOf (PheParams::playMode, 2);
    p.morphChord    = choiceOf (PheParams::morphChord, phe::dsp::kNumMorphChords - 1);

    p.clockMode       = choiceOf (PheParams::clockMode, 3);
    p.clockRate       = choiceOf (PheParams::clockRate, phe::core::kNumClockDivisions - 1);
    p.clocksPerSplice = choiceOf (PheParams::clocksPerSplice, phe::core::kNumStretchClocks - 1);
    p.retrigger       = choiceOf (PheParams::retrigger, phe::core::kNumRetriggers - 1);
    p.recSync         = choiceOf (PheParams::recSync, 1);
    return p;
}

void PhenomeEonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Silence any output channels that have no matching input (not expected with the layouts above).
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // What Logic says about tempo and song position (the clock features follow it; with them off it changes nothing).
    phe::core::HostInfo host;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
            {
                host.tempoValid = true;
                host.bpm = *bpm;
            }
            host.playing = position->getIsPlaying();
            if (auto ppq = position->getPpqPosition())
            {
                host.ppqValid = true;
                host.ppq = *ppq;
            }
            if (auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    host.beatsPerBar = (double) signature->numerator * 4.0 / (double) signature->denominator;
        }
    }
    engine.setHost (host);

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
    {
        // The reel's file (not its audio): where it is (relative to the reel folder when inside it), its size and its fingerprint,
        // so it can be found again if it was moved.
        if (library.hasBinding())
        {
            const auto b = library.binding();
            auto* node = xml->createNewChildElement ("REELFILE");
            node->setAttribute ("path", juce::String::fromUTF8 (b.path.c_str()));
            node->setAttribute ("size", juce::String ((juce::int64) b.size));
            node->setAttribute ("hash", juce::String::toHexString ((juce::int64) b.hash));
        }
        copyXmlToBinary (*xml, destData);
    }
}

void PhenomeEonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            phe::ReelLibrary::Binding b;
            bool hasFile = false;
            if (auto* node = xml->getChildByName ("REELFILE"))        // absent when the reel was never saved to a file
            {
                b.path = node->getStringAttribute ("path").toStdString();
                b.size = (uint64_t) node->getStringAttribute ("size").getLargeIntValue();
                b.hash = (uint64_t) node->getStringAttribute ("hash").getHexValue64();
                hasFile = ! b.path.empty();
                xml->removeChildElement (node, true);
            }
            library.restore (hasFile, b);

            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            switchTransientButtonsOff.store (true);
        }
}

// This is what Logic calls to make the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhenomeEonProcessor();
}
