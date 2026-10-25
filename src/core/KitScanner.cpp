#include "KitScanner.h"

#include "DrumKit.h"
#include "GlorpKit.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace glorp
{

namespace
{
    /// Directories that are never kit roots but can contain user data worth skipping
    /// when recursing (version-control and cache clutter from downloaded kits).
    bool isJunkDirectory (const fs::path& p)
    {
        const auto name = p.filename().string();

        return name == ".git" || name == ".svn" || name == "__MACOSX"
            || name == ".cache" || name == "node_modules";
    }

    /** True for drumkit manifests:
          drumkit.xml                  (modern convention)
          <Anything>.xml               (kits that ship named variants)
          <Name>.glorpkit / <Name>.zip (single-file packaged archives)
    */
    bool couldBeKitFileName (const fs::path& p)
    {
        if (! p.has_extension())
            return false;

        auto ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(),
                        [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });

        if (ext == ".glorpkit" || ext == ".zip")
            return true;

        if (ext != ".xml")
            return false;

        auto stem = p.stem().string();
        std::transform (stem.begin(), stem.end(), stem.begin(),
                        [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });

        return stem == "drumkit" || stem.find ("kit") != std::string::npos;
    }

    std::string homeDirectory()
    {
        if (const auto* h = std::getenv ("HOME"); h != nullptr && h[0] != '\0')
            return h;

        return {};
    }
}

//==============================================================================
std::string KitScanner::resolvePath (const std::string& path)
    {
        if (path.empty())
            return {};

        std::string p = path;

        if (p.front() == '~')
        {
            const auto home = homeDirectory();

            if (home.empty())
                return {};

            if (p == "~")
                p = home;
            else if (p[1] == '/' || p[1] == '\\')
                p = home + p.substr (1);
            else
                return {};   // ~user is not worth supporting
        }

        std::error_code ec;
        const fs::path absolute = fs::absolute (fs::path (p), ec);

        if (ec)
            return {};

        return absolute.lexically_normal().string();
    }

std::vector<std::string> KitScanner::defaultSearchPaths()
    {
        std::vector<std::string> paths;

        if (const auto* env = std::getenv ("GLORP_KITS_PATH"); env != nullptr && env[0] != '\0')
        {
            std::string current;

            for (const char* c = env;; ++c)
            {
                if (*c == ':' || *c == '\0')
                {
                    if (auto resolved = resolvePath (current); ! resolved.empty())
                        paths.push_back (std::move (resolved));

                    current.clear();

                    if (*c == '\0') break;
                }
                else
                {
                    current += *c;
                }
            }
        }

        if (const auto home = homeDirectory(); ! home.empty())
        {
            paths.push_back (home + "/.glorpdrums/kits");
            paths.push_back (home + "/.config/GlorpDrums/kits");
            paths.push_back (home + "/DrumGizmo/kits");
            paths.push_back (home + "/.local/share/drumgizmo/kits");
            paths.push_back (home + "/Documents/drumgizmo/kits");
        }

        paths.push_back ("/usr/share/drumgizmo/kits");
        paths.push_back ("/usr/local/share/drumgizmo/kits");
        paths.push_back ("/var/lib/drumgizmo/kits");

        // Remove duplicates while keeping order.
        std::vector<std::string> unique;
        std::vector<std::string> seen;

        for (auto& p : paths)
        {
            std::error_code ec;

            if (! fs::is_directory (p, ec))
                continue;

            if (std::find (seen.begin(), seen.end(), p) != seen.end())
                continue;

            seen.push_back (p);
            unique.push_back (std::move (p));
        }

        return unique;
    }

std::vector<std::string> KitScanner::findKitsUnder (const std::string& root, int maxDepth)
    {
        std::vector<std::string> found;

        std::error_code ec;
        const fs::path rootPath (root);

        if (! fs::is_directory (rootPath, ec))
            return found;

        // Iterative walk with an explicit depth counter: kits can be nested, and
        // recursion over a symlink loop would be a bad way to find that out.
        struct Frame { fs::path dir; int depth; };
        std::vector<Frame> stack { { rootPath, 0 } };

        while (! stack.empty())
        {
            auto frame = std::move (stack.back());
            stack.pop_back();

            std::error_code iterEc;
            fs::directory_iterator it (frame.dir, fs::directory_options::skip_permission_denied, iterEc);

            if (iterEc)
                continue;

            for (const auto& entry : it)
            {
                std::error_code entryEc;

                if (entry.is_directory (entryEc))
                {
                    if (frame.depth < maxDepth && ! isJunkDirectory (entry.path()))
                        stack.push_back ({ entry.path(), frame.depth + 1 });
                }
                else if (entry.is_regular_file (entryEc) && couldBeKitFileName (entry.path()))
                {
                    // Not every .xml is a manifest. describe() rejects anything
                    // whose root element is not <drumkit>, so instrument XMLs and
                    // midimaps are filtered out cheaply rather than by filename.
                    found.push_back (fs::absolute (entry.path(), entryEc).lexically_normal().string());
                }
            }
        }

        std::sort (found.begin(), found.end());
        return found;
    }

KitEntry KitScanner::describe (const std::string& drumkitPath, const std::string& rootName)
    {
        KitEntry entry;
        entry.drumkitPath = drumkitPath;
        entry.rootName = rootName;

        std::error_code ec;
        const fs::path p (drumkitPath);
        entry.folderName = p.has_parent_path() ? p.parent_path().filename().string() : p.stem().string();

        if (entry.folderName.empty())
            entry.folderName = p.stem().string();

        entry.displayName = entry.folderName;

        if (GlorpKitReader::isGlorpKitFile (drumkitPath))
        {
            GlorpKitManifest manifest;
            if (! GlorpKitReader::quickReadManifest (drumkitPath, manifest))
            {
                entry.valid = false;
                entry.error = manifest.error;
                return entry;
            }

            std::string kitTitle = manifest.title;
            if (kitTitle.empty() || kitTitle == "Untitled Kit")
            {
                const auto stem = std::filesystem::path (drumkitPath).stem().string();
                if (! stem.empty() && stem != "Untitled Kit" && stem != "Untitled_Kit")
                    kitTitle = stem;
            }

            entry.displayName = "[PKG] " + kitTitle;
            entry.instrumentCount = manifest.instrumentCount;
            entry.valid = true;
            return entry;
        }

        // Pull the title out without paying for a full instrument load.
        std::string xmlError;
        auto root = parseXmlFile (drumkitPath, xmlError);

        if (root == nullptr)
        {
            entry.valid = false;
            entry.error = xmlError;
            return entry;
        }

        if (root->tag != "drumkit")
        {
            entry.valid = false;
            entry.error = "root element is <" + root->tag + ">, expected <drumkit>";
            return entry;
        }

        if (auto* meta = root->child ("metadata"))
            if (auto* title = meta->child ("title"); title != nullptr && ! title->text.empty())
                entry.displayName = title->text;
        else if (auto name = root->attribute ("name"); ! name.empty())
            entry.displayName = name;

        // Kits that ship named variants (CrocellKit_tiny.xml, _small, _default,
        // _full) all share a title, so the filename is the only thing telling them
        // apart. Without this the menu would list "CrocellKit" four times.
        const auto stem = p.stem().string();

        if (entry.displayName == stem)
        {
            entry.displayName = stem;
        }
        else if (stem.size() > entry.displayName.size()
                 && stem.compare (0, entry.displayName.size(), entry.displayName) == 0)
        {
            auto suffix = stem.substr (entry.displayName.size());

            while (! suffix.empty() && (suffix.front() == '_' || suffix.front() == '-' || suffix.front() == ' '))
                suffix.erase (suffix.begin());

            if (! suffix.empty())
            {
                entry.variant = suffix;
                entry.displayName += "  (" + suffix + ")";
            }
        }

        if (auto* instruments = root->child ("instruments"))
        {
            // Count only the instruments that point at a file; a count of zero means
            // this is not a usable kit even though the XML is well formed.
            int usable = 0;

            for (auto* i : instruments->childrenNamed ("instrument"))
                if (i->hasAttribute ("file") && i->hasAttribute ("name"))
                    ++usable;

            entry.instrumentCount = usable;

            if (usable == 0)
            {
                entry.valid = false;
                entry.error = "drumkit declares no usable instruments";
                return entry;
            }
        }

        return entry;
    }

std::vector<KitEntry> KitScanner::scan (const std::vector<std::string>& roots)
    {
        std::vector<KitEntry> results;

        // root -> already-reported kits, so an overlapping root does not double up.
        std::vector<std::string> reported;

        for (auto& root : roots)
        {
            const auto rootName = fs::path (root).filename().string();

            for (auto& kitPath : findKitsUnder (root))
            {
                if (std::find (reported.begin(), reported.end(), kitPath) != reported.end())
                    continue;

                reported.push_back (kitPath);
                results.push_back (describe (kitPath, rootName));
            }
        }

        // Valid kits first, then alphabetical, so a broken kit never sits at the top.
        std::stable_sort (results.begin(), results.end(),
                          [] (const KitEntry& a, const KitEntry& b)
                          {
                              if (a.valid != b.valid)
                                  return a.valid > b.valid;

                              return a.displayName < b.displayName;
                          });

        return results;
    }

bool KitScanner::loadKit (const std::string& drumkitPath, DrumKit& kit)
    {
        kit = DrumKit::load (drumkitPath);
        return kit.error.empty();
    }

} // namespace glorp
