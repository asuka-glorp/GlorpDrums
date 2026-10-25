#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OpenRasterReader.h"
#include "GlorpKitProject.h"
#include "../ui/Theme.h"
#include <functional>
#include <vector>
#include <string>
#include <cctype>

namespace glorp
{

inline void drawDialogBox (juce::Graphics& g, juce::Rectangle<float> r,
                           juce::Colour fillCol, juce::Colour strokeCol,
                           float corner = 5.0f, float strokeW = 1.8f, unsigned seed = 1)
{
    g.setColour (fillCol);
    g.fillRoundedRectangle (r, corner);
    drawSketchRect (g, r, strokeCol, strokeW, seed);
}

struct OraLayerAssignment
{
    int layerIndex = 0;
    // 0 = Ignore / Do not import
    // 1 = Create New Kit Piece
    // 2 = Assign to existing piece
    int targetAction = 1;
    int targetPieceIdx = -1;
    std::string newPieceName;
};

struct OraImportResult
{
    bool applyStageCoordinates = true;
    std::vector<OraLayerAssignment> assignments;
};

//==============================================================================
/** Modal dialog overlay for mapping OpenRaster (.ora) layers to drumkit instruments. */
class OpenRasterModalOverlay : public juce::Component
{
public:
    using ConfirmCallback = std::function<void(const OraImportResult&, const OraDocument&)>;
    using CancelCallback  = std::function<void()>;

    OpenRasterModalOverlay (const OraDocument& inDoc,
                            const juce::String& inFileName,
                            const std::vector<EditablePiece>& inPieces,
                            Theme inTheme,
                            ConfirmCallback onConfirmCb,
                            CancelCallback onCancelCb)
        : doc (inDoc),
          fileName (inFileName),
          pieces (inPieces),
          theme (inTheme),
          onConfirm (std::move (onConfirmCb)),
          onCancel (std::move (onCancelCb))
    {
        setWantsKeyboardFocus (true);

        toggleCoords.setButtonText ("Position pieces according to .ORA stage layout (X, Y, Size)");
        toggleCoords.setToggleState (true, juce::dontSendNotification);
        toggleCoords.setColour (juce::ToggleButton::textColourId, theme.ink);
        toggleCoords.setColour (juce::ToggleButton::tickColourId, theme.accent);
        addAndMakeVisible (toggleCoords);

        btnCancel.setButtonText ("Cancel");
        btnCancel.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible (btnCancel);

        btnImport.setButtonText ("Import Selected Layers");
        btnImport.onClick = [this] { confirmAndApply(); };
        addAndMakeVisible (btnImport);

        btnClose.setButtonText ("X");
        btnClose.onClick = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible (btnClose);

        // Build layer row entries inside the scrollable content container
        contentComp = std::make_unique<juce::Component>();
        for (size_t i = 0; i < doc.layers.size(); ++i)
        {
            const auto& layer = doc.layers[i];
            auto row = std::make_unique<LayerRow> (static_cast<int> (i), layer, pieces, theme);
            contentComp->addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        viewport.setViewedComponent (contentComp.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    void confirmAndApply()
    {
        OraImportResult res;
        res.applyStageCoordinates = toggleCoords.getToggleState();

        for (size_t i = 0; i < rows.size(); ++i)
        {
            OraLayerAssignment a;
            a.layerIndex = static_cast<int> (i);
            const int selId = rows[i]->comboTarget.getSelectedId();

            if (selId == 1)
            {
                a.targetAction = 0; // Ignore
            }
            else if (selId == 2)
            {
                a.targetAction = 1; // Create new piece
                a.newPieceName = doc.layers[i].name;
            }
            else if (selId >= 100)
            {
                a.targetAction = 2; // Assign to existing piece
                a.targetPieceIdx = selId - 100;
            }
            res.assignments.push_back (a);
        }

        if (onConfirm)
            onConfirm (res, doc);
    }

    void paint (juce::Graphics& g) override
    {
        // Dark translucent scrim background
        g.fillAll (juce::Colour (0xbb0f1118));

        // Center modal card
        drawDialogBox (g, cardBounds, theme.panel, theme.ink, 8.0f, 2.5f, 701);

        // Title and Subtitle
        g.setColour (theme.ink);
        g.setFont (displayFont (17.0f));
        g.drawText ("IMPORT OPENRASTER (.ORA) LAYERED GRAPHICS",
                    cardBounds.getX() + 20.0f, cardBounds.getY() + 14.0f,
                    cardBounds.getWidth() - 60.0f, 24.0f,
                    juce::Justification::centredLeft, false);

        g.setColour (theme.inkSoft);
        g.setFont (uiFont (12.5f));
        juce::String sub = fileName + " — Canvas: " + juce::String (doc.canvasWidth) + " x "
                         + juce::String (doc.canvasHeight) + " px  |  "
                         + juce::String (static_cast<int> (doc.layers.size())) + " Raster Layer(s) Detected";
        g.drawText (sub, cardBounds.getX() + 20.0f, cardBounds.getY() + 38.0f,
                    cardBounds.getWidth() - 60.0f, 18.0f,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const float maxW = std::min (740.0f, static_cast<float> (getWidth()) - 32.0f);
        const float maxH = std::min (580.0f, static_cast<float> (getHeight()) - 32.0f);

        cardBounds = {
            std::round (static_cast<float> (getWidth()) * 0.5f - maxW * 0.5f),
            std::round (static_cast<float> (getHeight()) * 0.5f - maxH * 0.5f),
            maxW,
            maxH
        };

        btnClose.setBounds (static_cast<int> (cardBounds.getRight() - 38.0f),
                            static_cast<int> (cardBounds.getY() + 12.0f),
                            26, 26);

        toggleCoords.setBounds (static_cast<int> (cardBounds.getX() + 20.0f),
                                static_cast<int> (cardBounds.getY() + 64.0f),
                                static_cast<int> (cardBounds.getWidth() - 40.0f),
                                26);

        const int vpY = static_cast<int> (cardBounds.getY() + 96.0f);
        const int vpH = static_cast<int> (cardBounds.getHeight() - 156.0f);
        viewport.setBounds (static_cast<int> (cardBounds.getX() + 16.0f), vpY,
                            static_cast<int> (cardBounds.getWidth() - 32.0f), vpH);

        const int rowH = 54;
        const int totalH = static_cast<int> (rows.size()) * rowH;
        contentComp->setSize (viewport.getWidth() - (totalH > vpH ? 16 : 0), totalH);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            rows[i]->setBounds (0, static_cast<int> (i) * rowH, contentComp->getWidth(), rowH - 4);
        }

        const int by = static_cast<int> (cardBounds.getBottom() - 46.0f);
        btnCancel.setBounds (static_cast<int> (cardBounds.getRight() - 280.0f), by, 100, 32);
        btnImport.setBounds (static_cast<int> (cardBounds.getRight() - 170.0f), by, 150, 32);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onCancel) onCancel();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            confirmAndApply();
            return true;
        }
        return false;
    }

private:
    //==========================================================================
    struct LayerRow : public juce::Component
    {
        int layerIdx = 0;
        OraLayer layer;
        Theme theme;
        juce::ComboBox comboTarget;

        LayerRow (int inIdx, const OraLayer& inLayer, const std::vector<EditablePiece>& inPieces, Theme inTheme)
            : layerIdx (inIdx), layer (inLayer), theme (inTheme)
        {
            comboTarget.addItem ("(Do Not Import / Ignore)", 1);
            comboTarget.addItem ("+ Create New Kit Piece: \"" + juce::String (layer.name) + "\"", 2);

            int matchedPieceId = -1;
            const juce::String lNameNorm = juce::String (layer.name).toLowerCase().trim();

            for (size_t p = 0; p < inPieces.size(); ++p)
            {
                const int itemId = 100 + static_cast<int> (p);
                comboTarget.addItem ("Assign to Piece: " + juce::String (inPieces[p].name), itemId);

                const juce::String pNameNorm = juce::String (inPieces[p].name).toLowerCase().trim();
                const juce::String pIdNorm   = juce::String (inPieces[p].id).toLowerCase().trim();
                if (matchedPieceId < 0 && (lNameNorm == pNameNorm || lNameNorm == pIdNorm
                                           || lNameNorm.contains (pNameNorm) || pNameNorm.contains (lNameNorm)))
                {
                    matchedPieceId = itemId;
                }
            }

            // Default selection: preselect existing piece if matched, otherwise Create New
            if (matchedPieceId > 0)
                comboTarget.setSelectedId (matchedPieceId, juce::dontSendNotification);
            else
                comboTarget.setSelectedId (2, juce::dontSendNotification);

            addAndMakeVisible (comboTarget);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            drawDialogBox (g, b, theme.trough, theme.inkSoft, 4.0f, 1.2f, static_cast<unsigned> (720 + layerIdx));

            // Thumbnail
            juce::Rectangle<float> thumbR { b.getX() + 6.0f, b.getY() + 4.0f, 42.0f, 42.0f };
            if (layer.thumbnail.isValid())
            {
                g.drawImageWithin (layer.thumbnail,
                                   static_cast<int> (thumbR.getX()), static_cast<int> (thumbR.getY()),
                                   static_cast<int> (thumbR.getWidth()), static_cast<int> (thumbR.getHeight()),
                                   juce::RectanglePlacement::centred);
            }
            else
            {
                g.setColour (theme.inkSoft.withAlpha (0.25f));
                g.fillRect (thumbR);
            }
            g.setColour (theme.inkSoft);
            g.drawRect (thumbR, 1.0f);

            // Layer Name & Dimensions
            g.setColour (theme.ink);
            g.setFont (displayFont (13.0f));
            g.drawText (layer.name, thumbR.getRight() + 10.0f, b.getY() + 6.0f, 220.0f, 18.0f,
                        juce::Justification::centredLeft, true);

            g.setColour (theme.inkSoft);
            g.setFont (uiFont (11.5f));
            juce::String dim = "Pos: (" + juce::String (layer.x) + ", " + juce::String (layer.y) + ")  Size: "
                             + juce::String (layer.width) + "x" + juce::String (layer.height);
            g.drawText (dim, thumbR.getRight() + 10.0f, b.getY() + 25.0f, 220.0f, 16.0f,
                        juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            const int comboW = std::min (300, getWidth() - 290);
            comboTarget.setBounds (getWidth() - comboW - 10, (getHeight() - 28) / 2, comboW, 28);
        }
    };

    OraDocument doc;
    juce::String fileName;
    std::vector<EditablePiece> pieces;
    Theme theme;
    ConfirmCallback onConfirm;
    CancelCallback  onCancel;

    juce::Rectangle<float> cardBounds;
    juce::ToggleButton toggleCoords;
    juce::TextButton btnClose;
    juce::TextButton btnCancel;
    juce::TextButton btnImport;

    juce::Viewport viewport;
    std::unique_ptr<juce::Component> contentComp;
    std::vector<std::unique_ptr<LayerRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenRasterModalOverlay)
};

} // namespace glorp
