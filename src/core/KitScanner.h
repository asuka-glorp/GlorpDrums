/*
    KitScanner.h - finds drumkits on disk.

    DrumGizmo kits are plain directories containing a drumkit.xml, and there is
    no registry, no index and no standard location. In practice a musician has
    one or two of these:

      ~/DrumGizmo/kits        <- the DrumGizmo app default on Linux
      ~/.local/share/drumgizmo/kits
      /usr/share/drumgizmo/kits
      plus whatever they configure in GlorpDrums' own settings

    So we probe a list of likely roots and let the user add more. Each root is
    scanned once at startup; an explicit rescan happens when the FILE menu is
    opened or the user hits Refresh.
*/

#pragma once

#include <string>
#include <vector>

namespace glorp
{

struct KitEntry
{
    std::string drumkitPath;   ///< absolute path to the drumkit manifest
    std::string folderName;    ///< the kit's directory name
    std::string displayName;   ///< title from metadata, or folder name
    std::string variant;       ///< "tiny", "full", ... for kits that ship several
    std::string rootName;      ///< which scan root it came from, for grouping in the UI

    int instrumentCount = 0;   ///< as declared by the manifest

    bool valid = true;
    std::string error;         ///< why it failed to parse, if it did
};

class KitScanner
{
public:
    /// The directories searched when the user has not configured anything.
    /// GLORP_KITS_PATH (a colon-separated list) is prepended if set.
    static std::vector<std::string> defaultSearchPaths();

    /// Expands ~ and makes the path absolute. Returns an empty string if unusable.
    static std::string resolvePath (const std::string& path);

    /** Recursively finds every drumkit manifest beneath `root`, up to `maxDepth`
        levels. Both `drumkit.xml` and the named-variant form are picked up;
        anything whose root element is not <drumkit> is discarded by describe().
    */
    static std::vector<std::string> findKitsUnder (const std::string& root, int maxDepth = 6);

    /// Scans all roots and returns entries sorted by root then name.
    /// Duplicates (the same kit reachable through overlapping roots) are removed,
    /// keeping the shallowest/earliest root.
    static std::vector<KitEntry> scan (const std::vector<std::string>& roots);

    /// Cheap title extraction that does not require loading the whole kit.
    /// Falls back to the folder name.
    static KitEntry describe (const std::string& drumkitPath, const std::string& rootName);

    /// Full load, including samples. Slower; used when the user actually picks a kit.
    /// `error` is cleared on success.
    static bool loadKit (const std::string& drumkitPath, class DrumKit& kit);
};

} // namespace glorp
