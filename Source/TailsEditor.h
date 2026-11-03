#pragma once
#include "TailsProcessor.h"
#include "TailsUI.h"

/** The whole interface, laid out on a fixed 1280 x 800 grid. TailsEditor scales it to the window. */
class TailsView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    static constexpr int kW = 1280, kH = 800;

    explicit TailsView (TailsProcessor& p)
        : proc (p), chains (p), wave (p), well (p), lanesView (p), steps (p), midi (p),
          inMeter (p.engine->ui.inPeak), outMeter (p.engine->ui.outPeak)
    {
        setLookAndFeel (&lnf);
        using K = tails::Control::Kind;
        using namespace tails;

        auto global = [&] (const char* id, const char* label, K kind, bool dark = false) {
            auto* ctl = globals.add (new Control (p.state, id, label, kind));
            if (dark) { ctl->getProperties().set ("sgDark", true); ctl->setCaptionColour (c::onPlate); }
            wire (*ctl);
            addAndMakeVisible (ctl);
            globalById.set (id, ctl);
        };
        global ("g_sync", "SYNC", K::Menu, true); global ("g_bpm", "BPM", K::Box, true); global ("g_run", "RUN", K::Toggle, true);
        global ("g_split", "SPLIT", K::Toggle);
        global ("g_focus", "Focus", K::Knob); global ("g_smooth", "Smoothing", K::Knob); global ("g_decay", "Trans Decay", K::Knob);
        global ("g_source", "SOURCE", K::Menu);
        global ("g_in", "Input", K::Knob); global ("g_mix", "Mix", K::Knob); global ("g_out", "Output", K::Knob);

        for (int s = 0; s < kStreams; ++s)
        {
            auto add = [&] (int param, const char* label, K kind) {
                auto* ctl = strip.add (new Control (p.state, TailsProcessor::sid (s, param), label, kind));
                wire (*ctl);
                addAndMakeVisible (ctl);
            };
            add (sOn, "ON", K::Toggle); add (sSolo, "SOLO", K::Toggle); add (sTails, "TAILS", K::Toggle); add (sGain, "GAIN", K::HBar);
        }

        addAndMakeVisible (chains); addAndMakeVisible (wave); addAndMakeVisible (well);
        addAndMakeVisible (inMeter); addAndMakeVisible (outMeter);
        addAndMakeVisible (lanesView); addAndMakeVisible (steps);
        addChildComponent (midi);
        chains.onSelect = [this] (int s, int f) { selectFx (s, f); };
        chains.onRightBlock = [this] (int s, int f) { showParamMenu (TailsProcessor::sid (s, ChainView::onParam (f))); };
        lanesView.onSelect = [this] (int l) { selectLane (l); };

        for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &delBtn, &resetBtn, &clrBtn, &rndBtn, &loadBtn }) addAndMakeVisible (b);
        addAndMakeVisible (midiBtn);
        prevBtn.setButtonText ("<"); nextBtn.setButtonText (">");
        saveBtn.setButtonText ("SAVE"); delBtn.setButtonText ("DEL");
        midiBtn.setButtonText ("MIDI MAP");
        midiBtn.setClickingTogglesState (true);
        midiBtn.onClick = [this] { showMidi (midiBtn.getToggleState()); };
        resetBtn.setButtonText ("RESET"); clrBtn.setButtonText ("CLR"); rndBtn.setButtonText ("RND"); loadBtn.setButtonText ("LOAD IR");
        for (auto* b : { &resetBtn, &clrBtn, &rndBtn }) b->getProperties().set ("sgDeck", true);
        resetBtn.onClick = [this] { proc.setParam ("g_reset", 1); juce::Timer::callAfterDelay (60, [safe = juce::Component::SafePointer<TailsView> (this)] { if (safe != nullptr) safe->proc.setParam ("g_reset", 0); }); };
        clrBtn.onClick = [this] { for (auto& s : proc.engine->patterns[(size_t) lane].step) s.store (0.0f); };
        rndBtn.onClick = [this] {
            auto& r = juce::Random::getSystemRandom();
            for (auto& s : proc.engine->patterns[(size_t) lane].step) s.store (r.nextFloat() * 2.0f - 1.0f);
        };
        loadBtn.onClick = [this] { chooseIr(); };

        addAndMakeVisible (presetBox);
        presetBox.getProperties().set ("sgDark", true);
        refreshPresets();
        presetBox.onChange = [this] {
            const int i = presetBox.getSelectedItemIndex();
            if (i >= 0 && i != proc.presets.getCurrent()) proc.presets.load (i);
        };
        prevBtn.onClick = [this] { proc.presets.prev(); refreshPresets(); };
        nextBtn.onClick = [this] { proc.presets.next(); refreshPresets(); };
        delBtn.onClick = [this] { proc.presets.removeCurrent(); refreshPresets(); };
        saveBtn.onClick = [this] {
            auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", proc.presets.getCurrent() >= proc.presets.numFactory() ? proc.presets.getNames()[proc.presets.getCurrent()] : "My preset");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
                if (r == 1) { proc.presets.save (w->getTextEditorContents ("name")); refreshPresets(); }
            }), true);
        };

        sendLookAndFeelChange();
        setSize (kW, kH);
        selectFx (0, fxConv);
        selectLane (0);
        startTimerHz (30);
    }
    ~TailsView() override { setLookAndFeel (nullptr); }

    void selectFx (int s, int f)
    {
        stream = s; fx = f;
        chains.setSelected (s, f);
        well.setFx (s, f);
        buildFxControls();
        repaint();
    }

    void selectLane (int l)
    {
        lane = l;
        lanesView.setSelected (l);
        steps.setLane (l);
        buildLaneControls();
        repaint();
    }

    void showMidi (bool on)
    {
        midiBtn.setToggleState (on, juce::dontSendNotification);
        midi.refresh();
        midi.setVisible (on);
        chains.setVisible (! on); well.setVisible (! on);
        for (auto* c : fxControls) c->setVisible (! on);
        loadBtn.setVisible (! on && fx == tails::fxConv);
        repaint();
    }

    void tickForSnapshot() { for (int i = 0; i < 8; ++i) timerCallback(); }

    // ---- files dropped on the window go to the selected stream
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && isAudio (files[0]); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        proc.loadUserIr (stream, juce::File (files[0]));
        selectFx (stream, tails::fxConv);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace tails;
        const float W = (float) getWidth(), H = (float) getHeight();
        // ground: fox orange, a sky-blue slab with a plate edge, a light checkerboard over both, two white-tipped tails
        g.fillAll (c::ground);
        juce::Path band;
        band.addQuadrilateral (700, kHeaderH, W, kHeaderH, W, H, 1020, H);
        g.setColour (c::slab);
        g.fillPath (band);
        g.setColour (c::checker);
        for (int y = kHeaderH; y < getHeight(); y += 32)
            for (int x = (((y - kHeaderH) / 32) % 2) * 32; x < getWidth(); x += 64)
                g.fillRect (x, y, 32, 32);
        g.setColour (c::plate);
        g.drawLine (700, kHeaderH, 1020, H, 6.0f);
        foxTail (g, 1300.0f, 1120.0f, 900.0f, -1.42f, -0.30f, 150.0f);
        foxTail (g, 1300.0f, 1120.0f, 690.0f, -1.30f, -0.12f, 130.0f);

        g.setColour (c::groundAlt);
        g.fillRect (0.0f, 0.0f, W, (float) kHeaderH);
        furBand (g, { 0.0f, (float) kHeaderH - 6.0f, W, 6.0f });
        g.setFont (black (34.0f, -0.01f));
        const juce::String name ("MULTITAILS");
        g.setColour (c::go);      g.drawText (name, 22, 15, 340, 40, juce::Justification::centredLeft);
        g.setColour (c::signal);  g.drawText (name, 19, 12, 340, 40, juce::Justification::centredLeft);
        g.setColour (c::onPlate); g.drawText (name, 16, 9, 340, 40, juce::Justification::centredLeft);
        taggedPlate (g, { 330, 17, 300, 34 }, c::tag);

        const bool freeRun = proc.state.getRawParameterValue ("g_sync")->load() > 0.5f;
        auto console = juce::Rectangle<float> (kConsoleX, 12.0f, (freeRun ? kRunX - 12.0f : W - kMargin) - kConsoleX, 46.0f);
        g.setColour (c::plate);
        g.fillRoundedRectangle (console, 4.0f);
        g.setColour (c::onPlate.withAlpha (0.12f));
        g.drawRoundedRectangle (console.reduced (0.5f), 4.0f, 1.0f);
        g.fillRect (kConsoleX + 96.0f, 20.0f, 1.0f, 30.0f);
        if (! freeRun)
        {
            g.setColour (c::onPlate.withAlpha (0.8f));
            g.setFont (bold (11.0f, 0.06f));
            g.drawText ("TEMPO", (int) kConsoleX + 110, 16, 80, 14, juce::Justification::left);
            g.setFont (pixel (10.0f));
            g.drawText ("BPM  DAW", (int) kConsoleX + 110, 38, 80, 12, juce::Justification::left);
            g.setColour (c::accent);
            g.setFont (pixel (20.0f));
            g.drawText (juce::String (proc.uiBpm.load(), 1), (int) (W - kMargin) - 100, 23, 88, 24, juce::Justification::right);
        }

        // left column
        glassPanel (g, { 16, 88, 376, 696 }, "Transient and tonal", "Split");
        g.setColour (c::ink.withAlpha (0.14f));
        g.fillRect (32, 454, 344, 1);
        g.fillRect (32, 620, 344, 1);
        g.setColour (c::inkSoft);
        g.setFont (medium (10.0f, 0.3f));
        g.drawText ("STREAMS", 34, 460, 120, 12, juce::Justification::left);
        g.drawText ("OUTPUT", 34, 626, 120, 12, juce::Justification::left);
        g.setColour (c::ink);
        g.setFont (black (13.0f, 0.02f));
        g.drawText ("TRANSIENT", 32, 482, 110, 16, juce::Justification::left);
        g.drawText ("TONAL", 32, 552, 110, 16, juce::Justification::left);
        g.setFont (pixel (10.0f));
        g.setColour (c::inkSoft);
        g.drawText ("LATENCY " + juce::String (proc.getLatencySamples()) + " SAMPLES", 174, 626, 202, 12, juce::Justification::right);
        g.drawText ("IN", 302, 654, 74, 12, juce::Justification::left);
        g.drawText ("OUT", 302, 690, 74, 12, juce::Justification::left);

        // deck: the sequencer
        const auto deck = juce::Rectangle<float> (408.0f, (float) kDeckY, W - 408.0f - kMargin, H - kDeckY - kMargin);
        {
            juce::Path body;
            body.addRoundedRectangle (deck, 10.0f);
            juce::DropShadow (c::panelShadow, 28, { 0, 12 }).drawForPath (g, body);
            g.setColour (c::modGround);
            g.fillPath (body);
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (body);
            furBand (g, deck.withHeight (8.0f));
        }

        if (midiBtn.getToggleState())
        {
            glassPanel (g, { 408, 88, 856, 488 }, "Controllers", "MIDI Map");
            return;
        }
        glassPanel (g, { 408, 88, 856, 186 }, "Signal flow", "Chains");
        g.setColour (c::inkSoft);
        g.setFont (medium (11.5f));
        g.drawText ("Click a block to edit it, its dot to switch it. Drag it along the wire to reorder.", 560, 110, 660, 16, juce::Justification::right);
        glassPanel (g, { 408, 286, 856, 290 }, juce::String (kStreamNames[stream]) + " chain", kFxNames[fx]);
    }

    void resized() override
    {
        using namespace tails;
        auto at = [this] (const char* id, int x, int y, int w, int h) { if (auto* c = globalById[id]) c->setBounds (x, y, w, h); };
        prevBtn.setBounds (288, 20, 36, 30);
        presetBox.setBounds (356, 23, 244, 22);
        nextBtn.setBounds (634, 20, 36, 30);
        saveBtn.setBounds (676, 20, 62, 30);
        delBtn.setBounds (740, 20, 54, 30);
        midiBtn.setBounds (800, 20, 124, 30);
        at ("g_sync", (int) kConsoleX + 10, 15, 78, 40);
        at ("g_bpm", (int) kConsoleX + 108, 15, 62, 40);
        at ("g_run", (int) kRunX, 20, 87, 32);

        at ("g_split", 282, 100, 98, 30);
        wave.setBounds (32, 140, 344, 184);
        at ("g_focus", 32, 332, 86, 106); at ("g_smooth", 118, 332, 86, 106); at ("g_decay", 204, 332, 86, 106);
        at ("g_source", 294, 352, 84, 40);

        for (int s = 0; s < kStreams; ++s)
        {
            const int y = s == 0 ? 502 : 572;
            for (int i = 0; i < 3; ++i) strip[s * 4 + i]->setBounds (28 + i * 80, y, 80, 30);
            strip[s * 4 + 3]->setBounds (276, y - 16, 100, 40);
        }
        at ("g_in", 32, 644, 86, 106); at ("g_mix", 118, 644, 86, 106); at ("g_out", 204, 644, 86, 106);
        inMeter.setBounds (302, 668, 74, 8);
        outMeter.setBounds (302, 704, 74, 8);

        chains.setBounds (424, 140, 824, 2 * ChainView::kRowH + ChainView::kRowGap);
        well.setBounds (424, 338, 304, 226);
        midi.setBounds (424, 96, 824, 470);
        lanesView.setBounds (424, 612, 172, kLanes * LaneList::kRowH);
        steps.setBounds (608, 612, 420, 112);
        resetBtn.setBounds (1040, 736, 74, 30);
        clrBtn.setBounds (1116, 736, 64, 30);
        rndBtn.setBounds (1182, 736, 66, 30);
        layoutFxControls();
        layoutLaneControls();
    }

private:
    static constexpr int kDeckY = 592, kHeaderH = 72, kMargin = 16;
    static constexpr float kConsoleX = 1000.0f, kRunX = 1180.0f;

    static bool isAudio (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg");
    }

    void wire (tails::Control& ctl) { ctl.onRight = [this] (const juce::String& id) { showParamMenu (id); }; }

    /** Right-click on any control: MIDI learn, and for what the sequencer can reach, a lane. */
    void showParamMenu (const juce::String& id)
    {
        using namespace tails;
        const int index = proc.indexOfParam (id);
        if (index < 0) return;
        juce::PopupMenu m;
        m.addSectionHeader (proc.paramName (index));
        m.addItem (1, "MIDI Learn");
        const int slot = proc.midiMap.slotOf (index);
        m.addItem (2, "Remove MIDI mapping", slot >= 0);

        int dest = 0, target = 2;
        const auto prefix = id.substring (0, 3);
        const bool isStream = prefix == "tr_" || prefix == "to_";
        if (isStream)
        {
            target = prefix == "tr_" ? 0 : 1;
            const auto bare = id.substring (3);
            const auto& table = DestTable::get();
            for (int i = 0; i < table.count; ++i)
                if (bare == streamParams()[table.param[(size_t) i]].id) dest = kNumGlobalDest + i;
            if (bare == "gain") dest = target == 0 ? gdTransGain : gdTonalGain;
        }
        else if (id == "g_focus") dest = gdFocus;
        else if (id == "g_mix") dest = gdMix;
        if (dest > 0)
        {
            juce::PopupMenu lanes;
            for (int l = 0; l < kLanes; ++l) lanes.addItem (10 + l, "Lane " + juce::String (l + 1));
            m.addSubMenu ("Send to sequencer", lanes);
        }
        m.showMenuAsync (juce::PopupMenu::Options(), [this, index, dest, target] (int r) {
            if (r == 1) { proc.midiMap.learnParam = index; showMidi (true); }
            else if (r == 2) { proc.midiMap.clearParam (index); midi.refresh(); }
            else if (r >= 10)
            {
                const int l = r - 10;
                proc.setParam (TailsProcessor::lid (l, lOn), 1);
                proc.setParam (TailsProcessor::lid (l, lDest), (float) dest);
                proc.setParam (TailsProcessor::lid (l, lTarget), (float) target);
                selectLane (l);
            }
        });
    }

    void chooseIr()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load an impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) proc.loadUserIr (stream, fc.getResult());
        });
    }

    struct Place { int param; const char* label; tails::Control::Kind kind; int col, row; };   // row < 0: the title line

    void buildFxControls()
    {
        using namespace tails;
        using K = Control::Kind;
        fxControls.clear();
        fxPlaces.clear();
        static const std::vector<Place> tables[kNumFx] = {
            { { cIr, "", K::Menu, 0, -1 }, { cRev, "REVERSE", K::Toggle, 1, -1 },
              { cMix, "Mix", K::Knob, 0, 0 }, { cDecay, "Decay", K::Knob, 1, 0 }, { cDamp, "Damp", K::Knob, 2, 0 }, { cPre, "Predelay", K::Knob, 3, 0 },
              { cSize, "Size", K::Knob, 0, 1 }, { cWidth, "Width", K::Knob, 1, 1 }, { cLow, "Low Cut", K::Knob, 2, 1 }, { cHigh, "High Cut", K::Knob, 3, 1 } },
            { { tMode, "", K::Menu, 0, -1 }, { tSync, "SYNC", K::Toggle, 1, -1 }, { tNote, "", K::Menu, 2, -1 },
              { tH1, "1", K::Toggle, 3, -1 }, { tH2, "2", K::Toggle, 4, -1 }, { tH3, "3", K::Toggle, 5, -1 }, { tH4, "4", K::Toggle, 6, -1 },
              { tMix, "Mix", K::Knob, 0, 0 }, { tTime, "Time", K::Knob, 1, 0 }, { tFb, "Swell", K::Knob, 2, 0 }, { tTone, "Tone", K::Knob, 3, 0 }, { tDrive, "Drive", K::Knob, 4, 0 },
              { tWow, "Wow", K::Knob, 0, 1 }, { tFlut, "Flutter", K::Knob, 1, 1 }, { tSpread, "Spread", K::Knob, 2, 1 }, { tNoise, "Noise", K::Knob, 3, 1 } },
            { { dSync, "SYNC", K::Toggle, 0, -1 }, { dNote, "", K::Menu, 1, -1 }, { dPing, "PING PONG", K::Toggle, 2, -1 }, { dLofi, "LOFI", K::Toggle, 3, -1 }, { dAnalog, "", K::Menu, 4, -1 },
              { dMix, "Mix", K::Knob, 0, 0 }, { dTime, "Time", K::Knob, 1, 0 }, { dFb, "Feedback", K::Knob, 2, 0 }, { dDual, "R Ratio", K::Knob, 3, 0 },
              { dHp, "High Pass", K::Knob, 0, 1 }, { dLp, "Low Pass", K::Knob, 1, 1 }, { dDepth, "Mod Depth", K::Knob, 2, 1 }, { dRate, "Mod Rate", K::Knob, 3, 1 } },
            { { hMode, "", K::Menu, 0, -1 },
              { hMix, "Mix", K::Knob, 0, 0 }, { hRate, "Rate", K::Knob, 1, 0 }, { hDepth, "Depth", K::Knob, 2, 0 },
              { hTime, "Delay", K::Knob, 0, 1 }, { hTone, "Tone", K::Knob, 1, 1 }, { hGrit, "Grit", K::Knob, 2, 1 } },
            { { rSpeed, "", K::Menu, 0, -1 },
              { rMix, "Mix", K::Knob, 0, 0 }, { rBal, "Horn / Drum", K::Knob, 1, 0 }, { rDrive, "Drive", K::Knob, 2, 0 },
              { rMic, "Mic Angle", K::Knob, 0, 1 }, { rAccel, "Inertia", K::Knob, 1, 1 } },
            { { fHpOn, "HPF", K::Toggle, 0, -1 }, { fHpS, "", K::Menu, 1, -1 }, { fLpOn, "LPF", K::Toggle, 2, -1 }, { fLpS, "", K::Menu, 3, -1 }, { fBpOn, "BPF", K::Toggle, 4, -1 },
              { fHpF, "HPF Freq", K::Knob, 0, 0 }, { fHpQ, "HPF Reso", K::Knob, 1, 0 }, { fLpF, "LPF Freq", K::Knob, 2, 0 }, { fLpQ, "LPF Reso", K::Knob, 3, 0 }, { fMix, "Mix", K::Knob, 4, 0 },
              { fBpF, "BPF Freq", K::Knob, 0, 1 }, { fBpQ, "BPF Q", K::Knob, 1, 1 }, { fBpM, "BPF Amount", K::Knob, 2, 1 } } };
        for (auto& e : tables[fx])
        {
            auto* ctl = fxControls.add (new Control (proc.state, TailsProcessor::sid (stream, e.param), e.label, e.kind));
            wire (*ctl);
            addAndMakeVisible (ctl);
            ctl->sendLookAndFeelChange();
            fxPlaces.push_back ({ e, ctl });
        }
        loadBtn.setVisible (fx == fxConv && ! midiBtn.getToggleState());
        layoutFxControls();
        if (midiBtn.getToggleState()) for (auto* c : fxControls) c->setVisible (false);
    }

    void layoutFxControls()
    {
        const int headerY = 296, x0 = 740;
        int columns = 1;
        for (auto& e : fxPlaces) if (e.place.row >= 0) columns = juce::jmax (columns, e.place.col + 1);
        const int pitch = juce::jmin (124, 508 / columns);
        int titleX = 610;
        for (auto& e : fxPlaces)
        {
            if (e.place.row < 0)
            {
                const auto kind = e.place.kind;
                const bool head = fx == tails::fxTape && e.place.col >= 3;    // the four heads are small plates
                const int w = head ? 58 : (kind == tails::Control::Kind::Menu ? 112 : (juce::String (e.place.label).length() > 5 ? 130 : 98));
                e.control->setBounds (titleX, kind == tails::Control::Kind::Menu ? headerY + 6 : headerY + 2, w, kind == tails::Control::Kind::Menu ? 24 : 30);
                titleX += w + 6;
            }
            else
                e.control->setBounds (x0 + e.place.col * pitch + (pitch - 100) / 2, 338 + e.place.row * 116, 100, 112);
        }
        loadBtn.setBounds (titleX, headerY + 2, 104, 30);
    }

    void buildLaneControls()
    {
        using namespace tails;
        using K = Control::Kind;
        laneControls.clear();
        auto add = [&] (int param, const char* label, K kind, juce::Rectangle<int> r) {
            auto* ctl = laneControls.add (new Control (proc.state, TailsProcessor::lid (lane, param), label, kind));
            ctl->getProperties().set ("sgDark", true);
            ctl->setCaptionColour (c::onPlate);
            wire (*ctl);
            addAndMakeVisible (ctl);
            ctl->sendLookAndFeelChange();
            ctl->setBounds (r);
        };
        add (lDest, "DESTINATION", K::Menu, { 1040, 608, 208, 40 });
        add (lTarget, "STREAM", K::Menu, { 1040, 652, 100, 40 });
        add (lRate, "RATE", K::Menu, { 1148, 652, 100, 40 });
        add (lDir, "DIRECTION", K::Menu, { 1040, 694, 100, 40 });
        add (lAmt, "AMOUNT", K::HBar, { 1148, 694, 100, 38 });
        add (lLen, "LENGTH", K::HBar, { 608, 730, 96, 38 });
        add (lSlew, "SLEW", K::HBar, { 716, 730, 96, 38 });
        add (lSwing, "SWING", K::HBar, { 824, 730, 96, 38 });
        add (lChance, "CHANCE", K::HBar, { 932, 730, 96, 38 });
    }

    void layoutLaneControls() {}

    void timerCallback() override
    {
        using namespace tails;
        chains.tick(); wave.repaint(); lanesView.tick(); steps.tick();
        inMeter.tick(); outMeter.tick();
        if (well.isVisible()) well.tick();
        midi.tick();
        for (auto* ctl : fxControls)
            if (ctl->isVisible()) updateMod (*ctl);
        for (auto* ctl : strip) updateMod (*ctl);
        if (auto* c = globalById["g_focus"]) c->setModulation (proc.engine->ui.gnorm[gdFocus].load (std::memory_order_relaxed));
        if (auto* c = globalById["g_mix"]) c->setModulation (proc.engine->ui.gnorm[gdMix].load (std::memory_order_relaxed));

        const bool free = proc.state.getRawParameterValue ("g_sync")->load() > 0.5f;
        if (free != freeShown) { freeShown = free; repaint (0, 0, getWidth(), kHeaderH); }
        if (auto* c = globalById["g_bpm"]) c->setVisible (free);
        if (auto* c = globalById["g_run"]) c->setVisible (free);
        if (++slowTick >= 5)
        {
            slowTick = 0;
            if (const auto bpm = proc.uiBpm.load(); bpm != bpmShown) { bpmShown = bpm; repaint ((int) kConsoleX, 8, 1280 - (int) kConsoleX, 54); }
            if (presetBox.getSelectedItemIndex() != proc.presets.getCurrent()) refreshPresets();
        }
    }

    void updateMod (tails::Control& ctl)
    {
        const auto& id = ctl.getParamId();
        const int s = id.startsWith ("tr_") ? 0 : 1;
        const auto bare = id.substring (3);
        for (int p = 0; p < tails::kNumP; ++p)
            if (bare == tails::streamParams()[p].id)
            {
                ctl.setModulation (tails::streamParams()[p].type == tails::T::Float ? proc.engine->ui.norm[(size_t) s][(size_t) p].load (std::memory_order_relaxed) : -1.0f);
                return;
            }
    }

    void refreshPresets()
    {
        presetBox.clear (juce::dontSendNotification);
        presetBox.addItemList (proc.presets.getNames(), 1);
        presetBox.setSelectedItemIndex (proc.presets.getCurrent(), juce::dontSendNotification);
    }

    struct Placed { Place place; tails::Control* control; };

    TailsProcessor& proc;
    tails::LookAndFeel lnf;
    tails::ChainView chains;
    tails::SplitWave wave;
    tails::FxWell well;
    tails::LaneList lanesView;
    tails::StepView steps;
    tails::MidiPage midi;
    tails::Meter inMeter, outMeter;
    juce::OwnedArray<tails::Control> globals, strip, fxControls, laneControls;
    juce::HashMap<juce::String, tails::Control*> globalById;
    std::vector<Placed> fxPlaces;
    juce::TextButton prevBtn, nextBtn, saveBtn, delBtn, midiBtn, resetBtn, clrBtn, rndBtn, loadBtn;
    juce::ComboBox presetBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int stream = 0, fx = 0, lane = 0, slowTick = 0;
    bool freeShown = false;
    float bpmShown = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TailsView)
};

/** Resizable window with the proportions locked: the view is drawn as vectors at the window's own scale. The size is saved with the session. */
class TailsEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kW = TailsView::kW, kH = TailsView::kH;
    static constexpr double kMinScale = 0.6, kMaxScale = 2.0;

    explicit TailsEditor (TailsProcessor& p) : AudioProcessorEditor (p), proc (p), ui (p)
    {
        addAndMakeVisible (ui);
        ui.setBounds (0, 0, kW, kH);
        setResizable (true, true);
        setResizeLimits (juce::roundToInt (kW * kMinScale), juce::roundToInt (kH * kMinScale),
                         juce::roundToInt (kW * kMaxScale), juce::roundToInt (kH * kMaxScale));
        getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
        const double s = juce::jlimit (kMinScale, kMaxScale, (double) p.uiScale.load());
        setSize (juce::roundToInt (kW * s), juce::roundToInt (kH * s));
    }

    void resized() override
    {
        const float s = (float) getWidth() / (float) kW;
        ui.setTransform (juce::AffineTransform::scale (s));
        proc.uiScale = s;
    }

    void paint (juce::Graphics& g) override { g.fillAll (tails::c::ground); }

    // used by the headless tests
    void selectFx (int s, int f)   { ui.selectFx (s, f); }
    void selectLane (int l)        { ui.selectLane (l); }
    void showMidi (bool on)        { ui.showMidi (on); }
    void tickForSnapshot()         { ui.tickForSnapshot(); }

private:
    TailsProcessor& proc;
    TailsView ui;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TailsEditor)
};
