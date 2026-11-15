#pragma once
// ============================================================
//  OvPreset — read a .hinj preset written by the plug-in.
//
//  The format is JUCE's APVTS state dump: a root element holding one PARAM child
//  per parameter, with the id and the scaled value as attributes,
//
//      <?xml version="1.0" encoding="UTF-8"?>
//      <HINJ>
//        <PARAM id="airProtect" value="20000.0"/>
//        <PARAM id="sc1Freq"    value="115.0"/>
//        ...
//      </HINJ>
//
//  The plug-in loads it with juce::XmlDocument + ValueTree::fromXml.  The preview
//  does not link JUCE, so this reads the same files with a small scanner —
//  enough for this format and deliberately intolerant of anything else, so a
//  malformed file fails loudly instead of half-applying.
//
//  Only the values are returned; turning them into normalised parameter values
//  is the source's job, because it owns the ranges and the skews.
// ============================================================
#include <cctype>
#include <cstdio>
#include <map>
#include <string>

namespace ovpreset
{

/// id -> scaled value.
using Values = std::map<std::string, double>;

inline std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

/// Value of attribute `name` inside an element's text, or empty if absent.
inline std::string attr (const std::string& element, const std::string& name)
{
    for (size_t i = 0; i + name.size() < element.size(); ++i)
    {
        if (element.compare (i, name.size(), name) != 0) continue;
        // Must be a whole attribute name: the char before it cannot be a name
        // character, or "id" would also match the tail of another attribute.
        if (i > 0 && (std::isalnum ((unsigned char) element[i - 1]) || element[i - 1] == '_'))
            continue;

        size_t p = i + name.size();
        while (p < element.size() && std::isspace ((unsigned char) element[p])) ++p;
        if (p >= element.size() || element[p] != '=') continue;
        ++p;
        while (p < element.size() && std::isspace ((unsigned char) element[p])) ++p;
        if (p >= element.size()) return {};
        const char q = element[p];
        if (q != '"' && q != '\'') return {};
        ++p;

        const size_t end = element.find (q, p);
        if (end == std::string::npos) return {};
        return element.substr (p, end - p);
    }
    return {};
}

/// Parse a .hinj file.  Returns an empty map on failure; `error` says why.
inline Values read (const std::string& path, std::string* error = nullptr)
{
    Values out;

    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
    {
        if (error) *error = "cannot open " + path;
        return out;
    }

    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0)
        text.append (buf, n);
    std::fclose (f);

    // Walk the <PARAM ...> elements.  Each one is the span from '<' to '>'.
    size_t i = 0;
    while ((i = text.find ("<PARAM", i)) != std::string::npos)
    {
        const size_t close = text.find ('>', i);
        if (close == std::string::npos) break;

        const std::string element = text.substr (i, close - i + 1);
        i = close + 1;

        const std::string id  = trim (attr (element, "id"));
        const std::string val = trim (attr (element, "value"));
        if (id.empty() || val.empty()) continue;

        try
        {
            out[id] = std::stod (val);
        }
        catch (...)
        {
            // A value that is not a number is not a parameter setting.  Skipped
            // rather than written as a zero over a real parameter.
        }
    }

    if (out.empty() && error)
        *error = "no <PARAM id=... value=...> elements found in " + path;

    return out;
}

} // namespace ovpreset
