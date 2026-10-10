/*
    XmlLite.h - a deliberately small XML DOM reader.

    GlorpDrums only ever needs to read DrumGizmo's machine-generated XML:
    no namespaces, no CDATA, no DTDs, no entity soup beyond the five
    predefined ones plus numeric character references.

    Keeping this in-house means the drumkit parser can be built and tested
    without dragging the audio engine in with it.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace glorp
{

class XmlNode;
using XmlNodePtr = std::shared_ptr<XmlNode>;

class XmlNode
{
public:
    std::string tag;
    std::string text;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNodePtr> children;

    bool hasAttribute (const std::string& name) const noexcept;
    std::string attribute (const std::string& name, const std::string& fallback = {}) const noexcept;

    /// Attribute parsed as a float; returns fallback if absent or unparseable.
    float attributeFloat (const std::string& name, float fallback) const noexcept;
    /// Attribute parsed as an int; returns fallback if absent or unparseable.
    int attributeInt (const std::string& name, int fallback) const noexcept;
    /// Attribute parsed as a bool, accepting "true"/"1"/"yes" case-insensitively.
    bool attributeBool (const std::string& name, bool fallback) const noexcept;

    /// First direct child with the given tag, or nullptr.
    const XmlNode* child (const std::string& tag) const noexcept;
    /// All direct children with the given tag.
    std::vector<const XmlNode*> childrenNamed (const std::string& tag) const;
    /// First direct child carrying the given attribute, whatever its tag.
    const XmlNode* childWithAttribute (const std::string& attr, const std::string& value) const noexcept;

    /// Text of the first direct child with the given tag, trimmed. Empty if none.
    std::string childText (const std::string& tag) const;

private:
    friend class XmlParser;
};

/// Parses an XML document held in memory. Returns nullptr and fills `error` on failure.
XmlNodePtr parseXml (const std::string& source, std::string& error);

/// Reads a whole file into memory and parses it.
XmlNodePtr parseXmlFile (const std::string& path, std::string& error);

} // namespace glorp
