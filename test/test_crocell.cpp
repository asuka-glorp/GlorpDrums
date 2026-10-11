#include <iostream>
#include <cmath>
#include "DrumKit.h"
#include "Sampler.h"

int main()
{
    const std::string kitPath = "/home/servandoc/.config/GlorpDrums/kits/GlorpDemoKit/drumkit.xml";
    std::cout << "Loading: " << kitPath << std::endl;
    auto kit = glorp::DrumKit::load (kitPath);
    std::cout << "Kit error: " << kit.error << std::endl;
    std::cout << "Kit instruments: " << kit.instruments.size() << std::endl;
    for (const auto& inst : kit.instruments)
    {
        std::cout << "  Inst: " << inst.name << " samples: " << inst.samples.size() << " resolved: " << inst.resolvedChannels.size() << std::endl;
    }

    glorp::Sampler sampler;
    sampler.setSampleRateAndBlock (44100.0, 512);
    juce::String err;
    bool ok = sampler.loadKit (std::move (kit), err);
    std::cout << "Sampler loadKit ok: " << ok << ", err: " << err << std::endl;
    std::cout << "Sampler instruments: " << sampler.numInstruments() << std::endl;

    float left[512] = { 0 };
    float right[512] = { 0 };
    float* out[2] = { left, right };

    // Test Snare (instrument 3)
    sampler.noteOn (3, 38, 0.9f, 0);
    float maxPeak = 0.0f;
    for (int b = 0; b < 20; ++b)
    {
        sampler.render (out, 2, 512);
        for (int i = 0; i < 512; ++i)
        {
            maxPeak = std::max (maxPeak, std::abs (left[i]));
            maxPeak = std::max (maxPeak, std::abs (right[i]));
        }
    }
    std::cout << "Snare 20-block peak: " << maxPeak << " (" << (20.0f * std::log10 (std::max (1e-5f, maxPeak))) << " dB)" << std::endl;

    // Test Kick (instrument 0 is KDrumL or ChinaL? Let's check name)
    int kickIdx = -1;
    for (int i = 0; i < sampler.numInstruments(); ++i)
        if (sampler.instrumentName(i).containsIgnoreCase("KDrum") || sampler.instrumentName(i).containsIgnoreCase("Kick"))
        { kickIdx = i; break; }

    std::cout << "Kick instrument index: " << kickIdx << " name: " << sampler.instrumentName(kickIdx) << std::endl;
    sampler.noteOn (kickIdx, 36, 0.95f, 0);
    maxPeak = 0.0f;
    for (int b = 0; b < 20; ++b)
    {
        sampler.render (out, 2, 512);
        for (int i = 0; i < 512; ++i)
        {
            maxPeak = std::max (maxPeak, std::abs (left[i]));
            maxPeak = std::max (maxPeak, std::abs (right[i]));
        }
    }
    std::cout << "Kick 20-block peak: " << maxPeak << " (" << (20.0f * std::log10 (std::max (1e-5f, maxPeak))) << " dB)" << std::endl;

    return 0;
}
