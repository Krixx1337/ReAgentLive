// reagentlive CLI: tokens, numbers, refs and addresses as the console syntax writes them.
#pragma once
#include <optional>
#include <string>
#include "reagentlive/frontends/cli/syntax/command.h"

namespace reagentlive::cli {
namespace text {
inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}
// Next whitespace-separated token; `rest` keeps what follows.
inline std::string_view Next(std::string_view& rest) {
    rest = Trim(rest);
    size_t end = 0;
    while (end < rest.size() && rest[end] != ' ' && rest[end] != '\t') ++end;
    const auto token = rest.substr(0, end);
    rest = Trim(rest.substr(end));
    return token;
}
// Inside double quotes a backslash escapes only `"` and `\`; any other
// backslash is literal, so Windows paths need no doubling.
inline bool QuoteEscape(std::string_view text, size_t i) {
    return text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\');
}
// A double-quoted string at the start of `rest`, unescaped; `rest` keeps
// what follows. nullopt when `rest` does not start with a quote, or with
// `unterminated` set when the quote is never closed.
inline std::optional<std::string> Quoted(std::string_view& rest, bool& unterminated) {
    unterminated = false;
    rest = Trim(rest);
    if (!rest.starts_with('"')) return std::nullopt;
    std::string text;
    for (size_t i = 1; i < rest.size(); ++i) {
        if (QuoteEscape(rest, i)) { text += rest[++i]; continue; }
        if (rest[i] == '"') { rest = Trim(rest.substr(i + 1)); return text; }
        text += rest[i];
    }
    unterminated = true;
    return std::nullopt;
}
// The first of `chars` at or after `start` outside double quotes; npos if none.
inline size_t FindUnquoted(std::string_view text, std::string_view chars, size_t start = 0) {
    bool quoted = false;
    for (size_t i = start; i < text.size(); ++i) {
        if (quoted) {
            if (QuoteEscape(text, i)) ++i;
            else if (text[i] == '"') quoted = false;
        } else if (text[i] == '"') {
            quoted = true;
        } else if (chars.find(text[i]) != std::string_view::npos) {
            return i;
        }
    }
    return std::string_view::npos;
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
// $N → refs[N-1], or a literal ref.
inline bool Reference(std::string_view s, std::span<const Ref> refs, Ref& out) {
    if (LiteralRef(s, out)) return true;
    uint64_t n = 0;
    if (s.size() < 2 || s[0] != '$' || !Number(s.substr(1), n) || n == 0 || n > refs.size()) return false;
    out = refs[n - 1];
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
inline bool Address(std::string_view s, std::span<const Ref> refs, std::span<const Module> modules, uint64_t& out) {
    const size_t plus = s.find('+');
    const auto base = s.substr(0, plus);
    Ref ref{};
    const Module* module = nullptr;
    if (Reference(base, refs, ref)) out = ref.address;
    else if (Number(base, out)) {}
    else if ((module = ModuleNamed(base, modules))) out = module->base;
    else return false;
    if (plus == std::string_view::npos) return out != 0;
    uint64_t offset = 0;
    if (!Number(s.substr(plus + 1), offset) || offset > UINT64_MAX - out) return false;
    if (module && offset >= module->size) return false;
    out += offset;
    return out != 0;
}
// The object an address token is relative to ($N, $N+0x10 or a literal ref,
// with or without an offset); 0 for raw numbers and base.
inline uint64_t ObjectBase(std::string_view s, std::span<const Ref> refs) {
    Ref ref{};
    const auto base = s.substr(0, s.find('+'));
    return Reference(base, refs, ref) && ref.index >= 0 ? ref.address : 0;
}
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
// An address with an optional pointer chain: <addr>->0x10->0x8 (each
// offset a number). Read-only commands only.
inline bool ChainAddress(std::string_view s, std::span<const Ref> refs, std::span<const Module> modules, uint64_t& out,
                         std::vector<uint64_t>& chain) {
    const size_t arrow = s.find("->");
    if (!Address(s.substr(0, arrow), refs, modules, out)) return false;
    chain.clear();
    for (size_t at = arrow; at != std::string_view::npos;) {
        const size_t next = s.find("->", at + 2);
        uint64_t offset = 0;
        if (!Number(s.substr(at + 2, next == std::string_view::npos ? std::string_view::npos : next - at - 2), offset) ||
            chain.size() == MaxChainHops) return false;
        chain.push_back(offset);
        at = next;
    }
    return true;
}
inline bool IsChain(std::string_view s) { return s.find("->") != std::string_view::npos; }
constexpr auto ChainRefused = "a write goes to an address you resolved and read first, never through a pointer chain";

// $N and literal refs keep the full ref; a raw address is marked for identification by the agent.
inline bool Object(std::string_view s, std::span<const Ref> refs, Request& request) {
    Ref ref{};
    if (!Reference(s, refs, ref)) {
        uint64_t address = 0;
        if (!Number(s, address) || !address) return false;
        ref = {static_cast<uintptr_t>(address), -1, 0};
    }
    SetRef(request, ref);
    return true;
}
// A bare identifier names a class or struct, e.g. Character or BP_Door_C.
inline bool ClassName(std::string_view s) {
    if (s.empty() || s.size() >= MaxTypeName || s == "base" || (s[0] >= '0' && s[0] <= '9')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
// An object ref or address, or else a class name for the console to resolve.
inline bool ObjectOrClass(std::string_view s, std::span<const Ref> refs, Command& command) {
    if (Object(s, refs, command.request)) return true;
    if (!ClassName(s)) return false;
    command.className = s;
    return true;
}
}

// One-shot commands: split on ';' and newlines, blanks dropped. Separators
// inside braces belong to the command (batch { a; b }); a newline there
// counts as ';'. Inside double quotes nothing separates or nests.
inline std::vector<std::string> SplitCommands(std::string_view commands) {
    std::vector<std::string> lines;
    std::string line;
    int depth = 0;
    bool quoted = false;
    const auto flush = [&] {
        const auto trimmed = text::Trim(line);
        if (!trimmed.empty()) lines.emplace_back(trimmed);
        line.clear();
    };
    for (size_t i = 0; i < commands.size(); ++i) {
        const char c = commands[i];
        if (quoted) {
            line += c;
            if (text::QuoteEscape(commands, i)) line += commands[++i];
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '{') ++depth;
        else if (c == '}' && depth) --depth;
        if ((c == ';' || c == '\n') && !depth) { flush(); continue; }
        line += c == '\n' ? ';' : c;
    }
    flush();
    return lines;
}

}
