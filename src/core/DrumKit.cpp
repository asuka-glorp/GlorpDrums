#include "DrumKit.h"
#include "GlorpKit.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace glorp
{

namespace
{
    std::string parentDirectory (const std::string& path)
    {
        const fs::path p (path);
        return p.has_parent_path() ? p.parent_path().string() : std::string (".");
    }

    std::string baseName (const std::string& path)
    {
        const fs::path p (path);
        return p.stem().string();
    }

    /// Turns "Tom1_High" or "Tom1 High" into "Tom1 High" for display.
    std::string prettifyName (const std::string& name)
    {
        auto out = name;

        for (auto& c : out)
            if (c == '_')
                c = ' ';

        return out;
    }

    std::string joinPath (const std::string& dir, const std::string& relative)
    {
        if (relative.empty())
            return dir;

        const fs::path r (relative);
        if (r.is_absolute())
            return r.lexically_normal().string();

        return (fs::path (dir) / r).lexically_normal().string();
    }
}

//==============================================================================
std::string DrumKit::displayName() const
{
    if (! title.empty())
        return title;

    return baseName (parentDirectory (xmlPath));
}

std::string Instrument::samplePath (const AudioFileRef& ref) const
{
    if (ref.archiveOffset >= 0 && ! archivePath.empty())
        return "glorpkit://" + archivePath + "#" + std::to_string (ref.archiveOffset)
             + ":" + std::to_string (ref.archiveLength);

    return joinPath (baseDir, ref.file);
}

int DrumKit::indexOfInstrument (const std::string& n) const noexcept
{
    for (size_t i = 0; i < instruments.size(); ++i)
        if (instruments[i].name == n)
            return static_cast<int> (i);

    return -1;
}

int DrumKit::indexOfKitChannel (const std::string& n) const noexcept
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].name == n)
            return static_cast<int> (i);

    return -1;
}

//==============================================================================
bool DrumKit::parseInstrument (Instrument& instrument, const XmlNode& node,
                               const std::string& kitDir, std::string& error)
{
    instrument.name = node.attribute ("name");
    instrument.group = node.attribute ("group");

    const auto fileAttr = node.attribute ("file");

    if (instrument.name.empty())
    {
        error = "instrument has no name";
        return false;
    }

    if (fileAttr.empty())
    {
        error = "instrument '" + instrument.name + "' has no file attribute";
        return false;
    }

    instrument.xmlPath = joinPath (kitDir, fileAttr);
    instrument.baseDir = parentDirectory (instrument.xmlPath);
    instrument.displayName = prettifyName (instrument.name);
    instrument.bleedPiece = pieceForInstrumentName (instrument.name);

    for (auto* cm : node.childrenNamed ("channelmap"))
    {
        ChannelMap m;
        m.in   = cm->attribute ("in");
        m.out  = cm->attribute ("out");
        m.main = cm->attributeBool ("main", false);

        if (cm->hasAttribute ("bleed"))
            m.bleed = cm->attributeFloat ("bleed", 1.0f);
        else if (auto* b = cm->child ("bleed"))
            m.bleed = static_cast<float> (std::atof (b->text.c_str()));

        if (m.in.empty() || m.out.empty())
            continue;

        instrument.channelMaps.push_back (std::move (m));
    }

    if (auto* chokes = node.child ("chokes"))
    {
        for (auto* c : chokes->childrenNamed ("choke"))
        {
            ChokeSpec spec;
            spec.instrument = c->attribute ("instrument");

            if (c->hasAttribute ("choketime"))
            {
                spec.chokeTimeMs = c->attributeFloat ("choketime", 100.0f);
                spec.hasChokeTime = true;
            }

            if (! spec.instrument.empty())
                instrument.chokes.push_back (std::move (spec));
        }
    }

    return true;
}

bool DrumKit::loadInstrumentSamples (Instrument& instrument, std::string& error)
{
    std::string xmlError;
    auto root = parseXmlFile (instrument.xmlPath, xmlError);

    if (root == nullptr)
    {
        error = "could not read instrument XML '" + instrument.xmlPath + "': " + xmlError;
        return false;
    }

    if (root->tag != "instrument")
    {
        error = "'" + instrument.xmlPath + "' is not an instrument XML (root is <" + root->tag + ">)";
        return false;
    }

    // The instrument's own metadata title is nicer than the semantic name.
    if (auto* meta = root->child ("metadata"))
    {
        if (auto* t = meta->child ("title"))
            if (! t->text.empty())
                instrument.displayName = t->text;
    }

    auto* samples = root->child ("samples");

    if (samples == nullptr)
    {
        error = "instrument '" + instrument.name + "' has no <samples> node";
        return false;
    }

    for (auto* sn : samples->childrenNamed ("sample"))
    {
        SampleHit hit;
        hit.name = sn->attribute ("name");
        hit.power = sn->attributeFloat ("power", 1.0f);

        if (hit.name.empty())
            hit.name = "hit" + std::to_string (instrument.samples.size());

        for (auto* an : sn->childrenNamed ("audiofile"))
        {
            AudioFileRef ref;
            ref.channel = an->attribute ("channel");
            ref.file = an->attribute ("file");
            ref.fileChannel = an->attributeInt ("filechannel", 1) - 1; // XML is 1-based

            if (ref.channel.empty() || ref.file.empty())
            {
                warnings.push_back ("instrument '" + instrument.name + "', sample '" + hit.name
                                    + "': skipping <audiofile> with missing channel or file");
                continue;
            }

            if (ref.fileChannel < 0)
                ref.fileChannel = 0;

            hit.audioFiles.push_back (std::move (ref));
        }

        if (hit.audioFiles.empty())
        {
            warnings.push_back ("instrument '" + instrument.name + "', sample '" + hit.name
                                + "' has no usable <audiofile> entries");
            continue;
        }

        instrument.samples.push_back (std::move (hit));
    }

    return true;
}

bool DrumKit::resolveChannels (Instrument& instrument, std::string& error)
{
    instrument.resolvedChannels.assign (instrument.samples.size(), {});

    for (size_t s = 0; s < instrument.samples.size(); ++s)
    {
        auto& hit = instrument.samples[s];

        for (size_t a = 0; a < hit.audioFiles.size(); ++a)
        {
            const auto& ref = hit.audioFiles[a];

            // Find the channelmap that sends this instrument channel somewhere.
            // Pre-0.9.17 kits whose channel names match the kit channels directly may
            // have no channelmap at all, so fall back to identity mapping.
            const ChannelMap* map = nullptr;

            for (auto& m : instrument.channelMaps)
                if (m.in == ref.channel) { map = &m; break; }

            std::string destination;
            bool main = false;
            float bleed = 1.0f;

            if (map != nullptr)
            {
                destination = map->out;
                main = map->main;
                bleed = map->bleed;
            }
            else if (indexOfKitChannel (ref.channel) >= 0)
            {
                destination = ref.channel;
            }
            else
            {
                warnings.push_back ("instrument '" + instrument.name + "': channel '" + ref.channel
                                    + "' of sample '" + hit.name + "' is not mapped to any kit channel");
                continue;
            }

            if (indexOfKitChannel (destination) < 0)
            {
                warnings.push_back ("instrument '" + instrument.name + "': channelmap target '"
                                    + destination + "' is not a channel declared by the kit");
                continue;
            }

            Instrument::ChannelSource src;
            src.kitChannel = destination;
            src.audioFileIndex = static_cast<int> (a);
            src.main = main;
            src.bleed = bleed;

            instrument.resolvedChannels[s].push_back (std::move (src));
        }

        if (instrument.resolvedChannels[s].empty())
        {
            warnings.push_back ("instrument '" + instrument.name + "', sample '" + hit.name
                                + "' resolved to no kit channels and will be silent");
        }
    }

    (void) error;
    return true;
}

//==============================================================================
//==============================================================================
DrumKit DrumKit::loadNativeGlorpKit (GlorpKitReader& reader, const std::string& archivePath)
{
    DrumKit kit;
    kit.isArchive = true;
    kit.archivePath = archivePath;
    kit.xmlPath = archivePath;
    kit.isNativeGlorpKit = true;

    GlorpKitManifest manifest;
    std::vector<GlorpGraphic> graphics;
    if (! reader.readNativeKitManifest (manifest, graphics))
    {
        kit.error = "Could not parse kit.xml in " + archivePath;
        return kit;
    }

    kit.title = manifest.title;
    if (kit.title.empty() || kit.title == "Untitled Kit")
    {
        const auto stem = fs::path (archivePath).stem().string();
        if (! stem.empty() && stem != "Untitled Kit" && stem != "Untitled_Kit")
            kit.title = stem;
    }
    kit.author = manifest.author;
    kit.version = manifest.version;
    kit.license = manifest.license;
    kit.description = manifest.description;
    kit.hitGlowColor = manifest.hitGlowColor.empty() ? "#ff8200" : manifest.hitGlowColor;
    kit.declaredSampleRate = manifest.declaredSampleRate;

    std::map<std::string, std::string> graphicSrcMap;
    for (const auto& g : graphics)
        if (! g.name.empty() && ! g.src.empty())
            graphicSrcMap[g.name] = g.src;

    // Channels from mics
    for (const auto& m : manifest.mics)
    {
        KitChannel kc;
        kc.name = m.name;
        kc.title = m.title;
        kc.source = m.name;
        kit.channels.push_back (std::move (kc));
    }

    std::vector<GlorpInstrumentSpec> pieceSpecs;
    if (! reader.readNativeInstruments (pieceSpecs))
    {
        kit.error = "No valid instruments found in " + archivePath;
        return kit;
    }

    // Read factory mixer preset if present
    GlorpMixerSetting mixerSetting;
    if (reader.readFactoryMixer (mixerSetting))
    {
        kit.factoryMicLevels = mixerSetting.micLevels;
        kit.factoryInstrumentLevels = mixerSetting.instrumentLevels;
        kit.factoryInstrumentPans = mixerSetting.instrumentPans;
    }

    for (const auto& pieceSpec : pieceSpecs)
    {
        LogicalPiece lp;
        lp.name = pieceSpec.name;
        lp.id = pieceSpec.id;
        lp.graphic = pieceSpec.graphic;
        lp.outputBus = pieceSpec.outputBus;
        lp.chokeGroup = pieceSpec.chokeGroup;
        lp.normX = pieceSpec.normX;
        lp.normY = pieceSpec.normY;
        lp.normW = pieceSpec.normW;
        lp.zOrder = pieceSpec.zOrder;
        lp.hasCustomLayout = pieceSpec.hasCustomLayout;

        // Load embedded custom PNG bytes if present in the archive
        std::string pngArcPath;
        auto git = graphicSrcMap.find (pieceSpec.graphic);
        if (git != graphicSrcMap.end() && git->second.rfind ("builtin:", 0) != 0)
            pngArcPath = git->second;
        else if (! pieceSpec.graphic.empty() && pieceSpec.graphic.rfind ("builtin:", 0) != 0)
            pngArcPath = "assets/" + pieceSpec.graphic + ".png";

        if (! pngArcPath.empty() && reader.hasEntry (pngArcPath))
            reader.readEntryBytes (pngArcPath, lp.customPngBytes);

        for (const auto& zoneSpec : pieceSpec.zones)
        {
            Instrument inst;
            inst.name = pieceSpec.name + "." + zoneSpec.name;
            inst.displayName = pieceSpec.name + " " + zoneSpec.name;
            inst.logicalName = pieceSpec.name;
            inst.zoneName = zoneSpec.name;
            inst.bleedPiece = pieceForInstrumentName (pieceSpec.name);
            inst.group = pieceSpec.chokeGroup;
            inst.outputBus = pieceSpec.outputBus;
            inst.archivePath = archivePath;
            inst.index = static_cast<int> (kit.instruments.size());

            // Build choke specs
            for (const auto& ct : zoneSpec.chokes)
            {
                ChokeSpec cs;
                cs.instrument = ct.instrument;
                cs.chokeTimeMs = ct.chokeTimeMs;
                cs.hasChokeTime = true;
                inst.chokes.push_back (std::move (cs));
            }

            // In GlorpKit zone.xml, multiple audio files for the same hit share the same hit name/power.
            // Group samples by hit name or file while preserving order.
            std::vector<SampleHit> hitList;
            std::map<std::string, size_t> hitIndexMap;
            for (const auto& sref : zoneSpec.samples)
            {
                const auto hitKey = ! sref.name.empty() ? sref.name : sref.file;
                auto it = hitIndexMap.find (hitKey);
                if (it == hitIndexMap.end())
                {
                    hitIndexMap[hitKey] = hitList.size();
                    SampleHit hit;
                    hit.name = ! sref.name.empty() ? sref.name : fs::path (sref.file).stem().string();
                    hit.power = sref.power;

                    AudioFileRef aref;
                    aref.channel = sref.mic;
                    aref.file = sref.file;
                    aref.fileChannel = sref.fileChannel;
                    aref.archiveOffset = sref.archiveOffset;
                    aref.archiveLength = sref.archiveLength;
                    hit.audioFiles.push_back (std::move (aref));
                    hitList.push_back (std::move (hit));
                }
                else
                {
                    AudioFileRef aref;
                    aref.channel = sref.mic;
                    aref.file = sref.file;
                    aref.fileChannel = sref.fileChannel;
                    aref.archiveOffset = sref.archiveOffset;
                    aref.archiveLength = sref.archiveLength;
                    hitList[it->second].audioFiles.push_back (std::move (aref));
                }
            }
            inst.samples = std::move (hitList);

            // Set up channel mappings
            for (const auto& m : manifest.mics)
            {
                ChannelMap cm;
                cm.in = m.name;
                cm.out = m.name;
                cm.main = (m.name == "close" || m.name.find ("oh") != std::string::npos);
                inst.channelMaps.push_back (std::move (cm));
            }

            // Resolve channels
            std::string rerr;
            kit.resolveChannels (inst, rerr);

            // Suggested MIDI note
            if (zoneSpec.note >= 0)
                kit.suggestedMap[zoneSpec.note] = inst.displayName;
            if (zoneSpec.altNote >= 0)
                kit.suggestedMap[zoneSpec.altNote] = inst.displayName;

            ZoneItem zi;
            zi.name = zoneSpec.name;
            zi.samplerIndex = inst.index;
            zi.note = zoneSpec.note;
            zi.altNote = zoneSpec.altNote;
            zi.graphic = zoneSpec.graphic;
            lp.zones.push_back (std::move (zi));

            kit.instruments.push_back (std::move (inst));
        }

        kit.logicalPieces.push_back (std::move (lp));
    }

    return kit;
}

//==============================================================================
DrumKit DrumKit::loadFromArchive (const std::string& archivePath)
{
    DrumKit kit;
    kit.isArchive = true;
    kit.archivePath = archivePath;
    kit.xmlPath = archivePath;

    GlorpKitReader reader;
    if (! reader.open (archivePath))
    {
        kit.error = "Could not open drumkit archive: " + archivePath;
        return kit;
    }

    if (reader.isNativeGlorpKit())
        return loadNativeGlorpKit (reader, archivePath);

    return loadLegacyArchive (reader, archivePath);
}

//==============================================================================
DrumKit DrumKit::loadLegacyArchive (GlorpKitReader& reader, const std::string& archivePath)
{
    DrumKit kit;
    kit.isArchive = true;
    kit.archivePath = archivePath;
    kit.xmlPath = archivePath;

    // Check if any entries are compressed. If compressed, extract to managed cache directory.
    bool hasCompressedEntries = false;
    for (const auto& name : reader.listEntries())
    {
        if (const auto* e = reader.getEntry (name))
        {
            if (e->isCompressed)
            {
                hasCompressedEntries = true;
                break;
            }
        }
    }

    if (hasCompressedEntries)
    {
        const auto cacheDir = GlorpKitReader::getCacheDirectoryFor (archivePath);
        std::error_code ec;
        fs::create_directories (cacheDir, ec);

        const auto manifestName = reader.findManifestPath();
        const auto extractedManifest = cacheDir + "/" + (manifestName.empty() ? "drumkit.xml" : manifestName);

        if (! fs::exists (extractedManifest))
        {
            if (! reader.extractToDirectory (cacheDir))
            {
                kit.error = "Could not extract compressed drumkit archive to cache";
                return kit;
            }
        }

        auto extractedKit = DrumKit::load (extractedManifest);
        extractedKit.isArchive = true;
        extractedKit.archivePath = archivePath;
        return extractedKit;
    }

    // Uncompressed (zero-copy memory mapping path):
    const auto manifestPath = reader.findManifestPath();
    if (manifestPath.empty())
    {
        kit.error = "'" + archivePath + "' has no drumkit.xml or kit.xml manifest";
        return kit;
    }

    const auto xmlStr = reader.readEntryString (manifestPath);
    std::string xmlError;
    auto root = parseXml (xmlStr, xmlError);

    if (root == nullptr)
    {
        kit.error = "Could not read manifest in archive: " + xmlError;
        return kit;
    }

    if (root->tag != "drumkit" && root->tag != "glorpkit")
    {
        kit.error = "'" + archivePath + "' is not a drumkit archive (root is <" + root->tag + ">)";
        return kit;
    }

    kit.formatVersion = root->attribute ("version");
    kit.declaredSampleRate = root->attributeInt ("samplerate", 48000);
    kit.title = root->attribute ("name");

    if (auto* meta = root->child ("metadata"))
    {
        if (auto* n = meta->child ("version"))     kit.version     = n->text;
        if (auto* n = meta->child ("title"))       kit.title       = n->text;
        if (auto* n = meta->child ("description")) kit.description = n->text;
        if (auto* n = meta->child ("author"))      kit.author      = n->text;
        if (auto* n = meta->child ("email"))       kit.email       = n->text;
        if (auto* n = meta->child ("website"))     kit.website     = n->text;
        if (auto* n = meta->child ("license"))     kit.license     = n->text;
    }

    if (kit.title.empty())
        kit.title = fs::path (archivePath).stem().string();

    if (auto* chanNode = root->child ("channels"))
    {
        for (auto* c : chanNode->childrenNamed ("channel"))
        {
            KitChannel ch;
            ch.name = c->attribute ("name");
            if (auto* t = c->child ("title")) ch.title = t->text;
            if (auto* s = c->child ("source")) ch.source = s->text;
            else if (auto* m = c->child ("microphone")) ch.source = m->text;

            if (! ch.name.empty())
                kit.channels.push_back (std::move (ch));
        }
    }

    auto* instrumentsNode = root->child ("instruments");
    if (instrumentsNode == nullptr)
    {
        kit.error = "drumkit has no <instruments> node";
        return kit;
    }

    for (auto* node : instrumentsNode->childrenNamed ("instrument"))
    {
        Instrument instrument;
        instrument.name = node->attribute ("name");
        instrument.group = node->attribute ("group");
        instrument.archivePath = archivePath;
        instrument.displayName = prettifyName (instrument.name);

        const auto fileAttr = node->attribute ("file");
        if (instrument.name.empty() || fileAttr.empty())
            continue;

        for (auto* cm : node->childrenNamed ("channelmap"))
        {
            ChannelMap m;
            m.in   = cm->attribute ("in");
            m.out  = cm->attribute ("out");
            m.main = cm->attributeBool ("main", false);
            if (auto* b = cm->child ("bleed"))
                m.bleed = static_cast<float> (std::atof (b->text.c_str()));
            if (! m.in.empty() && ! m.out.empty())
                instrument.channelMaps.push_back (std::move (m));
        }

        if (auto* chokes = node->child ("chokes"))
        {
            for (auto* c : chokes->childrenNamed ("choke"))
            {
                ChokeSpec spec;
                spec.instrument = c->attribute ("instrument");
                if (c->hasAttribute ("choketime"))
                {
                    spec.chokeTimeMs = c->attributeFloat ("choketime", 100.0f);
                    spec.hasChokeTime = true;
                }
                if (! spec.instrument.empty())
                    instrument.chokes.push_back (std::move (spec));
            }
        }

        // Read instrument XML from archive
        std::string instXmlStr = reader.readEntryString (fileAttr);
        if (instXmlStr.empty())
            instXmlStr = reader.readEntryString ("instruments/" + fileAttr);

        if (instXmlStr.empty())
        {
            kit.warnings.push_back ("Instrument XML not found in archive: " + fileAttr);
            continue;
        }

        std::string instXmlError;
        auto instRoot = parseXml (instXmlStr, instXmlError);
        if (instRoot == nullptr)
        {
            kit.warnings.push_back ("Failed to parse instrument XML " + fileAttr + ": " + instXmlError);
            continue;
        }

        if (auto* meta = instRoot->child ("metadata"))
            if (auto* t = meta->child ("title"))
                if (! t->text.empty())
                    instrument.displayName = t->text;

        auto* samples = instRoot->child ("samples");
        if (samples != nullptr)
        {
            const auto instDir = parentDirectory (fileAttr);

            for (auto* sn : samples->childrenNamed ("sample"))
            {
                SampleHit hit;
                hit.name = sn->attribute ("name");
                hit.power = sn->attributeFloat ("power", 1.0f);
                if (hit.name.empty())
                    hit.name = "hit" + std::to_string (instrument.samples.size());

                for (auto* an : sn->childrenNamed ("audiofile"))
                {
                    AudioFileRef ref;
                    ref.channel = an->attribute ("channel");
                    ref.file = an->attribute ("file");
                    ref.fileChannel = an->attributeInt ("filechannel", 1) - 1;
                    if (ref.fileChannel < 0) ref.fileChannel = 0;

                    if (ref.channel.empty() || ref.file.empty())
                        continue;

                    // Locate audio file in reader
                    const GlorpKitEntry* entry = reader.getEntry (ref.file);
                    if (entry == nullptr && instDir != ".")
                        entry = reader.getEntry (joinPath (instDir, ref.file));
                    if (entry == nullptr)
                        entry = reader.getEntry ("samples/" + ref.file);
                    if (entry == nullptr)
                        entry = reader.getEntry ("instruments/" + ref.file);

                    if (entry != nullptr)
                    {
                        ref.archiveOffset = entry->dataOffset;
                        ref.archiveLength = entry->uncompressedSize;
                        hit.audioFiles.push_back (std::move (ref));
                    }
                    else
                    {
                        kit.warnings.push_back ("Sample file not found in archive: " + ref.file);
                    }
                }

                if (! hit.audioFiles.empty())
                    instrument.samples.push_back (std::move (hit));
            }
        }

        std::string resErr;
        kit.resolveChannels (instrument, resErr);

        if (instrument.isValid())
        {
            instrument.index = static_cast<int> (kit.instruments.size());
            kit.instruments.push_back (std::move (instrument));
        }
    }

    if (kit.instruments.empty())
        kit.error = "Archive contains no usable instruments";

    // Read companion midimap from archive if present
    const auto midimapPath = reader.findMidimapPath();
    if (! midimapPath.empty())
    {
        const auto mapStr = reader.readEntryString (midimapPath);
        std::string mapError;
        auto mapRoot = parseXml (mapStr, mapError);
        if (mapRoot != nullptr && mapRoot->tag == "midimap")
        {
            for (auto* m : mapRoot->childrenNamed ("map"))
            {
                const auto note = m->attributeInt ("note", -1);
                const auto& instr = m->attribute ("instr");
                if (note >= 0 && note < 128 && ! instr.empty())
                    kit.suggestedMap[note] = instr;
            }
        }
    }

    return kit;
}

//==============================================================================
DrumKit DrumKit::load (const std::string& drumkitXmlPath)
{
    if (GlorpKitReader::isGlorpKitFile (drumkitXmlPath))
        return loadFromArchive (drumkitXmlPath);

    DrumKit kit;
    kit.xmlPath = drumkitXmlPath;

    std::string xmlError;
    auto root = parseXmlFile (drumkitXmlPath, xmlError);

    if (root == nullptr)
    {
        kit.error = "could not read drumkit XML: " + xmlError;
        return kit;
    }

    // Accept both <drumkit version=...> and the legacy <drumkit name=...>.
    if (root->tag != "drumkit")
    {
        kit.error = "'" + drumkitXmlPath + "' is not a DrumGizmo drumkit (root is <" + root->tag + ">)";
        return kit;
    }

    kit.formatVersion = root->attribute ("version");
    kit.declaredSampleRate = root->attributeInt ("samplerate", 48000);

    // Kits predating the metadata block carry their title as a plain attribute.
    kit.title = root->attribute ("name");

    if (auto* meta = root->child ("metadata"))
    {
        if (auto* n = meta->child ("version"))   kit.version    = n->text;
        if (auto* n = meta->child ("title"))     kit.title      = n->text;
        if (auto* n = meta->child ("description")) kit.description = n->text;
        if (auto* n = meta->child ("author"))    kit.author     = n->text;
        if (auto* n = meta->child ("email"))     kit.email      = n->text;
        if (auto* n = meta->child ("website"))   kit.website    = n->text;
        if (auto* n = meta->child ("license"))   kit.license    = n->text;

        if (auto* logo = meta->child ("logo"))
        {
            if (const auto src = logo->attribute ("src"); ! src.empty())
                kit.imagePath = src;
        }

        if (auto* img = meta->child ("image"))
        {
            if (const auto src = img->attribute ("src"); ! src.empty())
                kit.imagePath = src;

            for (auto* cm : img->childrenNamed ("clickmap"))
                kit.imageClickMap.emplace_back (cm->attribute ("colour"), cm->attribute ("instrument"));
        }
    }

    if (auto* chanNode = root->child ("channels"))
    {
        for (auto* c : chanNode->childrenNamed ("channel"))
        {
            KitChannel ch;
            ch.name = c->attribute ("name");

            if (auto* t = c->child ("title")) ch.title = t->text;

            // The attribute was renamed "microphone" -> "source" at some point.
            if (auto* s = c->child ("source"))      ch.source = s->text;
            else if (auto* m = c->child ("microphone")) ch.source = m->text;

            if (! ch.name.empty())
                kit.channels.push_back (std::move (ch));
        }
    }

    auto* instrumentsNode = root->child ("instruments");

    if (instrumentsNode == nullptr)
    {
        kit.error = "drumkit has no <instruments> node";
        return kit;
    }

    const auto kitDir = parentDirectory (drumkitXmlPath);

    for (auto* node : instrumentsNode->childrenNamed ("instrument"))
    {
        Instrument instrument;
        std::string instrumentError;

        if (! kit.parseInstrument (instrument, *node, kitDir, instrumentError))
        {
            kit.warnings.push_back ("skipping instrument: " + instrumentError);
            continue;
        }

        if (! kit.loadInstrumentSamples (instrument, instrumentError))
        {
            kit.warnings.push_back (instrumentError);
            continue;
        }

        kit.resolveChannels (instrument, instrumentError);

        if (! instrument.isValid())
        {
            kit.warnings.push_back ("instrument '" + instrument.name + "' has no playable samples, skipping");
            continue;
        }

        instrument.index = static_cast<int> (kit.instruments.size());
        kit.instruments.push_back (std::move (instrument));
    }

    if (kit.instruments.empty())
        kit.error = "drumkit contains no usable instruments";

    // Optional companion midimap. Kits that ship named variants name the map to
    // match, e.g. CrocellKit_full.xml alongside Midimap_full.xml, so try the
    // variant-specific names before the plain one.
    struct MidimapCandidate { const char* name; };
    const auto stem = fs::path (drumkitXmlPath).stem().string();
    const auto lastSep = stem.find_last_of ("_-");
    const auto suffix = (lastSep != std::string::npos && lastSep + 1 < stem.size())
                            ? stem.substr (lastSep + 1)
                            : std::string();
    const auto prefix = (lastSep != std::string::npos) ? stem.substr (0, lastSep) : std::string();

    std::vector<std::string> midimapCandidates;

    // Suffix candidates (e.g. CrocellKit_full.xml -> Midimap_full.xml)
    if (! suffix.empty())
        for (const auto* pfx : { "Midimap_", "midimap_", "Midimap-", "midimap-" })
            midimapCandidates.push_back (joinPath (kitDir, std::string (pfx) + suffix + ".xml"));

    // Prefix candidates (e.g. CrocellKit_full.xml -> Midimap_CrocellKit.xml)
    if (! prefix.empty())
        for (const auto* pfx : { "Midimap_", "midimap_", "Midimap-", "midimap-" })
            midimapCandidates.push_back (joinPath (kitDir, std::string (pfx) + prefix + ".xml"));

    midimapCandidates.push_back (joinPath (kitDir, "midimap.xml"));
    midimapCandidates.push_back (joinPath (kitDir, "Midimap.xml"));

    // Also check any XML in kit directory containing "midimap"
    std::error_code dirEc;
    for (const auto& entry : fs::directory_iterator (fs::path (kitDir), dirEc))
    {
        if (! entry.is_regular_file())
            continue;
        auto p = entry.path();
        if (p.extension() == ".xml")
        {
            auto pstem = p.stem().string();
            std::string lowerStem = pstem;
            for (auto& c : lowerStem) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            if (lowerStem.find ("midimap") != std::string::npos)
                midimapCandidates.push_back (p.string());
        }
    }

    for (const auto& midimapPath : midimapCandidates)
    {
        if (! fs::exists (midimapPath))
            continue;

        std::string mapError;
        auto mapRoot = parseXmlFile (midimapPath, mapError);

        if (mapRoot != nullptr && mapRoot->tag == "midimap")
        {
            for (auto* m : mapRoot->childrenNamed ("map"))
            {
                const auto note = m->attributeInt ("note", -1);
                const auto& instr = m->attribute ("instr");

                if (note >= 0 && note < 128 && ! instr.empty())
                    kit.suggestedMap[note] = instr;
            }

            break;   // first match wins
        }

        if (! mapError.empty())
            kit.warnings.push_back ("could not read '" + midimapPath + "': " + mapError);
    }

    // Warn about midimap entries that point at instruments which did not survive loading.
    for (auto it = kit.suggestedMap.begin(); it != kit.suggestedMap.end();)
    {
        if (kit.indexOfInstrument (it->second) >= 0)
        {
            ++it;
            continue;
        }

        kit.warnings.push_back ("midimap note " + std::to_string (it->first) + " refers to unknown instrument '"
                                + it->second + "'");
        it = kit.suggestedMap.erase (it);
    }

    return kit;
}

} // namespace glorp
