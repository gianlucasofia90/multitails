#include "TailsProcessor.h"
#include "TailsEditor.h"
#include "Params.h"
#include "IrFiles.h"

using namespace tails;

juce::String TailsProcessor::sid (int stream, int p) { return juce::String (stream == 0 ? "tr_" : "to_") + streamParams()[p].id; }
juce::String TailsProcessor::lid (int lane, int p)   { return "l" + juce::String (lane + 1) + "_" + kLaneIds[p]; }

juce::AudioProcessorValueTreeState::ParameterLayout TailsProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (cparam ("g_sync", "Sync", itemsOf (kItemsSync, 2), 0));
    l.add (fparam ("g_bpm", "Free BPM", 20, 300, 120, "bpm"));
    l.add (bparam ("g_run", "Free Run", false));
    l.add (bparam ("g_split", "Split", true));
    l.add (fparam ("g_focus", "Focus", -100, 100, 0, "%"));
    l.add (fparam ("g_smooth", "Smoothing", 0, 100, 30, "%"));
    l.add (fparam ("g_decay", "Trans Decay", 2, 400, 40, "ms", 50));
    l.add (cparam ("g_source", "Source Type", itemsOf (kItemsSource, 6), 0));
    l.add (fparam ("g_in", "Input", -24, 24, 0, "dB"));
    l.add (fparam ("g_out", "Output", -24, 24, 0, "dB"));
    l.add (fparam ("g_mix", "Global Mix", 0, 100, 100, "%"));
    l.add (bparam ("g_reset", "Sequencer Reset", false));

    for (int s = 0; s < kStreams; ++s)
    {
        const juce::String prefix = s == 0 ? "Trans " : "Tonal ";
        for (int p = 0; p < kNumP; ++p)
        {
            const auto& d = streamParams()[p];
            const auto id = sid (s, p);
            const auto name = prefix + d.name;
            switch (d.type)
            {
                case T::Float:  l.add (fparam (id, name, d.lo, d.hi, d.def, d.unit, d.centre)); break;
                case T::Int:    l.add (iparam (id, name, (int) d.lo, (int) d.hi, (int) d.def)); break;
                case T::Choice: l.add (cparam (id, name, itemsOf (d.items, d.numItems), (int) d.def)); break;
                case T::Bool:   l.add (bparam (id, name, d.def > 0.5f)); break;
            }
        }
    }

    juce::StringArray dests, rates;
    for (auto* n : kGlobalDestNames) dests.add (n);
    const auto& table = DestTable::get();
    for (int i = 0; i < table.count; ++i) dests.add (streamParams()[table.param[(size_t) i]].name);
    for (auto* n : kDivisionNames) rates.add (n);
    for (int i = 0; i < kLanes; ++i)
    {
        const juce::String nm = "Lane " + juce::String (i + 1) + " ";
        l.add (bparam (lid (i, lOn), nm + "On", false));
        l.add (cparam (lid (i, lDest), nm + "Destination", dests, 0));
        l.add (cparam (lid (i, lTarget), nm + "Stream", itemsOf (kItemsTarget, 3), 2));
        l.add (cparam (lid (i, lRate), nm + "Rate", rates, kDiv116));
        l.add (iparam (lid (i, lLen), nm + "Length", 1, kSteps, 16));
        l.add (cparam (lid (i, lDir), nm + "Direction", itemsOf (kItemsDir, 5), 0));
        l.add (fparam (lid (i, lAmt), nm + "Amount", -100, 100, 50, "%"));
        l.add (fparam (lid (i, lSlew), nm + "Slew", 0, 100, 0, "%"));
        l.add (fparam (lid (i, lSwing), nm + "Swing", 0, 100, 0, "%"));
        l.add (fparam (lid (i, lChance), nm + "Chance", 0, 100, 100, "%"));
    }
    return l;
}

/** Decodes and builds impulse responses away from the audio thread. Polling keeps the audio thread out of it entirely. */
class TailsProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (TailsProcessor& p) : juce::Thread ("Multitails IR"), proc (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (25);
            proc.queueMidiFlush();
            if (! proc.prepared) continue;
            for (int s = 0; s < kStreams && ! threadShouldExit(); ++s)
            {
                proc.refreshIr (s, false);
                proc.engine->streams[(size_t) s]->conv.conv.collectGarbage();
            }
        }
    }
private:
    TailsProcessor& proc;
};

TailsProcessor::TailsProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "MULTITAILS", createLayout()),
      presets (*this)
{
    formats.registerBasicFormats();
    for (int s = 0; s < kStreams; ++s)
        for (int p = 0; p < kNumP; ++p)
            streamPtr[(size_t) s][(size_t) p] = state.getRawParameterValue (sid (s, p));
    for (int l = 0; l < kLanes; ++l)
        for (int p = 0; p < kNumLaneP; ++p)
            lanePtr[(size_t) l][(size_t) p] = state.getRawParameterValue (lid (l, p));
    gSync = state.getRawParameterValue ("g_sync");     gBpm = state.getRawParameterValue ("g_bpm");
    gRun = state.getRawParameterValue ("g_run");       gSplit = state.getRawParameterValue ("g_split");
    gFocus = state.getRawParameterValue ("g_focus");   gSmooth = state.getRawParameterValue ("g_smooth");
    gDecay = state.getRawParameterValue ("g_decay");   gSource = state.getRawParameterValue ("g_source");
    gIn = state.getRawParameterValue ("g_in");         gOut = state.getRawParameterValue ("g_out");
    gMix = state.getRawParameterValue ("g_mix");       gReset = state.getRawParameterValue ("g_reset");

    int index = 0;
    for (auto* p : getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
        ranged.push_back (r);
        rawValue.push_back (r != nullptr ? state.getRawParameterValue (r->paramID) : nullptr);
        if (r != nullptr) paramIndex[r->paramID] = index;
        ++index;
    }
    midiPending = std::make_unique<std::atomic<float>[]> (ranged.size());
    for (size_t i = 0; i < ranged.size(); ++i) midiPending[i].store (-1.0f);
    for (auto& s : previewT60) s.store (0.0f);

    pullParams();
    worker = std::make_unique<Worker> (*this);
    worker->startThread (juce::Thread::Priority::background);
}

TailsProcessor::~TailsProcessor()
{
    alive->store (false);
    worker->signalThreadShouldExit();
    worker->notify();
    worker->stopThread (4000);
}

bool TailsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void TailsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool wasPrepared = prepared;
    prepared = false;                     // the worker stays out while the engine is rebuilt
    const juce::ScopedLock lock (irLock);
    const bool rateChanged = sampleRate != sr || ! wasPrepared;
    sr = sampleRate;
    pullParams();
    engine->prepare (sampleRate);
    setLatencySamples (engine->latency());
    scratch.assign ((size_t) juce::jmax (samplesPerBlock, 4096) * 2, 0.0f);
    internalRunning = false;
    if (rateChanged) for (auto& b : built) b = {};
    for (int s = 0; s < kStreams; ++s) refreshIr (s, true);
    prepared = true;
}

void TailsProcessor::pullParams() noexcept
{
    auto& e = *engine;
    for (size_t s = 0; s < kStreams; ++s)
        for (size_t p = 0; p < kNumP; ++p)
            e.base[s][p] = streamPtr[s][p]->load (std::memory_order_relaxed);
    for (size_t l = 0; l < kLanes; ++l)
        for (size_t p = 0; p < kNumLaneP; ++p)
            e.lanes[l].v[p] = lanePtr[l][p]->load (std::memory_order_relaxed);
    auto& g = e.global;
    g.split = gSplit->load() > 0.5f;
    g.focus = gFocus->load();
    g.smooth = gSmooth->load();
    g.decay = gDecay->load();
    g.source = (int) gSource->load();
    g.input = gIn->load();
    g.output = gOut->load();
    g.mix = gMix->load();
}

void TailsProcessor::applyMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto m : midi)
    {
        if (m.numBytes > 3) continue;          // SysEx would allocate here, and nothing maps to it
        const auto msg = m.getMessage();
        midiMap.handle (msg,
            [this] (int p, float norm) {
                if (p < 0 || p >= (int) ranged.size() || ranged[(size_t) p] == nullptr) return;
                // the engine hears it now, the host and the interface at the next timer tick
                rawValue[(size_t) p]->store (ranged[(size_t) p]->convertFrom0to1 (norm), std::memory_order_relaxed);
                midiPending[(size_t) p].store (norm, std::memory_order_relaxed);
            },
            [this] (int p) {
                if (p < 0 || p >= (int) ranged.size() || ranged[(size_t) p] == nullptr) return 0.0f;
                return ranged[(size_t) p]->convertTo0to1 (rawValue[(size_t) p]->load (std::memory_order_relaxed));
            });
    }
}

void TailsProcessor::flushMidiMap()
{
    for (size_t i = 0; i < ranged.size(); ++i)
    {
        const float v = midiPending[i].exchange (-1.0f, std::memory_order_relaxed);
        if (v >= 0.0f && ranged[i] != nullptr)
        {
            ranged[i]->beginChangeGesture();
            ranged[i]->setValueNotifyingHost (v);
            ranged[i]->endChangeGesture();
        }
    }
}

/** Worker thread. No timer here: a plugin with no MIDI mapped posts nothing to the message thread. */
void TailsProcessor::queueMidiFlush()
{
    bool any = false;
    for (size_t i = 0; i < ranged.size() && ! any; ++i)
        any = midiPending[i].load (std::memory_order_relaxed) >= 0.0f;
    if (! any || flushQueued.exchange (true)) return;
    juce::MessageManager::callAsync ([this, token = alive] {
        if (! token->load()) return;
        flushQueued = false;
        flushMidiMap();
    });
}

juce::String TailsProcessor::paramName (int i) const { return juce::isPositiveAndBelow (i, (int) ranged.size()) && ranged[(size_t) i] != nullptr ? ranged[(size_t) i]->getName (64) : juce::String(); }
juce::String TailsProcessor::paramId (int i) const   { return juce::isPositiveAndBelow (i, (int) ranged.size()) && ranged[(size_t) i] != nullptr ? ranged[(size_t) i]->paramID : juce::String(); }

Transport TailsProcessor::readTransport (int numSamples, bool bypassed)
{
    Transport t;
    if (gSync->load() > 0.5f)
    {
        const bool run = gRun->load() > 0.5f;
        if (run && ! internalRunning) { internalPpq = 0.0; t.ppq = 0.0; }
        internalRunning = run;
        t.playing = run;
        t.bpm = juce::jmax (1.0f, gBpm->load());
        t.ppq = internalPpq;
        if (run) internalPpq += numSamples * t.bpm / (60.0 * sr);
    }
    else
    {
        internalRunning = false;
        juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
        if (auto* ph = getPlayHead()) pos = ph->getPosition();
        if (pos.hasValue())
        {
            t.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
            t.ppq = pos->getPpqPosition().orFallback (0.0);
            t.bpm = pos->getBpm().orFallback (120.0);
            if (t.bpm <= 0.0) t.bpm = 120.0;
        }
    }
    if (bypassed) t.playing = false;
    if (t.playing && ! wasPlaying) engine->seqReset (gSync->load() > 0.5f ? t.ppq : 0.0);    // a new start: back on the bar grid
    wasPlaying = t.playing;
    uiPlaying.store (t.playing, std::memory_order_relaxed);
    uiBpm.store ((float) t.bpm, std::memory_order_relaxed);
    return t;
}

void TailsProcessor::render (float* l, float* r, int n, const Transport& t) noexcept
{
    engine->process (l, r, n, t);
}

void TailsProcessor::runBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), inCh = getTotalNumInputChannels(), outCh = buffer.getNumChannels();
    applyMidi (midi);
    midi.clear();
    pullParams();
    engine->global.bypass = bypassed;

    const bool reset = gReset->load() > 0.5f;
    const auto t = readTransport (n, bypassed);
    if (reset && ! resetHeld)
    {
        if (gSync->load() > 0.5f) { internalPpq = 0.0; engine->seqReset (0.0); }
        else engine->seqReset (t.ppq);
    }
    resetHeld = reset;

    if (n == 0 || outCh == 0) return;
    if (outCh >= 2)
    {
        if (inCh == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
        render (buffer.getWritePointer (0), buffer.getWritePointer (1), n, t);
    }
    else
    {   // mono track: the right side of the engine runs on scratch space
        auto* l = buffer.getWritePointer (0);
        auto tt = t;
        int done = 0;
        const int room = juce::jmax (1, (int) scratch.size());
        while (done < n)
        {
            const int m = juce::jmin (room, n - done);
            juce::FloatVectorOperations::copy (scratch.data(), l + done, m);
            render (l + done, scratch.data(), m, tt);
            juce::FloatVectorOperations::add (l + done, scratch.data(), m);
            juce::FloatVectorOperations::multiply (l + done, 0.5f, m);
            tt.ppq += m * tt.bpm / (60.0 * sr);
            done += m;
        }
    }
}

void TailsProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)         { runBlock (b, m, false); }
void TailsProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { runBlock (b, m, true); }

// ---------------- impulse responses

juce::String TailsProcessor::userPathOf (int s) const
{
    const juce::SpinLock::ScopedLockType l (pathLock);
    return userPath[(size_t) s];
}

TailsProcessor::Wanted TailsProcessor::wanted (int s) const
{
    Wanted w;
    w.ir = juce::jlimit (0, kNumIrs - 1, (int) sf (s, cIr));
    w.size = sf (s, cSize) * 0.01f;
    w.reversed = sf (s, cRev) > 0.5f;
    if (w.ir == kUserIr) w.path = userPathOf (s);
    w.rate = sr;
    return w;
}

const TailsProcessor::Decoded* TailsProcessor::decode (const Wanted& w)
{
    const juce::String key = w.ir == kUserIr ? w.path : juce::String ("factory") + juce::String (w.ir);
    if (auto it = decoded.find (key); it != decoded.end()) return &it->second;

    std::unique_ptr<juce::AudioFormatReader> reader;
    if (w.ir == kUserIr)
    {
        if (w.path.isEmpty()) return nullptr;
        juce::File f (w.path);
        if (! f.existsAsFile()) return nullptr;
        reader.reset (formats.createReaderFor (f));
    }
    else
    {
        const juce::String prefix = juce::String (w.ir).paddedLeft ('0', 2) + "_";
        for (int i = 0; i < IrFiles::namedResourceListSize; ++i)
        {
            if (! juce::String (IrFiles::originalFilenames[i]).startsWith (prefix)) continue;
            int size = 0;
            const char* data = IrFiles::getNamedResource (IrFiles::namedResourceList[i], size);
            reader.reset (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false)));
            break;
        }
    }
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) return nullptr;

    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * kMaxIrSeconds * 2.0));
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    reader->read (&buf, 0, len, 0, true, true);        // a mono file fills both sides
    Decoded d;
    d.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    d.r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
    d.rate = reader->sampleRate;
    if (decoded.size() > 24) decoded.clear();
    return &(decoded[key] = std::move (d));
}

bool TailsProcessor::refreshIr (int s, bool immediately)
{
    const juce::ScopedTryLock quick (irLock);
    if (! immediately && ! quick.isLocked()) return false;
    const juce::ScopedLock lock (irLock);

    const auto w = wanted (s);
    if (w == built[(size_t) s]) return false;
    const auto* d = decode (w);
    irError[(size_t) s].store (d == nullptr, std::memory_order_relaxed);
    built[(size_t) s] = w;
    if (d == nullptr)
    {
        for (auto& v : preview[(size_t) s]) v.store (0.0f, std::memory_order_relaxed);
        previewSerial.fetch_add (1);
        return false;
    }

    // Size stretches the file; the level stays the same because the energy is normalised afterwards
    const double ratio = sr / d->rate * (double) w.size;
    auto l = resampleIr (d->l.data(), (int) d->l.size(), ratio);
    auto r = resampleIr (d->r.data(), (int) d->r.size(), ratio);
    double energy = 0.0;
    for (size_t i = 0; i < l.size(); ++i) energy += (double) l[i] * l[i] + (double) r[i] * r[i];
    const float g = energy > 0.0 ? (float) std::sqrt (2.0 / energy) : 1.0f;
    for (auto& v : l) v *= g;
    for (auto& v : r) v *= g;

    auto* ir = buildIr (std::move (l), std::move (r), sr, w.reversed);
    for (int i = 0; i < kPreviewPoints; ++i) preview[(size_t) s][(size_t) i].store (ir->preview[(size_t) i], std::memory_order_relaxed);
    previewT60[(size_t) s].store (ir->t60, std::memory_order_relaxed);
    previewSerial.fetch_add (1);
    auto& conv = engine->streams[(size_t) s]->conv.conv;
    if (immediately) conv.installNow (ir); else conv.post (ir);
    return true;
}

void TailsProcessor::syncImpulseResponses()
{
    if (sr <= 0.0) return;
    const bool running = prepared;
    prepared = false;
    const juce::ScopedLock lock (irLock);
    for (int s = 0; s < kStreams; ++s) refreshIr (s, false);
    prepared = running;
}

void TailsProcessor::loadUserIr (int s, const juce::File& f)
{
    {
        const juce::SpinLock::ScopedLockType l (pathLock);
        userPath[(size_t) s] = f.getFullPathName();
    }
    setParam (sid (s, cIr), (float) kUserIr);
    const juce::ScopedLock lock (irLock);
    built[(size_t) s] = {};      // the same menu item can mean a new file
}

juce::String TailsProcessor::irDisplayName (int s) const
{
    const int i = juce::jlimit (0, kNumIrs - 1, (int) sf (s, cIr));
    if (i != kUserIr) return kIrNames[i];
    const auto p = userPathOf (s);
    return p.isEmpty() ? juce::String ("No file loaded") : juce::File (p).getFileNameWithoutExtension();
}

// ---------------- parameters by id

void TailsProcessor::setParam (const juce::String& id, float value)
{
    if (auto* r = state.getParameter (id))
    {
        r->beginChangeGesture();
        r->setValueNotifyingHost (r->convertTo0to1 (value));
        r->endChangeGesture();
    }
}

void TailsProcessor::setOrder (int s, const int* order) { setParam (sid (s, sOrder), (float) rankFromOrder (order)); }
void TailsProcessor::getOrder (int s, int* order) const { orderFromRank ((int) sf (s, sOrder), order); }

// ---------------- state

juce::ValueTree TailsProcessor::patternsToTree() const
{
    juce::ValueTree tree ("PATTERNS");
    for (int l = 0; l < kLanes; ++l)
    {
        juce::StringArray cells;
        for (auto& c : engine->patterns[(size_t) l].step) cells.add (juce::String (c.load(), 3));
        juce::ValueTree node ("LANE");
        node.setProperty ("i", l, nullptr);
        node.setProperty ("steps", cells.joinIntoString (" "), nullptr);
        tree.appendChild (node, nullptr);
    }
    return tree;
}

void TailsProcessor::patternsFromTree (const juce::ValueTree& tree)
{
    for (const auto node : tree)
    {
        const int l = node.getProperty ("i", -1);
        if (! juce::isPositiveAndBelow (l, kLanes)) continue;
        auto cells = juce::StringArray::fromTokens (node.getProperty ("steps").toString(), " ", "");
        for (int i = 0; i < kSteps; ++i)
            engine->patterns[(size_t) l].step[(size_t) i].store (i < cells.size() ? juce::jlimit (-1.0f, 1.0f, cells[i].getFloatValue()) : 0.0f);
    }
}

juce::ValueTree TailsProcessor::midiToTree() const
{
    juce::ValueTree tree ("MIDIMAP");
    for (auto& s : midiMap.slots)
    {
        if (s.source.load() == msOff) continue;
        juce::ValueTree node ("SLOT");
        node.setProperty ("src", s.source.load(), nullptr);
        node.setProperty ("num", s.number.load(), nullptr);
        node.setProperty ("ch", s.channel.load(), nullptr);
        node.setProperty ("param", paramId (s.param.load()), nullptr);     // by id: the index could change between versions
        node.setProperty ("mode", s.mode.load(), nullptr);
        node.setProperty ("lo", s.lo.load(), nullptr);
        node.setProperty ("hi", s.hi.load(), nullptr);
        tree.appendChild (node, nullptr);
    }
    return tree;
}

void TailsProcessor::midiFromTree (const juce::ValueTree& tree)
{
    for (auto& s : midiMap.slots) s.clear();
    int i = 0;
    for (const auto node : tree)
    {
        if (i >= kMidiSlots) break;
        const int p = indexOfParam (node.getProperty ("param").toString());
        if (p < 0) continue;
        auto& s = midiMap.slots[(size_t) i++];
        s.number = (int) node.getProperty ("num", 1);
        s.channel = (int) node.getProperty ("ch", 0);
        s.param = p;
        s.mode = (int) node.getProperty ("mode", 0);
        s.lo = (float) node.getProperty ("lo", 0.0f);
        s.hi = (float) node.getProperty ("hi", 1.0f);
        s.source = (int) node.getProperty ("src", 0);
    }
    midiMap.learnedSerial.fetch_add (1);
}

std::unique_ptr<juce::XmlElement> TailsProcessor::createPresetXml()
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName ("PATTERNS"), nullptr);
    tree.removeChild (tree.getChildWithName ("MIDIMAP"), nullptr);
    tree.removeChild (tree.getChildWithName ("USERIR"), nullptr);
    tree.appendChild (patternsToTree(), nullptr);
    juce::ValueTree ir ("USERIR");
    for (int s = 0; s < kStreams; ++s) ir.setProperty ("path" + juce::String (s), userPathOf (s), nullptr);
    tree.appendChild (ir, nullptr);
    return tree.createXml();
}

bool TailsProcessor::applyPresetXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (state.state.getType())) return false;
    auto tree = juce::ValueTree::fromXml (xml);
    if (auto pat = tree.getChildWithName ("PATTERNS"); pat.isValid()) patternsFromTree (pat);
    if (auto ir = tree.getChildWithName ("USERIR"); ir.isValid())
    {
        const juce::SpinLock::ScopedLockType l (pathLock);
        for (int s = 0; s < kStreams; ++s) userPath[(size_t) s] = ir.getProperty ("path" + juce::String (s)).toString();
    }
    state.replaceState (tree);
    return true;
}

void TailsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = createPresetXml();
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.appendChild (midiToTree(), nullptr);
    tree.setProperty ("uiScale", uiScale.load(), nullptr);
    tree.setProperty ("preset", presets.getCurrent(), nullptr);
    if (auto out = tree.createXml()) copyXmlToBinary (*out, dest);
}

void TailsProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! applyPresetXml (*xml)) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (auto m = tree.getChildWithName ("MIDIMAP"); m.isValid()) midiFromTree (m);
    uiScale = (float) tree.getProperty ("uiScale", 1.0f);
    presets.markCurrent ((int) tree.getProperty ("preset", 0));
    if (prepared) syncImpulseResponses();
}

// ---------------- presets

void PresetEdit::g (const char* id, float v)  { p.setParam (id, v); }
void PresetEdit::tr (const char* id, float v) { p.setParam (juce::String ("tr_") + id, v); }
void PresetEdit::to (const char* id, float v) { p.setParam (juce::String ("to_") + id, v); }

void PresetEdit::chain (int stream, std::initializer_list<int> order)
{
    int o[kNumFx], i = 0, used = 0;
    for (int f : order) if (i < kNumFx) { o[i++] = f; used |= 1 << f; }
    for (int f = 0; f < kNumFx; ++f) if ((used & (1 << f)) == 0) o[i++] = f;
    p.setOrder (stream, o);
}

void PresetEdit::lane (int oneBased, const char* dest, int target, int rate, float amount, std::initializer_list<float> steps, float slew, int dir)
{
    const int l = oneBased - 1;
    int index = 0;
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.state.getParameter (TailsProcessor::lid (l, lDest))))
        index = c->choices.indexOf (dest);
    jassert (index > 0);
    p.setParam (TailsProcessor::lid (l, lOn), 1);
    p.setParam (TailsProcessor::lid (l, lDest), (float) index);
    p.setParam (TailsProcessor::lid (l, lTarget), (float) target);
    p.setParam (TailsProcessor::lid (l, lRate), (float) rate);
    p.setParam (TailsProcessor::lid (l, lAmt), amount);
    p.setParam (TailsProcessor::lid (l, lLen), (float) steps.size());
    p.setParam (TailsProcessor::lid (l, lSlew), slew);
    p.setParam (TailsProcessor::lid (l, lDir), (float) dir);
    int i = 0;
    for (float s : steps) if (i < kSteps) p.engine->patterns[(size_t) l].step[(size_t) i++].store (s);
}

namespace tails
{
namespace
{
constexpr int kBar = 2, k14 = kDiv14, k18 = 9, k18D = kDiv18D, k116 = kDiv116, k116T = 14, k5to1 = 18, k7to1 = 19;
int irIndex (const char* name) { for (int i = 0; i < kNumIrs; ++i) if (juce::String (kIrNames[i]) == name) return i; return 0; }
constexpr float O = 1.0f, X = -1.0f;
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> p = {
        { "Init", [] (PresetEdit&) {} },

        { "Amber Halls", [] (PresetEdit& e) {    // hits ring in a long grainy hall, the sustain stays close and wide
            e.g ("g_source", 1); e.g ("g_decay", 70);
            e.chain (0, { fxFilter, fxDelay, fxConv });
            e.tr ("gain", 3); e.to ("gain", 3);
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Digital Long")); e.tr ("c_mix", 70); e.tr ("c_decay", 72); e.tr ("c_damp", 38); e.tr ("c_width", 75);
            e.tr ("d_on", 1); e.tr ("d_mix", 24); e.tr ("d_note", (float) k18D); e.tr ("d_fb", 42); e.tr ("d_lp", 3800); e.tr ("d_hp", 260); e.tr ("d_analog", 2);
            e.tr ("f_on", 1); e.tr ("f_hpf", 180);
            e.chain (1, { fxChorus, fxConv, fxFilter });
            e.to ("h_on", 1); e.to ("h_rate", 0.32f); e.to ("h_depth", 58); e.to ("h_mode", 2); e.to ("h_mix", 32);
            e.to ("c_on", 1); e.to ("c_ir", (float) irIndex ("Digital Room")); e.to ("c_mix", 22); e.to ("c_width", 70);
            e.lane (1, "Conv Decay", 0, k116, 40, { 0, 0, -.7f, 0, 0, 0, -.7f, 0, 0, -.7f, 0, 0, 0, 0, -.7f, 0 });
        } },

        { "Incunabula Plate", [] (PresetEdit& e) {  // metal on the hits, gated by the grid; lanes of different lengths drift apart
            e.g ("g_source", 1); e.g ("g_decay", 22); e.g ("g_focus", 12); e.tr ("gain", 2); e.to ("gain", 2);
            e.chain (0, { fxConv, fxTape });
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Plate Medium")); e.tr ("c_mix", 78); e.tr ("c_pre", 8); e.tr ("c_low", 240);
            e.tr ("t_on", 1); e.tr ("t_mix", 22); e.tr ("t_sync", 1); e.tr ("t_note", (float) k18D); e.tr ("t_fb", 38); e.tr ("t_h1", 0); e.tr ("t_h2", 1); e.tr ("t_h3", 0); e.tr ("t_h4", 1);
            e.chain (1, { fxFilter, fxChorus });
            e.to ("f_on", 1); e.to ("f_hpf", 90); e.to ("f_lpf", 3400); e.to ("f_lpq", 42);
            e.to ("h_on", 1); e.to ("h_mix", 38); e.to ("h_rate", 0.44f); e.to ("h_grit", 34);
            e.lane (1, "Conv On", 0, k116, 100, { O, X, X, O, X, X, O, X, O, X, X, O, X, O, X });
            e.lane (2, "LPF Freq", 1, k116T, 30, { 0, .4f, -.2f, .8f, -.5f, .3f, 1, -.8f, .1f, .6f, -.3f }, 35);
            e.lane (3, "Conv Decay", 0, k18, 45, { -.8f, 0, -.3f, 0, -.8f, -.2f, 0 });
        } },

        { "Ambient Works Tape", [] (PresetEdit& e) {  // soft cassette-like wobble, chorus, a digital hall behind everything
            e.g ("g_source", 5); e.g ("g_decay", 90); e.tr ("gain", 3); e.to ("gain", 4);
            e.chain (0, { fxTape, fxConv });
            e.tr ("t_on", 1); e.tr ("t_mix", 45); e.tr ("t_time", 460); e.tr ("t_mode", 1); e.tr ("t_fb", 52); e.tr ("t_tone", -40); e.tr ("t_wow", 34); e.tr ("t_flut", 30); e.tr ("t_noise", 8);
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Digital Hall")); e.tr ("c_mix", 44); e.tr ("c_width", 70);
            e.chain (1, { fxChorus, fxTape, fxConv, fxFilter });
            e.to ("h_on", 1); e.to ("h_rate", 0.26f); e.to ("h_depth", 62); e.to ("h_mix", 38); e.to ("h_tone", 5200);
            e.to ("t_on", 1); e.to ("t_mode", 2); e.to ("t_mix", 22); e.to ("t_time", 620); e.to ("t_tone", -55); e.to ("t_wow", 42); e.to ("t_flut", 26); e.to ("t_noise", 10);
            e.to ("c_on", 1); e.to ("c_ir", (float) irIndex ("Digital Hall")); e.to ("c_mix", 30); e.to ("c_width", 70);
            e.to ("f_on", 1); e.to ("f_lpf", 7800); e.to ("f_hpf", 45);
        } },

        { "Echorec Swell", [] (PresetEdit& e) {
            e.g ("g_split", 0); e.g ("g_focus", -100); e.to ("gain", 3);
            e.to ("t_on", 1); e.to ("t_mode", 2); e.to ("t_time", 300); e.to ("t_fb", 62); e.to ("t_mix", 55); e.to ("t_drive", 40);
            e.to ("t_wow", 16); e.to ("t_flut", 18); e.to ("t_spread", 70); e.to ("t_h1", 1); e.to ("t_h2", 1); e.to ("t_h3", 1); e.to ("t_h4", 1);
        } },

        { "Echorec Heads", [] (PresetEdit& e) {     // the sequencer flips the heads, as a hand on the selector would
            e.g ("g_source", 1);
            e.tr ("t_on", 1); e.tr ("t_sync", 1); e.tr ("t_note", (float) k14); e.tr ("t_mix", 58); e.tr ("t_fb", 60); e.tr ("t_drive", 45); e.tr ("t_spread", 80);
            e.lane (1, "Tape Head 1", 0, k18, 100, { O, X, X, O, X, X, O, X });
            e.lane (2, "Tape Head 2", 0, k18, 100, { X, O, X, X, O, O, X, X });
            e.lane (3, "Tape Swell", 0, k14, 20, { -.4f, 0, .3f, 1 });
        } },

        { "EMT Plate", [] (PresetEdit& e) {
            e.g ("g_split", 0); e.g ("g_focus", -100);
            e.to ("c_on", 1); e.to ("c_ir", (float) irIndex ("Plate Medium")); e.to ("c_mix", 32); e.to ("c_pre", 18); e.to ("c_low", 110);
        } },

        { "Gated Snare", [] (PresetEdit& e) {
            e.g ("g_source", 1); e.g ("g_decay", 18);
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Digital Gate")); e.tr ("c_mix", 62); e.tr ("c_low", 300);
        } },

        { "Reverse Ghosts", [] (PresetEdit& e) {
            e.g ("g_source", 1); e.g ("g_decay", 30);
            e.chain (0, { fxConv, fxDelay });
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Digital Reverse")); e.tr ("c_mix", 64); e.tr ("c_high", 6000);
            e.tr ("d_on", 1); e.tr ("d_note", (float) k18D); e.tr ("d_mix", 22); e.tr ("d_ping", 1); e.tr ("d_fb", 35); e.tr ("d_hp", 400);
        } },

        { "Leslie Ramp", [] (PresetEdit& e) {       // chorale and tremolo every two bars; the rotors take their time, as they should
            e.g ("g_split", 0); e.g ("g_focus", -100); e.to ("gain", 3);
            e.to ("r_on", 1); e.to ("r_drive", 25);
            e.lane (1, "Rotary Speed", 1, kBar, 100, { 0, 1 });
        } },

        { "Rotary Keys", [] (PresetEdit& e) {
            e.g ("g_source", 3); e.to ("gain", 3);
            e.chain (1, { fxRotary, fxConv });
            e.to ("r_on", 1); e.to ("r_speed", 1); e.to ("r_drive", 30); e.to ("c_on", 1); e.to ("c_ir", (float) irIndex ("Plate Short")); e.to ("c_mix", 18);
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Studio")); e.tr ("c_mix", 30);
        } },

        { "Filter Steps", [] (PresetEdit& e) {
            e.g ("g_split", 0); e.g ("g_focus", -100);
            e.to ("f_on", 1); e.to ("f_lpf", 900); e.to ("f_lpq", 58); e.to ("f_lps", 1); e.to ("f_hpf", 60);
            e.lane (1, "LPF Freq", 1, k116, 42, { -.6f, .1f, 1, -.3f, .5f, -.8f, .8f, 0, -.6f, .3f, 1, -.9f, .4f, -.1f, .7f, -.4f }, 12);
            e.lane (2, "LPF Reso", 1, k5to1, 30, { 0, .5f, 1, .2f, .7f, -.4f, .3f });
        } },

        { "Ping Pong Tails", [] (PresetEdit& e) {   // the send opens on the grid and the repeats keep ringing after it closes
            e.g ("g_source", 2);
            e.chain (0, { fxDelay, fxConv });
            e.tr ("d_on", 1); e.tr ("d_note", (float) k18D); e.tr ("d_ping", 1); e.tr ("d_mix", 52); e.tr ("d_fb", 62); e.tr ("d_hp", 320); e.tr ("d_lp", 5200); e.tr ("d_analog", 3); e.tr ("d_depth", 8);
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Plate Long")); e.tr ("c_mix", 22); e.tr ("c_damp", 40);
            e.lane (1, "Delay On", 0, k116, 100, { O, X, X, X, X, X, O, X, X, X, O, X, X, X, X, X });
        } },

        { "Cathedral Wash", [] (PresetEdit& e) {
            e.g ("g_source", 5);
            e.to ("c_on", 1); e.to ("c_ir", (float) irIndex ("Cathedral")); e.to ("c_mix", 48);
            e.tr ("c_on", 1); e.tr ("c_ir", (float) irIndex ("Church")); e.tr ("c_mix", 30); e.tr ("c_pre", 40);
        } },

        { "LoFi Dub Hits", [] (PresetEdit& e) {
            e.g ("g_source", 1); e.g ("g_decay", 35);
            e.tr ("d_on", 1); e.tr ("d_note", (float) k18D); e.tr ("d_lofi", 1); e.tr ("d_fb", 88); e.tr ("d_mix", 48); e.tr ("d_analog", 4); e.tr ("d_lp", 3200); e.tr ("d_hp", 240);
            e.lane (1, "Delay Feedback", 0, k14, 20, { 0, 0, 0, 1 });
            e.lane (2, "Delay HP", 0, k7to1, 20, { 0, .3f, 1, -.3f, .5f });
        } },
    };
    return p;
}

juce::File PresetManager::userDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Multitails Presets");
    d.createDirectory();
    return d;
}

void PresetManager::refresh()
{
    names.clear();
    for (auto& f : factoryPresets()) names.add (juce::String ("Factory / ") + f.name);
    userFiles = userDir().findChildFiles (juce::File::findFiles, false, "*.mtails");
    userFiles.sort();
    for (auto& f : userFiles) names.add (f.getFileNameWithoutExtension());
}

void PresetManager::clearToDefaults()
{
    for (auto* p : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (std::abs (r->getValue() - r->getDefaultValue()) > 1.0e-6f) r->setValueNotifyingHost (r->getDefaultValue());
    for (auto& pat : proc.engine->patterns)
        for (auto& s : pat.step) s.store (0.0f);
}

void PresetManager::load (int index)
{
    if (index < 0 || index >= names.size()) return;
    current = index;
    if (index < numFactory())
    {
        clearToDefaults();
        PresetEdit e { proc };
        factoryPresets()[(size_t) index].build (e);
    }
    else if (auto xml = juce::XmlDocument::parse (userFiles[index - numFactory()]))
        proc.applyPresetXml (*xml);
}

bool PresetManager::save (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto f = userDir().getChildFile (clean + ".mtails");
    auto xml = proc.createPresetXml();
    const bool ok = xml != nullptr && xml->writeTo (f);
    refresh();
    current = names.indexOf (clean);
    return ok;
}

bool PresetManager::removeCurrent()
{
    if (current < numFactory()) return false;
    const bool ok = userFiles[current - numFactory()].deleteFile();
    refresh();
    current = 0;
    return ok;
}
} // namespace tails

#ifndef MULTITAILS_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TailsProcessor(); }
#endif
juce::AudioProcessorEditor* TailsProcessor::createEditor() { return new TailsEditor (*this); }
