/*
    GlorpKitEditorWindow.cpp - Standalone graphical .glorpkit Studio editor UI & audio preview engine.
    Follows the hand-drawn comic/sketch aesthetic of GlorpDrums (Theme::sketch() & Theme::dark()).
*/

#include "GlorpKitEditorWindow.h"
#include "GlorpDrumsAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace glorp
{

namespace
{

namespace Fonts
{
    inline juce::Font title (float h) { return displayFont (h); }
    inline juce::Font body  (float h) { return uiFont (h); }
}

namespace Sketch
{
    inline void paperGrain (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink)
    {
        g.setColour (ink.withAlpha (0.018f));
        for (float y = area.getY() + 6.0f; y < area.getBottom(); y += 18.0f)
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }

    inline void box (juce::Graphics& g, juce::Rectangle<float> r,
                     juce::Colour fillCol, juce::Colour strokeCol,
                     float corner = 5.0f, float strokeW = 1.8f, int passes = 1, int seed = 1)
    {
        g.setColour (fillCol);
        g.fillRoundedRectangle (r, corner);
        drawSketchRect (g, r, strokeCol, strokeW, (unsigned) seed);
        if (passes > 1)
            drawSketchRect (g, r.reduced (0.8f), strokeCol.withAlpha (0.45f), strokeW * 0.65f, (unsigned) (seed + 77));
    }

    inline void button (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                        juce::Colour bg, juce::Colour inkCol, bool active, int seed, const juce::Font& f)
    {
        g.setColour (inkCol.withAlpha (active ? 0.12f : 0.22f));
        g.fillRoundedRectangle (r.translated (1.5f, 2.0f), 5.0f);

        box (g, r, bg, inkCol, 5.0f, active ? 2.2f : 1.7f, 1, seed);
        g.setColour (inkCol);
        g.setFont (f);
        g.drawText (text, r.reduced (4.0f, 1.0f), juce::Justification::centred, true);
    }
}

juce::Colour parseHexColourSafe (const std::string& hex, juce::Colour fallback = juce::Colour (0xffff8200))
{
    if (hex.empty())
        return fallback;
    juce::String s = juce::String::fromUTF8 (hex.c_str()).trim();
    if (s.startsWithChar ('#'))
        s = s.substring (1);
    if (s.length() == 6)
        return juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
    if (s.length() == 8)
        return juce::Colour ((juce::uint32) s.getHexValue32());
    return fallback;
}

std::string toHexColourString (juce::Colour c)
{
    char buf[16];
    std::snprintf (buf, sizeof (buf), "#%02x%02x%02x",
                   (unsigned) c.getRed(), (unsigned) c.getGreen(), (unsigned) c.getBlue());
    return buf;
}

std::string normalizeSpriteKey (const std::string& g)
{
    juce::String s = juce::String::fromUTF8 (g.c_str()).toLowerCase().trim();
    if (s.endsWith (".png"))
        s = s.dropLastCharacters (4);
    if (s.contains ("kick"))  return "kick";
    if (s.contains ("snare")) return "snare";
    if (s.contains ("hihat") || s.contains ("hat")) return "hihat";
    if (s.contains ("tom_1") || s.contains ("tom1")) return "tom_1";
    if (s.contains ("tom_2") || s.contains ("tom2")) return "tom_2";
    if (s.contains ("f_tom") || s.contains ("floor") || s.contains ("ftom")) return "f_tom";
    if (s.contains ("crash_1") || s.contains ("crash1")) return "crash_1";
    if (s.contains ("crash_2") || s.contains ("crash2")) return "crash_2";
    if (s.contains ("ride")) return "ride";
    return s.toStdString();
}

juce::Image makeGlowSprite (const juce::Image& src, juce::Colour glowCol)
{
    if (! src.isValid()) return {};
    const int w = src.getWidth();
    const int h = src.getHeight();
    juce::Image mask (juce::Image::ARGB, w, h, true);
    {
        juce::Image::BitmapData sData (src, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData mData (mask, juce::Image::BitmapData::writeOnly);
        const juce::uint8 r = glowCol.getRed();
        const juce::uint8 g = glowCol.getGreen();
        const juce::uint8 b = glowCol.getBlue();
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto a = sData.getPixelColour (x, y).getAlpha();
                if (a > 12)
                    mData.setPixelColour (x, y, juce::Colour::fromRGBA (r, g, b, a));
            }
        }
    }
    juce::Image glow (juce::Image::ARGB, w, h, true);
    juce::Graphics gg (glow);
    const int offsets[8][2] = { {-8,0},{8,0},{0,-8},{0,8},{-6,-6},{6,-6},{-6,6},{6,6} };
    gg.setOpacity (0.28f);
    for (auto& o : offsets)
        gg.drawImageAt (mask, o[0], o[1]);
    gg.setOpacity (0.85f);
    gg.drawImageAt (mask, 0, 0);
    return glow;
}

const char* midiNoteName (int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static thread_local char buf[16];
    if (note < 0 || note > 127) return "---";
    const int octave = (note / 12) - 1;
    std::snprintf (buf, sizeof (buf), "%s%d (%d)", names[note % 12], octave, note);
    return buf;
}

} // namespace

//==============================================================================
GlorpKitEditorComponent::GlorpKitEditorComponent (bool enableAudioDevice)
{
    setSize (1280, 820);
    setWantsKeyboardFocus (true);

    sampler.setSampleRateAndBlock (48000.0, 512);
    sampler.setMultiOutMode (false);

    loadBuiltinSprites();
    project.createNewDefaultKit();
    hitGlowColour = parseHexColourSafe (project.hitGlowColor);

    auto setupEditor = [this] (juce::TextEditor& ed)
    {
        ed.setFont (Fonts::body (14.5f));
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        ed.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        ed.setColour (juce::TextEditor::focusedOutlineColourId, theme.accent);
        ed.setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (ed);
    };
    setupEditor (pieceNameEditor);
    setupEditor (pieceIdEditor);
    setupEditor (zoneNameEditor);

    pieceNameEditor.onTextChange = [this]()
    {
        if (suppressEditorCallbacks) return;
        if (selectedPieceIdx >= 0 && selectedPieceIdx < (int) project.pieces.size())
        {
            project.pieces[(size_t) selectedPieceIdx].name = pieceNameEditor.getText().toStdString();
            repaint();
        }
    };
    pieceIdEditor.onTextChange = [this]()
    {
        if (suppressEditorCallbacks) return;
        if (selectedPieceIdx >= 0 && selectedPieceIdx < (int) project.pieces.size())
        {
            project.pieces[(size_t) selectedPieceIdx].id = pieceIdEditor.getText().toStdString();
            repaint();
        }
    };
    zoneNameEditor.onTextChange = [this]()
    {
        if (suppressEditorCallbacks) return;
        if (selectedPieceIdx >= 0 && selectedPieceIdx < (int) project.pieces.size())
        {
            auto& p = project.pieces[(size_t) selectedPieceIdx];
            if (selectedZoneIdx >= 0 && selectedZoneIdx < (int) p.zones.size())
            {
                p.zones[(size_t) selectedZoneIdx].name = zoneNameEditor.getText().toStdString();
                reloadProjectIntoEngine();
                repaint();
            }
        }
    };

    reloadProjectIntoEngine();
    selectPiece (4, false); // Default to Snare

    if (enableAudioDevice)
    {
        auto err = deviceManager.initialiseWithDefaultDevices (0, 2);
        if (! err.isEmpty())
            deviceManager.initialise (0, 2, nullptr, true);
        deviceManager.addAudioCallback (this);
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            sampler.setSampleRateAndBlock (dev->getCurrentSampleRate(), dev->getCurrentBufferSizeSamples());
        else
            sampler.setSampleRateAndBlock (44100.0, 512);

        auto midiInputs = juce::MidiInput::getAvailableDevices();
        for (const auto& m : midiInputs)
        {
            deviceManager.setMidiInputDeviceEnabled (m.identifier, true);
            deviceManager.addMidiInputDeviceCallback (m.identifier, this);
        }
        audioDeviceActive = true;
    }
    else
    {
        sampler.setSampleRateAndBlock (44100.0, 512);
    }

    loadFirstTimeTutorialPreference();
    startTimerHz (30);
}

GlorpKitEditorComponent::~GlorpKitEditorComponent()
{
    stopTimer();
    if (audioDeviceActive)
    {
        deviceManager.removeAudioCallback (this);
        auto midiInputs = juce::MidiInput::getAvailableDevices();
        for (const auto& m : midiInputs)
            deviceManager.removeMidiInputDeviceCallback (m.identifier, this);
    }
}

void GlorpKitEditorComponent::loadBuiltinSprites()
{
    struct Entry { const char* key; const char* data; int size; };
    const Entry entries[] = {
        { "kick",    GlorpAssets::kick_png,    GlorpAssets::kick_pngSize },
        { "snare",   GlorpAssets::snare_png,   GlorpAssets::snare_pngSize },
        { "hihat",   GlorpAssets::hihat_png,   GlorpAssets::hihat_pngSize },
        { "tom_1",   GlorpAssets::tom_1_png,   GlorpAssets::tom_1_pngSize },
        { "tom_2",   GlorpAssets::tom_2_png,   GlorpAssets::tom_2_pngSize },
        { "f_tom",   GlorpAssets::f_tom_png,   GlorpAssets::f_tom_pngSize },
        { "crash_1", GlorpAssets::crash_1_png, GlorpAssets::crash_1_pngSize },
        { "crash_2", GlorpAssets::crash_2_png, GlorpAssets::crash_2_pngSize },
        { "ride",    GlorpAssets::ride_png,    GlorpAssets::ride_pngSize },
    };
    for (const auto& e : entries)
    {
        auto img = juce::ImageFileFormat::loadFrom (e.data, (size_t) e.size);
        builtinImages[e.key] = img;
        builtinImages[std::string (e.key) + ".png"] = img;
    }
}

void GlorpKitEditorComponent::rebuildPieceSprites()
{
    const auto& pieces = project.pieces;
    pieceSprites.resize (pieces.size());
    hitGlowColour = parseHexColourSafe (project.hitGlowColor, hitGlowColour);

    for (size_t i = 0; i < pieces.size(); ++i)
    {
        const auto& p = pieces[i];
        juce::Image img;
        if (! p.customPngBytes.empty())
            img = juce::ImageFileFormat::loadFrom (p.customPngBytes.data(), p.customPngBytes.size());

        if (! img.isValid())
        {
            const std::string key = normalizeSpriteKey (p.graphicId.empty() ? p.name : p.graphicId);
            auto it = builtinImages.find (key);
            if (it != builtinImages.end())
                img = it->second;
            else if (! builtinImages.empty())
                img = builtinImages["snare"];
        }

        pieceSprites[i].rawImage  = img;
        pieceSprites[i].glowImage = makeGlowSprite (img, hitGlowColour);
    }
    computeCanvasPieceBounds();
}

void GlorpKitEditorComponent::computeCanvasPieceBounds()
{
    if (canvasViewportBounds.isEmpty()) return;
    const auto& pieces = project.pieces;
    if (pieceSprites.size() != pieces.size())
        pieceSprites.resize (pieces.size());

    const float scaleX = canvasViewportBounds.getWidth() / 820.0f;
    const float scaleY = canvasViewportBounds.getHeight() / 470.0f;
    const float scale  = std::min (scaleX, scaleY);

    for (size_t i = 0; i < pieces.size(); ++i)
    {
        const auto& p = pieces[i];
        auto& sp = pieceSprites[i];
        float aspect = 1.0f;
        if (sp.rawImage.isValid() && sp.rawImage.getWidth() > 0)
            aspect = (float) sp.rawImage.getHeight() / (float) sp.rawImage.getWidth();

        const float drawW = std::max (36.0f, p.normW * scale);
        const float drawH = drawW * aspect;
        const float cx = canvasViewportBounds.getX() + p.normX * canvasViewportBounds.getWidth();
        const float cy = canvasViewportBounds.getY() + p.normY * canvasViewportBounds.getHeight();
        sp.canvasBounds = { cx - drawW * 0.5f, cy - drawH * 0.5f, drawW, drawH };
    }
}

void GlorpKitEditorComponent::reloadProjectIntoEngine()
{
    hitGlowColour = parseHexColourSafe (project.hitGlowColor, hitGlowColour);
    rebuildPieceSprites();
    auto runtimeKit = project.buildRuntimeDrumKit();
    juce::String loadErr;
    sampler.loadKit (std::move (runtimeKit), loadErr);
    rebuildSelectedHitWaveform();
    repaint();
}

bool GlorpKitEditorComponent::openKitFile (const juce::File& file)
{
    std::string err;
    bool ok = false;
    if (file.hasFileExtension (".glorpkit"))
        ok = project.openGlorpKitArchive (file.getFullPathName().toStdString(), err);
    else if (file.hasFileExtension (".xml"))
        ok = project.importDrumGizmoXml (file.getFullPathName().toStdString(), err);

    if (! ok)
    {
        showStatusToast ("Failed to open kit: " + juce::String (err));
        return false;
    }

    selectedPieceIdx = project.pieces.empty() ? -1 : 0;
    selectedZoneIdx  = 0;
    selectedHitIdx   = 0;
    reloadProjectIntoEngine();
    selectPiece (selectedPieceIdx, false);
    showStatusToast ("Loaded kit: " + juce::String (project.title) + " (" + file.getFileName() + ")");
    return true;
}

bool GlorpKitEditorComponent::saveGlorpKitFile (const juce::File& targetFile)
{
    std::string err;
    if (! project.exportToGlorpKit (targetFile.getFullPathName().toStdString(), err))
    {
        showStatusToast ("Export failed: " + juce::String (err));
        return false;
    }
    showStatusToast ("Saved v2.0 .glorpkit archive: " + targetFile.getFileName());
    return true;
}

void GlorpKitEditorComponent::setDarkTheme (bool dark)
{
    theme = dark ? Theme::dark() : Theme::sketch();
    for (auto* ed : { &pieceNameEditor, &pieceIdEditor, &zoneNameEditor })
    {
        ed->setColour (juce::TextEditor::textColourId, theme.ink);
        ed->setColour (juce::TextEditor::backgroundColourId, theme.panel);
        ed->setColour (juce::TextEditor::outlineColourId, theme.inkSoft.withAlpha (0.4f));
        ed->setColour (juce::TextEditor::focusedOutlineColourId, theme.accent);
    }
    pieceIdEditor.setColour (juce::TextEditor::textColourId, theme.inkSoft);
    repaint();
}

void GlorpKitEditorComponent::setHitGlowColourHex (const std::string& hexColor)
{
    project.hitGlowColor = hexColor;
    hitGlowColour = parseHexColourSafe (hexColor, hitGlowColour);
    rebuildPieceSprites();
    if (selectedPieceIdx >= 0 && selectedPieceIdx < (int) pieceSprites.size())
        pieceSprites[(size_t) selectedPieceIdx].flash = 1.0f;
    showStatusToast ("Updated Kit Hit Glow Color to " + juce::String (hexColor).toUpperCase());
    repaint();
}

void GlorpKitEditorComponent::setTutorialVisible (bool visible, int step)
{
    tutorialActive = visible;
    tutorialStep   = juce::jlimit (0, 4, step);
    if (! visible)
        saveFirstTimeTutorialPreference (true);
    repaint();
}

void GlorpKitEditorComponent::nextTutorialStep()
{
    if (tutorialStep < 4)
    {
        ++tutorialStep;
        repaint();
    }
    else
    {
        setTutorialVisible (false, 0);
        showStatusToast ("Tour completed! Click '? Tour' in the top header anytime to view it again.");
    }
}

void GlorpKitEditorComponent::prevTutorialStep()
{
    if (tutorialStep > 0)
    {
        --tutorialStep;
        repaint();
    }
}

void GlorpKitEditorComponent::loadFirstTimeTutorialPreference()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GlorpDrums");
    auto settingsFile = dir.getChildFile ("kit_editor_settings.xml");
    if (settingsFile.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse (settingsFile))
        {
            if (xml->getBoolAttribute ("tutorialCompleted", false))
            {
                tutorialActive = false;
                return;
            }
        }
    }
    tutorialActive = true;
    tutorialStep   = 0;
}

void GlorpKitEditorComponent::saveFirstTimeTutorialPreference (bool completed)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GlorpDrums");
    dir.createDirectory();
    auto settingsFile = dir.getChildFile ("kit_editor_settings.xml");
    juce::XmlElement root ("GlorpKitEditorSettings");
    root.setAttribute ("tutorialCompleted", completed);
    root.writeTo (settingsFile);
}

void GlorpKitEditorComponent::selectPiece (int pieceIndex, bool audition)
{
    const auto& pieces = project.pieces;
    if (pieces.empty())
    {
        selectedPieceIdx = -1;
        return;
    }
    selectedPieceIdx = juce::jlimit (0, (int) pieces.size() - 1, pieceIndex);
    selectedZoneIdx  = 0;
    selectedHitIdx   = 0;

    suppressEditorCallbacks = true;
    const auto& p = pieces[(size_t) selectedPieceIdx];
    pieceNameEditor.setText (p.name, juce::dontSendNotification);
    pieceIdEditor.setText (p.id, juce::dontSendNotification);
    if (! p.zones.empty())
        zoneNameEditor.setText (p.zones.front().name, juce::dontSendNotification);
    pieceNameEditor.setColour (juce::TextEditor::textColourId, theme.ink);
    pieceIdEditor.setColour (juce::TextEditor::textColourId, theme.inkSoft);
    zoneNameEditor.setColour (juce::TextEditor::textColourId, theme.ink);
    suppressEditorCallbacks = false;

    rebuildSelectedHitWaveform();
    if (audition)
        triggerAuditionForPiece (selectedPieceIdx);
    repaint();
}

void GlorpKitEditorComponent::selectZone (int zoneIndex, bool audition)
{
    const auto& pieces = project.pieces;
    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) pieces.size()) return;
    const auto& zones = pieces[(size_t) selectedPieceIdx].zones;
    if (zones.empty()) return;

    selectedZoneIdx = juce::jlimit (0, (int) zones.size() - 1, zoneIndex);
    selectedHitIdx  = 0;

    suppressEditorCallbacks = true;
    zoneNameEditor.setText (zones[(size_t) selectedZoneIdx].name, juce::dontSendNotification);
    zoneNameEditor.setColour (juce::TextEditor::textColourId, theme.ink);
    suppressEditorCallbacks = false;

    rebuildSelectedHitWaveform();
    if (audition)
        triggerAuditionForZone (selectedPieceIdx, selectedZoneIdx);
    repaint();
}

void GlorpKitEditorComponent::selectHit (int hitIndex, bool audition)
{
    const auto& pieces = project.pieces;
    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) pieces.size()) return;
    const auto& zones = pieces[(size_t) selectedPieceIdx].zones;
    if (selectedZoneIdx < 0 || selectedZoneIdx >= (int) zones.size()) return;
    const auto& hits = zones[(size_t) selectedZoneIdx].hits;
    if (hits.empty())
    {
        selectedHitIdx = -1;
        rebuildSelectedHitWaveform();
        repaint();
        return;
    }
    selectedHitIdx = juce::jlimit (0, (int) hits.size() - 1, hitIndex);
    rebuildSelectedHitWaveform();
    if (audition)
        triggerAuditionForZone (selectedPieceIdx, selectedZoneIdx);
    repaint();
}

void GlorpKitEditorComponent::rebuildSelectedHitWaveform()
{
    selectedHitWaveform.assign (64, 0.0f);
    const auto& pieces = project.pieces;
    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) pieces.size()) return;
    const auto& zones = pieces[(size_t) selectedPieceIdx].zones;
    if (selectedZoneIdx < 0 || selectedZoneIdx >= (int) zones.size()) return;
    const auto& hits = zones[(size_t) selectedZoneIdx].hits;

    float power = 0.65f;
    std::string pathToRead;
    if (selectedHitIdx >= 0 && selectedHitIdx < (int) hits.size())
    {
        const auto& h = hits[(size_t) selectedHitIdx];
        power = juce::jlimit (0.15f, 1.0f, h.power);
        if (! h.micSamples.empty())
            pathToRead = h.micSamples.front().sourceDiskPath;
    }

    if (! pathToRead.empty() && juce::File (pathToRead).existsAsFile())
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (juce::File (pathToRead)) })
        {
            const int total = (int) std::min<juce::int64> (r->lengthInSamples, 48000);
            if (total > 64)
            {
                juce::AudioBuffer<float> buf (1, total);
                r->read (&buf, 0, total, 0, true, false);
                const float* d = buf.getReadPointer (0);
                const int step = std::max (1, total / 64);
                for (int i = 0; i < 64; ++i)
                {
                    float pk = 0.0f;
                    for (int j = 0; j < step && (i * step + j) < total; ++j)
                        pk = std::max (pk, std::abs (d[i * step + j]));
                    selectedHitWaveform[(size_t) i] = juce::jlimit (0.04f, 1.0f, pk);
                }
                return;
            }
        }
    }

    for (int i = 0; i < 64; ++i)
    {
        const float t = (float) i / 63.0f;
        const float env = std::exp (-t * 4.8f) * power;
        const float mod = 0.55f + 0.45f * std::abs (std::sin ((float) (i * 1.9 + selectedHitIdx * 0.7)));
        selectedHitWaveform[(size_t) i] = juce::jlimit (0.04f, 1.0f, env * mod);
    }
}

void GlorpKitEditorComponent::triggerAuditionForPiece (int pieceIdx)
{
    if (pieceIdx < 0 || pieceIdx >= (int) project.pieces.size()) return;
    if (pieceIdx < (int) pieceSprites.size())
        pieceSprites[(size_t) pieceIdx].flash = 1.0f;
    triggerAuditionForZone (pieceIdx, 0);
}

void GlorpKitEditorComponent::triggerAuditionForZone (int pieceIdx, int zoneIdx)
{
    if (pieceIdx < 0 || pieceIdx >= (int) project.pieces.size()) return;
    if (pieceIdx < (int) pieceSprites.size())
        pieceSprites[(size_t) pieceIdx].flash = 1.0f;

    const auto& p = project.pieces[(size_t) pieceIdx];
    if (zoneIdx >= 0 && zoneIdx < (int) p.zones.size())
    {
        int samplerIdx = project.getSamplerIndexForZone (pieceIdx, zoneIdx);
        if (samplerIdx >= 0 && samplerIdx < sampler.numInstruments())
        {
            sampler.noteOn (samplerIdx, 0.85f, sampler.currentSamplePosition());
        }
        else
        {
            const juce::String instIdDot = juce::String (p.name + "." + p.zones[(size_t) zoneIdx].name);
            const juce::String instIdUnderscore = juce::String (p.name + "_" + p.zones[(size_t) zoneIdx].name);
            const juce::String pName = juce::String (p.name);
            for (int i = 0; i < sampler.numInstruments(); ++i)
            {
                if (sampler.instrumentName (i).equalsIgnoreCase (instIdDot)
                    || sampler.instrumentName (i).equalsIgnoreCase (instIdUnderscore)
                    || sampler.instrumentName (i).equalsIgnoreCase (pName))
                {
                    sampler.noteOn (i, 0.85f, sampler.currentSamplePosition());
                    break;
                }
            }
        }
    }

    startAuditionSynth (pieceIdx, 0.85f);
    repaint();
}

void GlorpKitEditorComponent::showStatusToast (const juce::String& text)
{
    statusBannerText  = text;
    statusBannerAlpha = 1.0f;
    repaint();
}

//==============================================================================
void GlorpKitEditorComponent::resized()
{
    auto full = getLocalBounds().toFloat();
    headerBounds = full.removeFromTop (52.0f);

    auto body = full.reduced (10.0f, 8.0f);
    leftPanelBounds  = body.removeFromLeft (236.0f);
    body.removeFromLeft (10.0f);
    rightPanelBounds = body.removeFromRight (302.0f);
    body.removeFromRight (10.0f);

    const float canvasH = std::round (body.getHeight() * 0.62f);
    canvasPanelBounds = body.removeFromTop (canvasH);
    body.removeFromTop (10.0f);
    matrixPanelBounds = body;

    canvasViewportBounds = canvasPanelBounds.reduced (12.0f, 38.0f).withTrimmedBottom (6.0f);
    computeCanvasPieceBounds();

    cardPieceBounds  = { rightPanelBounds.getX() + 10.0f, rightPanelBounds.getY() + 36.0f, rightPanelBounds.getWidth() - 20.0f, 224.0f };
    cardZoneBounds   = { rightPanelBounds.getX() + 10.0f, cardPieceBounds.getBottom() + 8.0f, rightPanelBounds.getWidth() - 20.0f, 178.0f };
    cardSampleBounds = { rightPanelBounds.getX() + 10.0f, cardZoneBounds.getBottom() + 8.0f, rightPanelBounds.getWidth() - 20.0f,
                         std::max (160.0f, rightPanelBounds.getBottom() - (cardZoneBounds.getBottom() + 18.0f)) };

    pieceNameEditor.setBounds (juce::Rectangle<float> (cardPieceBounds.getX() + 92.0f, cardPieceBounds.getY() + 30.0f,
                                                       cardPieceBounds.getWidth() - 102.0f, 22.0f).toNearestInt());
    pieceIdEditor.setBounds (juce::Rectangle<float> (cardPieceBounds.getX() + 92.0f, cardPieceBounds.getY() + 56.0f,
                                                     cardPieceBounds.getWidth() - 102.0f, 22.0f).toNearestInt());
    zoneNameEditor.setBounds (juce::Rectangle<float> (cardZoneBounds.getX() + 92.0f, cardZoneBounds.getY() + 26.0f,
                                                      cardZoneBounds.getWidth() - 102.0f, 22.0f).toNearestInt());
}

//==============================================================================
void GlorpKitEditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    Sketch::paperGrain (g, getLocalBounds().toFloat(), theme.ink);

    paintHeader (g);
    paintLeftPanel (g);
    paintCanvasPanel (g);
    paintMatrixPanel (g);
    paintRightInspector (g);

    if (tutorialActive)
        paintGuidedTutorialOverlay (g);
}

void GlorpKitEditorComponent::paintHeader (juce::Graphics& g)
{
    Sketch::box (g, headerBounds.reduced (2.0f, 1.0f), theme.panel, theme.ink, 4.0f, 2.2f, 2, 11);

    auto titleArea = headerBounds.withLeft (16.0f).withWidth (228.0f);
    g.setColour (theme.ink);
    g.setFont (Fonts::title (21.0f));
    g.drawText ("GLORPKIT STUDIO", titleArea.withTop (6.0f).withHeight (24.0f), juce::Justification::centredLeft, false);
    g.setColour (theme.accent);
    g.setFont (Fonts::body (12.5f));
    g.drawText ("v2.0 Standalone Kit Architect", titleArea.withTop (28.0f).withHeight (18.0f), juce::Justification::centredLeft, false);

    float x = 242.0f;
    const float btnY = 10.0f, btnH = 32.0f;
    auto drawHeaderBtn = [&] (juce::Rectangle<float>& outRect, float w, const juce::String& label, juce::Colour bg, int seed)
    {
        outRect = { x, btnY, w, btnH };
        Sketch::button (g, outRect, label, bg, theme.ink, false, seed, Fonts::title (13.0f));
        x += w + 7.0f;
    };

    drawHeaderBtn (btnNewKit,   68.0f, "New Kit",         theme.trough, 21);
    drawHeaderBtn (btnOpenKit,  86.0f, "Open Kit...",     theme.trough, 23);
    drawHeaderBtn (btnImportDg, 108.0f, "Import XML...",  theme.trough, 25);
    drawHeaderBtn (btnSaveKit,  124.0f, "Save .glorpkit", theme.accent, 27);

    float rx = headerBounds.getRight() - 12.0f;
    btnThemeToggle = { rx - 68.0f, btnY, 68.0f, btnH };
    rx = btnThemeToggle.getX() - 7.0f;
    Sketch::button (g, btnThemeToggle, theme.isDark ? "Sketch" : "Dark", theme.trough, theme.ink, false, 31, Fonts::title (13.0f));

    btnTour = { rx - 68.0f, btnY, 68.0f, btnH };
    rx = btnTour.getX() - 7.0f;
    Sketch::button (g, btnTour, "? Tour", tutorialActive ? theme.accent : theme.trough, theme.ink, tutorialActive, 33, Fonts::title (13.0f));

    btnAudioSettings = { rx - 68.0f, btnY, 68.0f, btnH };
    rx = btnAudioSettings.getX() - 7.0f;
    Sketch::button (g, btnAudioSettings, "Audio", theme.trough, theme.ink, false, 34, Fonts::title (13.0f));

    // Kit-Wide Hit Glow Color Selector Button
    btnGlowColor = { rx - 128.0f, btnY, 128.0f, btnH };
    rx = btnGlowColor.getX() - 8.0f;
    Sketch::box (g, btnGlowColor, theme.trough, theme.ink, 5.0f, 1.8f, 2, 35);
    juce::Rectangle<float> swatch = { btnGlowColor.getX() + 8.0f, btnGlowColor.getY() + 7.0f, 18.0f, 18.0f };
    g.setColour (hitGlowColour);
    g.fillRoundedRectangle (swatch, 4.0f);
    g.setColour (theme.ink);
    g.drawRoundedRectangle (swatch, 4.0f, 1.5f);
    g.setFont (Fonts::title (12.0f));
    g.drawText ("Glow " + juce::String (toHexColourString (hitGlowColour)).toUpperCase(),
                btnGlowColor.withLeft (swatch.getRight() + 5.0f), juce::Justification::centredLeft, false);

    // Kit Metadata Pill
    pillKitMeta = { x + 2.0f, btnY, std::max (120.0f, rx - (x + 4.0f)), btnH };
    Sketch::box (g, pillKitMeta, theme.trough, theme.inkSoft, 5.0f, 1.5f, 1, 39);
    g.setColour (theme.ink);
    g.setFont (Fonts::body (13.5f));
    juce::String metaStr = "Kit: " + juce::String (project.title)
                         + "  |  " + juce::String (project.sampleRate) + " Hz";
    g.drawText (metaStr, pillKitMeta.reduced (10.0f, 2.0f), juce::Justification::centredLeft, true);
}

void GlorpKitEditorComponent::paintLeftPanel (juce::Graphics& g)
{
    const juce::Colour goodCol (0xff2ecc71);
    Sketch::box (g, leftPanelBounds, theme.panel, theme.ink, 6.0f, 2.2f, 2, 41);

    auto hdr = leftPanelBounds.withHeight (30.0f).reduced (12.0f, 4.0f);
    g.setColour (theme.ink);
    g.setFont (Fonts::title (14.0f));
    g.drawText ("Z-LAYER STACK (TOP -> BACK)", hdr, juce::Justification::centredLeft, false);

    const float topBtnY = leftPanelBounds.getY() + 32.0f;
    const float bw = (leftPanelBounds.getWidth() - 32.0f) / 3.0f;
    btnAddPiece = { leftPanelBounds.getX() + 10.0f, topBtnY, bw, 25.0f };
    btnDupPiece = { btnAddPiece.getRight() + 6.0f,  topBtnY, bw, 25.0f };
    btnDelPiece = { btnDupPiece.getRight() + 6.0f,  topBtnY, bw, 25.0f };
    Sketch::button (g, btnAddPiece, "+ Piece", theme.accent, theme.ink, false, 43, Fonts::title (12.0f));
    Sketch::button (g, btnDupPiece, "Dup",     theme.trough, theme.ink, false, 45, Fonts::title (12.0f));
    Sketch::button (g, btnDelPiece, "Delete",  theme.trough, theme.ink, false, 47, Fonts::title (12.0f));

    const auto& pieces = project.pieces;
    std::vector<int> order (pieces.size());
    for (size_t i = 0; i < pieces.size(); ++i) order[i] = (int) i;
    std::sort (order.begin(), order.end(), [&] (int a, int b)
    {
        if (pieces[(size_t) a].zOrder != pieces[(size_t) b].zOrder)
            return pieces[(size_t) a].zOrder > pieces[(size_t) b].zOrder;
        return a < b;
    });

    layerRowHits.clear();
    float ry = topBtnY + 32.0f;
    const float rowH = 38.0f;
    const float maxRowsBottom = leftPanelBounds.getBottom() - 166.0f;

    for (int idx : order)
    {
        if (ry + rowH > maxRowsBottom) break;
        const auto& p = pieces[(size_t) idx];
        const bool isSel = (idx == selectedPieceIdx);

        LayerRowHit hit;
        hit.pieceIndex = idx;
        hit.rowBounds  = { leftPanelBounds.getX() + 9.0f, ry, leftPanelBounds.getWidth() - 18.0f, rowH - 3.0f };

        juce::Colour rowBg  = isSel ? theme.accent.withAlpha (0.24f) : theme.trough;
        juce::Colour rowBrd = isSel ? theme.accent : theme.inkSoft.withAlpha (0.65f);
        if (isDraggingLayerRow && hit.pieceIndex == dragLayerPieceIndex)
        {
            rowBg = theme.accent.withAlpha (0.42f);
            rowBrd = hitGlowColour;
        }
        Sketch::box (g, hit.rowBounds, rowBg, rowBrd, 5.0f, isSel ? 2.1f : 1.4f, 1, 50 + idx);

        hit.visBounds = { hit.rowBounds.getX() + 5.0f, hit.rowBounds.getY() + 7.0f, 20.0f, 20.0f };
        g.setColour (p.visible ? goodCol.withAlpha (0.32f) : theme.panel);
        g.fillRoundedRectangle (hit.visBounds, 4.0f);
        g.setColour (theme.ink);
        g.drawRoundedRectangle (hit.visBounds, 4.0f, 1.2f);
        g.setFont (Fonts::title (11.0f));
        g.drawText (p.visible ? "V" : "-", hit.visBounds, juce::Justification::centred, false);

        hit.hitBounds  = { hit.rowBounds.getRight() - 32.0f, hit.rowBounds.getY() + 6.0f, 27.0f, 22.0f };
        hit.downBounds = { hit.hitBounds.getX() - 21.0f,     hit.rowBounds.getY() + 6.0f, 18.0f, 22.0f };
        hit.upBounds   = { hit.downBounds.getX() - 20.0f,    hit.rowBounds.getY() + 6.0f, 18.0f, 22.0f };

        Sketch::button (g, hit.upBounds,   "^",   theme.panel,  theme.ink, false, 60 + idx, Fonts::title (11.0f));
        Sketch::button (g, hit.downBounds, "v",   theme.panel,  theme.ink, false, 70 + idx, Fonts::title (11.0f));
        Sketch::button (g, hit.hitBounds,  "Hit", theme.accent, theme.ink, false, 80 + idx, Fonts::title (10.5f));

        auto textArea = juce::Rectangle<float> (hit.visBounds.getRight() + 6.0f, hit.rowBounds.getY() + 2.0f,
                                                hit.upBounds.getX() - (hit.visBounds.getRight() + 8.0f), 18.0f);
        g.setColour (theme.ink);
        g.setFont (Fonts::title (13.0f));
        g.drawText (p.name, textArea, juce::Justification::centredLeft, true);

        g.setColour (theme.inkSoft);
        g.setFont (Fonts::body (11.5f));
        juce::String sub = "Z:" + juce::String (p.zOrder) + " | " + juce::String ((int) p.zones.size()) + "z"
                         + (p.customPngBytes.empty() ? "" : " | PNG");
        g.drawText (sub, textArea.withY (hit.rowBounds.getY() + 17.0f), juce::Justification::centredLeft, true);

        layerRowHits.push_back (hit);
        ry += rowH;
    }

    micBoxBounds = { leftPanelBounds.getX() + 9.0f, leftPanelBounds.getBottom() - 156.0f,
                     leftPanelBounds.getWidth() - 18.0f, 147.0f };
    Sketch::box (g, micBoxBounds, theme.trough, theme.ink, 5.0f, 1.8f, 2, 91);

    g.setColour (theme.ink);
    g.setFont (Fonts::title (12.5f));
    g.drawText ("KIT MIC CHANNELS (" + juce::String ((int) project.mics.size()) + ")",
                micBoxBounds.withHeight (24.0f).reduced (8.0f, 2.0f), juce::Justification::centredLeft, false);

    btnAddMic = { micBoxBounds.getRight() - 56.0f, micBoxBounds.getY() + 4.0f, 50.0f, 19.0f };
    Sketch::button (g, btnAddMic, "+ Mic", theme.accent, theme.ink, false, 93, Fonts::title (10.5f));

    micPillHits.clear();
    const auto& chans = project.mics;
    const float pillW = (micBoxBounds.getWidth() - 22.0f) * 0.5f;
    const float pillH = 22.0f;
    for (size_t i = 0; i < chans.size() && i < 8; ++i)
    {
        const int col = (int) (i % 2);
        const int row = (int) (i / 2);
        juce::Rectangle<float> r { micBoxBounds.getX() + 8.0f + col * (pillW + 6.0f),
                                   micBoxBounds.getY() + 28.0f + row * (pillH + 5.0f),
                                   pillW, pillH };
        Sketch::box (g, r, theme.panel, theme.inkSoft, 4.0f, 1.2f, 1, 95 + (int) i);
        g.setColour (theme.ink);
        g.setFont (Fonts::body (12.0f));
        g.drawText (juce::String ((int) i + 1) + ": " + juce::String (chans[i].name),
                    r.reduced (6.0f, 1.0f), juce::Justification::centredLeft, true);
        micPillHits.push_back ({ (int) i, r });
    }
}

void GlorpKitEditorComponent::paintCanvasPanel (juce::Graphics& g)
{
    Sketch::box (g, canvasPanelBounds, theme.panel, theme.ink, 6.0f, 2.2f, 2, 101);

    g.setColour (theme.ink);
    g.setFont (Fonts::title (14.0f));
    g.drawText ("2D KIT STAGE CANVAS (DRAG TO POSITION, WHEEL/CORNER TO SCALE, CLICK TO AUDITION)",
                canvasPanelBounds.withHeight (32.0f).withLeft (canvasPanelBounds.getX() + 14.0f),
                juce::Justification::centredLeft, false);

    float tx = canvasPanelBounds.getRight() - 12.0f;
    const float ty = canvasPanelBounds.getY() + 6.0f, th = 24.0f;
    auto makeToolBtn = [&] (juce::Rectangle<float>& r, float w, const juce::String& txt, bool active, int seed)
    {
        r = { tx - w, ty, w, th };
        tx = r.getX() - 6.0f;
        Sketch::button (g, r, txt, active ? theme.accent : theme.trough, theme.ink, active, seed, Fonts::title (11.5f));
    };
    makeToolBtn (btnResetLayout, 64.0f, "Reset",   false,          103);
    makeToolBtn (btnSendBack,    58.0f, "Back",    false,          105);
    makeToolBtn (btnBringFront,  62.0f, "Front",   false,          107);
    makeToolBtn (btnPreviewKit,  76.0f, "Preview", previewKitMode, 109);
    makeToolBtn (btnSnapGrid,    62.0f, "Grid",    snapToGrid,     111);

    juce::Colour stageBg = theme.isDark ? juce::Colour (0xff12141a) : juce::Colour (0xffefe7d6);
    Sketch::box (g, canvasViewportBounds, stageBg, theme.ink, 6.0f, 1.9f, 2, 113);

    {
        juce::Graphics::ScopedSaveState clipGuard (g);
        g.reduceClipRegion (canvasViewportBounds.reduced (2.0f).toNearestInt());

        if (snapToGrid)
        {
            g.setColour (theme.inkSoft.withAlpha (0.18f));
            for (int gx = 1; gx < 10; ++gx)
            {
                const float x = canvasViewportBounds.getX() + canvasViewportBounds.getWidth() * ((float) gx / 10.0f);
                g.drawVerticalLine ((int) x, canvasViewportBounds.getY(), canvasViewportBounds.getBottom());
            }
            for (int gy = 1; gy < 6; ++gy)
            {
                const float y = canvasViewportBounds.getY() + canvasViewportBounds.getHeight() * ((float) gy / 6.0f);
                g.drawHorizontalLine ((int) y, canvasViewportBounds.getX(), canvasViewportBounds.getRight());
            }
        }

        const auto& pieces = project.pieces;
        std::vector<int> zSorted (pieces.size());
        for (size_t i = 0; i < pieces.size(); ++i) zSorted[i] = (int) i;
        std::sort (zSorted.begin(), zSorted.end(), [&] (int a, int b)
        {
            if (pieces[(size_t) a].zOrder != pieces[(size_t) b].zOrder)
                return pieces[(size_t) a].zOrder < pieces[(size_t) b].zOrder;
            return a < b;
        });

        for (int idx : zSorted)
        {
            const auto& p = pieces[(size_t) idx];
            if (! p.visible || idx >= (int) pieceSprites.size()) continue;
            const auto& sp = pieceSprites[(size_t) idx];
            const bool isSel = (idx == selectedPieceIdx);
            const float baseAlpha = previewKitMode ? 1.0f : (isSel ? 1.0f : 0.30f);

            const float glowAlpha = std::max (sp.flash, isSel ? 0.45f : 0.0f);
            if (glowAlpha > 0.01f && sp.glowImage.isValid())
            {
                g.setOpacity (juce::jlimit (0.0f, 1.0f, glowAlpha * (isSel || previewKitMode ? 1.0f : 0.5f)));
                g.drawImage (sp.glowImage, sp.canvasBounds.expanded (10.0f));
                g.setOpacity (1.0f);
            }

            if (sp.rawImage.isValid())
            {
                g.setOpacity (baseAlpha);
                const float pop = 1.0f + sp.flash * 0.05f;
                auto drawRect = sp.canvasBounds.withSizeKeepingCentre (sp.canvasBounds.getWidth() * pop,
                                                                       sp.canvasBounds.getHeight() * pop);
                g.drawImage (sp.rawImage, drawRect);
                g.setOpacity (1.0f);
            }

            if (isSel)
            {
                g.setColour (theme.accent);
                const float dashLengths[2] = { 6.0f, 4.0f };
                g.drawDashedLine (juce::Line<float> (sp.canvasBounds.getTopLeft(), sp.canvasBounds.getTopRight()), dashLengths, 2, 2.0f);
                g.drawDashedLine (juce::Line<float> (sp.canvasBounds.getTopRight(), sp.canvasBounds.getBottomRight()), dashLengths, 2, 2.0f);
                g.drawDashedLine (juce::Line<float> (sp.canvasBounds.getBottomRight(), sp.canvasBounds.getBottomLeft()), dashLengths, 2, 2.0f);
                g.drawDashedLine (juce::Line<float> (sp.canvasBounds.getBottomLeft(), sp.canvasBounds.getTopLeft()), dashLengths, 2, 2.0f);

                juce::Rectangle<float> handle { sp.canvasBounds.getRight() - 7.0f, sp.canvasBounds.getBottom() - 7.0f, 14.0f, 14.0f };
                g.setColour (theme.accent);
                g.fillRect (handle);
                g.setColour (theme.ink);
                g.drawRect (handle, 1.5f);

                juce::Rectangle<float> tag { sp.canvasBounds.getCentreX() - 76.0f,
                                             std::max (canvasViewportBounds.getY() + 4.0f, sp.canvasBounds.getY() - 24.0f),
                                             152.0f, 20.0f };
                Sketch::box (g, tag, theme.panel.withAlpha (0.94f), theme.accent, 4.0f, 1.5f, 1, 130 + idx);
                g.setColour (theme.ink);
                g.setFont (Fonts::title (11.5f));
                char cbuf[64];
                std::snprintf (cbuf, sizeof (cbuf), "%s (%.2f, %.2f) Z:%d",
                               p.name.c_str(), p.normX, p.normY, p.zOrder);
                g.drawText (cbuf, tag.reduced (4.0f, 1.0f), juce::Justification::centred, true);
            }
        }

        if (statusBannerAlpha > 0.02f && statusBannerText.isNotEmpty())
        {
            juce::Rectangle<float> toast { canvasViewportBounds.getX() + 14.0f,
                                           canvasViewportBounds.getBottom() - 30.0f,
                                           canvasViewportBounds.getWidth() - 28.0f, 24.0f };
            g.setColour (theme.panel.withAlpha (0.92f));
            g.fillRoundedRectangle (toast, 5.0f);
            g.setColour (theme.accent);
            g.drawRoundedRectangle (toast, 5.0f, 1.5f);
            g.setColour (theme.ink);
            g.setFont (Fonts::body (13.0f));
            g.drawText (statusBannerText, toast.reduced (10.0f, 2.0f), juce::Justification::centredLeft, true);
        }
    }
}

void GlorpKitEditorComponent::paintMatrixPanel (juce::Graphics& g)
{
    const juce::Colour goodCol (0xff2ecc71);
    Sketch::box (g, matrixPanelBounds, theme.panel, theme.ink, 6.0f, 2.2f, 2, 151);

    const auto& pieces = project.pieces;
    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) pieces.size()) return;
    const auto& piece = pieces[(size_t) selectedPieceIdx];

    g.setColour (theme.ink);
    g.setFont (Fonts::title (13.5f));
    g.drawText ("ARTICULATION ZONES & VELOCITY / ROUND-ROBIN MATRIX",
                matrixPanelBounds.withHeight (26.0f).withLeft (matrixPanelBounds.getX() + 12.0f),
                juce::Justification::centredLeft, false);

    btnImportWavs = { matrixPanelBounds.getRight() - 122.0f, matrixPanelBounds.getY() + 6.0f, 110.0f, 24.0f };
    btnAutoPower  = { btnImportWavs.getX() - 104.0f,         matrixPanelBounds.getY() + 6.0f, 98.0f,  24.0f };
    Sketch::button (g, btnAutoPower,  "Auto Power",     theme.trough, theme.ink, false, 153, Fonts::title (11.5f));
    Sketch::button (g, btnImportWavs, "+ Import WAVs",  theme.accent, theme.ink, false, 155, Fonts::title (11.5f));

    zoneTabHits.clear();
    float zx = matrixPanelBounds.getX() + 12.0f;
    const float zy = matrixPanelBounds.getY() + 31.0f;
    for (size_t z = 0; z < piece.zones.size(); ++z)
    {
        const auto& zn = piece.zones[z];
        const bool isZSel = ((int) z == selectedZoneIdx);
        juce::String label = juce::String (zn.name) + " (N:" + juce::String (zn.note) + ")";
        const float tw = std::max (112.0f, (float) label.length() * 7.6f + 22.0f);
        juce::Rectangle<float> tabR { zx, zy, tw, 24.0f };
        Sketch::button (g, tabR, label, isZSel ? theme.accent : theme.trough, theme.ink, isZSel, 160 + (int) z, Fonts::title (11.5f));
        zoneTabHits.push_back ({ (int) z, tabR });
        zx += tw + 6.0f;
    }
    juce::Rectangle<float> addZoneR { zx, zy, 92.0f, 24.0f };
    const juce::Colour pastelGreen { 0xffa7f3d0 };
    Sketch::button (g, addZoneR, "+ Add Zone", pastelGreen, juce::Colour (0xff064e3b), false, 175, Fonts::title (11.5f));
    zoneTabHits.push_back ({ -1, addZoneR });
    zx += 98.0f;

    btnDeleteZone = { zx, zy, 90.0f, 24.0f };
    const bool canDeleteZone = (piece.zones.size() > 1);
    const juce::Colour pastelCoral { 0xfffecaca };
    Sketch::button (g, btnDeleteZone, "Delete Zone",
                    canDeleteZone ? pastelCoral : theme.trough.withAlpha (0.4f),
                    canDeleteZone ? juce::Colour (0xff7f1d1d) : theme.inkSoft,
                    false, 177, Fonts::title (11.5f));

    matrixCellHits.clear();
    if (selectedZoneIdx < 0 || selectedZoneIdx >= (int) piece.zones.size()) return;
    const auto& zone = piece.zones[(size_t) selectedZoneIdx];

    auto gridArea = matrixPanelBounds.reduced (12.0f, 8.0f).withTop (zy + 30.0f);
    const int numVelRows = 5;
    const int numRrCols  = 4;
    const float labelColW = 86.0f;
    const float colHeaderH = 20.0f;
    const float cellW = (gridArea.getWidth() - labelColW) / (float) numRrCols;
    const float cellH = std::max (20.0f, (gridArea.getHeight() - colHeaderH) / (float) numVelRows);

    const char* velLabels[5] = { "v5 (FF 1.0)", "v4 (F 0.8)", "v3 (MF 0.6)", "v2 (MP 0.4)", "v1 (PP 0.2)" };
    for (int c = 0; c < numRrCols; ++c)
    {
        juce::Rectangle<float> hdr { gridArea.getX() + labelColW + c * cellW, gridArea.getY(), cellW - 4.0f, colHeaderH };
        g.setColour (theme.inkSoft);
        g.setFont (Fonts::title (11.5f));
        g.drawText ("Round Robin RR" + juce::String (c + 1), hdr, juce::Justification::centred, false);
    }

    for (int r = 0; r < numVelRows; ++r)
    {
        const int velLayer = (numVelRows - 1) - r;
        const float cy = gridArea.getY() + colHeaderH + r * cellH;
        juce::Rectangle<float> rowLbl { gridArea.getX(), cy, labelColW - 6.0f, cellH - 3.0f };
        g.setColour (theme.ink);
        g.setFont (Fonts::title (11.5f));
        g.drawText (velLabels[r], rowLbl, juce::Justification::centredLeft, false);

        for (int c = 0; c < numRrCols; ++c)
        {
            juce::Rectangle<float> cellR { gridArea.getX() + labelColW + c * cellW, cy, cellW - 5.0f, cellH - 3.0f };
            int foundHit = -1;
            for (size_t hi = 0; hi < zone.hits.size(); ++hi)
            {
                const int hVel = juce::jlimit (0, numVelRows - 1, zone.hits[hi].velocityLayerIndex);
                const int hRr  = juce::jlimit (0, numRrCols - 1,  zone.hits[hi].roundRobinIndex);
                if (hVel == velLayer && hRr == c)
                {
                    foundHit = (int) hi;
                    break;
                }
            }
            // Fallback sequential placement when hits don't have explicit grid coords yet
            if (foundHit < 0)
            {
                const int flatIdx = r * numRrCols + c;
                if (flatIdx < (int) zone.hits.size() && zone.hits.size() <= 4)
                    foundHit = flatIdx;
            }

            const bool isCellSel = (foundHit >= 0 && foundHit == selectedHitIdx);
            juce::Colour cellBg  = foundHit >= 0
                                       ? (isCellSel ? theme.accent.withAlpha (0.32f) : goodCol.withAlpha (0.18f))
                                       : theme.trough.withAlpha (0.65f);
            juce::Colour cellBrd = isCellSel ? theme.accent : theme.inkSoft.withAlpha (0.65f);
            Sketch::box (g, cellR, cellBg, cellBrd, 4.0f, isCellSel ? 2.0f : 1.2f, 1, 200 + r * 10 + c);

            if (foundHit >= 0)
            {
                const auto& h = zone.hits[(size_t) foundHit];
                g.setColour (theme.ink);
                g.setFont (Fonts::body (11.5f));
                char pbuf[64];
                std::snprintf (pbuf, sizeof (pbuf), "%s  [p:%.2f]", h.name.c_str(), h.power);
                g.drawText (pbuf, cellR.reduced (6.0f, 1.0f), juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour (theme.inkSoft.withAlpha (0.55f));
                g.setFont (Fonts::body (11.0f));
                g.drawText ("(drop WAV)", cellR, juce::Justification::centred, false);
            }

            matrixCellHits.push_back ({ foundHit, velLayer, c, cellR });
        }
    }
}

void GlorpKitEditorComponent::paintRightInspector (juce::Graphics& g)
{
    const juce::Colour goodCol (0xff2ecc71);
    Sketch::box (g, rightPanelBounds, theme.panel, theme.ink, 6.0f, 2.2f, 2, 261);

    g.setColour (theme.ink);
    g.setFont (Fonts::title (14.0f));
    g.drawText ("PIECE, ZONE & SAMPLE INSPECTOR",
                rightPanelBounds.withHeight (32.0f).reduced (12.0f, 4.0f),
                juce::Justification::centredLeft, false);

    const auto& pieces = project.pieces;
    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) pieces.size()) return;
    const auto& piece = pieces[(size_t) selectedPieceIdx];

    // 1. PIECE GRAPHICS & STAGE LAYOUT CARD
    Sketch::box (g, cardPieceBounds, theme.trough, theme.ink, 5.0f, 1.8f, 2, 263);
    g.setColour (theme.accent);
    g.setFont (Fonts::title (12.5f));
    g.drawText ("1. PIECE GRAPHICS & LAYOUT",
                cardPieceBounds.withHeight (24.0f).reduced (10.0f, 2.0f), juce::Justification::centredLeft, false);

    auto drawFieldRow = [&] (float y, const juce::String& lbl)
    {
        g.setColour (theme.inkSoft);
        g.setFont (Fonts::body (12.5f));
        g.drawText (lbl, juce::Rectangle<float> (cardPieceBounds.getX() + 10.0f, y, 80.0f, 22.0f),
                    juce::Justification::centredLeft, false);
        Sketch::box (g, { cardPieceBounds.getX() + 88.0f, y, cardPieceBounds.getWidth() - 98.0f, 22.0f },
                     theme.panel, theme.inkSoft, 4.0f, 1.2f, 1, (int) y);
    };
    drawFieldRow (cardPieceBounds.getY() + 30.0f, "Display Name:");
    drawFieldRow (cardPieceBounds.getY() + 56.0f, "Piece ID:");

    const float sprY = cardPieceBounds.getY() + 84.0f;
    btnBuiltinAsset  = { cardPieceBounds.getX() + 10.0f, sprY, (cardPieceBounds.getWidth() - 26.0f) * 0.48f, 25.0f };
    btnLoadCustomPng = { btnBuiltinAsset.getRight() + 6.0f, sprY, (cardPieceBounds.getWidth() - 26.0f) * 0.52f, 25.0f };
    Sketch::button (g, btnBuiltinAsset,  "Sprite: " + juce::String (piece.graphicId), theme.panel, theme.ink, false, 271, Fonts::title (11.0f));
    Sketch::button (g, btnLoadCustomPng, piece.customPngBytes.empty() ? "Load Custom PNG..." : "Custom PNG Active",
                    piece.customPngBytes.empty() ? theme.accent : goodCol.withAlpha (0.4f), theme.ink, false, 273, Fonts::title (11.0f));

    const float normY = cardPieceBounds.getY() + 116.0f;
    const float pw = (cardPieceBounds.getWidth() - 32.0f) * 0.25f;
    pillNormX  = { cardPieceBounds.getX() + 10.0f,          normY, pw, 36.0f };
    pillNormY  = { pillNormX.getRight() + 4.0f,             normY, pw, 36.0f };
    pillNormW  = { pillNormY.getRight() + 4.0f,             normY, pw, 36.0f };
    pillZOrder = { pillNormW.getRight() + 4.0f,             normY, pw, 36.0f };

    auto drawMetricPill = [&] (const juce::Rectangle<float>& r, const char* title, const juce::String& val, int seed)
    {
        Sketch::box (g, r, theme.panel, theme.inkSoft, 4.0f, 1.3f, 1, seed);
        g.setColour (theme.inkSoft);
        g.setFont (Fonts::body (10.5f));
        g.drawText (title, r.withHeight (15.0f), juce::Justification::centred, false);
        g.setColour (theme.ink);
        g.setFont (Fonts::title (12.5f));
        g.drawText (val, r.withTop (r.getY() + 15.0f), juce::Justification::centred, false);
    };
    drawMetricPill (pillNormX,  "Pos X",  juce::String (piece.normX, 2),       275);
    drawMetricPill (pillNormY,  "Pos Y",  juce::String (piece.normY, 2),       277);
    drawMetricPill (pillNormW,  "Width",  juce::String ((int) piece.normW) + "px", 279);
    drawMetricPill (pillZOrder, "Z-Ord",  juce::String (piece.zOrder),         281);

    const float busY = cardPieceBounds.getY() + 158.0f;
    btnOutputBus  = { cardPieceBounds.getX() + 10.0f, busY, cardPieceBounds.getWidth() - 20.0f, 25.0f };
    btnChokeGroup = { cardPieceBounds.getX() + 10.0f, busY + 30.0f, cardPieceBounds.getWidth() - 20.0f, 25.0f };
    Sketch::button (g, btnOutputBus,  "Mixer Bus #" + juce::String (piece.outputBus + 1) + " (" + juce::String (piece.name) + ")", theme.panel, theme.ink, false, 283, Fonts::title (11.5f));
    juce::String chokeLbl = (piece.chokeGroup.empty() || piece.chokeGroup == "none")
                                ? "Choke Group: None (Polyphonic)"
                                : ("Choke Group: " + juce::String (piece.chokeGroup));
    Sketch::button (g, btnChokeGroup, chokeLbl, theme.panel, theme.ink, false, 285, Fonts::title (11.5f));

    // 2. ARTICULATION ZONE & MIDI MAPPING CARD
    Sketch::box (g, cardZoneBounds, theme.trough, theme.ink, 5.0f, 1.8f, 2, 291);
    g.setColour (theme.accent);
    g.setFont (Fonts::title (12.5f));
    g.drawText ("2. ZONE MIDI & ARTICULATION RULES",
                cardZoneBounds.withHeight (24.0f).reduced (10.0f, 2.0f), juce::Justification::centredLeft, false);

    if (selectedZoneIdx >= 0 && selectedZoneIdx < (int) piece.zones.size())
    {
        const auto& zn = piece.zones[(size_t) selectedZoneIdx];
        g.setColour (theme.inkSoft);
        g.setFont (Fonts::body (12.5f));
        g.drawText ("Zone Name:", juce::Rectangle<float> (cardZoneBounds.getX() + 10.0f, cardZoneBounds.getY() + 26.0f, 80.0f, 22.0f),
                    juce::Justification::centredLeft, false);
        Sketch::box (g, { cardZoneBounds.getX() + 88.0f, cardZoneBounds.getY() + 26.0f, cardZoneBounds.getWidth() - 98.0f, 22.0f },
                     theme.panel, theme.inkSoft, 4.0f, 1.2f, 1, 292);

        const float zy1 = cardZoneBounds.getY() + 52.0f;
        btnMidiNote  = { cardZoneBounds.getX() + 10.0f, zy1, cardZoneBounds.getWidth() - 96.0f, 26.0f };
        btnMidiLearn = { btnMidiNote.getRight() + 6.0f, zy1, 70.0f, 26.0f };
        Sketch::button (g, btnMidiNote,  "Note: " + juce::String (midiNoteName (zn.note)), theme.panel, theme.ink, false, 293, Fonts::title (11.5f));
        Sketch::button (g, btnMidiLearn, midiLearnArmed ? "Listening" : "Learn",
                        midiLearnArmed ? theme.warning.withAlpha (0.55f) : theme.accent, theme.ink, midiLearnArmed, 295, Fonts::title (11.5f));

        btnAltNote = { cardZoneBounds.getX() + 10.0f, zy1 + 32.0f, cardZoneBounds.getWidth() - 20.0f, 25.0f };
        Sketch::button (g, btnAltNote, "Alt Note: " + juce::String (zn.altNote >= 0 ? midiNoteName (zn.altNote) : "None"),
                        theme.panel, theme.ink, false, 297, Fonts::title (11.5f));

        btnChokeTarget = { cardZoneBounds.getX() + 10.0f, zy1 + 63.0f, cardZoneBounds.getWidth() - 20.0f, 25.0f };
        juce::String ctText = zn.chokes.empty() ? "Chokes Zone: None"
                                                : ("Chokes Zone: " + juce::String (zn.chokes.front().zone) + " (" + juce::String ((int) zn.chokes.front().chokeTimeMs) + "ms)");
        Sketch::button (g, btnChokeTarget, ctText, theme.panel, theme.ink, false, 299, Fonts::title (11.5f));

        g.setColour (theme.inkSoft);
        g.setFont (Fonts::body (12.0f));
        g.drawText ("Samples in Zone: " + juce::String ((int) zn.hits.size()) + " hits mapped across velocity layers",
                    cardZoneBounds.withY (zy1 + 94.0f).withHeight (20.0f).reduced (10.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }

    // 3. SAMPLE WAVEFORM & MULTI-MIC ASSIGNMENT CARD
    Sketch::box (g, cardSampleBounds, theme.trough, theme.ink, 5.0f, 1.8f, 2, 301);
    g.setColour (theme.accent);
    g.setFont (Fonts::title (12.5f));
    g.drawText ("3. SAMPLE CELL & MULTI-MIC ROUTING",
                cardSampleBounds.withHeight (24.0f).reduced (10.0f, 2.0f), juce::Justification::centredLeft, false);

    btnPlaySample = { cardSampleBounds.getRight() - 68.0f, cardSampleBounds.getY() + 4.0f, 58.0f, 20.0f };
    Sketch::button (g, btnPlaySample, "Audition", theme.accent, theme.ink, false, 303, Fonts::title (10.5f));

    waveformPreviewBounds = { cardSampleBounds.getX() + 10.0f, cardSampleBounds.getY() + 28.0f,
                              cardSampleBounds.getWidth() - 20.0f, 54.0f };
    Sketch::box (g, waveformPreviewBounds, theme.panel, theme.inkSoft, 4.0f, 1.3f, 1, 305);

    if (! selectedHitWaveform.empty())
    {
        const float bw = (waveformPreviewBounds.getWidth() - 12.0f) / (float) selectedHitWaveform.size();
        const float midY = waveformPreviewBounds.getCentreY();
        g.setColour (hitGlowColour.withAlpha (0.85f));
        for (size_t i = 0; i < selectedHitWaveform.size(); ++i)
        {
            const float h = selectedHitWaveform[i] * (waveformPreviewBounds.getHeight() - 10.0f);
            g.fillRect (waveformPreviewBounds.getX() + 6.0f + (float) i * bw, midY - h * 0.5f, std::max (1.5f, bw - 1.0f), h);
        }
    }

    micAssignHits.clear();
    if (selectedZoneIdx >= 0 && selectedZoneIdx < (int) piece.zones.size())
    {
        const auto& zn = piece.zones[(size_t) selectedZoneIdx];
        if (selectedHitIdx >= 0 && selectedHitIdx < (int) zn.hits.size())
        {
            const auto& hit = zn.hits[(size_t) selectedHitIdx];
            float my = waveformPreviewBounds.getBottom() + 6.0f;
            const float rowH = 22.0f;
            for (const auto& mf : hit.micSamples)
            {
                if (my + rowH > cardSampleBounds.getBottom() - 4.0f) break;
                juce::Rectangle<float> mr { cardSampleBounds.getX() + 10.0f, my, cardSampleBounds.getWidth() - 20.0f, rowH - 2.0f };
                Sketch::box (g, mr, theme.panel, theme.inkSoft.withAlpha (0.6f), 3.0f, 1.1f, 1, 310 + (int) my);
                g.setColour (theme.ink);
                g.setFont (Fonts::body (11.5f));
                g.drawText ("[" + juce::String (mf.mic) + "]  " + juce::String (mf.fileName),
                            mr.reduced (6.0f, 1.0f), juce::Justification::centredLeft, true);
                micAssignHits.push_back ({ mf.mic, mr });
                my += rowH;
            }
        }
    }
}

//==============================================================================
void GlorpKitEditorComponent::paintGuidedTutorialOverlay (juce::Graphics& g)
{
    struct StepSpec
    {
        const char* title;
        const char* body;
        juce::Rectangle<float> spotlight;
    };

    const StepSpec steps[5] = {
        {
            "Step 1 of 5: Welcome to GlorpKit Studio!",
            "Design custom v2.0 .glorpkit drum instruments with custom PNG sprites, Z-layer ordering, multi-zone articulations, and zero-copy uncompressed audio. Use the top header to create a New Kit, open an existing .glorpkit, import DrumGizmo XMLs, or pick a Kit-Wide Hit Glow Color!",
            headerBounds.reduced (4.0f, 2.0f)
        },
        {
            "Step 2 of 5: Z-Layer Stack & Kit Mic Buses",
            "The Left Panel controls visual depth order (front-to-back) and multi-mic studio buses (Close, OH L/R, Room, PZM). Click '^' or 'v' to move cymbals in front of toms, toggle canvas visibility, or click 'Hit' to audition any piece.",
            leftPanelBounds
        },
        {
            "Step 3 of 5: Interactive 2D Stage Canvas",
            "Drag any drum or cymbal directly on the stage to position it! Drag the bottom-right corner handle (or scroll the mouse wheel) to scale its size. Clicking a piece triggers live audio audition and flashes your custom Hit Glow Color.",
            canvasPanelBounds
        },
        {
            "Step 4 of 5: Articulation Zones & Velocity/RR Matrix",
            "Each kit piece can have multiple articulation zones (e.g. Snare Center, Rimshot, Sidestick; Ride Bow, Bell, Edge). Drag & drop WAV files into the 5x4 Velocity x Round-Robin grid, or click 'Auto Power' to sort layers by RMS/Peak loudness!",
            matrixPanelBounds
        },
        {
            "Step 5 of 5: Piece Graphics, Custom PNGs & Export",
            "Use the Right Inspector to load custom transparent PNG artwork (embedded inside the .glorpkit under assets/), assign MIDI notes with 1-click MIDI Learn, inspect sample waveforms, and click 'Save .glorpkit' when ready to play in GlorpDrums!",
            rightPanelBounds
        }
    };

    const auto& cur = steps[juce::jlimit (0, 4, tutorialStep)];

    const auto full = getLocalBounds().toFloat();
    const auto spot = cur.spotlight.expanded (3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillRect (juce::Rectangle<float> (full.getX(), full.getY(), full.getWidth(), std::max (0.0f, spot.getY() - full.getY())));
    g.fillRect (juce::Rectangle<float> (full.getX(), spot.getBottom(), full.getWidth(), std::max (0.0f, full.getBottom() - spot.getBottom())));
    g.fillRect (juce::Rectangle<float> (full.getX(), spot.getY(), std::max (0.0f, spot.getX() - full.getX()), spot.getHeight()));
    g.fillRect (juce::Rectangle<float> (spot.getRight(), spot.getY(), std::max (0.0f, full.getRight() - spot.getRight()), spot.getHeight()));

    g.setColour (theme.accent);
    g.drawRoundedRectangle (spot, 8.0f, 3.5f);
    g.setColour (hitGlowColour);
    g.drawRoundedRectangle (spot.expanded (2.5f), 9.0f, 2.0f);

    const float cardW = 470.0f;
    const float cardH = 198.0f;
    float cx = full.getCentreX() - cardW * 0.5f;
    float cy = full.getCentreY() - cardH * 0.5f;
    if (tutorialStep == 0)      { cy = headerBounds.getBottom() + 18.0f; }
    else if (tutorialStep == 1) { cx = leftPanelBounds.getRight() + 24.0f; cy = leftPanelBounds.getY() + 60.0f; }
    else if (tutorialStep == 2) { cy = canvasPanelBounds.getBottom() + 12.0f; }
    else if (tutorialStep == 3) { cy = matrixPanelBounds.getY() - cardH - 14.0f; }
    else if (tutorialStep == 4) { cx = rightPanelBounds.getX() - cardW - 24.0f; cy = rightPanelBounds.getY() + 80.0f; }

    tutorialCardBounds = { juce::jlimit (16.0f, full.getWidth() - cardW - 16.0f, cx),
                           juce::jlimit (16.0f, full.getHeight() - cardH - 16.0f, cy),
                           cardW, cardH };

    Sketch::box (g, tutorialCardBounds, theme.panel, theme.ink, 9.0f, 2.8f, 3, 501);

    juce::Rectangle<float> badge { tutorialCardBounds.getX() + 16.0f, tutorialCardBounds.getY() + 14.0f, 128.0f, 20.0f };
    Sketch::box (g, badge, theme.accent, theme.ink, 4.0f, 1.4f, 1, 503);
    g.setColour (theme.ink);
    g.setFont (Fonts::title (11.5f));
    g.drawText ("INTERACTIVE TOUR", badge, juce::Justification::centred, false);

    g.setColour (theme.ink);
    g.setFont (Fonts::title (16.5f));
    g.drawText (cur.title,
                juce::Rectangle<float> (tutorialCardBounds.getX() + 16.0f, tutorialCardBounds.getY() + 38.0f,
                                        tutorialCardBounds.getWidth() - 32.0f, 24.0f),
                juce::Justification::centredLeft, false);

    g.setColour (theme.ink);
    g.setFont (Fonts::body (14.0f));
    g.drawFittedText (cur.body,
                      juce::Rectangle<float> (tutorialCardBounds.getX() + 16.0f, tutorialCardBounds.getY() + 66.0f,
                                              tutorialCardBounds.getWidth() - 32.0f, 76.0f).toNearestInt(),
                      juce::Justification::topLeft, 5);

    for (int i = 0; i < 5; ++i)
    {
        juce::Rectangle<float> dot { tutorialCardBounds.getX() + 18.0f + i * 18.0f,
                                     tutorialCardBounds.getBottom() - 32.0f, 10.0f, 10.0f };
        g.setColour (i == tutorialStep ? theme.accent : theme.inkSoft.withAlpha (0.4f));
        g.fillEllipse (dot);
        g.setColour (theme.ink);
        g.drawEllipse (dot, 1.2f);
    }

    const float by = tutorialCardBounds.getBottom() - 40.0f;
    tutorialSkipBtnBounds = { tutorialCardBounds.getX() + 122.0f, by, 84.0f, 28.0f };
    tutorialPrevBtnBounds = { tutorialCardBounds.getRight() - 208.0f, by, 82.0f, 28.0f };
    tutorialNextBtnBounds = { tutorialCardBounds.getRight() - 118.0f, by, 102.0f, 28.0f };

    Sketch::button (g, tutorialSkipBtnBounds, "Skip Tour", theme.trough, theme.inkSoft, false, 505, Fonts::title (12.0f));
    if (tutorialStep > 0)
        Sketch::button (g, tutorialPrevBtnBounds, "<- Prev", theme.trough, theme.ink, false, 507, Fonts::title (12.5f));
    Sketch::button (g, tutorialNextBtnBounds, tutorialStep == 4 ? "Finish Tour!" : "Next ->",
                    theme.accent, theme.ink, false, 509, Fonts::title (12.5f));
}

//==============================================================================
int GlorpKitEditorComponent::canvasPieceAt (juce::Point<float> pt) const
{
    if (! canvasViewportBounds.contains (pt)) return -1;
    const auto& pieces = project.pieces;
    std::vector<int> zDesc (pieces.size());
    for (size_t i = 0; i < pieces.size(); ++i) zDesc[i] = (int) i;
    std::sort (zDesc.begin(), zDesc.end(), [&] (int a, int b)
    {
        if (pieces[(size_t) a].zOrder != pieces[(size_t) b].zOrder)
            return pieces[(size_t) a].zOrder > pieces[(size_t) b].zOrder;
        return a < b;
    });

    for (int idx : zDesc)
    {
        if (! pieces[(size_t) idx].visible || idx >= (int) pieceSprites.size()) continue;
        const auto& sp = pieceSprites[(size_t) idx];
        if (! sp.canvasBounds.contains (pt)) continue;

        if (sp.rawImage.isValid())
        {
            const float u = (pt.x - sp.canvasBounds.getX()) / std::max (1.0f, sp.canvasBounds.getWidth());
            const float v = (pt.y - sp.canvasBounds.getY()) / std::max (1.0f, sp.canvasBounds.getHeight());
            const int px = juce::jlimit (0, sp.rawImage.getWidth() - 1, (int) (u * (float) sp.rawImage.getWidth()));
            const int py = juce::jlimit (0, sp.rawImage.getHeight() - 1, (int) (v * (float) sp.rawImage.getHeight()));
            if (sp.rawImage.getPixelAt (px, py).getAlpha() > 20)
                return idx;
        }
        else
        {
            return idx;
        }
    }
    return -1;
}

void GlorpKitEditorComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto pt = e.position;

    if (tutorialActive)
    {
        if (tutorialNextBtnBounds.contains (pt)) { nextTutorialStep(); return; }
        if (tutorialStep > 0 && tutorialPrevBtnBounds.contains (pt)) { prevTutorialStep(); return; }
        if (tutorialSkipBtnBounds.contains (pt)) { setTutorialVisible (false, 0); return; }
        if (! tutorialCardBounds.contains (pt) && btnTour.contains (pt))
        {
            setTutorialVisible (false, 0);
            return;
        }
        return;
    }

    if (btnNewKit.contains (pt))
    {
        project.createNewDefaultKit();
        reloadProjectIntoEngine();
        selectPiece (4, false);
        showStatusToast ("Initialized new default 8-piece .glorpkit v2.0 project");
        return;
    }
    if (btnOpenKit.contains (pt))      { promptOpenKit(); return; }
    if (btnImportDg.contains (pt))     { promptImportDrumGizmo(); return; }
    if (btnSaveKit.contains (pt))      { promptSaveGlorpKit(); return; }
    if (btnAudioSettings.contains (pt)){ showAudioSettingsDialog(); return; }
    if (btnGlowColor.contains (pt))    { showHitGlowColorMenu(); return; }
    if (btnTour.contains (pt))         { setTutorialVisible (true, 0); return; }
    if (btnThemeToggle.contains (pt))  { setDarkTheme (! theme.isDark); return; }
    if (pillKitMeta.contains (pt))     { showKitMetadataDialog(); return; }

    if (btnAddPiece.contains (pt))
    {
        int newIdx = project.addPiece ("Aux Perc " + std::to_string (project.pieces.size() + 1), "snare");
        reloadProjectIntoEngine();
        selectPiece (newIdx, true);
        showStatusToast ("Added new kit piece");
        return;
    }
    if (btnDupPiece.contains (pt) && selectedPieceIdx >= 0)
    {
        int newIdx = project.duplicatePiece (selectedPieceIdx);
        reloadProjectIntoEngine();
        selectPiece (newIdx, true);
        showStatusToast ("Duplicated selected kit piece");
        return;
    }
    if (btnDelPiece.contains (pt) && selectedPieceIdx >= 0 && project.pieces.size() > 1)
    {
        project.removePiece (selectedPieceIdx);
        reloadProjectIntoEngine();
        selectPiece (std::max (0, selectedPieceIdx - 1), false);
        showStatusToast ("Removed kit piece");
        return;
    }
    for (const auto& row : layerRowHits)
    {
        if (row.visBounds.contains (pt))
        {
            auto& p = project.pieces[(size_t) row.pieceIndex];
            p.visible = ! p.visible;
            repaint();
            return;
        }
        if (row.upBounds.contains (pt))
        {
            project.movePieceZOrder (row.pieceIndex, +1);
            selectPiece (row.pieceIndex, false);
            return;
        }
        if (row.downBounds.contains (pt))
        {
            project.movePieceZOrder (row.pieceIndex, -1);
            selectPiece (row.pieceIndex, false);
            return;
        }
        if (row.hitBounds.contains (pt))
        {
            selectPiece (row.pieceIndex, true);
            return;
        }
        if (row.rowBounds.contains (pt))
        {
            if (e.getNumberOfClicks() >= 2)
            {
                promptRenamePiece (row.pieceIndex);
                return;
            }
            selectPiece (row.pieceIndex, false);
            isDraggingLayerRow = true;
            dragLayerPieceIndex = row.pieceIndex;
            dragLayerStartMouseY = pt.y;
            return;
        }
    }
    if (btnAddMic.contains (pt))
    {
        std::string newMic = "AuxMic" + std::to_string (project.mics.size() + 1);
        project.addChannel (newMic);
        showStatusToast ("Added kit mic channel: " + juce::String (newMic));
        repaint();
        return;
    }
    for (const auto& mp : micPillHits)
    {
        if (mp.bounds.contains (pt))
        {
            promptRenameMic (mp.micIndex);
            return;
        }
    }

    if (btnSnapGrid.contains (pt))   { snapToGrid = ! snapToGrid; repaint(); return; }
    if (btnPreviewKit.contains (pt)) { previewKitMode = ! previewKitMode; repaint(); return; }
    if (btnBringFront.contains (pt) && selectedPieceIdx >= 0)
    {
        project.bringPieceToFront (selectedPieceIdx);
        repaint();
        return;
    }
    if (btnSendBack.contains (pt) && selectedPieceIdx >= 0)
    {
        project.sendPieceToBack (selectedPieceIdx);
        repaint();
        return;
    }
    if (btnResetLayout.contains (pt))
    {
        GlorpKitProject defProj;
        defProj.createNewDefaultKit();
        auto& pieces = project.pieces;
        for (size_t i = 0; i < pieces.size() && i < defProj.pieces.size(); ++i)
        {
            pieces[i].normX  = defProj.pieces[i].normX;
            pieces[i].normY  = defProj.pieces[i].normY;
            pieces[i].normW  = defProj.pieces[i].normW;
            pieces[i].zOrder = defProj.pieces[i].zOrder;
        }
        computeCanvasPieceBounds();
        showStatusToast ("Reset stage coordinates to default layout");
        return;
    }

    if (canvasViewportBounds.contains (pt))
    {
        if (selectedPieceIdx >= 0 && selectedPieceIdx < (int) pieceSprites.size())
        {
            const auto& sp = pieceSprites[(size_t) selectedPieceIdx];
            juce::Rectangle<float> handle { sp.canvasBounds.getRight() - 10.0f, sp.canvasBounds.getBottom() - 10.0f, 20.0f, 20.0f };
            if (handle.contains (pt))
            {
                isResizingCanvasPiece = true;
                dragStartPos = pt;
                dragStartW   = project.pieces[(size_t) selectedPieceIdx].normW;
                return;
            }
        }

        const int hitPiece = canvasPieceAt (pt);
        if (hitPiece >= 0)
        {
            selectPiece (hitPiece, true);
            isDraggingCanvasPiece = true;
            dragStartPos = pt;
            dragStartX   = project.pieces[(size_t) hitPiece].normX;
            dragStartY   = project.pieces[(size_t) hitPiece].normY;
            return;
        }
    }

    if (btnAutoPower.contains (pt) && selectedPieceIdx >= 0)
    {
        project.autoComputeZonePowers (selectedPieceIdx, selectedZoneIdx);
        reloadProjectIntoEngine();
        showStatusToast ("Auto-computed RMS/Peak sample power & sorted velocity layers");
        return;
    }
    if (btnImportWavs.contains (pt))
    {
        promptImportWavsForCurrentZone ("close");
        return;
    }
    if (btnDeleteZone.contains (pt) && selectedPieceIdx >= 0)
    {
        auto& p = project.pieces[(size_t) selectedPieceIdx];
        if (p.zones.size() > 1 && selectedZoneIdx >= 0 && selectedZoneIdx < (int) p.zones.size())
        {
            const std::string oldName = p.zones[(size_t) selectedZoneIdx].name;
            std::string err;
            project.deleteZone (selectedPieceIdx, selectedZoneIdx, err);
            reloadProjectIntoEngine();
            selectZone (std::max (0, selectedZoneIdx - 1), false);
            showStatusToast ("Deleted zone '" + juce::String (oldName) + "'");
        }
        return;
    }
    for (const auto& zt : zoneTabHits)
    {
        if (zt.bounds.contains (pt))
        {
            if (zt.zoneIndex == -1 && selectedPieceIdx >= 0)
            {
                auto& p = project.pieces[(size_t) selectedPieceIdx];
                EditableZone nz;
                nz.name = "zone" + std::to_string (p.zones.size() + 1);
                nz.note = 60 + (int) p.zones.size();
                EditableHit sample;
                sample.name = p.id + "_" + nz.name + "_v1_rr1";
                sample.power = 0.8f;
                sample.velocityLayerIndex = 4;
                sample.roundRobinIndex = 0;
                EditableMicSample ms;
                ms.mic = "close";
                ms.fileName = sample.name + "_close.wav";
                sample.micSamples.push_back (std::move (ms));
                nz.hits.push_back (std::move (sample));
                p.zones.push_back (std::move (nz));
                reloadProjectIntoEngine();
                selectZone ((int) p.zones.size() - 1, true);
                showStatusToast ("Added new articulation zone");
            }
            else if (zt.zoneIndex >= 0)
            {
                if (e.getNumberOfClicks() >= 2 && selectedPieceIdx >= 0)
                {
                    promptRenameZone (selectedPieceIdx, zt.zoneIndex);
                    return;
                }
                selectZone (zt.zoneIndex, true);
            }
            return;
        }
    }
    for (const auto& cell : matrixCellHits)
    {
        if (cell.bounds.contains (pt))
        {
            if (cell.hitIndex >= 0)
                selectHit (cell.hitIndex, true);
            else
                promptImportWavsForCurrentZone ("close");
            return;
        }
    }

    if (btnBuiltinAsset.contains (pt))  { showBuiltinAssetMenu(); return; }
    if (btnLoadCustomPng.contains (pt)) { promptLoadCustomPng(); return; }
    if (btnOutputBus.contains (pt))     { showOutputBusMenu(); return; }
    if (btnChokeGroup.contains (pt))    { showChokeGroupMenu(); return; }
    if (btnMidiNote.contains (pt))      { showMidiNoteMenu(); return; }
    if (btnMidiLearn.contains (pt))
    {
        midiLearnArmed = ! midiLearnArmed;
        showStatusToast (midiLearnArmed ? "MIDI Learn armed — strike any pad on your e-kit..." : "MIDI Learn cancelled");
        repaint();
        return;
    }
    if (btnChokeTarget.contains (pt) && selectedPieceIdx >= 0)
    {
        auto& p = project.pieces[(size_t) selectedPieceIdx];
        if (selectedZoneIdx >= 0 && selectedZoneIdx < (int) p.zones.size())
        {
            auto& zn = p.zones[(size_t) selectedZoneIdx];
            if (zn.chokes.empty() && p.zones.size() > 1)
            {
                GlorpChokeTarget ct;
                ct.instrument = p.name;
                ct.zone = p.zones[((size_t) selectedZoneIdx + 1) % p.zones.size()].name;
                ct.chokeTimeMs = 65.0f;
                zn.chokes.push_back (ct);
            }
            else
            {
                zn.chokes.clear();
            }
            repaint();
        }
        return;
    }
    if (btnPlaySample.contains (pt) || waveformPreviewBounds.contains (pt))
    {
        triggerAuditionForZone (selectedPieceIdx, selectedZoneIdx);
        return;
    }
    for (const auto& mr : micAssignHits)
    {
        if (mr.bounds.contains (pt))
        {
            promptImportWavsForCurrentZone (mr.micName);
            return;
        }
    }
}

void GlorpKitEditorComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (isDraggingLayerRow && dragLayerPieceIndex >= 0)
    {
        for (const auto& row : layerRowHits)
        {
            if (row.pieceIndex != dragLayerPieceIndex && row.rowBounds.contains (e.position))
            {
                int currentZ = project.pieces[(size_t) dragLayerPieceIndex].zOrder;
                int targetZ  = project.pieces[(size_t) row.pieceIndex].zOrder;
                project.pieces[(size_t) dragLayerPieceIndex].zOrder = targetZ;
                project.pieces[(size_t) row.pieceIndex].zOrder = currentZ;
                repaint();
                break;
            }
        }
        return;
    }

    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) project.pieces.size()) return;
    auto& piece = project.pieces[(size_t) selectedPieceIdx];

    if (isDraggingCanvasPiece && canvasViewportBounds.getWidth() > 20.0f)
    {
        const float dx = (e.position.x - dragStartPos.x) / canvasViewportBounds.getWidth();
        const float dy = (e.position.y - dragStartPos.y) / canvasViewportBounds.getHeight();
        float nx = juce::jlimit (0.06f, 0.94f, dragStartX + dx);
        float ny = juce::jlimit (0.08f, 0.92f, dragStartY + dy);
        if (snapToGrid && ! e.mods.isShiftDown())
        {
            nx = std::round (nx * 40.0f) / 40.0f;
            ny = std::round (ny * 40.0f) / 40.0f;
        }
        piece.normX = nx;
        piece.normY = ny;
        computeCanvasPieceBounds();
        repaint();
    }
    else if (isResizingCanvasPiece)
    {
        const float dw = (e.position.x - dragStartPos.x) * 1.4f;
        piece.normW = juce::jlimit (64.0f, 360.0f, dragStartW + dw);
        computeCanvasPieceBounds();
        repaint();
    }
}

void GlorpKitEditorComponent::mouseUp (const juce::MouseEvent&)
{
    isDraggingCanvasPiece = false;
    isResizingCanvasPiece = false;
    isDraggingLayerRow = false;
    dragLayerPieceIndex = -1;
}

void GlorpKitEditorComponent::mouseMove (const juce::MouseEvent& e)
{
    if (canvasViewportBounds.contains (e.position))
    {
        const int hit = canvasPieceAt (e.position);
        setMouseCursor (hit >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void GlorpKitEditorComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (selectedPieceIdx < 0 || selectedPieceIdx >= (int) project.pieces.size()) return;
    auto& piece = project.pieces[(size_t) selectedPieceIdx];

    if (canvasViewportBounds.contains (e.position) || pillNormW.contains (e.position))
    {
        piece.normW = juce::jlimit (64.0f, 360.0f, piece.normW + wheel.deltaY * 28.0f);
        computeCanvasPieceBounds();
        repaint();
    }
    else if (pillZOrder.contains (e.position))
    {
        project.movePieceZOrder (selectedPieceIdx, wheel.deltaY > 0.0f ? +1 : -1);
        repaint();
    }
}

bool GlorpKitEditorComponent::keyPressed (const juce::KeyPress& key)
{
    if (tutorialActive)
    {
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey) { nextTutorialStep(); return true; }
        if (key == juce::KeyPress::leftKey)  { prevTutorialStep(); return true; }
        if (key == juce::KeyPress::escapeKey) { setTutorialVisible (false, 0); return true; }
    }
    if (key == juce::KeyPress::spaceKey)
    {
        triggerAuditionForZone (selectedPieceIdx, selectedZoneIdx);
        return true;
    }
    return false;
}

//==============================================================================
bool GlorpKitEditorComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg")
            || f.endsWithIgnoreCase (".png") || f.endsWithIgnoreCase (".glorpkit") || f.endsWithIgnoreCase (".xml"))
            return true;
    }
    return false;
}

void GlorpKitEditorComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    juce::File first (files[0]);

    if (first.hasFileExtension (".glorpkit") || first.hasFileExtension (".xml"))
    {
        openKitFile (first);
        return;
    }
    if (first.hasFileExtension (".png") && selectedPieceIdx >= 0)
    {
        juce::MemoryBlock mb;
        if (first.loadFileAsData (mb) && mb.getSize() > 8)
        {
            auto& p = project.pieces[(size_t) selectedPieceIdx];
            p.customPngFileName = first.getFileName().toStdString();
            p.customPngBytes.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
            rebuildPieceSprites();
            showStatusToast ("Loaded custom PNG sprite for " + juce::String (p.name) + ": " + first.getFileName());
            repaint();
        }
        return;
    }

    std::vector<std::string> wavPaths;
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg"))
            wavPaths.push_back (f.toStdString());
    }
    if (! wavPaths.empty() && selectedPieceIdx >= 0)
    {
        int count = project.importAudioFilesIntoZone (selectedPieceIdx, selectedZoneIdx, wavPaths, "close");
        reloadProjectIntoEngine();
        showStatusToast ("Imported & RMS-sorted " + juce::String (count) + " audio samples into zone");
    }
    juce::ignoreUnused (x, y);
}

//==============================================================================
void GlorpKitEditorComponent::audioDeviceIOCallbackWithContext (const float* const* /*inputChannelData*/,
                                                                int /*numInputChannels*/,
                                                                float* const* outputChannelData,
                                                                int numOutputChannels,
                                                                int numSamples,
                                                                const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    if (numOutputChannels < 1 || outputChannelData[0] == nullptr) return;
    sampler.render (outputChannelData, numOutputChannels, numSamples);

    if (synthVoice.active)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (synthVoice.env < 0.001f)
            {
                synthVoice.active = false;
                break;
            }
            float tone = std::sin (synthVoice.phase);
            float noise = (((float) std::rand() / (float) RAND_MAX) * 2.0f - 1.0f);
            synthVoice.filterState += 0.45f * (noise - synthVoice.filterState);
            float smp = ((1.0f - synthVoice.noiseMix) * tone + synthVoice.noiseMix * synthVoice.filterState) * synthVoice.env * 0.45f;

            outputChannelData[0][i] += smp;
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                outputChannelData[1][i] += smp;

            synthVoice.phase += synthVoice.freq * (juce::MathConstants<float>::twoPi / 44100.0f);
            if (synthVoice.phase > juce::MathConstants<float>::twoPi)
                synthVoice.phase -= juce::MathConstants<float>::twoPi;
            synthVoice.freq = std::max (25.0f, synthVoice.freq * synthVoice.freqDecay);
            synthVoice.env *= synthVoice.envDecay;
        }
    }
}

void GlorpKitEditorComponent::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        sampler.setSampleRateAndBlock (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void GlorpKitEditorComponent::audioDeviceStopped() {}

void GlorpKitEditorComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! message.isNoteOn()) return;
    const int note = message.getNoteNumber();
    const float vel = message.getFloatVelocity();

    juce::MessageManager::callAsync ([this, note, vel]()
    {
        if (midiLearnArmed && selectedPieceIdx >= 0)
        {
            auto& p = project.pieces[(size_t) selectedPieceIdx];
            if (selectedZoneIdx >= 0 && selectedZoneIdx < (int) p.zones.size())
            {
                p.zones[(size_t) selectedZoneIdx].note = note;
                midiLearnArmed = false;
                showStatusToast ("Assigned MIDI Note " + juce::String (midiNoteName (note)) + " to " + juce::String (p.zones[(size_t) selectedZoneIdx].name));
                repaint();
                return;
            }
        }

        const auto& pieces = project.pieces;
        for (size_t pi = 0; pi < pieces.size(); ++pi)
        {
            for (size_t zi = 0; zi < pieces[pi].zones.size(); ++zi)
            {
                const auto& z = pieces[pi].zones[zi];
                if (z.note == note || z.altNote == note)
                {
                    if (pi < pieceSprites.size())
                        pieceSprites[pi].flash = std::max (pieceSprites[pi].flash, vel);
                    const juce::String instId = pieces[pi].zones.size() == 1
                                                    ? juce::String (pieces[pi].name)
                                                    : juce::String (pieces[pi].name + "_" + z.name);
                    for (int idx = 0; idx < sampler.numInstruments(); ++idx)
                    {
                        if (sampler.instrumentName (idx).equalsIgnoreCase (instId))
                        {
                            sampler.noteOn (idx, vel, sampler.currentSamplePosition());
                            break;
                        }
                    }
                    startAuditionSynth ((int) pi, vel);
                    repaint();
                }
            }
        }
    });
}

void GlorpKitEditorComponent::timerCallback()
{
    bool dirty = false;
    for (auto& sp : pieceSprites)
    {
        if (sp.flash > 0.005f)
        {
            sp.flash *= 0.80f;
            if (sp.flash < 0.01f) sp.flash = 0.0f;
            dirty = true;
        }
    }
    if (statusBannerAlpha > 0.0f)
    {
        statusBannerAlpha = std::max (0.0f, statusBannerAlpha - 0.008f);
        dirty = true;
    }
    if (dirty)
        repaint (canvasViewportBounds.toNearestInt());
}

//==============================================================================
void GlorpKitEditorComponent::promptOpenKit()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open .glorpkit Archive", juce::File(), "*.glorpkit");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile())
                                  openKitFile (f);
                          });
}

void GlorpKitEditorComponent::promptImportDrumGizmo()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Import DrumGizmo Kit XML", juce::File(), "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile())
                                  openKitFile (f);
                          });
}

void GlorpKitEditorComponent::promptSaveGlorpKit()
{
    juce::File defaultFile = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                 .getChildFile (juce::String (project.title).replaceCharacter (' ', '_') + ".glorpkit");
    auto chooser = std::make_shared<juce::FileChooser> ("Save .glorpkit v2.0 Archive", defaultFile, "*.glorpkit");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f != juce::File())
                                  saveGlorpKitFile (f.withFileExtension (".glorpkit"));
                          });
}

void GlorpKitEditorComponent::promptLoadCustomPng()
{
    if (selectedPieceIdx < 0) return;
    auto chooser = std::make_shared<juce::FileChooser> ("Select Custom Transparent PNG Sprite", juce::File(), "*.png");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile() && selectedPieceIdx >= 0)
                              {
                                  juce::MemoryBlock mb;
                                  if (f.loadFileAsData (mb) && mb.getSize() > 8)
                                  {
                                      auto& p = project.pieces[(size_t) selectedPieceIdx];
                                      p.customPngFileName = f.getFileName().toStdString();
                                      p.customPngBytes.assign ((const uint8_t*) mb.getData(),
                                                               (const uint8_t*) mb.getData() + mb.getSize());
                                      rebuildPieceSprites();
                                      showStatusToast ("Embedded custom PNG sprite: " + f.getFileName());
                                      repaint();
                                  }
                              }
                          });
}

void GlorpKitEditorComponent::promptImportWavsForCurrentZone (const std::string& targetMic)
{
    if (selectedPieceIdx < 0) return;
    auto chooser = std::make_shared<juce::FileChooser> ("Select WAV/FLAC Audio Samples for Zone (" + juce::String (targetMic) + ")",
                                                        juce::File(), "*.wav;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, chooser, targetMic] (const juce::FileChooser& fc)
                          {
                              auto results = fc.getResults();
                              std::vector<std::string> paths;
                              for (const auto& f : results)
                                  if (f.existsAsFile())
                                      paths.push_back (f.getFullPathName().toStdString());
                              if (! paths.empty())
                              {
                                  int n = project.importAudioFilesIntoZone (selectedPieceIdx, selectedZoneIdx, paths, targetMic);
                                  reloadProjectIntoEngine();
                                  showStatusToast ("Imported " + juce::String (n) + " audio files into " + juce::String (targetMic));
                              }
                          });
}

void GlorpKitEditorComponent::showHitGlowColorMenu()
{
    struct PresetColor { const char* name; const char* hex; };
    static const PresetColor presets[] = {
        { "Glorp Orange (Default)  #FF8200", "#ff8200" },
        { "Radioactive Slime Green #2ECC71", "#2ecc71" },
        { "Electric Cyan           #00D2FF", "#00d2ff" },
        { "Hot Comic Magenta       #FF2E93", "#ff2e93" },
        { "Neon Laser Purple       #A855F7", "#a855f7" },
        { "Golden Sunburst         #F5B82E", "#f5b82e" },
        { "Crimson Blaze           #FF3B30", "#ff3b30" },
        { "Pure White Halo         #FFFFFF", "#ffffff" },
    };

    juce::PopupMenu m;
    m.addSectionHeader ("Kit-Wide Hit Glow Color");
    const std::string curHex = toHexColourString (hitGlowColour);
    for (int i = 0; i < 8; ++i)
    {
        const bool ticked = (juce::String (curHex).equalsIgnoreCase (presets[i].hex));
        m.addItem (i + 1, presets[i].name, true, ticked);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (btnGlowColor.toNearestInt())),
                     [this] (int res)
                     {
                         if (res >= 1 && res <= 8)
                             setHitGlowColourHex (presets[res - 1].hex);
                     });
}

void GlorpKitEditorComponent::showBuiltinAssetMenu()
{
    if (selectedPieceIdx < 0) return;
    static const char* names[] = { "kick", "snare", "hihat", "tom_1", "tom_2", "f_tom", "crash_1", "crash_2", "ride" };
    juce::PopupMenu m;
    m.addSectionHeader ("Built-in Sketch Sprite");
    for (int i = 0; i < 9; ++i)
        m.addItem (i + 1, names[i]);
    m.addSeparator();
    m.addItem (99, "Clear Custom PNG Override");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (btnBuiltinAsset.toNearestInt())),
                     [this] (int res)
                     {
                         if (selectedPieceIdx < 0) return;
                         auto& p = project.pieces[(size_t) selectedPieceIdx];
                         if (res >= 1 && res <= 9)
                         {
                             p.graphicId = names[res - 1];
                             p.customPngBytes.clear();
                             p.customPngFileName.clear();
                             rebuildPieceSprites();
                             repaint();
                         }
                         else if (res == 99)
                         {
                             p.customPngBytes.clear();
                             p.customPngFileName.clear();
                             rebuildPieceSprites();
                             repaint();
                         }
                     });
}

void GlorpKitEditorComponent::showOutputBusMenu()
{
    if (selectedPieceIdx < 0) return;
    juce::PopupMenu m;
    for (int i = 0; i < 16; ++i)
        m.addItem (i + 1, "Stereo Bus #" + juce::String (i + 1) + " (Out " + juce::String (i * 2 + 1) + "/" + juce::String (i * 2 + 2) + ")");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (btnOutputBus.toNearestInt())),
                     [this] (int res)
                     {
                         if (res >= 1 && res <= 16 && selectedPieceIdx >= 0)
                         {
                             project.pieces[(size_t) selectedPieceIdx].outputBus = res - 1;
                             repaint();
                         }
                     });
}

void GlorpKitEditorComponent::showChokeGroupMenu()
{
    if (selectedPieceIdx < 0) return;
    static const char* groups[] = { "none", "hihat", "cymbal_mute", "aux_perc" };
    juce::PopupMenu m;
    m.addItem (1, "none (Polyphonic)");
    m.addItem (2, "hihat (Hi-Hat Open/Closed Choke)");
    m.addItem (3, "cymbal_mute (Cymbal Mute Group)");
    m.addItem (4, "aux_perc (Aux Percussion Group)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (btnChokeGroup.toNearestInt())),
                     [this] (int res)
                     {
                         if (res >= 1 && res <= 4 && selectedPieceIdx >= 0)
                         {
                             project.pieces[(size_t) selectedPieceIdx].chokeGroup = groups[res - 1];
                             repaint();
                         }
                     });
}

void GlorpKitEditorComponent::showMidiNoteMenu()
{
    if (selectedPieceIdx < 0) return;
    juce::PopupMenu m;
    m.addSectionHeader ("General MIDI Drum Standard Notes");
    const int gmNotes[] = { 35, 36, 37, 38, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 55, 57, 59 };
    for (int n : gmNotes)
        m.addItem (n + 100, midiNoteName (n));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (btnMidiNote.toNearestInt())),
                     [this] (int res)
                     {
                         if (res >= 100 && selectedPieceIdx >= 0)
                         {
                             auto& p = project.pieces[(size_t) selectedPieceIdx];
                             if (selectedZoneIdx >= 0 && selectedZoneIdx < (int) p.zones.size())
                             {
                                 p.zones[(size_t) selectedZoneIdx].note = res - 100;
                                 repaint();
                             }
                         }
                     });
}

void GlorpKitEditorComponent::showKitMetadataDialog()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Kit Sample Rate");
    m.addItem (1, "44100 Hz", true, project.sampleRate == 44100);
    m.addItem (2, "48000 Hz", true, project.sampleRate == 48000);
    m.addItem (3, "96000 Hz", true, project.sampleRate == 96000);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (pillKitMeta.toNearestInt())),
                     [this] (int res)
                     {
                         if (res == 1) project.sampleRate = 44100;
                         if (res == 2) project.sampleRate = 48000;
                         if (res == 3) project.sampleRate = 96000;
                         repaint();
                     });
}

void GlorpKitEditorComponent::startAuditionSynth (int pieceIdx, float velocity)
{
    if (pieceIdx < 0 || pieceIdx >= (int) project.pieces.size()) return;
    const auto& p = project.pieces[(size_t) pieceIdx];
    const juce::String gid = juce::String (p.graphicId).toLowerCase();
    const juce::String name = juce::String (p.name).toLowerCase();

    synthVoice.active = true;
    synthVoice.phase = 0.0f;
    synthVoice.filterState = 0.0f;

    if (gid.contains ("kick") || name.contains ("kick"))
    {
        synthVoice.freq = 150.0f;
        synthVoice.freqDecay = 0.9982f;
        synthVoice.env = velocity * 0.95f;
        synthVoice.envDecay = 0.9993f;
        synthVoice.noiseMix = 0.05f;
    }
    else if (gid.contains ("snare") || name.contains ("snare"))
    {
        synthVoice.freq = 230.0f;
        synthVoice.freqDecay = 0.996f;
        synthVoice.env = velocity * 0.90f;
        synthVoice.envDecay = 0.9986f;
        synthVoice.noiseMix = 0.65f;
    }
    else if (gid.contains ("hihat") || name.contains ("hihat") || gid.contains ("hh") || name.contains ("hh"))
    {
        synthVoice.freq = 4200.0f;
        synthVoice.freqDecay = 0.999f;
        synthVoice.env = velocity * 0.70f;
        synthVoice.envDecay = 0.9965f;
        synthVoice.noiseMix = 0.95f;
    }
    else if (gid.contains ("crash") || name.contains ("crash") || gid.contains ("ride") || name.contains ("ride"))
    {
        synthVoice.freq = 3200.0f;
        synthVoice.freqDecay = 0.9994f;
        synthVoice.env = velocity * 0.75f;
        synthVoice.envDecay = 0.9996f;
        synthVoice.noiseMix = 0.92f;
    }
    else if (gid.contains ("tom") || name.contains ("tom"))
    {
        if (gid.contains ("1") || name.contains ("1") || gid.contains ("high"))
            synthVoice.freq = 180.0f;
        else if (gid.contains ("2") || name.contains ("2") || gid.contains ("mid"))
            synthVoice.freq = 135.0f;
        else
            synthVoice.freq = 90.0f; // floor tom

        synthVoice.freqDecay = 0.9986f;
        synthVoice.env = velocity * 0.90f;
        synthVoice.envDecay = 0.9991f;
        synthVoice.noiseMix = 0.08f;
    }
    else
    {
        synthVoice.freq = 160.0f;
        synthVoice.freqDecay = 0.998f;
        synthVoice.env = velocity * 0.85f;
        synthVoice.envDecay = 0.9988f;
        synthVoice.noiseMix = 0.30f;
    }
}

void GlorpKitEditorComponent::showAudioSettingsDialog()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (
        deviceManager,
        0, 2, // inputs
        2, 2, // outputs
        true, // midi in
        false, // midi out
        false, // channels as stereo pairs
        false  // hide advanced
    );
    selector->setSize (500, 380);

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Audio & MIDI Settings";
    opt.dialogBackgroundColour = theme.panel;
    opt.content.setOwned (selector);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

void GlorpKitEditorComponent::promptRenamePiece (int pieceIndex)
{
    if (pieceIndex < 0 || pieceIndex >= (int) project.pieces.size()) return;
    const auto currentName = juce::String (project.pieces[(size_t) pieceIndex].name);

    auto* alert = new juce::AlertWindow ("Rename Kit Piece", "Enter a new name for this drum/cymbal:", juce::AlertWindow::QuestionIcon);
    alert->addTextEditor ("pieceName", currentName, "Piece Name");
    alert->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert, pieceIndex] (int result)
    {
        if (result == 1)
        {
            auto newName = alert->getTextEditorContents ("pieceName").trim();
            if (newName.isNotEmpty())
            {
                project.renamePiece (pieceIndex, newName.toStdString());
                reloadProjectIntoEngine();
                selectPiece (pieceIndex, false);
                showStatusToast ("Renamed piece to '" + newName + "'");
            }
        }
        delete alert;
    }));
}

void GlorpKitEditorComponent::promptRenameMic (int micIndex)
{
    if (micIndex < 0 || micIndex >= (int) project.mics.size()) return;
    const auto currentName = juce::String (project.mics[(size_t) micIndex].name);

    auto* alert = new juce::AlertWindow ("Rename Mic Bus", "Enter a new name for this microphone channel:", juce::AlertWindow::QuestionIcon);
    alert->addTextEditor ("micName", currentName, "Mic Name");
    alert->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert, micIndex] (int result)
    {
        if (result == 1)
        {
            auto newName = alert->getTextEditorContents ("micName").trim();
            if (newName.isNotEmpty())
            {
                project.renameMic (micIndex, newName.toStdString());
                reloadProjectIntoEngine();
                showStatusToast ("Renamed mic bus to '" + newName + "'");
                repaint();
            }
        }
        delete alert;
    }));
}

void GlorpKitEditorComponent::promptRenameZone (int pieceIndex, int zoneIndex)
{
    if (pieceIndex < 0 || pieceIndex >= (int) project.pieces.size()) return;
    auto& p = project.pieces[(size_t) pieceIndex];
    if (zoneIndex < 0 || zoneIndex >= (int) p.zones.size()) return;
    const auto currentName = juce::String (p.zones[(size_t) zoneIndex].name);

    auto* alert = new juce::AlertWindow ("Rename Articulation Zone", "Enter a new name for this articulation zone:", juce::AlertWindow::QuestionIcon);
    alert->addTextEditor ("zoneName", currentName, "Zone Name");
    alert->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, alert, pieceIndex, zoneIndex] (int result)
    {
        if (result == 1)
        {
            auto newName = alert->getTextEditorContents ("zoneName").trim();
            if (newName.isNotEmpty())
            {
                project.renameZone (pieceIndex, zoneIndex, newName.toStdString());
                reloadProjectIntoEngine();
                selectZone (zoneIndex, false);
                showStatusToast ("Renamed zone to '" + newName + "'");
            }
        }
        delete alert;
    }));
}

} // namespace glorp
