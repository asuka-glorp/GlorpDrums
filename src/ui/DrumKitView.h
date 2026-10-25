/*
    DrumKitView.h - drum kit view rendering bundled PNG instrument assets with orange hit glow.
*/

#pragma once

#include "../core/DrumKit.h"
#include "Theme.h"

#include <array>
#include <functional>
#include <vector>

namespace glorp
{

enum class KitPiece
{
    kick,
    snare,
    tom1,
    tom2,
    floorTom,
    hihat,
    ride,
    crash1,
    crash2,
    count
};

class DrumKitView : public juce::Component,
                    private juce::Timer
{
public:
    DrumKitView();

    void setInstrumentNames (const std::vector<juce::String>& names);

    /// Applies custom PNG graphics, hit glow color, 2D canvas coordinates, and Z-order layering from a loaded DrumKit.
    void applyKitVisuals (const DrumKit& kit);

    void setHitGlowColour (juce::Colour colour);
    juce::Colour getHitGlowColour() const noexcept { return hitGlowColour; }
    static juce::Colour parseHexColour (const std::string& hex, juce::Colour fallback = juce::Colour (0xffff8200));

    /// Fires a hit animation on the piece that best matches instrumentName.
    void triggerHit (const juce::String& instrumentName, float velocity);
    void triggerHitForPiece (KitPiece piece, float velocity);

    /// Fires a hit animation on the dynamic piece that owns the given sampler index.
    /// Returns true if a matching dynamic piece was found and animated.
    bool triggerHitForSamplerIndex (int samplerIndex, float velocity);

    /// Which piece an instrument name most likely refers to.
    static KitPiece pieceForName (const juce::String& name);

    /// Fired when a piece is clicked to trigger sound (at max velocity) and select in settings panel.
    std::function<void (KitPiece, float velocity)> onPieceHit;
    std::function<void (KitPiece)> onPieceClicked;
    std::function<void (int dynamicIndex, const juce::String& pieceName, float velocity)> onDynamicPieceHit;
    std::function<void (int dynamicIndex, const juce::String& pieceName)> onDynamicPieceClicked;

    bool isEditMode() const noexcept { return editMode; }
    void setEditMode (bool enabled);
    void setSelectedPiece (KitPiece piece) { selectedPiece = piece; repaint(); }

    void setTheme (const Theme& newTheme);

    KitPiece pieceAt (juce::Point<float> pos) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    struct PieceCoords
    {
        float normX = 0.0f;
        float normY = 0.0f;
        float normW = 100.0f;
    };

    static const char* pieceIdentifier (KitPiece piece);
    static juce::String pieceDisplayName (KitPiece piece);

    void loadLayoutFromFile();
    bool saveLayoutToFile();
    void resetToDefaultLayout();
    juce::String generateCppCode() const;

    /// Returns the dirty-rectangle screen bounds for a single kit piece (including its glow halo).
    juce::Rectangle<int> dirtyBoundsForPiece (KitPiece piece) const noexcept;

    /// Diagnostic accessors for verifying pre-scaled bitmap and static composite caches.
    bool isPieceScaledCacheValid (KitPiece piece) const noexcept;
    bool isStaticKitCacheValid() const noexcept { return staticKitImage.isValid(); }
    juce::Image getPieceImage (KitPiece piece) const noexcept
    {
        if (piece != KitPiece::count)
            return pieces[static_cast<size_t> (piece)].image;
        return {};
    }

    struct DynamicPiece
    {
        std::string id;
        std::string name;
        std::string graphic;
        std::vector<int> samplerIndices;
        std::vector<std::string> instrumentNames;
        PieceCoords coords;
        int zOrder = 0;
        juce::Rectangle<float> bounds;
        juce::Rectangle<float> glowBounds;
        juce::Image image;
        juce::Image glowImage;
        juce::Image scaledImage;
        juce::Image scaledGlowImage;
        float flash = 0.0f;
        float squash = 0.0f;
        KitPiece mappedClassicPiece = KitPiece::count;
    };

    bool hasDynamicPieces() const noexcept { return ! dynamicPieces.empty(); }
    size_t getNumDynamicPieces() const noexcept { return dynamicPieces.size(); }
    const std::vector<DynamicPiece>& getDynamicPieces() const noexcept { return dynamicPieces; }
    int dynamicPieceAt (juce::Point<float> pos) const;
    void triggerHitForDynamicPiece (int index, float velocity);
    juce::Image getDefaultImageForGraphic (const std::string& graphic, KitPiece fallbackPiece);

private:
    struct Piece
    {
        juce::Rectangle<float> bounds;
        juce::Rectangle<float> glowBounds;
        juce::Image image;
        juce::Image glowImage;
        juce::Image scaledImage;
        juce::Image scaledGlowImage;
        float flash = 0.0f;
        float squash = 0.0f;
    };

    void loadPieceImages();
    void layout();
    void rebuildStaticKitCache();

    std::array<Piece, static_cast<size_t> (KitPiece::count)> pieces;
    std::array<PieceCoords, static_cast<size_t> (KitPiece::count)> coords;
    std::vector<DynamicPiece> dynamicPieces;
    std::vector<juce::String> instrumentNames;

    KitPiece hoveredPiece = KitPiece::count;
    KitPiece selectedPiece = KitPiece::count;

    bool editMode = false;
    bool isDraggingPiece = false;
    bool isResizingPiece = false;
    juce::Point<float> dragStartMouse;
    float dragStartNormX = 0.0f;
    float dragStartNormY = 0.0f;
    float dragStartNormW = 0.0f;

    juce::String toastText;
    float toastAlpha = 0.0f;

    juce::Rectangle<float> editToggleBtnBounds;
    juce::Rectangle<float> saveBtnBounds;
    juce::Rectangle<float> copyBtnBounds;
    juce::Rectangle<float> resetBtnBounds;
    juce::Rectangle<float> exitBtnBounds;

    Theme theme = Theme::sketch();
    juce::Colour hitGlowColour { 0xffff8200 };
    std::array<int, static_cast<size_t> (KitPiece::count)> pieceZOrder {{ 1, 4, 5, 6, 9, 3, 7, 2, 8 }};
    bool hasLayout = false;

    juce::Image unifiedKitOutlineImage;
    juce::Image staticKitImage;
    void rebuildKitOutline();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitView)
};

} // namespace glorp
