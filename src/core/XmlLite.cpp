#include "XmlLite.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace glorp
{

bool XmlNode::hasAttribute (const std::string& n) const noexcept
{
    for (auto& a : attributes)
        if (a.first == n)
            return true;

    return false;
}

std::string XmlNode::attribute (const std::string& n, const std::string& fallback) const noexcept
{
    for (auto& a : attributes)
        if (a.first == n)
            return a.second;

    return fallback;
}

float XmlNode::attributeFloat (const std::string& n, float fallback) const noexcept
{
    if (! hasAttribute (n))
        return fallback;

    const auto s = attribute (n);
    char* end = nullptr;
    const double v = std::strtod (s.c_str(), &end);

    if (end == s.c_str())
        return fallback;

    return static_cast<float> (v);
}

int XmlNode::attributeInt (const std::string& n, int fallback) const noexcept
{
    if (! hasAttribute (n))
        return fallback;

    const auto s = attribute (n);
    char* end = nullptr;
    const long v = std::strtol (s.c_str(), &end, 10);

    if (end == s.c_str())
        return fallback;

    return static_cast<int> (v);
}

bool XmlNode::attributeBool (const std::string& n, bool fallback) const noexcept
{
    if (! hasAttribute (n))
        return fallback;

    auto s = attribute (n);
    for (auto& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

    return s == "true" || s == "1" || s == "yes";
}

const XmlNode* XmlNode::child (const std::string& t) const noexcept
{
    for (auto& c : children)
        if (c->tag == t)
            return c.get();

    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed (const std::string& t) const
{
    std::vector<const XmlNode*> result;

    for (auto& c : children)
        if (c->tag == t)
            result.push_back (c.get());

    return result;
}

const XmlNode* XmlNode::childWithAttribute (const std::string& attr, const std::string& value) const noexcept
{
    for (auto& c : children)
        if (c->attribute (attr) == value)
            return c.get();

    return nullptr;
}

std::string XmlNode::childText (const std::string& t) const
{
    if (auto* c = child (t))
        return c->text;

    return {};
}

//==============================================================================
namespace
{
    struct Cursor
    {
        const std::string& src;
        size_t pos = 0;

        bool eof() const noexcept { return pos >= src.size(); }
        char peek() const noexcept { return pos < src.size() ? src[pos] : '\0'; }
        char get() noexcept { return pos < src.size() ? src[pos++] : '\0'; }

        bool lookingAt (const char* s) const noexcept
        {
            return src.compare (pos, std::char_traits<char>::length (s), s) == 0;
        }

        void skipWhitespace() noexcept
        {
            while (pos < src.size() && std::isspace (static_cast<unsigned char> (src[pos])) != 0)
                ++pos;
        }

        bool lookingAtWhitespace() const noexcept
        {
            return pos < src.size() && std::isspace (static_cast<unsigned char> (src[pos])) != 0;
        }
    };

    bool isNameChar (char c) noexcept
    {
        return std::isalnum (static_cast<unsigned char> (c)) != 0
            || c == '_' || c == '-' || c == '.' || c == ':' || c == '/';
    }

    std::string decodeEntities (const std::string& in, std::string& error)
    {
        std::string out;
        out.reserve (in.size());

        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] != '&')
            {
                out += in[i];
                continue;
            }

            const auto semi = in.find (';', i);

            if (semi == std::string::npos || semi - i > 12)
            {
                error = "malformed entity reference";
                return {};
            }

            const auto entity = in.substr (i + 1, semi - i - 1);

            if (entity == "amp")       { out += '&';  i = semi; }
            else if (entity == "lt")   { out += '<';  i = semi; }
            else if (entity == "gt")   { out += '>';  i = semi; }
            else if (entity == "quot") { out += '"';  i = semi; }
            else if (entity == "apos") { out += '\''; i = semi; }
            else if (! entity.empty() && entity[0] == '#')
            {
                const int base = (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) ? 16 : 10;
                const auto digits = entity.substr (base == 16 ? 2 : 1);
                char* end = nullptr;
                const long cp = std::strtol (digits.c_str(), &end, base);

                if (end == digits.c_str() || cp <= 0)
                {
                    error = "bad numeric character reference &" + entity + ";";
                    return {};
                }

                // Enough for anything a drumkit description could plausibly hold.
                if (cp > 0x10FFFF)
                {
                    error = "character reference out of range";
                    return {};
                }

                // UTF-8 encode.
                const auto cp32 = static_cast<unsigned int> (cp);

                if (cp32 < 0x80)
                {
                    out += static_cast<char> (cp32);
                }
                else if (cp32 < 0x800)
                {
                    out += static_cast<char> (0xC0 | (cp32 >> 6));
                    out += static_cast<char> (0x80 | (cp32 & 0x3F));
                }
                else if (cp32 < 0x10000)
                {
                    out += static_cast<char> (0xE0 | (cp32 >> 12));
                    out += static_cast<char> (0x80 | ((cp32 >> 6) & 0x3F));
                    out += static_cast<char> (0x80 | (cp32 & 0x3F));
                }
                else
                {
                    out += static_cast<char> (0xF0 | (cp32 >> 18));
                    out += static_cast<char> (0x80 | ((cp32 >> 12) & 0x3F));
                    out += static_cast<char> (0x80 | ((cp32 >> 6) & 0x3F));
                    out += static_cast<char> (0x80 | (cp32 & 0x3F));
                }

                i = semi;
            }
            else
            {
                error = "unknown entity &" + entity + ";";
                return {};
            }
        }

        return out;
    }

    std::string trim (const std::string& s)
    {
        size_t a = 0;
        size_t b = s.size();

        while (a < b && std::isspace (static_cast<unsigned char> (s[a])) != 0) ++a;
        while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1])) != 0) --b;

        return s.substr (a, b - a);
    }

    /// Skips comments, processing instructions, doctypes and CDATA sections.
    /// Returns false if `cursor` sits on something that should have been skipped.
    bool skipNonElement (Cursor& c)
    {
        if (c.lookingAt ("<!--"))
        {
            const auto end = c.src.find ("-->", c.pos);
            if (end == std::string::npos) return false;
            c.pos = end + 3;
            return true;
        }

        if (c.lookingAt ("<?"))
        {
            const auto end = c.src.find ("?>", c.pos);
            if (end == std::string::npos) return false;
            c.pos = end + 2;
            return true;
        }

        if (c.lookingAt ("<!"))
        {
            // DOCTYPE and friends: skip to the matching '>' at depth zero.
            int depth = 0;

            for (; ! c.eof(); ++c.pos)
            {
                if (c.src[c.pos] == '<') ++depth;
                else if (c.src[c.pos] == '>' && --depth == 0) { ++c.pos; return true; }
            }

            return false;
        }

        return false;
    }

    enum class TagEnd
    {
        complete,   ///< '>' - children and a closing tag follow
        selfClosed  ///< '/>' - element is empty
    };

    bool parseAttributes (Cursor& c, XmlNode& node, TagEnd& how, std::string& error)
    {
        how = TagEnd::complete;

        for (;;)
        {
            c.skipWhitespace();

            if (c.eof())            { error = "unexpected end of file inside a tag"; return false; }
            if (c.lookingAt ("/>")) { c.pos += 2; how = TagEnd::selfClosed; return true; }
            if (c.peek() == '>')    { ++c.pos; how = TagEnd::complete; return true; }

            if (! isNameChar (c.peek()))
            {
                error = std::string ("unexpected character '") + c.peek() + "' in tag";
                return false;
            }

            std::string name;
            while (! c.eof() && isNameChar (c.peek())) name += c.get();

            c.skipWhitespace();

            if (c.peek() != '=')
            {
                error = "attribute '" + name + "' has no value";
                return false;
            }

            ++c.pos;
            c.skipWhitespace();

            const char quote = c.peek();
            if (quote != '"' && quote != '\'')
            {
                error = "attribute '" + name + "' value is not quoted";
                return false;
            }

            ++c.pos;
            std::string rawValue;

            while (! c.eof() && c.peek() != quote)
                rawValue += c.get();

            if (c.eof())
            {
                error = "unterminated attribute value";
                return false;
            }

            ++c.pos;

            auto value = decodeEntities (rawValue, error);
            if (! error.empty()) return false;

            node.attributes.emplace_back (std::move (name), std::move (value));
        }
    }

    /// Parses one element, assuming `c` sits on its '<'.
    XmlNodePtr parseElement (Cursor& c, std::string& error)
    {
        if (c.get() != '<') { error = "expected '<'"; return nullptr; }

        auto node = std::make_shared<XmlNode>();

        while (! c.eof() && isNameChar (c.peek()) && c.peek() != '/')
            node->tag += c.get();

        if (node->tag.empty()) { error = "element has no name"; return nullptr; }

        TagEnd how = TagEnd::complete;

        if (! parseAttributes (c, *node, how, error))
            return nullptr;

        if (how == TagEnd::selfClosed)
            return node;

        // Body ------------------------------------------------------------
        for (;;)
        {
            std::string rawText;

            while (! c.eof() && c.peek() != '<')
                rawText += c.get();

            if (! c.eof())
            {
                auto decoded = decodeEntities (rawText, error);
                if (! error.empty()) return nullptr;

                node->text += decoded;
            }

            if (c.eof()) { error = "unterminated element <" + node->tag + ">"; return nullptr; }

            if (c.lookingAt ("</"))
            {
                c.pos += 2;
                std::string closing;
                while (! c.eof() && isNameChar (c.peek())) closing += c.get();

                c.skipWhitespace();
                if (c.get() != '>') { error = "malformed closing tag"; return nullptr; }

                if (closing != node->tag)
                {
                    error = "closing tag </" + closing + "> does not match <" + node->tag + ">";
                    return nullptr;
                }

                node->text = trim (node->text);
                return node;
            }

            if (skipNonElement (c)) continue;

            auto child = parseElement (c, error);
            if (child == nullptr) return nullptr;

            node->children.push_back (std::move (child));
        }
    }
}

XmlNodePtr parseXml (const std::string& source, std::string& error)
{
    error.clear();

    Cursor c { source };

    // Strip a UTF-8 BOM if present.
    if (source.size() >= 3
         && static_cast<unsigned char> (source[0]) == 0xEF
         && static_cast<unsigned char> (source[1]) == 0xBB
         && static_cast<unsigned char> (source[2]) == 0xBF)
        c.pos = 3;

    for (;;)
    {
        c.skipWhitespace();

        if (c.eof()) { error = "document contains no root element"; return nullptr; }

        if (skipNonElement (c)) continue;

        return parseElement (c, error);
    }
}

XmlNodePtr parseXmlFile (const std::string& path, std::string& error)
{
    std::ifstream in (path, std::ios::binary);

    if (! in)
    {
        error = "could not open '" + path + "'";
        return nullptr;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    return parseXml (buffer.str(), error);
}

} // namespace glorp
