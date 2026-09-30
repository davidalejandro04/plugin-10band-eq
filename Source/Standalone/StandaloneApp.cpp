#include "StandaloneEngine.h"
#include "FileRenderer.h"
#include "PluginEditor.h"

class StandaloneContent final : public juce::Component, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    explicit StandaloneContent (bool openAudio = true, juce::PropertiesFile* savedSettings = nullptr)
        : engine (processor), settings (savedSettings)
    {
        if (settings != nullptr)
        {
            juce::MemoryBlock state;
            if (state.fromBase64Encoding (settings->getValue ("eqState")))
                processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        }
        editor = std::make_unique<TenBandEQAudioProcessorEditor> (processor);
        editor->setResizable (false, false);
        setLookAndFeel (&editor->getTheme());
        editor->setTooltipParent (*this);
        editor->onThemeChanged = [this]
        {
            sendLookAndFeelChange();
            repaint();
            if (onAppearanceChanged) onAppearanceChanged();
        };
        viewport.setViewedComponent (editor.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        for (auto* component : std::initializer_list<juce::Component*> { &mode, &output, &source, &refresh, &open,
            &play, &stop, &exportButton, &cancelExport, &systemButton, &seek, &fileLabel, &status, &timeLabel, &performance })
            addAndMakeVisible (component);
        mode.addItem ("File editing", 1);
        mode.addItem ("System output (Windows)", 2);
        mode.setSelectedId (1);
        mode.onChange = [this] { engine.stop(); updateMode(); };
        output.setTextWhenNothingSelected ("Choose speakers / headphones");
        source.setTextWhenNothingSelected ("Choose system playback source");
        source.onChange = [this] { if (engine.isSystemRunning()) engine.stop(); };
        output.onChange = [this] { showResult (engine.selectOutput (output.getText())); };
        refresh.onClick = [this] { engine.stop(); refreshDevices(); };
        open.onClick = [this] { chooseFile(); };
        play.onClick = [this] { if (engine.isPlaying()) engine.pause(); else engine.play(); };
        stop.onClick = [this] { engine.stop(); engine.seek (0.0); };
        systemButton.onClick = [this]
        {
            if (engine.isSystemRunning()) { engine.stop(); status.setText ("System processing stopped.", juce::dontSendNotification); return; }
            const auto index = source.getSelectedId() - 1;
            if (index < 0 || index >= static_cast<int> (endpoints.size()))
            { status.setText ("Select a source playback device first.", juce::dontSendNotification); return; }
            const auto result = engine.startSystem (endpoints[static_cast<size_t> (index)]);
            showResult (result);
            if (result.wasOk()) status.setText ("System audio is being equalized. Keep other apps routed to the source device.", juce::dontSendNotification);
        };
        seek.setSliderStyle (juce::Slider::LinearHorizontal);
        seek.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        seek.setRange (0, 1, 0.001);
        seek.onDragEnd = [this] { engine.seek (seek.getValue()); };
        seek.onValueChange = [this] { if (! updatingSeek && ! seek.isMouseButtonDown()) engine.seek (seek.getValue()); };
        exportButton.onClick = [this] { chooseExport(); };
        cancelExport.onClick = [this] { if (exportJob) exportJob->cancelled.store (true); };
        fileLabel.setText ("Open or drop an audio file to begin.", juce::dontSendNotification);
        timeLabel.setJustificationType (juce::Justification::centredRight);
        performance.setJustificationType (juce::Justification::centredRight);
        setSize (1040, 930);
        if (openAudio) showResult (engine.initialise());
        refreshDevices();
        if (settings != nullptr)
        {
            const auto savedOutput = settings->getValue ("outputDevice");
            for (int i = 0; i < output.getNumItems(); ++i)
                if (output.getItemText (i) == savedOutput) output.setSelectedId (output.getItemId (i));
        }
        updateMode();
        if (status.getText().isEmpty())
            status.setText ("Choose an output device, then open a file or switch to System output.", juce::dontSendNotification);
        timerCallback();
        startTimerHz (10);
    }

    ~StandaloneContent() override
    {
        stopTimer();
        chooser.reset();
        if (exportJob) { exportJob->cancelled.store (true); exportJob.reset(); }
        engine.stop();
        if (settings != nullptr)
        {
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            settings->setValue ("eqState", state.toBase64Encoding());
            settings->setValue ("outputDevice", engine.currentOutput());
        }
        editor->onThemeChanged = nullptr;
        viewport.setViewedComponent (nullptr);
        setLookAndFeel (nullptr);
        editor.reset();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() == 1) load (juce::File (files[0]));
    }

    void paint (juce::Graphics& g) override
    {
        const auto p = ui::palette (*this);
        g.fillAll (p.background);
        g.setColour (p.muted);
        g.setFont (ui::font (11.0f, true));
        g.drawText ("MODE", 20, 6, 190, 18, juce::Justification::centredLeft);
        g.drawText ("OUTPUT DEVICE", getWidth() < 820 ? 20 : 244, getWidth() < 820 ? 66 : 6, 300, 18, juce::Justification::centredLeft);
    }

    void setLightMode (bool light) { editor->setLightMode (light); }
    std::function<void()> onAppearanceChanged;

    void resized() override
    {
        const auto compact = getWidth() < 820;
        const auto offset = compact ? 60 : 0;
        mode.setBounds (20, 26, compact ? getWidth() - 40 : 208, 34);
        output.setBounds (compact ? 20 : 244, compact ? 86 : 26, getWidth() - (compact ? 88 : 312), 34);
        refresh.setBounds (getWidth() - 60, compact ? 86 : 26, 40, 34);
        source.setBounds (20, 76 + offset, getWidth() - 210, 36);
        systemButton.setBounds (getWidth() - 176, 76 + offset, 156, 36);
        open.setBounds (20, 76 + offset, 116, 36);
        play.setBounds (144, 76 + offset, 40, 36);
        stop.setBounds (192, 76 + offset, 40, 36);
        exportButton.setBounds (getWidth() - 200, 76 + offset, 132, 36);
        cancelExport.setBounds (getWidth() - 60, 76 + offset, 40, 36);
        fileLabel.setBounds (20, 118 + offset, getWidth() - 40, 28);
        seek.setBounds (20, 150 + offset, getWidth() - 180, 24);
        timeLabel.setBounds (getWidth() - 154, 150 + offset, 134, 24);
        status.setBounds (20, 180 + offset, getWidth() - (compact ? 40 : 240), 34);
        performance.setBounds (getWidth() - 218, compact ? 216 + offset : 180, 198, compact ? 20 : 34);
        const auto toolbarHeight = compact ? 302 : 222;
        viewport.setBounds (0, toolbarHeight, getWidth(), juce::jmax (1, getHeight() - toolbarHeight));
        if (editor)
        {
            const auto minimumHeight = compact ? 620 : 540;
            const auto scroll = viewport.getHeight() < minimumHeight;
            const auto editorWidth = getWidth() - (scroll ? viewport.getScrollBarThickness() : 0);
            editor->setSize (editorWidth, juce::jmax (editorWidth < 820 ? 620 : 540, viewport.getHeight()));
        }
    }

private:
    void showResult (const juce::Result& result)
    {
        if (result.failed()) status.setText (result.getErrorMessage(), juce::dontSendNotification);
    }

    void refreshDevices()
    {
        const auto selectedSource = source.getText();
        output.clear (juce::dontSendNotification);
        const auto outputs = engine.outputNames();
        output.addItemList (outputs, 1);
        output.setSelectedId (outputs.indexOf (engine.currentOutput()) + 1, juce::dontSendNotification);
        endpoints = SystemAudioSource::getEndpoints();
        source.clear (juce::dontSendNotification);
        for (size_t i = 0; i < endpoints.size(); ++i)
        {
            source.addItem (endpoints[i].name, static_cast<int> (i) + 1);
            if (endpoints[i].name == selectedSource) source.setSelectedId (static_cast<int> (i) + 1, juce::dontSendNotification);
        }
    }

    void updateMode()
    {
        const auto editing = mode.getSelectedId() == 1;
        for (auto* component : std::initializer_list<juce::Component*> { &open, &play, &stop, &exportButton, &cancelExport, &seek, &timeLabel })
            component->setVisible (editing);
        source.setVisible (! editing);
        systemButton.setVisible (! editing);
        fileLabel.setText (editing ? (engine.loadedFile() == juce::File() ? "Open or drop MP3, WAV, AIFF, FLAC or Ogg audio." : engine.loadedFile().getFileName())
                                  : "Route Windows to a virtual playback device; select that as source and your physical speakers as output.",
                           juce::dontSendNotification);
    }

    void load (const juce::File& file)
    {
        const auto result = engine.loadFile (file);
        showResult (result);
        if (result.wasOk())
        {
            mode.setSelectedId (1, juce::dontSendNotification);
            seek.setRange (0.0, juce::jmax (0.001, engine.duration()), 0.001);
            updateMode();
            status.setText ("Adjust the EQ while playing. Export WAV saves a processed copy at the original sample rate.", juce::dontSendNotification);
        }
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open audio", juce::File(), "*.mp3;*.wav;*.aiff;*.aif;*.flac;*.ogg");
        const juce::Component::SafePointer<StandaloneContent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& dialog) { if (safe && dialog.getResult().existsAsFile()) safe->load (dialog.getResult()); });
    }

    void chooseExport()
    {
        if (engine.loadedFile() == juce::File() || exportJob) return;
        chooser = std::make_unique<juce::FileChooser> ("Export equalized WAV", engine.loadedFile().getSiblingFile (
            engine.loadedFile().getFileNameWithoutExtension() + "-equalized.wav"), "*.wav");
        const juce::Component::SafePointer<StandaloneContent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe] (const juce::FileChooser& dialog)
            {
                if (! safe || dialog.getResult() == juce::File()) return;
                auto destination = dialog.getResult();
                if (! destination.hasFileExtension ("wav"))
                {
                    safe->status.setText ("Use a .wav filename for export.", juce::dontSendNotification);
                    return;
                }
                juce::MemoryBlock state;
                safe->processor.getStateInformation (state);
                safe->exportJob = std::make_unique<fileaudio::ExportJob> (safe->engine.loadedFile(), destination, std::move (state));
                if (! safe->exportJob->startThread())
                {
                    safe->status.setText ("Could not start the export worker.", juce::dontSendNotification);
                    safe->exportJob.reset();
                }
            });
    }

    static juce::String timeText (double seconds)
    {
        const auto total = static_cast<int> (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    void timerCallback() override
    {
        performance.setText ("Audio CPU " + juce::String (engine.cpuUsage() * 100.0, 1) + "%"
            + (engine.isSystemRunning() ? " | drops " + juce::String (engine.dropouts()) : juce::String()), juce::dontSendNotification);
        play.setButtonText (engine.isPlaying() ? "Pause" : "Play");
        play.setIcon (engine.isPlaying() ? ui::Icon::pause : ui::Icon::play);
        play.setTooltip (play.getButtonText());
        play.setEnabled (engine.loadedFile() != juce::File());
        exportButton.setEnabled (engine.loadedFile() != juce::File() && ! exportJob);
        cancelExport.setEnabled (exportJob != nullptr);
        systemButton.setButtonText (engine.isSystemRunning() ? "Stop system EQ" : "Start system EQ");
        const auto seekMaximum = juce::jmax (0.001, engine.duration());
        if (seek.getMaximum() != seekMaximum) seek.setRange (0.0, seekMaximum, 0.001);
        if (! seek.isMouseButtonDown())
        {
            updatingSeek = true;
            seek.setValue (engine.position(), juce::dontSendNotification);
            updatingSeek = false;
        }
        timeLabel.setText (timeText (engine.position()) + " / " + timeText (engine.duration()), juce::dontSendNotification);
        if (engine.isSystemRunning() && engine.captureError().isNotEmpty())
        {
            const auto error = engine.captureError();
            engine.stop();
            status.setText (error, juce::dontSendNotification);
        }
        if (exportJob)
        {
            if (exportJob->isThreadRunning()) status.setText ("Exporting WAV: " + juce::String (exportJob->progress.load() * 100.0f, 0) + "%", juce::dontSendNotification);
            else
            {
                status.setText (exportJob->result.wasOk() ? "Export complete. The original file was preserved." : exportJob->result.getErrorMessage(), juce::dontSendNotification);
                exportJob.reset();
            }
        }
    }

    TenBandEQAudioProcessor processor;
    StandaloneEngine engine;
    juce::PropertiesFile* settings = nullptr;
    std::unique_ptr<TenBandEQAudioProcessorEditor> editor;
    juce::Viewport viewport;
    juce::ComboBox mode, output, source;
    ui::IconButton refresh { "Refresh devices", ui::Icon::refresh }, open { "Open audio", ui::Icon::folder, true },
                   play { "Play", ui::Icon::play }, stop { "Stop", ui::Icon::stop },
                   exportButton { "Export WAV", ui::Icon::exportFile, true }, cancelExport { "Cancel export", ui::Icon::close };
    ui::IconButton systemButton { "Start system EQ", ui::Icon::power, true };
    juce::Slider seek;
    juce::Label fileLabel, status, timeLabel, performance;
    std::vector<SystemAudioSource::Endpoint> endpoints;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<fileaudio::ExportJob> exportJob;
    bool updatingSeek = false;
};

class TenBandEQApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Ten Band EQ"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.startsWith ("--window-preview "))
        {
            const auto args = juce::StringArray::fromTokens (commandLine, true);
            Window preview (nullptr, false, false);
            if (args.contains ("--light"))
                static_cast<StandaloneContent*> (preview.getContentComponent())->setLightMode (true);
            juce::FileOutputStream output (juce::File::getCurrentWorkingDirectory().getChildFile (args[1].unquoted()));
            output.setPosition (0);
            output.truncate();
            const auto success = output.openedOk() && juce::PNGImageFormat().writeImageToStream (
                preview.createComponentSnapshot (preview.getLocalBounds()), output);
            setApplicationReturnValue (success ? 0 : 1);
            quit();
            return;
        }
        // Offscreen smoke test: no audio device is opened and no sound is emitted.
        if (commandLine.startsWith ("--preview "))
        {
            StandaloneContent content (false);
            const auto args = juce::StringArray::fromTokens (commandLine, true);
            if (args.contains ("--compact")) content.setSize (560, 700);
            content.setLightMode (args.contains ("--light"));
            juce::FileOutputStream output (juce::File::getCurrentWorkingDirectory().getChildFile (args[1].unquoted()));
            output.setPosition (0);
            output.truncate();
            const auto success = output.openedOk() && juce::PNGImageFormat().writeImageToStream (
                content.createComponentSnapshot (content.getLocalBounds()), output);
            setApplicationReturnValue (success ? 0 : 1);
            quit();
            return;
        }
        juce::PropertiesFile::Options options;
        options.applicationName = "TenBandEQ";
        options.folderName = "TenBandEQ";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);
        window = std::make_unique<Window> (properties.getUserSettings());
    }
    void shutdown() override { window.reset(); properties.saveIfNeeded(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        explicit Window (juce::PropertiesFile* settings, bool openAudio = true, bool show = true)
            : DocumentWindow ("Ten Band EQ", juce::Colour (0xff090909), allButtons, show)
        {
            setUsingNativeTitleBar (false);
            setTitleBarHeight (38);
            setLookAndFeel (&windowTheme);
            auto* content = new StandaloneContent (openAudio, settings);
            setContentOwned (content, true);
            content->onAppearanceChanged = [this, content]
            {
                auto& theme = static_cast<ui::Theme&> (content->getLookAndFeel());
                windowTheme.setLight (theme.isLight());
                setBackgroundColour (windowTheme.colours().background);
                sendLookAndFeelChange();
                repaint();
            };
            content->onAppearanceChanged();
            setResizable (true, false);
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());
            if (display == nullptr) display = displays.getPrimaryDisplay();
            // JUCE display bounds are logical coordinates, accounting for DPI scaling.
            const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1280, 800);
            const auto height = juce::jmax (1, juce::roundToInt (static_cast<double> (area.getHeight()) * 0.60));
            const auto width = juce::jmax (1, juce::jmin (1040, juce::roundToInt (static_cast<double> (area.getWidth()) * 0.90)));
            setResizeLimits (juce::jmin (560, width), juce::jmin (400, height),
                             juce::jmax (1800, area.getWidth()), juce::jmax (1400, area.getHeight()));
            setBounds (area.getCentreX() - width / 2, area.getCentreY() - height / 2, width, height);
            setVisible (show);
        }
        ~Window() override
        {
            if (auto* content = static_cast<StandaloneContent*> (getContentComponent()))
                content->onAppearanceChanged = nullptr;
            clearContentComponent();
            setLookAndFeel (nullptr);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    private:
        ui::Theme windowTheme;
    };
    juce::ApplicationProperties properties;
    std::unique_ptr<Window> window;
};

JUCE_CREATE_APPLICATION_DEFINE (TenBandEQApplication)
