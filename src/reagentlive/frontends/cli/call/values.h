// reagentlive CLI: the values a call carries: numbers, refs, byte strings and
// the typed JSON values (addresses, pointer chains, field paths, filters).
#pragma once
#include <expected>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "reagentlive/frontends/cli/call/command.h"

namespace reagentlive::cli {
namespace text {
inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}
// Next whitespace-separated word; `rest` keeps what follows (layouts, files).
inline std::string_view Next(std::string_view& rest) {
    rest = Trim(rest);
    size_t end = 0;
    while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t') ++end;
    const auto token = rest.substr(0, end);
    rest = Trim(rest.substr(end));
    return token;
}
inline int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Case-insensitive substring (ASCII); an empty part matches.
inline bool ContainsNoCase(std::string_view text, std::string_view part) {
    return std::ranges::search(text, part, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    }).begin() != text.end() || part.empty();
}
// 0x-prefixed hex, otherwise decimal.
inline bool Number(std::string_view s, uint64_t& out) {
    out = 0;
    const bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    if (hex) s.remove_prefix(2);
    if (s.empty() || s.size() > (hex ? 16u : 19u)) return false;
    for (char c : s) {
        const int digit = hex ? HexDigit(c) : (c >= '0' && c <= '9' ? c - '0' : -1);
        if (digit < 0) return false;
        out = out * (hex ? 16 : 10) + static_cast<uint64_t>(digit);
    }
    return true;
}
// Literal ref 0x<address>#<index>:<serial>, as RefText prints it.
inline bool LiteralRef(std::string_view s, Ref& out) {
    const size_t hash = s.find('#'), colon = s.find(':');
    uint64_t address = 0, index = 0, serial = 0;
    if (hash == std::string_view::npos || colon == std::string_view::npos || colon < hash ||
        !Number(s.substr(0, hash), address) || !address || !Number(s.substr(hash + 1, colon - hash - 1), index) ||
        !Number(s.substr(colon + 1), serial) || index > INT32_MAX || serial > INT32_MAX) return false;
    out = {static_cast<uintptr_t>(address), static_cast<int32_t>(index), static_cast<int32_t>(serial)};
    return true;
}
inline bool SameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}
// "base" is the exe, listed first; any other name matches a module's, ignoring case.
inline const Module* ModuleNamed(std::string_view name, std::span<const Module> modules) {
    if (name == "base") return modules.empty() ? nullptr : &modules.front();
    for (const auto& module : modules)
        if (SameText(module.name, name)) return &module;
    return nullptr;
}
// Hex pairs, spaced or packed.
inline bool Bytes(std::string_view s, uint8_t* out, size_t capacity, uint32_t& length) {
    length = 0;
    int high = -1;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        const int digit = HexDigit(c);
        if (digit < 0) return false;
        if (high < 0) { high = digit; continue; }
        if (length >= capacity) return false;
        out[length++] = static_cast<uint8_t>(high << 4 | digit);
        high = -1;
    }
    return high < 0;
}
// A literal ref keeps the full ref; "0x..." is an address marked for the agent to identify.
inline bool Object(std::string_view s, Request& request) {
    Ref ref{};
    if (!LiteralRef(s, ref)) {
        uint64_t address = 0;
        if (!s.starts_with("0x") || !Number(s, address) || !address) return false;
        ref = {static_cast<uintptr_t>(address), -1, 0};
    }
    SetRef(request, ref);
    return true;
}
// A bare identifier names a class or struct, e.g. Character or BP_Door_C.
inline bool ClassName(std::string_view s) {
    if (s.empty() || s.size() >= MaxTypeName || (s[0] >= '0' && s[0] <= '9')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
// An object ref or address, or else a class name for the console to resolve.
inline bool ObjectOrClass(std::string_view s, Command& command) {
    if (Object(s, command.request)) return true;
    if (!ClassName(s)) return false;
    command.className = s;
    return true;
}
}

// The typed JSON values of a call (call/verb.h's vocabulary), read once the
// call passed its shape checks (check.h): what is left to refuse here is what
// needs the module list or a limit.
namespace value {
using Json = nlohmann::json;

// An address and what it was given relative to: `object` is the ref's
// address when the base was a literal ref (a write guards on it), else 0;
// `chain` the offsets of a pointer chain, resolved by the console.
struct Place {
    uint64_t address = 0, object = 0;
    std::vector<uint64_t> chain;
};

inline uint64_t Int(const Json& value) {
    if (value.is_number_unsigned()) return value.get<uint64_t>();
    if (value.is_number_integer()) return static_cast<uint64_t>(value.get<int64_t>());
    uint64_t out = 0;
    if (value.is_string()) text::Number(value.get<std::string>(), out);
    return out;
}

// Each offset but the last reads a pointer, so a chain of one reads none: it
// would answer the bytes at base+off as if a pointer had been followed.
constexpr auto ChainNoHop = "chain needs 2+ offsets (each but the last reads a pointer); "
    "one offset is {\"base\": <addr>, \"offset\": N}";
constexpr auto ChainRefused = "a write goes to an address you resolved and read first, never through a pointer chain";

// An addr ("0x..." hex, a literal ref, {module, rva}, {base, offset}); with
// `chains`, also {base, chain}. Errors say what is wrong with it.
inline std::expected<Place, std::string> Address(const Json& value, std::span<const Module> modules, bool chains = false) {
    using Out = std::expected<Place, std::string>;
    Place place;
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        Ref ref{};
        if (text::LiteralRef(text, ref)) {
            place.address = ref.address;
            if (ref.index >= 0) place.object = ref.address;
            return place;
        }
        if (!text.starts_with("0x") || !text::Number(text, place.address) || !place.address)
            return Out(std::unexpect, "'" + text + "' is not an address: \"0x...\" hex or a literal ref");
        return place;
    }
    if (!value.is_object()) return Out(std::unexpect, "an address is \"0x...\", a literal ref or an object");
    if (value.contains("module")) {
        const auto name = value["module"].get<std::string>();
        const Module* module = text::ModuleNamed(name, modules);
        if (!module) return Out(std::unexpect, "no loaded module '" + name + "' (modules lists them)");
        const uint64_t rva = Int(value["rva"]);
        if (rva >= module->size) return Out(std::unexpect, "the rva is past " + name + "'s image");
        place.address = module->base + rva;
        return place;
    }
    auto base = Address(value["base"], modules, false);
    if (!base) return base;
    if (value.contains("offset")) {
        const uint64_t offset = Int(value["offset"]);
        if (offset > UINT64_MAX - base->address) return Out(std::unexpect, "base + offset overflows");
        base->address += offset;
        return base;
    }
    if (!chains) return Out(std::unexpect, ChainRefused);
    for (const auto& hop : value["chain"]) base->chain.push_back(Int(hop));
    if (base->chain.size() == 1) return Out(std::unexpect, ChainNoHop);
    if (base->chain.size() > MaxChainHops)
        return Out(std::unexpect, "a chain takes at most " + std::to_string(MaxChainHops) + " offsets");
    return base;
}

// A path's steps joined by '.', as the pack walks them; why not, or empty.
inline std::string Path(const Json& value, std::string& out, size_t deepest) {
    out.clear();
    if (value.size() > deepest) return "a path is up to " + std::to_string(deepest) + " steps";
    for (const auto& step : value) {
        const auto name = step.get<std::string>();
        if (name.empty() || name.find('.') != std::string::npos || name.size() >= MaxText)
            return "a path step is one field name ([Owner:]Name, no '.'): '" + name + "'";
        out += (out.empty() ? "" : ".") + name;
    }
    return {};
}
}
}
