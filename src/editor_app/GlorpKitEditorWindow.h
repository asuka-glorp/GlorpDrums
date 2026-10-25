/*
    GlorpKitEditorWindow.h - Standalone graphical .glorpkit Studio editor UI & audio preview engine.
    Follows the hand-drawn comic/sketch aesthetic of GlorpDrums (Theme::sketch() & Theme::dark()).
*/

#pragma once

#include "GlorpKitProject.h"
#include "OpenRasterReader.h"
#include "OpenRasterImportDialog.h"
#include "../core/Sampler.h"
#include "../core/SampleCache.h"
#include "../ui/Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace glorp
{

class GlorpKitEditorComponent : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                public juce::AudioIODeviceCallback,
                                public juce::MidiInputCallback,
                                private juce::Timer
{
public:
    GlorpKitEditorComponent (bool enableAudioDevice = true);
    ~GlorpKitEditorComponent() override;

    // -- Project & State Accessors (also used by automated verification tests) --
    GlorpKitProject& getProject() noexcept { return project; }
    const GlorpKitProject& getProject() const noexcept { return project; }

    void reloadProjectIntoEngine();
    bool openKitFile (const juce::File& file);
    bool saveGlorpKitFile (const juce::File& targetFile);

    void setDarkTheme (bool dark);
    bool isDarkTheme() const noexcept { return theme.isDark; }

    void setHitGlowColourHex (const std::string& hexColor);
    juce::Colour getHitGlowColour() const noexcept { return hitGlowColour; }

    void setTutorialVisible (bool visible, int step = 0);
    bool isTutorialVisible() const noexcept { return tutorialActive; }
    int  getTutorialStep() const noexcept { return tutorialStep; }
    void nextTutorialStep();
    void prevTutorialStep();

    void selectPiece (int pieceIndex, bool audition = false);
    void selectZone (int zoneIndex, bool audition = false);
    void selectHit (int hitIndex, bool audition = false);

    void setPreviewMode (bool preview) { previewKitMode = preview; repaint(); }
    bool isPreviewMode() const noexcept { return previewKitMode; }

    bool isOraOverlayActive() const noexcept { return oraOverlay != nullptr; }
    OpenRasterModalOverlay* getOraOverlay() noexcept { return oraOverlay.get(); }
    void dismissOraOverlay() { oraOverlay.reset(); repaint(); }
    void importOpenRasterFile (const juce::File& file);

    struct MatrixCellHit
    {
        int hitIndex = -1;
        int velLayer = 0;
        int rrIndex  = 0;
        juce::Rectangle<float> bounds;
    };

    // -- juce::Component -----------------------------------------------------
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    // -- juce::FileDragAndDropTarget -----------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // -- juce::AudioIODeviceCallback & MidiInputCallback ---------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    void timerCallback() override;

    // -- Core Model & Audio Audition Engine ----------------------------------
    GlorpKitProject project;
    Sampler sampler;
    juce::AudioDeviceManager deviceManager;
    bool audioDeviceActive = false;

    Theme theme = Theme::sketch();
    juce::Colour hitGlowColour { 0xffff8200 };

    // -- Selection State -----------------------------------------------------
    int selectedPieceIdx = 4; // Default to Snare
    int selectedZoneIdx  = 0;
    int selectedHitIdx   = 0;
    bool midiLearnArmed  = false;

    // -- Canvas & Sprite Cache -----------------------------------------------
    struct CachedSprite
    {
        juce::Image rawImage;
        juce::Image glowImage;
        juce::Rectangle<float> canvasBounds;
        float flash = 0.0f;
    };
    std::map<std::string, juce::Image> builtinImages;
    std::vector<CachedSprite> pieceSprites;
    std::vector<float> selectedHitWaveform;

    bool snapToGrid = true;
    bool previewKitMode = false;
    bool isDraggingCanvasPiece = false;
    bool isResizingCanvasPiece = false;
    bool isDraggingLayerRow = false;
    int  dragLayerPieceIndex = -1;
    float dragLayerStartMouseY = 0.0f;
    juce::Point<float> dragStartPos;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float dragStartW = 160.0f;

    juce::String statusBannerText = "Ready — Drag pieces on the 2D canvas or drop WAV samples into the matrix";
    float statusBannerAlpha = 0.0f;

    // -- Guided First-Time Tutorial State ------------------------------------
    bool tutorialActive = false;
    int  tutorialStep   = 0; // 0..4 (5 steps)
    juce::Rectangle<float> tutorialCardBounds;
    juce::Rectangle<float> tutorialPrevBtnBounds;
    juce::Rectangle<float> tutorialNextBtnBounds;
    juce::Rectangle<float> tutorialSkipBtnBounds;

    // -- Interactive Hit-Test Geometries -------------------------------------
    // Top Header
    juce::Rectangle<float> headerBounds;
    juce::Rectangle<float> btnNewKit, btnOpenKit, btnImportDg, btnImportOra, btnSaveKit;
    juce::Rectangle<float> pillKitMeta, btnGlowColor, btnAudioSettings, btnTour, btnThemeToggle;

    // Left Panel (Z-Layer Stack & Mic Buses)
    juce::Rectangle<float> leftPanelBounds;
    juce::Rectangle<float> btnAddPiece, btnDupPiece, btnDelPiece;
    struct LayerRowHit
    {
        int pieceIndex = 0;
        juce::Rectangle<float> rowBounds;
        juce::Rectangle<float> visBounds;
        juce::Rectangle<float> upBounds;
        juce::Rectangle<float> downBounds;
        juce::Rectangle<float> hitBounds;
    };
    std::vector<LayerRowHit> layerRowHits;
    juce::Rectangle<float> micBoxBounds, btnAddMic;
    struct MicPillHit
    {
        int micIndex = 0;
        juce::Rectangle<float> bounds;
    };
    std::vector<MicPillHit> micPillHits;

    // Center-Top Panel (2D Stage Canvas)
    juce::Rectangle<float> canvasPanelBounds;
    juce::Rectangle<float> canvasViewportBounds;
    juce::Rectangle<float> btnSnapGrid, btnPreviewKit, btnBringFront, btnSendBack, btnResetLayout;

    // Center-Bottom Panel (Articulation Zones & Velocity/RR Matrix)
    juce::Rectangle<float> matrixPanelBounds;
    juce::Rectangle<float> btnAutoPower, btnImportWavs, btnDeleteZone;
    struct ZoneTabHit
    {
        int zoneIndex = -1; // -1 = "+ Add Zone"
        juce::Rectangle<float> bounds;
    };
    std::vector<ZoneTabHit> zoneTabHits;
    std::vector<MatrixCellHit> matrixCellHits;

    // Right Panel (Piece, Zone & Sample Inspector)
    juce::Rectangle<float> rightPanelBounds;
    juce::Rectangle<float> cardPieceBounds, cardZoneBounds, cardSampleBounds;
    juce::Rectangle<float> btnLoadCustomPng, btnBuiltinAsset, btnOutputBus, btnChokeGroup;
    juce::Rectangle<float> pillNormX, pillNormY, pillNormW, pillZOrder;
    juce::Rectangle<float> btnMidiNote, btnAltNote, btnMidiLearn, btnChokeTarget, btnPlaySample;
    juce::Rectangle<float> waveformPreviewBounds;
    struct MicAssignRowHit
    {
        std::string micName;
        juce::Rectangle<float> bounds;
    };
    std::vector<MicAssignRowHit> micAssignHits;

    // Inline Editors for Piece Name / Kit Title
    juce::TextEditor pieceNameEditor;
    juce::TextEditor pieceIdEditor;
    juce::TextEditor zoneNameEditor;
    bool suppressEditorCallbacks = false;

    struct AuditionSynthVoice
    {
        bool active = false;
        float phase = 0.0f;
        float freq = 120.0f;
        float freqDecay = 0.992f;
        float env = 0.0f;
        float envDecay = 0.994f;
        float noiseMix = 0.0f;
        float filterState = 0.0f;
    } synthVoice;
    void startAuditionSynth (int pieceIdx, float velocity);
    void showAudioSettingsDialog();
    void promptRenameMic (int micIndex);
    void promptRenamePiece (int pieceIndex);
    void promptRenameZone (int pieceIndex, int zoneIndex);

    // -- Internal Helpers ----------------------------------------------------
    void loadBuiltinSprites();
    void rebuildPieceSprites();
    void computeCanvasPieceBounds();
    void rebuildSelectedHitWaveform();
    int  canvasPieceAt (juce::Point<float> pt) const;
    void triggerAuditionForPiece (int pieceIdx);
    void triggerAuditionForZone (int pieceIdx, int zoneIdx);
    void showStatusToast (const juce::String& text);
    void loadFirstTimeTutorialPreference();
    void saveFirstTimeTutorialPreference (bool completed);

    void promptOpenKit();
    void promptImportDrumGizmo();
    void promptImportOpenRaster();
    void applyOpenRasterImport (const juce::File& file, const OraDocument& doc, const OraImportResult& res);
    void promptSaveGlorpKit();
    void promptLoadCustomPng();
    void promptImportWavsForCurrentZone (const std::string& targetMic = "close");
    void promptImportWavForSpecificCell (int velLayer, int roundRobin);
    void showMatrixCellContextMenu (const MatrixCellHit& cell);
    void showHitGlowColorMenu();
    void showBuiltinAssetMenu();
    void showOutputBusMenu();
    void showChokeGroupMenu();
    void showMidiNoteMenu();
    void showKitMetadataDialog();

    std::unique_ptr<OpenRasterModalOverlay> oraOverlay;

    void paintHeader (juce::Graphics& g);
    void paintLeftPanel (juce::Graphics& g);
    void paintCanvasPanel (juce::Graphics& g);
    void paintMatrixPanel (juce::Graphics& g);
    void paintRightInspector (juce::Graphics& g);
    void paintGuidedTutorialOverlay (juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlorpKitEditorComponent)
};

} // namespace glorp
