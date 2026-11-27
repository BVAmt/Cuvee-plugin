#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout CuveeProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dbText  = [] (float v, int) { return String (v, 1) + " dB"; };
    auto pctText = [] (float v, int) { return String (roundToInt (v * 100.f)) + " %"; };

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input",
        NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp", 1 }, "Compress",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "wow", 1 }, "Wow",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 0.15f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lofi", 1 }, "Lo-Fi",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "noise", 1 }, "Noise",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "noisetype", 1 }, "Noise Type",
        StringArray { "Tape Hiss", "Vinyl Crackle", "Mains Hum" }, 0));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    return { p.begin(), p.end() };
}

//==============================================================================
CuveeProcessor::CuveeProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pIn        = apvts.getRawParameterValue ("input");
    pComp      = apvts.getRawParameterValue ("comp");
    pWow       = apvts.getRawParameterValue ("wow");
    pLofi      = apvts.getRawParameterValue ("lofi");
    pNoise     = apvts.getRawParameterValue ("noise");
    pNoiseType = apvts.getRawParameterValue ("noisetype");
    pOut       = apvts.getRawParameterValue ("output");
    pMix       = apvts.getRawParameterValue ("mix");

    for (int i = 0; i < maxCh; ++i)
        rng[i].setSeed (1234 + i * 77);
}

bool CuveeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

//==============================================================================
void CuveeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    const int maxD = (int) std::ceil (0.025 * sampleRate);
    wowDelay.setMaximumDelayInSamples (maxD);
    wowDelay.prepare (spec);
    dryDelay.setMaximumDelayInSamples (maxD);
    dryDelay.prepare (spec);
    wowDelay.reset();
    dryDelay.reset();

    baseSamples = std::round (baseDelayMs * 0.001f * (float) sampleRate);
    setLatencySamples ((int) baseSamples);

    auto init = [&] (juce::SmoothedValue<float>& s, float v, double t = 0.03)
    {
        s.reset (sampleRate, t);
        s.setCurrentAndTargetValue (v);
    };
    init (smIn,   juce::Decibels::decibelsToGain (pIn->load()));
    init (smOut,  juce::Decibels::decibelsToGain (pOut->load()));
    init (smMix,  pMix->load());
    init (smComp, pComp->load(), 0.05);
    init (smWow,  pWow->load(),  0.05);
    init (smLofi, pLofi->load(), 0.05);
    init (smNoise, 0.f);

    const float fs = (float) sampleRate;
    atkCoef  = std::exp (-1.f / (0.008f * fs));
    relSlow  = std::exp (-1.f / (0.280f * fs));
    relFast  = std::exp (-1.f / (0.080f * fs));
    scHpCoef = 1.f - std::exp (-juce::MathConstants<float>::twoPi * 90.f / fs);

    grEnv = 0.f;
    phSlow = phFlut = drift = driftTarget = 0.f;
    driftCount = 0;
    humPh = 0.f;
    for (int c = 0; c < maxCh; ++c)
    {
        scHp[c] = 0.f; holdCnt[c] = held[c] = 0.f;
        lp1[c] = lp2[c] = hpS[c] = 0.f;
        nzLp[c] = crackEnv[c] = 0.f;
    }
}

//==============================================================================
void CuveeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), maxCh);
    const int n     = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    const float fs    = (float) sr;
    const float twoPi = juce::MathConstants<float>::twoPi;

    smIn.setTargetValue   (juce::Decibels::decibelsToGain (pIn->load()));
    smOut.setTargetValue  (juce::Decibels::decibelsToGain (pOut->load()));
    smMix.setTargetValue  (pMix->load());
    smComp.setTargetValue (pComp->load());
    smWow.setTargetValue  (pWow->load());
    smLofi.setTargetValue (pLofi->load());
    const float nv = pNoise->load();
    smNoise.setTargetValue (nv <= 0.001f ? 0.f : juce::Decibels::decibelsToGain (-66.f + nv * 42.f));
    const int noiseType = (int) pNoiseType->load();

    // --- per-block lo-fi constants
    const float lo     = pLofi->load();
    const float lpHz   = juce::jmin (0.45f * fs, 20000.f) * std::pow (0.12f, lo);
    const float lpCoef = juce::jmin (0.99f, 1.f - std::exp (-twoPi * lpHz / fs));
    const float hpHz   = 25.f + lo * 140.f;
    const float hpCoef = 1.f - std::exp (-twoPi * hpHz / fs);
    const float holdStep = 1.f + lo * lo * 7.f;
    const float levels   = std::pow (2.f, 16.f - 8.f * lo - 1.f);

    // --- per-block modulation constants
    const float slowInc = 0.6f / fs;
    const float flutInc = 6.3f / fs;
    const float maxExc  = maxExcurMs * 0.001f * fs;
    const float driftCoef = 1.f - std::exp (-twoPi * 0.9f / fs);
    const float humInc  = twoPi * 50.f / fs;
    const float crackleProb = 12.f / fs;

    float* data[maxCh] {};
    for (int c = 0; c < numCh; ++c) data[c] = buffer.getWritePointer (c);

    for (int i = 0; i < n; ++i)
    {
        const float inG  = smIn.getNextValue();
        const float outG = smOut.getNextValue();
        const float mix  = smMix.getNextValue();
        const float amt  = smComp.getNextValue();
        const float wowA = smWow.getNextValue();
        const float loA  = smLofi.getNextValue();
        const float nzG  = smNoise.getNextValue();

        // ---------- input + sidechain
        float x[maxCh] {};
        float sc = 0.f;
        for (int c = 0; c < numCh; ++c)
        {
            x[c] = data[c][i] * inG;
            scHp[c] += scHpCoef * (x[c] - scHp[c]);
            sc = juce::jmax (sc, std::abs (x[c] - scHp[c]));
        }

        // ---------- compressor (soft knee, stereo linked, program dependent release)
        const float levelDb = juce::Decibels::gainToDecibels (sc, -100.f);
        const float thr   = -4.f - amt * 26.f;
        const float ratio = 1.f + amt * 9.f;
        const float slope = 1.f - 1.f / ratio;
        const float over  = levelDb - thr;

        float gr;
        if (2.f * over < -kneeDb)               gr = 0.f;
        else if (2.f * std::abs (over) <= kneeDb) { const float t = over + kneeDb * 0.5f; gr = slope * t * t / (2.f * kneeDb); }
        else                                    gr = slope * over;

        const float tRel = juce::jmin (grEnv / 10.f, 1.f);
        const float relC = relSlow + (relFast - relSlow) * tRel;
        const float coef = gr > grEnv ? atkCoef : relC;
        grEnv = gr + coef * (grEnv - gr);

        const float makeupDb = -thr * slope * 0.5f;
        const float g = juce::Decibels::decibelsToGain (makeupDb - grEnv);

        // ---------- wow / flutter / drift
        phSlow += slowInc; if (phSlow >= 1.f) phSlow -= 1.f;
        phFlut += flutInc; if (phFlut >= 1.f) phFlut -= 1.f;
        if (--driftCount <= 0)
        {
            driftTarget = rng[0].nextFloat() * 2.f - 1.f;
            driftCount  = (int) (fs * 0.7f);
        }
        drift += driftCoef * (driftTarget - drift);

        float dry[maxCh] {}, y[maxCh] {};
        for (int c = 0; c < numCh; ++c)
        {
            dryDelay.pushSample (c, x[c]);
            dry[c] = dryDelay.popSample (c, baseSamples, true);

            const float off  = (float) c * 0.19f;
            const float s    = std::sin (twoPi * (phSlow + off));
            const float f    = std::sin (twoPi * (phFlut + off * 2.f));
            const float mod  = wowA * maxExc * (0.7f * s + 0.04f * f + 0.5f * drift);

            wowDelay.pushSample (c, x[c] * g);
            y[c] = wowDelay.popSample (c, baseSamples + mod, true);
        }

        // ---------- hum phase
        humPh += humInc; if (humPh > twoPi) humPh -= twoPi;

        // ---------- lo-fi + noise + mix
        for (int c = 0; c < numCh; ++c)
        {
            float v = y[c];

            if (loA > 0.0005f)
            {
                const float drive = 1.f + loA * 3.5f;
                v = std::tanh (v * drive) / std::sqrt (drive);

                holdCnt[c] += 1.f;
                if (holdCnt[c] >= holdStep) { holdCnt[c] -= holdStep; held[c] = v; }
                v = held[c];

                v = std::round (v * levels) / levels;
            }

            lp1[c] += lpCoef * (v - lp1[c]);
            lp2[c] += lpCoef * (lp1[c] - lp2[c]);
            v = lp2[c];
            hpS[c] += hpCoef * (v - hpS[c]);
            v -= hpS[c];

            if (nzG > 0.f)
            {
                const float white = rng[c].nextFloat() * 2.f - 1.f;
                float nz = 0.f;
                switch (noiseType)
                {
                    case 0:  // tape hiss
                        nzLp[c] += 0.35f * (white - nzLp[c]);
                        nz = nzLp[c] * 1.5f;
                        break;
                    case 1:  // vinyl: soft hiss + random pops
                        nzLp[c] += 0.2f * (white - nzLp[c]);
                        if (rng[c].nextFloat() < crackleProb)
                        {
                            const float r = rng[c].nextFloat();
                            crackEnv[c] = (rng[c].nextBool() ? 1.f : -1.f) * (0.2f + r * r);
                        }
                        crackEnv[c] *= 0.92f;
                        nz = nzLp[c] * 0.6f + crackEnv[c] * 4.f;
                        break;
                    default: // 50 Hz mains hum
                        nz = 0.8f * std::sin (humPh) + 0.3f * std::sin (2.f * humPh)
                           + 0.15f * std::sin (3.f * humPh) + white * 0.03f;
                        break;
                }
                v += nz * nzG;
            }

            data[c][i] = (dry[c] * (1.f - mix) + v * mix) * outG;
        }
    }

    grMeter.store (grEnv);
}

//==============================================================================
juce::AudioProcessorEditor* CuveeProcessor::createEditor() { return new CuveeEditor (*this); }

void CuveeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void CuveeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CuveeProcessor(); }
