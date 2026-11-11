// reagentlive CLI: names for members reflection does not list (a block of
// native state inside a Pad_ array), kept in a hand-owned file beside the
// console and shown by fields, dump and get. Pure; tests cover it.
//
// One member per line, whitespace-separated:
//   <Owner> <offset> <type>[n] <Name> <timestamp>
// Owner is the reflected class that declares the fields around it; type and
// count are a `read --as` layout field's; a trailing '?' on Name marks an
// unconfirmed name; timestamp is the main image's PE timestamp when it was
// labelled (hex). A later line for the same owner and offset replaces an
// earlier one. Blank lines and '#' comments are ignored.
//
// Patches move native offsets, and nothing in the image says so: a member
// labelled under another timestamp still shows, marked stale, until it is
// labelled again.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/layout.h"
#include "reagentlive/frontends/cli/syntax/text.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli {
struct NativeMember {
    std::string owner;
    layout::Field field;        // Type, count and name; `offset` is the member's offset in the object.
    uint32_t timestamp = 0;
    size_t line = 0;            // 1-based line in the file.
};

// "f64[3]" or "u8".
inline std::string NativeType(const layout::Field& field) {
    return field.array ? field.type + "[" + std::to_string(field.count) + "]" : field.type;
}

// The file's line for a member.
inline std::string NativeLine(std::string_view owner, const layout::Field& field, uint32_t timestamp) {
    char numbers[48];
    snprintf(numbers, sizeof(numbers), "0x%x", field.offset);
    std::string line = std::string(owner) + ' ' + numbers + ' ' + NativeType(field) + ' ' + field.name + ' ';
    snprintf(numbers, sizeof(numbers), "0x%08x", timestamp);
    return line + numbers;
}

class NativeNames {
public:
    // Adds the stream's members. Returns the 1-based line of the first
    // malformed line, or 0; `why` says what is wrong with it.
    size_t Load(std::istream& in, std::string* why = nullptr) {
        std::string line;
        for (size_t number = 1; std::getline(in, line); ++number) {
            std::string_view rest = line;
            if (const auto hash = rest.find('#'); hash != std::string_view::npos) rest = rest.substr(0, hash);
            if (text::Trim(rest).empty()) continue;
            NativeMember member;
            member.owner = text::Next(rest);
            uint64_t offset = 0, timestamp = 0;
            const bool offsetOk = text::Number(text::Next(rest), offset) && offset <= UINT32_MAX;
            const auto type = text::Next(rest);
            const auto name = text::Next(rest);
            auto field = layout::ParseField(std::string(type) + " " + std::string(name), true);
            const bool stampOk = text::Number(text::Next(rest), timestamp) && timestamp <= UINT32_MAX;
            if (member.owner.empty() || !offsetOk || !field || field->type.empty() || !stampOk || !rest.empty()) {
                if (why) *why = !field ? field.error() : "expected <Owner> <offset> <type>[n] <Name> <timestamp>";
                return number;
            }
            member.field = std::move(*field);
            member.field.offset = static_cast<uint32_t>(offset);
            member.timestamp = static_cast<uint32_t>(timestamp);
            member.line = number;
            std::erase_if(members_, [&](const NativeMember& m) {
                return m.owner == member.owner && m.field.offset == member.field.offset;
            });
            members_.push_back(std::move(member));
        }
        return 0;
    }
    bool Empty() const { return members_.empty(); }

    // Members of the classes that declare these reflected fields, by offset.
    std::vector<const NativeMember*> For(std::span<const ue::Field> reflected) const {
        std::vector<const NativeMember*> out;
        for (const auto& member : members_)
            if (std::ranges::any_of(reflected, [&](const ue::Field& f) { return f.owner == member.owner; }))
                out.push_back(&member);
        std::ranges::sort(out, {}, [](const NativeMember* m) { return m->field.offset; });
        return out;
    }

private:
    std::vector<NativeMember> members_;
};

// Whether `wanted` names the member: its name, with the trailing '?' optional.
inline bool NativeMatches(const NativeMember& member, std::string_view wanted) {
    std::string_view name = member.field.name;
    if (name == wanted) return true;
    if (name.ends_with('?') && !wanted.ends_with('?')) name.remove_suffix(1);
    return name == wanted;
}

// A member checked against the object it is shown on: why it is refused
// (overlapping a reflected field, or outside the class size the agent
// reports; empty when it is shown), and whether it was labelled under
// another build of the main image. 0 for classSize or timestamp: unknown.
struct NativeCheck {
    const NativeMember* member;
    std::string problem;
    bool stale = false;
};
inline NativeCheck CheckNative(const NativeMember& member, std::span<const ue::Field> reflected, uint32_t classSize,
                               uint32_t timestamp) {
    NativeCheck check{&member, {}, timestamp && member.timestamp != timestamp};
    const uint64_t begin = member.field.offset, end = begin + member.field.Size();
    char text[64];
    for (const auto& field : reflected) {
        if (!field.size || field.offset >= end || uint64_t{field.offset} + field.size <= begin) continue;
        snprintf(text, sizeof(text), " at 0x%x", field.offset);
        check.problem = "overlaps reflected " + field.owner + ":" + field.name + text;
        return check;
    }
    if (classSize && end > classSize) {
        snprintf(text, sizeof(text), "ends at 0x%llx, past the class size 0x%x", static_cast<unsigned long long>(end), classSize);
        check.problem = text;
    }
    return check;
}

// The class a member at `offset` is filed under: `wanted` when it declares
// one of the fields, else the declarer of the last field before the offset
// (or of the first field). Empty without reflected fields.
inline std::string NativeOwner(std::span<const ue::Field> reflected, uint32_t offset, std::string_view wanted = {}) {
    if (!wanted.empty() && std::ranges::any_of(reflected, [&](const ue::Field& f) { return f.owner == wanted; }))
        return std::string(wanted);
    const ue::Field* owner = nullptr;
    for (const auto& field : reflected)
        if (field.offset <= offset && (!owner || field.offset >= owner->offset)) owner = &field;
    if (!owner && !reflected.empty())
        owner = &*std::ranges::min_element(reflected, {}, &ue::Field::offset);
    return owner ? owner->owner : std::string();
}
}
