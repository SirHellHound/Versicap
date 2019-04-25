
#include "engine/AudioEngine.h"

#include "gui/LayerPropertiesContentView.h"
#include "gui/LayersTableContentView.h"
#include "gui/NoteParams.h"
#include "gui/ProjectPropertiesContentView.h"
#include "gui/ProjectConcertinaPanel.h"
#include "gui/SamplePropertiesContentView.h"
#include "gui/SamplesTableContentView.h"
#include "gui/SampleEditContentView.h"

#include "gui/MainComponent.h"
#include "gui/UnlockForm.h"

#include "Versicap.h"
#include "RenderContext.h"
#include "UnlockStatus.h"
#include "Utils.h"

#define VCP_DO_LOGO  1
namespace vcp {

class RenderProgress : public Component
{
public:
    RenderProgress()
        : progress (-1.0),
          bar (progress)
    {
        addAndMakeVisible (bar);

        addAndMakeVisible (text);
        text.setText ("Waiting...", dontSendNotification);
        text.setJustificationType (Justification::topLeft);
        text.setFont (Font (13.f));

        addAndMakeVisible (cancelButton);
        cancelButton.setButtonText ("Cancel");

        // shadow.setShadowProperties (DropShadow (Colours::black.withAlpha (0.7f), 6, Point<int> (0, 1)));
        // setComponentEffect (&shadow);

        setSize (300, 140);
    }

    void setProgressText (const String& newText)
    {
        jassert(newText.isNotEmpty());
        text.setText (newText, dontSendNotification);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (kv::LookAndFeel_KV1::widgetBackgroundColor);
        g.setColour (kv::LookAndFeel_KV1::widgetBackgroundColor.brighter());
        g.drawRect (getLocalBounds().toFloat(), 1.5);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r = r.withWidth (jmax (240, r.getWidth()))
             .withHeight (jmax (140, r.getHeight()))
             .reduced (30, 0);

        r.removeFromTop (getHeight() / 3);
        bar.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        text.setBounds (r.removeFromTop (30));
        
        int buttonSize = 26;
        r.removeFromBottom (30);
        cancelButton.changeWidthToFitText (buttonSize);
        cancelButton.setBounds (r.removeFromBottom (buttonSize)
                                 .withWidth (cancelButton.getWidth())
                                 .withX ((getWidth() / 2) - (cancelButton.getWidth() / 2)));
        cancelButton.onClick = [this]() { if (onCancel) onCancel(); };
    }

    std::function<void()> onCancel;

private:
    double progress;
    ProgressBar bar;
    Label text;
    TextButton cancelButton;
    DropShadowEffect shadow;
};

class MainComponent::Content : public Component,
                               public Value::Listener,
                               private Timer
{
public:
    Content (MainComponent& o, Versicap& vc)
        : owner (o),
          versicap (vc),
          meterLeft (1, true),
          meterRight (1, true)
    {
        project = versicap.getProject();

        setOpaque (true);

        addAndMakeVisible (panicButton);
        panicButton.setButtonText ("Panic");
        panicButton.onClick = [this]() { versicap.getAudioEngine().panic(); };

        addAndMakeVisible (importButton);
        importButton.setButtonText ("Open");
        importButton.onClick = [this]()
        {
            FileChooser chooser ("Open Versicap File", Versicap::getProjectsPath(), 
                "*.versicap", true, false, this);
            if (chooser.browseForFileToOpen())
            {
                versicap.loadProject (chooser.getResult());
            }
        };

        addAndMakeVisible (exportButton);
        exportButton.setButtonText ("Save");
        exportButton.onClick = [this]()
        {
            FileChooser chooser ("Save Versicap File", Versicap::getProjectsPath(), 
                "*.versicap", true, false, this);
            if (chooser.browseForFileToSave (true))
            {
                versicap.saveProject (chooser.getResult());
            }
        };

        addAndMakeVisible (recordButton);
        recordButton.setButtonText ("Record");
        recordButton.onClick = [this]() { owner.startRendering(); };

        addAndMakeVisible (notes);

        view.reset (new SampleEditContentView (versicap));
        addAndMakeVisible (view.get());

        layer.reset (new LayerPropertiesContentView (versicap));
        addAndMakeVisible (layer.get());

        addAndMakeVisible (projectPanel);
        projectPanel.createPanels (versicap);

        addAndMakeVisible (meterLeft);
        addAndMakeVisible (meterRight);

        logo = ImageCache::getFromMemory (BinaryData::versicap_v1_png,
                                          BinaryData::versicap_v1_pngSize);

        progress.onCancel = std::bind (&Versicap::stopRendering, &vc);
        setSize (440, 340);

        projectName.addListener (this);
        setProject (versicap.getProject());

        startTimerHz (30);
    }

    ~Content()
    {
        projectName.removeListener (this);
        if (auto* _unlock = unlock.getComponent())
            delete _unlock;
    }

    Rectangle<int> getNameRectangle() const { return { 50, 6, 150, 32 }; }

    void valueChanged (Value& value) override
    {
        if (value.refersToSameSourceAs (projectName))
        {
            repaint (getNameRectangle());
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (kv::LookAndFeel_KV1::widgetBackgroundColor.darker());
       #if VCP_DO_LOGO
        g.drawImageWithin (logo, 12, 6, 32, 32, RectanglePlacement::centred, false);
        g.setColour (kv::LookAndFeel_KV1::textColor);
        g.drawText (project.getProperty (Tags::name), getNameRectangle(), 
                    Justification::centredLeft);
       #endif
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto r2 = r.removeFromTop (40).reduced (0, 11);

        auto r4 = getLocalBounds().removeFromTop(40).removeFromRight (244);
        r4.removeFromTop (8);
        r4.removeFromRight (5);
        meterLeft.setBounds (r4.removeFromTop (10));
        r4.removeFromTop (1);
        meterRight.setBounds (r4.removeFromTop (10));

        notes.setBounds ((getWidth() / 2) - (notes.getRequiredWidth() / 2), 
                         r2.getY(), notes.getRequiredWidth(), r2.getHeight());

        panicButton.changeWidthToFitText (r2.getHeight());
        panicButton.setBounds (228, r2.getY(), 40, r2.getHeight());

        r.removeFromTop (1);
        r.removeFromLeft (6);
        r.removeFromRight (6);
        r.removeFromBottom (8);

        r2 = r.removeFromLeft (220);
        r2.removeFromTop (4);
        projectPanel.setBounds (r2);

        auto r3 = r.removeFromRight (240);
        r3.removeFromTop (4);
        layer->setBounds (r3);

        r.removeFromLeft (2);        
        view->setBounds (r);

        if (overlay.isVisible())
        {
            overlay.setBounds (getLocalBounds());
        }

        if (progress.isVisible())
        {
            progress.setBounds (getAlertBounds());
        }

        if (unlock != nullptr && unlock->isVisible())
        {
            unlock->setBounds (getAlertBounds());
        }
    }

    Rectangle<int> getAlertBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (380, 180);
    }

    void showProgress (bool showIt)
    {
        showOverlay (showIt);

        if (showIt)
        {
            addAndMakeVisible (progress, 9999);
        }
        else
        {
            removeChildComponent (&progress);
        }

        resized();
    }

    void showOverlay (bool showIt)
    {
        if (showIt)
        {
            addAndMakeVisible (overlay, 9998);
        }
        else
        {
            removeChildComponent (&overlay);
        }

        resized();
    }

    void showUnlockForm()
    {
        showOverlay (true);
        unlock = new UnlockForm (versicap.getUnlockStatus(), 
            "Sign in to your account to unlock Versicap", false, true);
        addAndMakeVisible (unlock.getComponent(), 10000);
        resized();
    }

    RenderProgress& getRenderProgress() { return progress; }

    void setProject (const Project& newProject)
    {
        project = newProject;
        projectName.referTo (project.getPropertyAsValue (Tags::name));
        notes.setProject (project);
        layer->setProject (project);
        repaint();
    }

    ValueTree createState()
    {
        ValueTree data ("state");
        ValueTree projectPanelData = data.getOrCreateChildWithName ("project", nullptr);
        for (int i = 0; i < projectPanel.getNumPanels(); ++i)
        {
            ValueTree panelData ("panel");
            auto* const panel = projectPanel.getPanel (i);
            panelData.setProperty ("height", panel->getHeight(), nullptr);
            projectPanelData.appendChild (panelData, nullptr);
        }

        if (auto xml = std::unique_ptr<XmlElement> (layer->getOpennessState()))
        {
            MemoryOutputStream mo;
            xml->writeToStream (mo, String());
            data.setProperty ("propsOpenness", mo.getMemoryBlock().toBase64Encoding(), nullptr);
        }

        return data;
    }

    void applyState (const ValueTree& data)
    {
        auto projectPanelData = data.getChildWithName ("project");
        for (int i = 0; i < projectPanelData.getNumChildren(); ++i)
        {
            auto child = projectPanelData.getChild (i);
            int height = child.getProperty ("height");
            if (auto* const panel = projectPanel.getPanel (i))
                projectPanel.setPanelSize (panel, height, false);
        }

        MemoryBlock mb;
        if (mb.fromBase64Encoding (data.getProperty ("propsOpenness").toString()))
            if (auto xml = std::unique_ptr<XmlElement> (XmlDocument::parse (mb.toString())))
                layer->restoreOpennessState (*xml);
    }

private:
    friend class MainComponent;
    MainComponent& owner;
    Versicap& versicap;
    Project project;
    Value projectName;

    AudioEngine::MonitorPtr monitor;
    kv::DigitalMeter meterLeft;
    kv::DigitalMeter meterRight;

    std::unique_ptr<ContentView> view;
    std::unique_ptr<LayerPropertiesContentView> layer;

    TextButton importButton;
    TextButton exportButton;
    TextButton recordButton;
    TextButton panicButton;

    ProjectConcertinaPanel projectPanel;

    RenderProgress progress;

    NoteParams notes;

    ComboBox sourceCombo;

    Component::SafePointer<UnlockForm> unlock;

    Image logo;

    struct Overlay : public Component
    {
        Overlay()
        {
            setInterceptsMouseClicks (true, false);
        }

        bool hitTest (int, int) override
        {
            return true;
        }

        void paint (Graphics& g) override
        {
            g.setColour (Colours::black);
            g.setOpacity (0.24f);
            g.fillAll();
        }

    } overlay;

    void timerCallback() override
    {
        if (! monitor)
            monitor = versicap.getAudioEngine().getMonitor();
        if (monitor)
        {
            meterLeft.setValue (0, monitor->levelLeft.get());
            meterLeft.refresh();
            meterRight.setValue (0, monitor->levelRight.get());
            meterRight.refresh();
        }
    }
};

MainComponent::MainComponent (Versicap& vc)
    : ContentComponent (vc),
      versicap (vc)
{
    setOpaque (true);
    content.reset (new Content (*this, vc));
    addAndMakeVisible (content.get());
    setSize (600, 400);

    versicap.addListener (this);
    versicap.getUnlockStatus().addChangeListener (this);
    if (! (bool) versicap.getUnlockStatus().isUnlocked())
        content->showUnlockForm();
}

MainComponent::~MainComponent()
{
    versicap.removeListener (this);
    versicap.getUnlockStatus().removeChangeListener (this);
    content.reset();
}

void MainComponent::getState (String& state)
{
    ValueTree data (content->createState());
    MemoryOutputStream mo;
    {
        GZIPCompressorOutputStream gz (mo);
        data.writeToStream (gz);
    }
    state = mo.getMemoryBlock().toBase64Encoding();
}

void MainComponent::applyState (const String& state)
{
    MemoryBlock mb;
    mb.fromBase64Encoding (state);
    const ValueTree data = (mb.getSize() > 0)
        ? ValueTree::readFromGZIPData (mb.getData(), mb.getSize())
        : ValueTree();
    if (! data.isValid())
        return;
    content->applyState (data);
}

void MainComponent::changeListenerCallback (ChangeBroadcaster* bcaster)
{
    if (bcaster == &versicap.getUnlockStatus())
    {
        if ((bool) versicap.getUnlockStatus().isUnlocked())
        {
            content->showOverlay (false);
        }
        else
        {
            content->showUnlockForm();
        }
    }
}

//=============================================================================
void MainComponent::projectChanged()
{
    content->setProject (versicap.getProject());
}

void MainComponent::renderWillStart()
{
    auto& progress = content->getRenderProgress();
    progress.setProgressText ("Waiting...");
    content->showProgress (true);
}

void MainComponent::renderStarted()
{
    auto& progress = content->getRenderProgress();
    progress.setProgressText ("Rendering...");
}

void MainComponent::renderWillStop()
{
    content->showProgress (false);
}

void MainComponent::saveSettings()
{
    
}

void MainComponent::startRendering()
{
    auto result = versicap.startRendering();
    if (result.failed())
    {
        AlertWindow::showNativeDialogBox ("Versicap", result.getErrorMessage(), false);
        return;
    }
}

void MainComponent::stopRendering()
{

}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (DocumentWindow::backgroundColourId));
}

void MainComponent::resized()
{
    content->setBounds (getLocalBounds());
}

}
