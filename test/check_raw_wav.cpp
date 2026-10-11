#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include "SampleCache.h"

int main()
{
    glorp::SampleCache cache;
    // Let's open a snare sample
    const std::string path = "/home/servandoc/.config/GlorpDrums/kits/GlorpDemoKit/Snare/samples/1-Snare.wav";
    auto handle = cache.open (path);
    std::cout << "Valid: " << handle.valid << ", channels: " << handle.numChannels
              << ", frames: " << handle.numFrames << ", rate: " << handle.sampleRate << std::endl;

    if (handle.valid)
    {
        for (int ch = 0; ch < handle.numChannels; ++ch)
        {
            std::vector<float> buf (handle.numFrames);
            handle.read (0, handle.numFrames, ch, buf.data());
            float peak = 0.0f;
            for (float s : buf) peak = std::max (peak, std::abs (s));
            std::cout << "Channel " << ch << " peak: " << peak << " (" << (20.0f * std::log10 (std::max (1e-6f, peak))) << " dB)\n";
        }
    }
    return 0;
}
