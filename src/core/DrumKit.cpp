#include "DrumKit.h"

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

    for (auto* cm : node.childrenNamed ("channelmap"))
    {
        ChannelMap m;
        m.in   = cm->attribute ("in");
        m.out  = cm->attribute ("out");
        m.main = cm->attributeBool ("main", false);

        // Pre-0.9.17 kits encoded bleed as a trailing element rather than an attribute.
        if (auto* b = cm->child ("bleed"))
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

            if (map != nullptr)
            {
                destination = map->out;
                main = map->main;
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
DrumKit DrumKit::load (const std::string& drumkitXmlPath)
{
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
    const auto variant = stem.substr (0, stem.rfind ('_'));

    std::vector<std::string> midimapCandidates;

    if (! variant.empty() && variant != stem)
        for (const auto* prefix : { "Midimap_", "midimap_", "Midimap-", "midimap-" })
            midimapCandidates.push_back (joinPath (kitDir, std::string (prefix) + variant + ".xml"));

    midimapCandidates.push_back (joinPath (kitDir, "midimap.xml"));
    midimapCandidates.push_back (joinPath (kitDir, "Midimap.xml"));

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
