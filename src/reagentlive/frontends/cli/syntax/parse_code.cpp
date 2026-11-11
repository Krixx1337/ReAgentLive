// reagentlive CLI: parsing the native code verbs (disasm, xrefs, region, vtable, sig).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli::parse {
std::optional<Parsed> Code(std::string_view verb, std::string_view rest, [[maybe_unused]] std::span<const Ref> refs,
    [[maybe_unused]] std::span<const Module> modules) {
    using namespace text;
    Command command;
    [[maybe_unused]] Request& request = command.request;
    const auto fail = [](std::string why) { return Parsed(std::unexpect, std::move(why)); };
    if (verb == "disasm") {
        constexpr auto usage = "usage: disasm <addr>[->off...] [count 1..64] [--function] [--follow]";
        uint64_t count = DefaultDisasm;
        if (!ChainAddress(Next(rest), refs, modules, request.address, command.chain)) return fail(usage);
        if (!rest.empty() && !rest.starts_with("--") && (!Number(Next(rest), count) || !count || count > MaxDisasm))
            return fail(usage);
        while (!rest.empty()) {
            const auto option = Next(rest);
            if (option == "--function" && !command.function) command.function = true;
            else if (option == "--follow" && !command.follow) command.follow = true;
            else return fail(usage);
        }
        command.kind = Kind::Disasm;
        command.limit = static_cast<uint32_t>(count);
        request.op = op::Read;
        return command;
    }
    // xrefs <addr> [--module M] [--all]: relative fields and stored pointers aimed at addr.
    if (verb == "xrefs") {
        constexpr auto usage = "usage: xrefs <addr> [--module <name>] [--all]";
        if (!Address(Next(rest), refs, modules, request.address)) return fail(usage);
        while (!rest.empty()) {
            const auto option = Next(rest);
            if (option == "--all" && !command.all) command.all = true;
            else if (option == "--module" && command.target.empty()) {
                command.target = Next(rest);
                if (command.target.empty()) return fail(usage);
            } else return fail(usage);
        }
        command.kind = Kind::Xrefs;
        request.op = op::Xrefs;
        return command;
    }
    // touches <addr> <offset>... [--range N]: functions near addr whose [reg + disp] operands use the offsets.
    if (verb == "touches") {
        constexpr auto usage = "usage: touches <addr> <offset> [offset ...] [--range <bytes>]";
        if (!Address(Next(rest), refs, modules, request.address)) return fail(usage);
        command.range = touch::DefaultRange;
        bool range = false;
        while (!rest.empty()) {
            const auto token = Next(rest);
            uint64_t n = 0;
            if (token == "--range" && !range) {
                if (!Number(Next(rest), n) || !n || n > touch::MaxRange) return fail("--range must be 1.." + std::to_string(touch::MaxRange));
                command.range = n;
                range = true;
            } else if (Number(token, n) && n && n <= UINT32_MAX && command.offsets.size() < touch::MaxOffsets) {
                if (std::ranges::find(command.offsets, static_cast<uint32_t>(n)) == command.offsets.end())
                    command.offsets.push_back(static_cast<uint32_t>(n));
            } else return fail(usage);
        }
        if (command.offsets.empty()) return fail(usage);
        command.kind = Kind::Touches;
        request.op = op::ReadBatch;
        return command;
    }
    // region <addr>: one VirtualQuery in the target, named by the console.
    if (verb == "region") {
        if (!ChainAddress(Next(rest), refs, modules, request.address, command.chain) || !rest.empty())
            return fail("usage: region <addr>[->off...]");
        command.kind = Kind::Region;
        request.op = op::Region;
        return command;
    }
    // vtable <addr> [count] [--offset N] [--table] [--resolve]: slots until the first that is not code.
    if (verb == "vtable") {
        constexpr auto usage = "usage: vtable <addr>[->off...] [count 1..512] [--offset N] [--table] [--resolve]";
        uint64_t count = 0;
        if (!ChainAddress(Next(rest), refs, modules, request.address, command.chain)) return fail(usage);
        if (!rest.empty() && !rest.starts_with("--") && (!Number(Next(rest), count) || !count || count > MaxRead / 8))
            return fail(usage);
        bool offset = false;
        while (!rest.empty()) {
            const auto option = Next(rest);
            if (option == "--table" && !command.table) command.table = true;
            else if (option == "--resolve" && !command.bodies) command.bodies = true;
            else if (option == "--offset" && !offset) {
                if (!Number(Next(rest), command.offset) || command.offset > 0x100000) return fail(usage);
                offset = true;
            } else return fail(usage);
        }
        if (offset && command.table) return fail("--offset reads a table pointer; --table says there is none");
        command.kind = Kind::Vtable;
        command.limit = static_cast<uint32_t>(count);
        request.op = op::Read;
        return command;
    }
    // sig --check <pattern> [--module M] [--expect <addr> [--offset N | --resolve next,field[,size]]]
    // sig --check --file <patterns> | sig --bytes <addr> [len]
    if (verb == "sig") {
        constexpr auto usage = "usage: sig --check <pattern> [--module <name>] [--expect <addr>] [--offset N | "
                               "--resolve next,field[,size]] | sig --check --file <patterns> | sig --bytes <addr> [len]";
        command.kind = Kind::Sig;
        request.op = op::Search;
        const auto mode = Next(rest);
        if (mode == "--bytes") {
            uint64_t length = sig::DefaultBytes;
            if (!Address(Next(rest), refs, modules, request.address)) return fail(usage);
            if (!rest.empty() && (!Number(Next(rest), length) || !rest.empty())) return fail(usage);
            if (!length || length > sig::MaxPattern) return fail("length must be 1.." + std::to_string(sig::MaxPattern));
            command.sigMode = Command::SigMode::Bytes;
            request.op = op::Read;
            request.length = static_cast<uint32_t>(length);
            return command;
        }
        if (mode != "--check") return fail(usage);
        if (rest.starts_with("--file")) {
            Next(rest);
            command.file = Next(rest);
            if (command.file.empty() || !rest.empty()) return fail(usage);
            command.sigMode = Command::SigMode::File;
            return command;
        }
        const size_t options = rest.find(" --");
        auto pattern = sig::ParsePattern(rest.starts_with("--") ? std::string_view{} : rest.substr(0, options));
        if (!pattern) return fail(pattern.error());
        command.pattern = std::move(*pattern);
        if (auto anchor = sig::ChooseAnchor(command.pattern); !anchor) return fail(anchor.error());
        rest = rest.starts_with("--") ? rest : options == std::string_view::npos ? std::string_view{} : Trim(rest.substr(options));
        bool offset = false;
        while (!rest.empty()) {
            const auto option = Next(rest), value = Next(rest);
            uint64_t n = 0;
            if (value.empty()) return fail(usage);
            if (option == "--module" && command.target.empty()) command.target = value;
            else if (option == "--expect" && !command.expect && Address(value, refs, modules, n)) command.expect = n;
            else if (option == "--offset" && !offset && Number(value, n) && n <= UINT32_MAX) {
                command.offset = n;
                offset = true;
            } else if (option == "--resolve" && !command.resolve) {
                auto resolve = sig::ParseResolve(value);
                if (!resolve) return fail(resolve.error());
                command.resolve = *resolve;
            } else return fail(usage);
        }
        if (offset && !command.expect) return fail("--offset needs --expect");
        if (offset && command.resolve) return fail("--offset and --resolve do not combine");
        return command;
    }
    return std::nullopt;
}
}
