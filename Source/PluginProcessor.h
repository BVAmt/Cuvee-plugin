#pragma once
#include <JuceHeader.h>

class CuveeProcessor : public juce::AudioProcessor
{
public:
    CuveeProcessor();
    ~CuveeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cuvee"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> grMeter { 0.0f };   // gain reduction in dB, read by the editor

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int   maxCh        = 2;
    static constexpr float baseDelayMs  = 6.0f;   // fixed latency of the wow section
    static constexpr float maxExcurMs   = 4.0f;   // max modulation depth
    static constexpr float kneeDb       = 8.0f;

    double sr = 44100.0;
    float  baseSamples = 264.f;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> wowDelay { 4096 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>        dryDelay { 4096 };

    // parameters
    std::atomic<float> *pIn, *pComp, *pWow, *pLofi, *pNoise, *pNoiseType, *pOut, *pMix;

    juce::SmoothedValue<float> smIn, smOut, smMix, smComp, smWow, smLofi, smNoise;

    // compressor
    float atkCoef = 0.f, relSlow = 0.f, relFast = 0.f, scHpCoef = 0.f;
    float scHp[maxCh] {};
    float grEnv = 0.f;

    // wow / flutter / drift
    float phSlow = 0.f, phFlut = 0.f, drift = 0.f, driftTarget = 0.f;
    int   driftCount = 0;

    // lo-fi
    float holdCnt[maxCh] {}, held[maxCh] {};
    float lp1[maxCh] {}, lp2[maxCh] {}, hpS[maxCh] {};

    // noise
    juce::Random rng[maxCh];
    float nzLp[maxCh] {}, crackEnv[maxCh] {};
    float humPh = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CuveeProcessor)
};
