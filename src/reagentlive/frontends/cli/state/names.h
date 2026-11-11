// reagentlive CLI: readable names for obfuscated reflected fields. Pure; tests cover it.
//
// A names file has one field per line, whitespace-separated:
//   <Owner> <offset> <size> <boolMask> <Name>
// Owner is the reflected class or struct name (no C++ prefix). Offset and
// mask as `fields` reports them (bools: the byte holding the bit and its
// mask, 0 otherwise). Size 0 matches any size. A trailing '?' on Name marks
// an unconfirmed name. Blank lines and '#' comments are ignored.
#pragma once
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include "reagentlive/frontends/cli/syntax/parse.h"

namespace reagentlive::cli {
class Names {
public:
    // Adds the stream's entries; a later entry for the same field replaces an
    // earlier one. Returns the 1-based line of the first malformed line, or 0.
    size_t Load(std::istream& in) {
        std::string line;
        for (size_t number = 1; std::getline(in, line); ++number) {
            std::string_view rest = line;
            if (const auto hash = rest.find('#'); hash != std::string_view::npos) rest = rest.substr(0, hash);
            if (text::Trim(rest).empty()) continue;
            const auto owner = text::Next(rest);
            uint64_t offset = 0, size = 0, mask = 0;
            const bool numbers = text::Number(text::Next(rest), offset) && text::Number(text::Next(rest), size) &&
                text::Number(text::Next(rest), mask);
            const auto name = text::Next(rest);
            if (!numbers || name.empty() || name == "?" || !rest.empty() || offset > UINT32_MAX ||
                size > UINT32_MAX || mask > 0xff) return number;
            entries_[Key(owner, static_cast<uint32_t>(offset), static_cast<uint8_t>(mask))] =
                {static_cast<uint32_t>(size), std::string(name)};
        }
        return 0;
    }
    size_t Size() const { return entries_.size(); }

    // The readable name for a reflected field, or empty when unknown or when
    // the size disagrees.
    std::string_view Find(std::string_view owner, uint32_t offset, uint32_t size, uint8_t boolMask) const {
        const auto found = entries_.find(Key(owner, offset, boolMask));
        if (found == entries_.end() || (found->second.size && found->second.size != size)) return {};
        return found->second.name;
    }

    // "Readable (raw)" when the readable name differs from the reflected one.
    template<class Field> std::string Display(const Field& field) const {
        const auto name = Find(field.owner, field.offset, field.size, field.boolMask);
        if (name.empty() || name == field.name) return field.name;
        return std::string(name) + " (" + field.name + ")";
    }

    // Whether `wanted` names this field: its reflected name or its readable
    // name (a trailing '?' optional).
    template<class Field> bool Matches(const Field& field, std::string_view wanted) const {
        if (wanted == field.name) return true;
        auto name = Find(field.owner, field.offset, field.size, field.boolMask);
        if (name.ends_with('?') && !wanted.ends_with('?')) name.remove_suffix(1);
        return !name.empty() && wanted == name;
    }

private:
    struct Entry { uint32_t size; std::string name; };
    static std::string Key(std::string_view owner, uint32_t offset, uint8_t mask) {
        return std::string(owner) + '\n' + std::to_string(offset) + '\n' + std::to_string(mask);
    }
    std::unordered_map<std::string, Entry> entries_;
};
}
