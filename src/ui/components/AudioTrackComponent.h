#pragma once
#include <JuceHeader.h>

/**
    Base track UI with:
       - Mute button
       - Solo button
       - Vertical gain fader with dB scale
       - A track title
    Derived classes can add more controls (freq editors, upload button, etc.)
*/
class AudioTrackComponent : public juce::Component,
                            public juce::Button::Listener,
                            public juce::Slider::Listener
{
public:
    AudioTrackComponent (const juce::String& trackName)
    {
        // Configure track title
        trackTitle.setText (trackName, juce::dontSendNotification);
        trackTitle.setJustificationType (juce::Justification::centred);
        juce::Font titleFont;
        titleFont.setHeight(16.0f);
        titleFont = titleFont.boldened();
        trackTitle.setFont(titleFont);
        addAndMakeVisible (trackTitle);

        // Configure buttons
        muteButton.setButtonText ("M");
        muteButton.setClickingTogglesState (true);
        muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        muteButton.addListener (this);
        addAndMakeVisible (muteButton);

        soloButton.setButtonText ("S");
        soloButton.setClickingTogglesState (true);
        soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
        soloButton.addListener (this);
        addAndMakeVisible (soloButton);

        // Configure fader as vertical with dB scale
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 20);
        
        // Set up dB scale (0.0 = -60dB, 1.0 = +12dB)
        fader.setRange (0.0, 1.0, 0.01);
        fader.setValue (60.0/72.0); // Default at 0dB (approx 0.833)
        
        // In older JUCE versions, we can't use setTextValueFunction, 
        // so we'll just hide the textbox and use our own label instead
        fader.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        
        fader.addListener (this);
        addAndMakeVisible (fader);
        
        // Set up fader value label - make it editable
        faderValueLabel.setJustificationType(juce::Justification::centred);
        faderValueLabel.setText("0.0 dB", juce::dontSendNotification);
        faderValueLabel.setEditable(true, true, false);
        faderValueLabel.onTextChange = [this]() {
            juce::String text = faderValueLabel.getText();
            double dbValue = text.getDoubleValue();
            
            // Clamp to valid dB range (-60 to +12)
            dbValue = juce::jlimit(-60.0, 12.0, dbValue);
            
            // Convert dB to fader position (0.0-1.0)
            double faderPos = (dbValue + 60.0) / 72.0;
            fader.setValue(faderPos, juce::sendNotification);
        };
        addAndMakeVisible(faderValueLabel);
        
        // Set up dB scale labels
        dbScaleLabels.add(new juce::Label("", "+12"));
        dbScaleLabels.add(new juce::Label("", "0"));
        dbScaleLabels.add(new juce::Label("", "-12"));
        dbScaleLabels.add(new juce::Label("", "-24"));
        dbScaleLabels.add(new juce::Label("", "-60"));
        
        for (auto* label : dbScaleLabels)
        {
            label->setJustificationType(juce::Justification::centredRight);
            label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            juce::Font scaleFont;
            scaleFont.setHeight(10.0f);
            label->setFont(scaleFont);
            addAndMakeVisible(label);
        }
        
        // Set up track meter
        trackMeter = std::make_unique<TrackMeterComponent>();
        addAndMakeVisible(trackMeter.get());
    }

    ~AudioTrackComponent() override {}

    bool isMuted() const                { return muteButton.getToggleState(); }
    bool isSolo()  const                { return soloButton.getToggleState(); }
    void setSoloState (bool s)          { soloButton.setToggleState (s, juce::dontSendNotification); }
    void setMuteState (bool s)          { muteButton.setToggleState (s, juce::sendNotification); }
    
    // Access fader value
    float getGain() const               { return fader.getValue(); }
    void setGain(float newGain)         { fader.setValue(newGain); }

    // Parent can set these callbacks
    std::function<void(AudioTrackComponent*)>        onSoloChanged;
    std::function<void(AudioTrackComponent*, float)> onGainChanged;
    
    // Meter access for audio level updates
    void setMeterLevel(float level)
    {
        if (trackMeter)
            trackMeter->setLevel(level);
    }
    

    // Convert from UI fader (0.0-1.0) to actual linear gain using proper dB conversion
    float getActualGain() const
    {
        double faderVal = fader.getValue();
        
        // Map fader 0.0-1.0 to -60dB to +12dB range
        double dB = -60.0 + (72.0 * faderVal);
        
        // Handle silence threshold (anything below -60dB is effectively silent)
        if (dB <= -60.0) return 0.0f;
        
        // Standard dB to linear conversion: gain = 10^(dB/20)
        return (float)std::pow(10.0, dB / 20.0);
    }

    void resized() override
    {
        auto totalBounds = getLocalBounds().reduced(2);
        int totalHeight = totalBounds.getHeight();
        int totalWidth = totalBounds.getWidth();
        
        // Percentage-based layout
        int titleHeight = (int)(totalHeight * 0.08); // 8% for title
        int controlsHeight = (int)(totalHeight * 0.12); // 12% for derived controls
        int buttonHeight = (int)(totalHeight * 0.10); // 10% for mute/solo buttons at bottom
        int valueHeight = (int)(totalHeight * 0.06); // 6% for value label
        
        // Ensure minimum sizes
        titleHeight = juce::jmax(titleHeight, 20);
        controlsHeight = juce::jmax(controlsHeight, 40);
        buttonHeight = juce::jmax(buttonHeight, 25);
        valueHeight = juce::jmax(valueHeight, 18);
        
        // Track title at the very top
        auto titleArea = totalBounds.removeFromTop(titleHeight);
        trackTitle.setBounds(titleArea);
        
        // Leave space for derived components to add their controls
        auto controlsArea = totalBounds.removeFromTop(controlsHeight);
        
        // Mute/Solo buttons at the very bottom
        auto buttonArea = totalBounds.removeFromBottom(buttonHeight);
        int buttonSize = juce::jmin(buttonHeight - 4, 30);
        int buttonsWidth = buttonSize * 2 + 5;
        auto centeredButtonArea = buttonArea.withSizeKeepingCentre(buttonsWidth, buttonSize);
        muteButton.setBounds(centeredButtonArea.removeFromLeft(buttonSize));
        centeredButtonArea.removeFromLeft(5);
        soloButton.setBounds(centeredButtonArea);
        
        // Value display just above buttons
        auto valueArea = totalBounds.removeFromBottom(valueHeight);
        faderValueLabel.setBounds(valueArea);
        juce::Font valueFont;
        valueFont.setHeight(12.0f);
        valueFont.setStyleFlags(juce::Font::plain);
        faderValueLabel.setFont(valueFont);
        faderValueLabel.setColour(juce::Label::textColourId, juce::Colours::black);
        
        // The remaining area is for the fader and meter - this fills the space between
        auto faderArea = totalBounds.reduced(2);
        int faderHeight = faderArea.getHeight();
        
        // Just center the fader - no internal meter or scale labels
        int faderWidth = 25;
        int startX = faderArea.getX() + (faderArea.getWidth() - faderWidth) / 2;
        
        // Position fader centered
        fader.setBounds(startX, faderArea.getY(), faderWidth, faderHeight);
        
        // Hide internal meter and scale labels
        if (trackMeter)
            trackMeter->setVisible(false);
        for (auto* label : dbScaleLabels)
            label->setVisible(false);
    }

    void buttonClicked (juce::Button* b) override
    {
        if (b == &muteButton)
        {
            updateFaderState();
            
            if (onGainChanged)
                onGainChanged (this, getActualGain());
        }
        else if (b == &soloButton)
        {
            if (onSoloChanged)
                onSoloChanged (this);
                
            updateFaderState();
        }
    }

    void sliderValueChanged (juce::Slider* s) override
    {
        if (s == &fader)
        {
            // Update the value label with formatted dB value
            updateFaderValueLabel();
            
            if (onGainChanged)
                onGainChanged (this, getActualGain());
        }
    }
    
    void updateFaderState()
    {
        // Disable fader if muted
        fader.setEnabled(!muteButton.getToggleState());
        updateFaderValueLabel();
    }
    
    void paint(juce::Graphics& g) override
    {
        // Use a semi-transparent light background for each track
        g.setColour(juce::Colours::white.withAlpha(0.08f)); // Very light white with low alpha
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    }

    void updateFaderValueLabel()
    {
        if (muteButton.getToggleState())
        {
            faderValueLabel.setText("MUTED", juce::dontSendNotification);
        }
        else
        {
            double faderVal = fader.getValue();
            
            // Use same dB calculation as getActualGain() for consistency
            double dB = -60.0 + (72.0 * faderVal);
            
            juce::String dbText;
            if (dB <= -60.0) {
                dbText = "-Inf dB";
            } else {
                // Format with 1 decimal place
                dbText = juce::String(dB, 1) + " dB";
            }
            
            faderValueLabel.setText(dbText, juce::dontSendNotification);
        }
    }

protected:
    juce::Label      trackTitle;
    juce::TextButton muteButton, soloButton;
    juce::Slider     fader;
    juce::Label      faderValueLabel;
    juce::OwnedArray<juce::Label> dbScaleLabels;
    
    // Simple track meter component
    class TrackMeterComponent : public juce::Component, private juce::Timer
    {
    public:
        TrackMeterComponent()
        {
            level = 0.0f;
            peak = 0.0f;
            startTimerHz(30); // 30fps updates
        }
        
        void setLevel(float newLevel)
        {
            level = newLevel;
            
            // Update peak hold
            if (newLevel > peak) peak = newLevel;
            
            // Decay peaks slowly
            peak *= 0.95f;
        }
        
        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().reduced(1);
            
            // Background
            g.setColour(juce::Colours::black);
            g.fillRect(bounds);
            
            // Scale to dB (-60 to 0)
            float levelDb = juce::Decibels::gainToDecibels(level, -60.0f);
            float peakDb = juce::Decibels::gainToDecibels(peak, -60.0f);
            
            // Convert to 0-1 range
            float levelNorm = (levelDb + 60.0f) / 60.0f;
            float peakNorm = (peakDb + 60.0f) / 60.0f;
            
            int levelHeight = (int)(bounds.getHeight() * levelNorm);
            int peakHeight = (int)(bounds.getHeight() * peakNorm);
            
            // Draw level bar
            auto levelRect = bounds.removeFromBottom(levelHeight);
            
            // Color gradient: green -> yellow -> red
            if (levelNorm < 0.75f)
                g.setColour(juce::Colours::green.interpolatedWith(juce::Colours::yellow, levelNorm / 0.75f));
            else
                g.setColour(juce::Colours::yellow.interpolatedWith(juce::Colours::red, (levelNorm - 0.75f) / 0.25f));
            
            g.fillRect(levelRect);
            
            // Draw peak line
            if (peak > 0.001f)
            {
                g.setColour(juce::Colours::white);
                int peakY = bounds.getBottom() - peakHeight;
                g.drawLine(bounds.getX(), peakY, bounds.getRight(), peakY, 1.0f);
            }
        }
        
    private:
        void timerCallback() override
        {
            repaint();
        }
        
        float level, peak;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackMeterComponent)
    };
    
    std::unique_ptr<TrackMeterComponent> trackMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTrackComponent)
};
