#include "DrumKitView.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

namespace
{
    juce::Image makeGlowImage (const juce::Image& src, juce::Colour glowColour = juce::Colour (0xffff8200))
    {
        if (! src.isValid())
            return {};

        const int sw = std::max (1, src.getWidth() / 2);
        const int sh = std::max (1, src.getHeight() / 2);
        const int pad = 36;
        const int gw = sw + pad * 2;
        const int gh = sh + pad * 2;

        auto smallSrc = src.rescaled (sw, sh, juce::Graphics::mediumResamplingQuality);

        // Convert the piece's alpha mask into solid bright vibrant glow colour
        juce::Image orangeMask (juce::Image::ARGB, sw, sh, true);
        for (int y = 0; y < sh; ++y)
        {
            for (int x = 0; x < sw; ++x)
            {
                const auto a = smallSrc.getPixelAt (x, y).getAlpha();
                if (a > 15)
                    orangeMask.setPixelAt (x, y, glowColour.withAlpha (a / 255.0f));
            }
        }

        juce::Image glow (juce::Image::ARGB, gw, gh, true);
        juce::Graphics g (glow);

        // Radial layered expansion to generate a soft, powerful, feathered orange aura
        const int maxRadius = 24;
        for (int r = maxRadius; r >= 2; r -= 2)
        {
            const float factor = 1.0f - (static_cast<float> (r) / static_cast<float> (maxRadius));
            const float alpha = 0.06f + factor * 0.14f;
            g.setOpacity (alpha);

            const float fr = static_cast<float> (r);
            for (int a = 0; a < 8; ++a)
            {
                const float angle = static_cast<float> (a) * 0.785398f;
                const int ox = static_cast<int> (pad + std::cos (angle) * fr);
                const int oy = static_cast<int> (pad + std::sin (angle) * fr);
                g.drawImageAt (orangeMask, ox, oy);
            }
        }

        g.setOpacity (0.95f);
        g.drawImageAt (orangeMask, pad, pad);

        return glow;
    }
}

//==============================================================================
DrumKitView::DrumKitView()
{
    setOpaque (true);
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
    loadPieceImages();
    loadLayoutFromFile();
    startTimerHz (60);
}

const char* DrumKitView::pieceIdentifier (KitPiece piece)
{
    switch (piece)
    {
        case KitPiece::kick:     return "kick";
        case KitPiece::snare:    return "snare";
        case KitPiece::tom1:     return "tom1";
        case KitPiece::tom2:     return "tom2";
        case KitPiece::floorTom: return "floorTom";
        case KitPiece::hihat:    return "hihat";
        case KitPiece::ride:     return "ride";
        case KitPiece::crash1:   return "crash1";
        case KitPiece::crash2:   return "crash2";
        default:                 return "unknown";
    }
}

juce::String DrumKitView::pieceDisplayName (KitPiece piece)
{
    switch (piece)
    {
        case KitPiece::kick:     return "Kick Drum";
        case KitPiece::snare:    return "Snare Drum";
        case KitPiece::tom1:     return "High Tom 1";
        case KitPiece::tom2:     return "Mid Tom 2";
        case KitPiece::floorTom: return "Floor Tom";
        case KitPiece::hihat:    return "Hi-Hat";
        case KitPiece::ride:     return "Ride Cymbal";
        case KitPiece::crash1:   return "Crash 1";
        case KitPiece::crash2:   return "Crash 2 / China";
        default:                 return "Piece";
    }
}

void DrumKitView::resetToDefaultLayout()
{
    // Fine-tuned drum kit element positions
    coords[static_cast<size_t> (KitPiece::kick)]     = { -155.2f, -329.7f, 345.0f };
    coords[static_cast<size_t> (KitPiece::snare)]    = { -212.7f, -259.5f, 180.0f };
    coords[static_cast<size_t> (KitPiece::tom1)]     = { -122.6f, -420.7f, 138.0f };
    coords[static_cast<size_t> (KitPiece::tom2)]     = {   15.9f, -408.0f, 155.0f };
    coords[static_cast<size_t> (KitPiece::floorTom)] = {  107.2f, -272.1f, 195.0f };
    coords[static_cast<size_t> (KitPiece::hihat)]    = { -299.6f, -404.7f, 160.0f };
    coords[static_cast<size_t> (KitPiece::ride)]     = {   77.7f, -507.0f, 210.0f };
    coords[static_cast<size_t> (KitPiece::crash1)]   = { -235.0f, -502.9f, 190.0f };
    coords[static_cast<size_t> (KitPiece::crash2)]   = {  187.3f, -420.8f, 205.0f };
    hasLayout = false;
    repaint();
}

void DrumKitView::loadLayoutFromFile()
{
    resetToDefaultLayout();

    juce::File layoutFile;
    const auto localAssets = juce::File::getCurrentWorkingDirectory().getChildFile ("assets/kit_layout.json");
    const auto homeConfig = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                .getChildFile (".config/GlorpDrums/kit_layout.json");

    if (localAssets.existsAsFile())
        layoutFile = localAssets;
    else if (homeConfig.existsAsFile())
        layoutFile = homeConfig;

    if (! layoutFile.existsAsFile())
        return;

    const auto jsonVar = juce::JSON::parse (layoutFile);
    if (! jsonVar.isObject())
        return;

    auto* obj = jsonVar.getDynamicObject();
    if (! obj)
        return;

    for (size_t i = 0; i < static_cast<size_t> (KitPiece::count); ++i)
    {
        const auto id = juce::Identifier (pieceIdentifier (static_cast<KitPiece> (i)));
        if (obj->hasProperty (id))
        {
            auto pieceVar = obj->getProperty (id);
            if (pieceVar.isObject())
            {
                auto* pObj = pieceVar.getDynamicObject();
                if (pObj)
                {
                    coords[i].normX = static_cast<float> (pObj->getProperty ("x"));
                    coords[i].normY = static_cast<float> (pObj->getProperty ("y"));
                    coords[i].normW = static_cast<float> (pObj->getProperty ("w"));
                }
            }
        }
    }
    hasLayout = false;
    repaint();
}

bool DrumKitView::saveLayoutToFile()
{
    auto obj = std::make_unique<juce::DynamicObject>();

    for (size_t i = 0; i < static_cast<size_t> (KitPiece::count); ++i)
    {
        auto pObj = std::make_unique<juce::DynamicObject>();
        pObj->setProperty ("x", coords[i].normX);
        pObj->setProperty ("y", coords[i].normY);
        pObj->setProperty ("w", coords[i].normW);
        obj->setProperty (pieceIdentifier (static_cast<KitPiece> (i)), juce::var (pObj.release()));
    }

    const juce::var root (obj.release());
    const auto jsonString = juce::JSON::toString (root, true);

    bool saved = false;
    const auto localAssets = juce::File::getCurrentWorkingDirectory().getChildFile ("assets/kit_layout.json");
    if (localAssets.getParentDirectory().isDirectory())
    {
        localAssets.replaceWithText (jsonString);
        saved = true;
    }

    const auto homeDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                             .getChildFile (".config/GlorpDrums");
    homeDir.createDirectory();
    const auto homeConfig = homeDir.getChildFile ("kit_layout.json");
    homeConfig.replaceWithText (jsonString);
    saved = true;

    toastText = "Layout saved to assets/kit_layout.json!";
    toastAlpha = 1.0f;
    repaint();
    return saved;
}

juce::String DrumKitView::generateCppCode() const
{
    juce::String s;
    s << "    // Fine-tuned drum kit element positions for resetToDefaultLayout():\n";
    for (size_t i = 0; i < static_cast<size_t> (KitPiece::count); ++i)
    {
        const auto p = static_cast<KitPiece> (i);
        const auto id = pieceIdentifier (p);
        s << "    coords[static_cast<size_t> (KitPiece::" << id << ")] = { "
          << juce::String (coords[i].normX, 1) << "f, "
          << juce::String (coords[i].normY, 1) << "f, "
          << juce::String (coords[i].normW, 1) << "f };\n";
    }
    return s;
}

void DrumKitView::setEditMode (bool enabled)
{
    if (editMode == enabled)
        return;

    editMode = enabled;
    hasLayout = false;
    layout();

    if (editMode)
    {
        grabKeyboardFocus();
        toastText = "WYSIWYG Layout Edit Mode Enabled";
        toastAlpha = 1.0f;
    }
    else
    {
        selectedPiece = KitPiece::count;
        isDraggingPiece = false;
        isResizingPiece = false;
        toastAlpha = 0.0f;
        toastText = {};
    }
    repaint();
}

void DrumKitView::loadPieceImages()
{
    pieces[static_cast<size_t> (KitPiece::kick)].image =
        imageFromMemory (GlorpAssets::kick_png, GlorpAssets::kick_pngSize);
    pieces[static_cast<size_t> (KitPiece::snare)].image =
        imageFromMemory (GlorpAssets::snare_png, GlorpAssets::snare_pngSize);
    pieces[static_cast<size_t> (KitPiece::hihat)].image =
        imageFromMemory (GlorpAssets::hihat_png, GlorpAssets::hihat_pngSize);
    pieces[static_cast<size_t> (KitPiece::tom1)].image =
        imageFromMemory (GlorpAssets::tom_1_png, GlorpAssets::tom_1_pngSize);
    pieces[static_cast<size_t> (KitPiece::tom2)].image =
        imageFromMemory (GlorpAssets::tom_2_png, GlorpAssets::tom_2_pngSize);
    pieces[static_cast<size_t> (KitPiece::floorTom)].image =
        imageFromMemory (GlorpAssets::f_tom_png, GlorpAssets::f_tom_pngSize);
    pieces[static_cast<size_t> (KitPiece::crash1)].image =
        imageFromMemory (GlorpAssets::crash_1_png, GlorpAssets::crash_1_pngSize);
    pieces[static_cast<size_t> (KitPiece::crash2)].image =
        imageFromMemory (GlorpAssets::crash_2_png, GlorpAssets::crash_2_pngSize);
    pieces[static_cast<size_t> (KitPiece::ride)].image =
        imageFromMemory (GlorpAssets::ride_png, GlorpAssets::ride_pngSize);

    for (auto& p : pieces)
    {
        p.scaledImage = {};
        p.scaledGlowImage = {};
        if (p.image.isValid())
            p.glowImage = makeGlowImage (p.image, hitGlowColour);
    }
}

juce::Colour DrumKitView::parseHexColour (const std::string& hex, juce::Colour fallback)
{
    if (hex.empty())
        return fallback;

    juce::String s (hex);
    s = s.trim();
    if (s.startsWithChar ('#'))
        s = s.substring (1);

    if (s.length() == 6)
        s = "ff" + s;

    if (s.length() == 8)
        return juce::Colour (static_cast<juce::uint32> (s.getHexValue64()));

    return fallback;
}

void DrumKitView::setHitGlowColour (juce::Colour colour)
{
    if (hitGlowColour == colour)
        return;

    hitGlowColour = colour;
    for (auto& p : pieces)
    {
        p.scaledGlowImage = {};
        if (p.image.isValid())
            p.glowImage = makeGlowImage (p.image, hitGlowColour);
    }
    hasLayout = false;
    layout();
    repaint();
}

void DrumKitView::applyKitVisuals (const DrumKit& kit)
{
    hitGlowColour = parseHexColour (kit.hitGlowColor, juce::Colour (0xffff8200));
    loadPieceImages();
    loadLayoutFromFile();

    if (! kit.isNativeGlorpKit || kit.logicalPieces.empty())
    {
        hasLayout = false;
        layout();
        repaint();
        return;
    }

    std::array<int, static_cast<size_t> (KitPiece::count)> zRank {{ 1, 4, 5, 6, 9, 3, 7, 2, 8 }};
    bool anyCustomZ = false;

    for (const auto& lp : kit.logicalPieces)
    {
        auto kp = pieceForName (lp.id);
        if (kp == KitPiece::count)
            kp = pieceForName (lp.name);
        if (kp == KitPiece::count)
            kp = pieceForName (lp.graphic);
        if (kp == KitPiece::count)
            continue;

        const auto idx = static_cast<size_t> (kp);

        if (! lp.customPngBytes.empty())
        {
            auto customImg = juce::ImageFileFormat::loadFrom (lp.customPngBytes.data(), lp.customPngBytes.size());
            if (customImg.isValid())
            {
                pieces[idx].image = customImg;
                pieces[idx].scaledImage = {};
                pieces[idx].scaledGlowImage = {};
                pieces[idx].glowImage = makeGlowImage (customImg, hitGlowColour);
            }
        }

        if (lp.hasCustomLayout)
        {
            coords[idx].normX = lp.normX;
            coords[idx].normY = lp.normY;
            coords[idx].normW = std::clamp (lp.normW, 40.0f, 650.0f);
            zRank[idx] = lp.zOrder;
            anyCustomZ = true;
        }
    }

    if (anyCustomZ)
    {
        pieceZOrder = zRank;
    }

    hasLayout = false;
    layout();
    rebuildKitOutline();
    rebuildStaticKitCache();
    repaint();
}

KitPiece DrumKitView::pieceForName (const juce::String& name)
{
    const auto n = name.toLowerCase();

    if (n.contains ("kick") || n.contains ("kdrum") || n.contains ("bassdrum")
        || n.contains ("bdrum") || n.contains ("bd"))
        return KitPiece::kick;

    if (n.contains ("snare") || n.contains ("sd") || n.contains ("snr")
        || n.contains ("rimclick") || n.contains ("rimshot") || n.contains ("crosstick"))
        return KitPiece::snare;

    // Check Tom 2 first (so "tom 2" or "tom2" or "tom13" never gets stolen by "tom")
    if (n.contains ("tom2") || n.contains ("tom 2") || n.contains ("racktom2")
        || n.contains ("rack 2") || n.contains ("midtom") || n.contains ("mediumtom")
        || n.contains ("tom13") || n.contains ("tom 13"))
        return KitPiece::tom2;

    // Check Floor Tom ("floor", "ftom", "tom3", "tom 3", "tom14", "tom16", "tom18", etc.)
    if (n.contains ("floor") || n.contains ("ftom") || n.contains ("floortom")
        || n.contains ("tom3") || n.contains ("tom 3") || n.contains ("racktom3")
        || n.contains ("lowtom") || n.contains ("tom16") || n.contains ("tom 16")
        || n.contains ("tom18") || n.contains ("tom 18") || n.contains ("tom14") || n.contains ("tom 14"))
        return KitPiece::floorTom;

    // Check Tom 1
    if (n.contains ("tom1") || n.contains ("tom 1") || n.contains ("racktom1")
        || n.contains ("rack 1") || n.contains ("hightom") || n.contains ("smalltom")
        || n.contains ("tom10") || n.contains ("tom 10")
        || n.contains ("tom12") || n.contains ("tom 12")
        || (n.contains ("tom") && ! n.contains ("cowbell")))
        return KitPiece::tom1;

    if (n.contains ("cowbell"))
        return KitPiece::count;

    if (n.contains ("ride") || n.contains ("bell"))
        return KitPiece::ride;

    if (n.contains ("crash2") || n.contains ("crash 2") || n.contains ("china") || n.contains ("splash"))
        return KitPiece::crash2;

    if (n.contains ("crash") || n.contains ("cymbal"))
        return KitPiece::crash1;

    if (n.contains ("hihat") || n.contains ("hi-hat") || n.contains ("hh")
        || n.contains ("hatclosed") || n.contains ("hatopen") || n.contains ("hat"))
        return KitPiece::hihat;

    return KitPiece::count;
}

void DrumKitView::setInstrumentNames (const std::vector<juce::String>& names)
{
    instrumentNames = names;
    repaint();
}

juce::Rectangle<int> DrumKitView::dirtyBoundsForPiece (KitPiece piece) const noexcept
{
    if (piece == KitPiece::count)
        return {};

    const auto& p = pieces[static_cast<size_t> (piece)];
    if (p.bounds.isEmpty())
        return getLocalBounds();

    const auto region = p.glowBounds.isEmpty() ? p.bounds : p.glowBounds.getUnion (p.bounds);
    return region.expanded (8.0f).getSmallestIntegerContainer().getIntersection (getLocalBounds());
}

bool DrumKitView::isPieceScaledCacheValid (KitPiece piece) const noexcept
{
    if (piece == KitPiece::count)
        return false;

    const auto& p = pieces[static_cast<size_t> (piece)];
    return p.scaledImage.isValid() && p.scaledGlowImage.isValid();
}

void DrumKitView::triggerHit (const juce::String& instrumentName, float velocity)
{
    const auto piece = pieceForName (instrumentName);
    if (piece != KitPiece::count)
        triggerHitForPiece (piece, velocity);
}

void DrumKitView::triggerHitForPiece (KitPiece piece, float velocity)
{
    if (piece == KitPiece::count)
        return;

    auto& p = pieces[static_cast<size_t> (piece)];
    p.flash = std::clamp (velocity, 0.40f, 1.0f);
    p.squash = std::clamp (velocity * 0.75f, 0.20f, 1.0f);
    repaint (dirtyBoundsForPiece (piece));
}

KitPiece DrumKitView::pieceAt (juce::Point<float> pos) const
{
    // Hit-test in reverse Z-order (foreground first)
    const KitPiece order[] = {
        KitPiece::floorTom,
        KitPiece::crash2,
        KitPiece::tom2,
        KitPiece::tom1,
        KitPiece::ride,
        KitPiece::snare,
        KitPiece::hihat,
        KitPiece::crash1,
        KitPiece::kick
    };

    for (auto piece : order)
    {
        const auto& p = pieces[static_cast<size_t> (piece)];
        if (p.bounds.contains (pos) && p.image.isValid())
        {
            const float u = (pos.x - p.bounds.getX()) / p.bounds.getWidth();
            const float v = (pos.y - p.bounds.getY()) / p.bounds.getHeight();
            const int px = juce::jlimit (0, p.image.getWidth() - 1, static_cast<int> (u * p.image.getWidth()));
            const int py = juce::jlimit (0, p.image.getHeight() - 1, static_cast<int> (v * p.image.getHeight()));

            if (p.image.getPixelAt (px, py).getAlpha() > 25)
                return piece;
        }
    }

    return KitPiece::count;
}

void DrumKitView::mouseDown (const juce::MouseEvent& e)
{
    if (editMode)
    {
        if (saveBtnBounds.contains (e.position))
        {
            saveLayoutToFile();
            return;
        }
        if (copyBtnBounds.contains (e.position))
        {
            juce::SystemClipboard::copyTextToClipboard (generateCppCode());
            toastText = "C++ layout code copied to clipboard!";
            toastAlpha = 1.0f;
            repaint();
            return;
        }
        if (resetBtnBounds.contains (e.position))
        {
            resetToDefaultLayout();
            toastText = "Layout reset to defaults!";
            toastAlpha = 1.0f;
            repaint();
            return;
        }
        if (exitBtnBounds.contains (e.position))
        {
            setEditMode (false);
            return;
        }

        // 2. Check if clicking on resize handle of selected piece
        if (selectedPiece != KitPiece::count)
        {
            const auto& p = pieces[static_cast<size_t> (selectedPiece)];
            const auto handleBounds = juce::Rectangle<float> (p.bounds.getRight() - 22.0f,
                                                               p.bounds.getBottom() - 22.0f,
                                                               24.0f, 24.0f);
            if (handleBounds.contains (e.position))
            {
                isResizingPiece = true;
                dragStartMouse = e.position;
                dragStartNormW = coords[static_cast<size_t> (selectedPiece)].normW;
                return;
            }
        }

        // 3. Check if clicking on any piece to select & drag
        const auto piece = pieceAt (e.position);
        if (piece != KitPiece::count)
        {
            selectedPiece = piece;
            isDraggingPiece = true;
            dragStartMouse = e.position;
            dragStartNormX = coords[static_cast<size_t> (selectedPiece)].normX;
            dragStartNormY = coords[static_cast<size_t> (selectedPiece)].normY;
            repaint();
            return;
        }

        // Clicked on empty space: deselect
        selectedPiece = KitPiece::count;
        repaint();
        return;
    }

    // Normal play mode: click triggers drum hit
    const auto piece = pieceAt (e.position);
    if (piece != KitPiece::count)
    {
        constexpr float maxVelocity = 1.0f;
        triggerHitForPiece (piece, maxVelocity);

        if (onPieceHit != nullptr)
            onPieceHit (piece, maxVelocity);

        if (onPieceClicked != nullptr)
            onPieceClicked (piece);
    }
}

void DrumKitView::mouseDrag (const juce::MouseEvent& e)
{
    if (! editMode || selectedPiece == KitPiece::count)
        return;

    const float W = static_cast<float> (getWidth());
    const float H = static_cast<float> (getHeight());
    const float scale = std::max (0.01f, std::min (W / 880.0f, H / 620.0f));

    if (isResizingPiece)
    {
        const float dw = (e.position.x - dragStartMouse.x) / scale;
        coords[static_cast<size_t> (selectedPiece)].normW = std::clamp (dragStartNormW + dw, 40.0f, 650.0f);
        hasLayout = false;
        layout();
        repaint();
        return;
    }

    if (isDraggingPiece)
    {
        const float dx = (e.position.x - dragStartMouse.x) / scale;
        const float dy = (e.position.y - dragStartMouse.y) / scale;
        coords[static_cast<size_t> (selectedPiece)].normX = dragStartNormX + dx;
        coords[static_cast<size_t> (selectedPiece)].normY = dragStartNormY + dy;
        hasLayout = false;
        layout();
        repaint();
        return;
    }
}

void DrumKitView::mouseUp (const juce::MouseEvent&)
{
    isDraggingPiece = false;
    isResizingPiece = false;
}

void DrumKitView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (editMode && selectedPiece != KitPiece::count)
    {
        coords[static_cast<size_t> (selectedPiece)].normW =
            std::clamp (coords[static_cast<size_t> (selectedPiece)].normW + wheel.deltaY * 35.0f, 40.0f, 650.0f);
        hasLayout = false;
        layout();
        repaint();
    }
}

bool DrumKitView::keyPressed (const juce::KeyPress& key)
{
    // Developer toggle shortcut: Ctrl + Shift + E or Cmd + Shift + E
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown()
        && (key.getKeyCode() == 'E' || key.getKeyCode() == 'e'))
    {
        setEditMode (! editMode);
        return true;
    }

    if (! editMode || selectedPiece == KitPiece::count)
        return false;

    const float step = key.getModifiers().isShiftDown() ? 10.0f : 1.0f;
    const float W = static_cast<float> (getWidth());
    const float H = static_cast<float> (getHeight());
    const float scale = std::max (0.01f, std::min (W / 880.0f, H / 620.0f));
    const float normStep = step / scale;

    auto& c = coords[static_cast<size_t> (selectedPiece)];

    if (key.isKeyCode (juce::KeyPress::leftKey))
    {
        c.normX -= normStep;
    }
    else if (key.isKeyCode (juce::KeyPress::rightKey))
    {
        c.normX += normStep;
    }
    else if (key.isKeyCode (juce::KeyPress::upKey))
    {
        c.normY -= normStep;
    }
    else if (key.isKeyCode (juce::KeyPress::downKey))
    {
        c.normY += normStep;
    }
    else if (key.getTextCharacter() == '[' || key.getTextCharacter() == '-')
    {
        c.normW = std::clamp (c.normW - 4.0f, 40.0f, 650.0f);
    }
    else if (key.getTextCharacter() == ']' || key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
    {
        c.normW = std::clamp (c.normW + 4.0f, 40.0f, 650.0f);
    }
    else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
    {
        saveLayoutToFile();
        return true;
    }
    else
    {
        return false;
    }

    hasLayout = false;
    layout();
    repaint();
    return true;
}

void DrumKitView::mouseMove (const juce::MouseEvent& e)
{
    if (editMode)
    {
        if (selectedPiece != KitPiece::count)
        {
            const auto& p = pieces[static_cast<size_t> (selectedPiece)];
            const auto handleBounds = juce::Rectangle<float> (p.bounds.getRight() - 22.0f,
                                                               p.bounds.getBottom() - 22.0f,
                                                               24.0f, 24.0f);
            if (handleBounds.contains (e.position))
            {
                setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor);
                return;
            }
        }

        const auto piece = pieceAt (e.position);
        if (piece != hoveredPiece)
        {
            hoveredPiece = piece;
            setMouseCursor (piece != KitPiece::count ? juce::MouseCursor::DraggingHandCursor
                                                     : juce::MouseCursor::NormalCursor);
            repaint();
        }
        return;
    }

    const auto piece = pieceAt (e.position);
    if (piece != hoveredPiece)
    {
        hoveredPiece = piece;
        setMouseCursor (piece != KitPiece::count ? juce::MouseCursor::PointingHandCursor
                                                 : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DrumKitView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredPiece != KitPiece::count)
    {
        hoveredPiece = KitPiece::count;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DrumKitView::timerCallback()
{
    if (! isVisible())
        return;

    juce::Rectangle<int> dirtyRect;
    constexpr float dt = 1.0f / 60.0f;

    for (size_t i = 0; i < static_cast<size_t> (KitPiece::count); ++i)
    {
        auto& p = pieces[i];
        bool changed = false;

        if (p.flash > 0.001f)
        {
            p.flash = std::max (0.0f, p.flash - dt / 0.22f); // ~220ms smooth decay
            if (p.flash <= 0.001f)
                p.flash = 0.0f;
            changed = true;
        }
        if (p.squash > 0.001f)
        {
            p.squash = std::max (0.0f, p.squash - dt / 0.15f);
            if (p.squash <= 0.001f)
                p.squash = 0.0f;
            changed = true;
        }

        if (changed)
            dirtyRect = dirtyRect.getUnion (dirtyBoundsForPiece (static_cast<KitPiece> (i)));
    }

    if (toastAlpha > 0.001f)
    {
        toastAlpha = std::max (0.0f, toastAlpha - dt / 2.0f); // fade out over 2s
        if (toastAlpha <= 0.001f)
            toastAlpha = 0.0f;
        const int W = getWidth();
        const int H = getHeight();
        const juce::Rectangle<int> toastRect ((W - 320) / 2 - 6, H - 56, 332, 36);
        dirtyRect = dirtyRect.getUnion (toastRect.getIntersection (getLocalBounds()));
    }

    if (! dirtyRect.isEmpty())
        repaint (dirtyRect);
}

void DrumKitView::setTheme (const Theme& newTheme)
{
    const bool darkChanged = (theme.isDark != newTheme.isDark);
    const bool bgChanged = (theme.background != newTheme.background);
    theme = newTheme;
    if (darkChanged || bgChanged)
    {
        hasLayout = false;
        layout();
    }
    repaint();
}

void DrumKitView::resized()
{
    hasLayout = false;
    layout();
}

void DrumKitView::rebuildKitOutline()
{
    if (! theme.isDark)
    {
        unifiedKitOutlineImage = {};
        return;
    }

    const int W = getWidth();
    const int H = getHeight();
    if (W <= 50 || H <= 50)
        return;

    juce::Image composite (juce::Image::ARGB, W, H, true);
    {
        juce::Graphics cg (composite);
        const KitPiece renderOrder[] = {
            KitPiece::kick,
            KitPiece::crash1,
            KitPiece::hihat,
            KitPiece::snare,
            KitPiece::tom1,
            KitPiece::tom2,
            KitPiece::ride,
            KitPiece::crash2,
            KitPiece::floorTom
        };

        for (auto piece : renderOrder)
        {
            const auto& p = pieces[static_cast<size_t> (piece)];
            if (p.scaledImage.isValid() && ! p.bounds.isEmpty())
                cg.drawImageAt (p.scaledImage, juce::roundToInt (p.bounds.getX()), juce::roundToInt (p.bounds.getY()));
            else if (p.image.isValid() && ! p.bounds.isEmpty())
                cg.drawImage (p.image, p.bounds, juce::RectanglePlacement::stretchToFit);
        }
    }

    std::vector<uint8_t> mask (static_cast<size_t> (W * H), 0);
    {
        juce::Image::BitmapData srcData (composite, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                if (srcData.getPixelColour (x, y).getAlpha() > 25)
                    mask[static_cast<size_t> (y * W + x)] = 1;
            }
        }
    }

    // Circular dilation kernel with smooth anti-aliased edge falloff
    const int R = 6;
    struct KernelPt { int dx, dy; float dist; };
    std::vector<KernelPt> kernel;
    for (int dy = -R; dy <= R; ++dy)
    {
        for (int dx = -R; dx <= R; ++dx)
        {
            float d = std::hypot (static_cast<float> (dx), static_cast<float> (dy));
            if (d <= static_cast<float> (R) + 0.75f)
                kernel.push_back ({ dx, dy, d });
        }
    }

    std::vector<uint8_t> outlineAlpha (static_cast<size_t> (W * H), 0);

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            if (! mask[static_cast<size_t> (y * W + x)])
                continue;

            bool isBoundary = (x == 0 || x == W - 1 || y == 0 || y == H - 1 ||
                               ! mask[static_cast<size_t> (y * W + (x - 1))] ||
                               ! mask[static_cast<size_t> (y * W + (x + 1))] ||
                               ! mask[static_cast<size_t> ((y - 1) * W + x)] ||
                               ! mask[static_cast<size_t> ((y + 1) * W + x)]);

            if (isBoundary)
            {
                for (const auto& pt : kernel)
                {
                    int nx = x + pt.dx;
                    int ny = y + pt.dy;
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H)
                    {
                        float falloff = std::clamp ((static_cast<float> (R) + 0.75f - pt.dist) / 1.0f, 0.0f, 1.0f);
                        uint8_t a = static_cast<uint8_t> (falloff * 255.0f);
                        size_t idx = static_cast<size_t> (ny * W + nx);
                        if (a > outlineAlpha[idx])
                            outlineAlpha[idx] = a;
                    }
                }
            }
            else
            {
                outlineAlpha[static_cast<size_t> (y * W + x)] = 255;
            }
        }
    }

    juce::Image outImg (juce::Image::ARGB, W, H, true);
    {
        juce::Image::BitmapData dstData (outImg, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                uint8_t a = outlineAlpha[static_cast<size_t> (y * W + x)];
                if (a > 0)
                    dstData.setPixelColour (x, y, juce::Colour (0xffffffff).withAlpha (static_cast<float> (a) / 255.0f));
            }
        }
    }

    unifiedKitOutlineImage = outImg;
}

void DrumKitView::rebuildStaticKitCache()
{
    const int W = getWidth();
    const int H = getHeight();
    if (W <= 50 || H <= 50)
    {
        staticKitImage = {};
        return;
    }

    juce::Image cache (juce::Image::ARGB, W, H, false);
    {
        juce::Graphics cg (cache);
        cg.fillAll (theme.background);

        if (theme.isDark && unifiedKitOutlineImage.isValid())
            cg.drawImageAt (unifiedKitOutlineImage, 0, 0);

        const KitPiece renderOrder[] = {
            KitPiece::kick,
            KitPiece::crash1,
            KitPiece::hihat,
            KitPiece::snare,
            KitPiece::tom1,
            KitPiece::tom2,
            KitPiece::ride,
            KitPiece::crash2,
            KitPiece::floorTom
        };

        for (auto piece : renderOrder)
        {
            const auto& p = pieces[static_cast<size_t> (piece)];
            if (p.scaledImage.isValid() && ! p.bounds.isEmpty())
            {
                cg.drawImageAt (p.scaledImage,
                                juce::roundToInt (p.bounds.getX()),
                                juce::roundToInt (p.bounds.getY()));
            }
        }
    }

    staticKitImage = cache;
}

void DrumKitView::layout()
{
    if (hasLayout)
        return;

    const float W = static_cast<float> (getWidth());
    const float H = static_cast<float> (getHeight());

    if (W <= 50.0f || H <= 50.0f)
        return;

    const float scale = std::min (W / 880.0f, H / 620.0f);
    const float cx = W * 0.50f;
    const float gy = H * 0.94f;

    for (size_t i = 0; i < static_cast<size_t> (KitPiece::count); ++i)
    {
        auto& p = pieces[i];
        if (! p.image.isValid())
            continue;

        const float aspect = static_cast<float> (p.image.getHeight()) / static_cast<float> (p.image.getWidth());
        const float width = coords[i].normW * scale;
        const float height = width * aspect;
        const float x = cx + coords[i].normX * scale;
        const float y = gy + coords[i].normY * scale;

        p.bounds = juce::Rectangle<float> (x, y, width, height);

        const int targetW = std::max (1, juce::roundToInt (width));
        const int targetH = std::max (1, juce::roundToInt (height));
        if (p.scaledImage.getWidth() != targetW || p.scaledImage.getHeight() != targetH)
            p.scaledImage = p.image.rescaled (targetW, targetH, juce::Graphics::mediumResamplingQuality);

        if (p.glowImage.isValid() && p.glowImage.getWidth() > 72 && p.glowImage.getHeight() > 72)
        {
            const float padRatioX = 36.0f / static_cast<float> (p.glowImage.getWidth() - 72);
            const float padRatioY = 36.0f / static_cast<float> (p.glowImage.getHeight() - 72);
            p.glowBounds = p.bounds.expanded (p.bounds.getWidth() * padRatioX,
                                              p.bounds.getHeight() * padRatioY);
            const int glowW = std::max (1, juce::roundToInt (p.glowBounds.getWidth()));
            const int glowH = std::max (1, juce::roundToInt (p.glowBounds.getHeight()));
            if (p.scaledGlowImage.getWidth() != glowW || p.scaledGlowImage.getHeight() != glowH)
                p.scaledGlowImage = p.glowImage.rescaled (glowW, glowH, juce::Graphics::mediumResamplingQuality);
        }
        else
        {
            p.glowBounds = p.bounds;
        }
    }

    // Top Header Buttons
    const float topY = 12.0f;
    editToggleBtnBounds = {};
    if (editMode)
    {
        exitBtnBounds  = juce::Rectangle<float> (W - 80.0f, topY, 70.0f, 26.0f);
        resetBtnBounds = juce::Rectangle<float> (W - 160.0f, topY, 72.0f, 26.0f);
        copyBtnBounds  = juce::Rectangle<float> (W - 262.0f, topY, 94.0f, 26.0f);
        saveBtnBounds  = juce::Rectangle<float> (W - 378.0f, topY, 108.0f, 26.0f);
    }
    else
    {
        saveBtnBounds = {};
        copyBtnBounds = {};
        resetBtnBounds = {};
        exitBtnBounds = {};
    }

    hasLayout = true;
    rebuildKitOutline();
    rebuildStaticKitCache();
}

void DrumKitView::paint (juce::Graphics& g)
{
    layout();

    const float W = static_cast<float> (getWidth());
    const float H = static_cast<float> (getHeight());

    bool anyAnimating = false;
    for (const auto& p : pieces)
    {
        if (p.flash > 0.001f || p.squash > 0.001f)
        {
            anyAnimating = true;
            break;
        }
    }

    // Fast path: when stationary in play mode, blit the pre-composited static kit cache directly
    if (! editMode && ! anyAnimating && staticKitImage.isValid())
    {
        g.drawImageAt (staticKitImage, 0, 0);
    }
    else
    {
        g.fillAll (theme.background);

        // 1. Blueprint Grid Overlay in Edit Mode
        if (editMode)
        {
            g.setColour (juce::Colour (0xff203048).withAlpha (0.45f));
            g.fillRect (getLocalBounds().toFloat());

            g.setColour (juce::Colours::cyan.withAlpha (0.06f));
            constexpr float gridSize = 24.0f;
            for (float x = 0; x < W; x += gridSize)
                g.drawVerticalLine (static_cast<int> (x), 0.0f, H);
            for (float y = 0; y < H; y += gridSize)
                g.drawHorizontalLine (static_cast<int> (y), 0.0f, W);

            // Center lines
            g.setColour (juce::Colours::cyan.withAlpha (0.22f));
            g.drawVerticalLine (static_cast<int> (W * 0.5f), 0.0f, H);
            g.drawHorizontalLine (static_cast<int> (H * 0.94f), 0.0f, W);
        }

        // 2. Render unified drumkit white contour in Dark Theme
        if (theme.isDark && unifiedKitOutlineImage.isValid())
        {
            g.drawImageAt (unifiedKitOutlineImage, 0, 0);
        }

        // 3. Render back-to-front Z-order matching user layer stack:
        // kick -> crash_1 -> hihat -> snare -> tom_1 -> tom_2 -> ride -> crash_2 -> f_tom
        const KitPiece renderOrder[] = {
            KitPiece::kick,
            KitPiece::crash1,
            KitPiece::hihat,
            KitPiece::snare,
            KitPiece::tom1,
            KitPiece::tom2,
            KitPiece::ride,
            KitPiece::crash2,
            KitPiece::floorTom
        };

        const auto clipBounds = g.getClipBounds();

        for (auto piece : renderOrder)
        {
            const auto& p = pieces[static_cast<size_t> (piece)];
            if (! p.image.isValid() || p.bounds.isEmpty())
                continue;

            const auto pieceClip = (p.glowBounds.isEmpty() ? p.bounds : p.glowBounds.getUnion (p.bounds))
                                       .expanded (6.0f)
                                       .getSmallestIntegerContainer();
            if (! clipBounds.intersects (pieceClip))
                continue;

            // Radiant orange halo glow behind the piece when hit (using pre-scaled glow cache)
            if (p.flash > 0.001f)
            {
                g.setOpacity (std::clamp (p.flash, 0.0f, 1.0f));
                if (p.scaledGlowImage.isValid())
                {
                    g.drawImageAt (p.scaledGlowImage,
                                   juce::roundToInt (p.glowBounds.getX()),
                                   juce::roundToInt (p.glowBounds.getY()));
                }
                else if (p.glowImage.isValid())
                {
                    g.drawImage (p.glowImage, p.glowBounds, juce::RectanglePlacement::stretchToFit);
                }
                g.setOpacity (1.0f);
            }

            // Squash/pulse on impact, or 1:1 fast blit when stationary
            if (p.squash > 0.001f)
            {
                const float pulseScale = 1.0f + p.squash * 0.035f;
                const auto drawBounds = p.bounds.withSizeKeepingCentre (p.bounds.getWidth() * pulseScale,
                                                                        p.bounds.getHeight() * pulseScale);
                g.drawImage (p.scaledImage.isValid() ? p.scaledImage : p.image,
                             drawBounds, juce::RectanglePlacement::stretchToFit);
            }
            else if (p.scaledImage.isValid())
            {
                g.drawImageAt (p.scaledImage,
                               juce::roundToInt (p.bounds.getX()),
                               juce::roundToInt (p.bounds.getY()));
            }
            else
            {
                g.drawImage (p.image, p.bounds, juce::RectanglePlacement::stretchToFit);
            }
        }
    }

    // 3. Edit Mode overlays (selection, bounding box, handles, HUD)
    if (editMode)
    {
        // Outline for hovered piece (if not selected)
        if (hoveredPiece != KitPiece::count && hoveredPiece != selectedPiece)
        {
            const auto& hp = pieces[static_cast<size_t> (hoveredPiece)];
            g.setColour (juce::Colours::cyan.withAlpha (0.6f));
            g.drawRect (hp.bounds, 1.5f);

            g.setFont (uiFont (12.0f));
            g.drawText (pieceDisplayName (hoveredPiece),
                        hp.bounds.getX(), hp.bounds.getY() - 16.0f, hp.bounds.getWidth(), 16.0f,
                        juce::Justification::centredLeft, false);
        }

        // Active selection box & resize handle for selected piece
        if (selectedPiece != KitPiece::count)
        {
            const auto& sp = pieces[static_cast<size_t> (selectedPiece)];
            const auto b = sp.bounds;

            g.setColour (theme.accent);
            g.drawRect (b, 2.0f);

            // 4 corner handles
            constexpr float cs = 8.0f;
            g.fillRect (b.getX() - cs * 0.5f, b.getY() - cs * 0.5f, cs, cs);
            g.fillRect (b.getRight() - cs * 0.5f, b.getY() - cs * 0.5f, cs, cs);
            g.fillRect (b.getX() - cs * 0.5f, b.getBottom() - cs * 0.5f, cs, cs);
            g.fillRect (b.getRight() - cs * 0.5f, b.getBottom() - cs * 0.5f, cs, cs);

            // Bottom-right resize grip
            const auto handleRect = juce::Rectangle<float> (b.getRight() - 22.0f, b.getBottom() - 22.0f, 22.0f, 22.0f);
            g.setColour (theme.accent.withAlpha (0.85f));
            g.fillRect (handleRect);
            g.setColour (juce::Colours::black);
            g.drawLine (handleRect.getX() + 6.0f, handleRect.getBottom() - 4.0f, handleRect.getRight() - 4.0f, handleRect.getY() + 6.0f, 2.0f);
            g.drawLine (handleRect.getX() + 12.0f, handleRect.getBottom() - 4.0f, handleRect.getRight() - 4.0f, handleRect.getY() + 12.0f, 2.0f);

            // Floating HUD Pill
            const auto& c = coords[static_cast<size_t> (selectedPiece)];
            const juce::String hudText = pieceDisplayName (selectedPiece).toUpperCase()
                + "  |  X: " + juce::String (c.normX, 1)
                + "  Y: " + juce::String (c.normY, 1)
                + "  W: " + juce::String (c.normW, 1);

            g.setFont (uiFont (12.0f));
            const float hudW = 260.0f;
            const float hudH = 22.0f;
            const float hudX = juce::jlimit (10.0f, W - hudW - 10.0f, b.getCentreX() - hudW * 0.5f);
            const float hudY = (b.getY() > 40.0f) ? b.getY() - hudH - 6.0f : b.getBottom() + 6.0f;

            g.setColour (juce::Colour (0xee141416));
            g.fillRoundedRectangle (hudX, hudY, hudW, hudH, 4.0f);
            g.setColour (theme.accent);
            g.drawRoundedRectangle (hudX, hudY, hudW, hudH, 4.0f, 1.2f);
            g.drawText (hudText, hudX, hudY, hudW, hudH, juce::Justification::centred, false);
        }

        // Top info banner
        g.setColour (juce::Colour (0xcc18181c));
        g.fillRoundedRectangle (12.0f, 12.0f, 430.0f, 26.0f, 4.0f);
        g.setColour (theme.accent);
        g.drawRoundedRectangle (12.0f, 12.0f, 430.0f, 26.0f, 4.0f, 1.0f);
        g.setFont (uiFont (11.5f));
        g.drawText ("✎ WYSIWYG EDIT: Drag piece | Corner / Wheel = Resize | Arrows = Nudge",
                    18.0f, 12.0f, 420.0f, 26.0f, juce::Justification::centredLeft, false);

        // Header Action buttons
        auto drawButton = [&] (const juce::Rectangle<float>& r, const juce::String& text, bool primary)
        {
            g.setColour (primary ? theme.accent : juce::Colour (0xdd222228));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (primary ? juce::Colours::black : theme.ink);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
            g.setFont (uiFont (11.0f));
            g.drawText (text, r, juce::Justification::centred, false);
        };

        drawButton (saveBtnBounds, "SAVE LAYOUT", true);
        drawButton (copyBtnBounds, "COPY C++", false);
        drawButton (resetBtnBounds, "RESET", false);
        drawButton (exitBtnBounds, "✕ DONE", false);
    }

    // 4. Toast notification pill
    if (toastAlpha > 0.001f && toastText.isNotEmpty())
    {
        g.setOpacity (toastAlpha);
        const float tW = 320.0f;
        const float tH = 28.0f;
        const float tX = (W - tW) * 0.5f;
        const float tY = H - 52.0f;

        g.setColour (juce::Colour (0xee0f2818));
        g.fillRoundedRectangle (tX, tY, tW, tH, 6.0f);
        g.setColour (juce::Colours::lightgreen);
        g.drawRoundedRectangle (tX, tY, tW, tH, 6.0f, 1.4f);
        g.setFont (uiFont (12.0f));
        g.drawText (toastText, tX, tY, tW, tH, juce::Justification::centred, false);
        g.setOpacity (1.0f);
    }
}

} // namespace glorp
