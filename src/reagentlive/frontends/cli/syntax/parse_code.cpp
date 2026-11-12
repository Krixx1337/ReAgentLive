// reagentlive CLI: parsing the native code verbs (disasm, xrefs, touches, region, vtable, sig-*).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli::parse {
using namespace text;

// disasm <addr> [count] [--function] [--follow]: one read, decoded in the console.
Parsed Disasm(const Args& args, const ParseContext& context) {
    Command command;
    uint64_t count = DefaultDisasm;
    std::string why;
    if (!ChainAddress(args.At(0), context.refs, context.modules, command.request.address, command.chain, &why)) return Fail(why);
    if (!args.At(1).empty() && (!Number(args.At(1), count) || !count || count > MaxDisasm))
        return Fail("count must be 1.." + std::to_string(MaxDisasm));
    command.function = args.Has("--function");
    command.follow = args.Has("--follow");
    command.kind = Kind::Disasm;
    command.limit = static_cast<uint32_t>(count);
    command.request.op = op::Read;
    return command;
}

// xrefs <addr> [--module M] [--all]: relative fields and stored pointers aimed at addr.
Parsed Xrefs(const Args& args, const ParseContext& context) {
    Command command;
    if (!Address(args.At(0), context.refs, context.modules, command.request.address)) return Fail();
    command.all = args.Has("--all");
    command.target = args.Value("--module");
    command.kind = Kind::Xrefs;
    command.request.op = op::Xrefs;
    return command;
}

// touches <addr> <offset>... [--range N]: functions near addr whose [reg + disp] operands use the offsets.
Parsed Touches(const Args& args, const ParseContext& context) {
    Command command;
    if (!Address(args.At(0), context.refs, context.modules, command.request.address)) return Fail();
    command.range = touch::DefaultRange;
    uint64_t n = 0;
    if (args.Has("--range")) {
        if (!Number(args.Value("--range"), n) || !n || n > touch::MaxRange)
            return Fail("--range must be 1.." + std::to_string(touch::MaxRange));
        command.range = n;
    }
    for (size_t i = 1; i < args.positionals.size(); ++i) {
        if (!Number(args.At(i), n) || !n || n > UINT32_MAX || command.offsets.size() == touch::MaxOffsets)
            return Fail("up to " + std::to_string(touch::MaxOffsets) + " offsets, each a number above 0");
        if (std::ranges::find(command.offsets, static_cast<uint32_t>(n)) == command.offsets.end())
            command.offsets.push_back(static_cast<uint32_t>(n));
    }
    command.kind = Kind::Touches;
    command.request.op = op::ReadBatch;
    return command;
}

// region <addr>: one VirtualQuery in the target, named by the console.
Parsed Region(const Args& args, const ParseContext& context) {
    Command command;
    std::string why;
    if (!ChainAddress(args.At(0), context.refs, context.modules, command.request.address, command.chain, &why)) return Fail(why);
    command.kind = Kind::Region;
    command.request.op = op::Region;
    return command;
}

// vtable <addr> [count] [--offset N] [--table] [--resolve]: slots until the first that is not code.
Parsed Vtable(const Args& args, const ParseContext& context) {
    Command command;
    uint64_t count = 0;
    std::string why;
    if (!ChainAddress(args.At(0), context.refs, context.modules, command.request.address, command.chain, &why)) return Fail(why);
    if (!args.At(1).empty() && (!Number(args.At(1), count) || !count || count > MaxRead / 8))
        return Fail("count must be 1.." + std::to_string(MaxRead / 8));
    command.table = args.Has("--table");
    command.bodies = args.Has("--resolve");
    if (args.Has("--offset")) {
        if (!Number(args.Value("--offset"), command.offset) || command.offset > 0x100000) return Fail("--offset is 0..0x100000");
        if (command.table) return Fail("--offset reads a table pointer; --table says there is none");
    }
    command.kind = Kind::Vtable;
    command.limit = static_cast<uint32_t>(count);
    command.request.op = op::Read;
    return command;
}

// sig-check <pattern> [--module M] [--expect <addr> [--offset N | --resolve next,field[,size]]]
Parsed SigCheck(const Args& args, const ParseContext& context) {
    Command command;
    command.kind = Kind::Sig;
    command.sigMode = Command::SigMode::Check;
    command.request.op = op::Search;
    auto pattern = sig::ParsePattern(args.From(0));
    if (!pattern) return Fail(pattern.error());
    command.pattern = std::move(*pattern);
    if (auto anchor = sig::ChooseAnchor(command.pattern); !anchor) return Fail(anchor.error());
    command.target = args.Value("--module");
    uint64_t n = 0;
    if (args.Has("--expect")) {
        if (!Address(args.Value("--expect"), context.refs, context.modules, n)) return Fail("--expect is an address");
        command.expect = n;
    }
    if (args.Has("--offset")) {
        if (!Number(args.Value("--offset"), n) || n > UINT32_MAX) return Fail("--offset is a number");
        if (!command.expect) return Fail("--offset needs --expect");
        if (args.Has("--resolve")) return Fail("--offset and --resolve do not combine");
        command.offset = n;
    }
    if (args.Has("--resolve")) {
        auto resolve = sig::ParseResolve(args.Value("--resolve"));
        if (!resolve) return Fail(resolve.error());
        command.resolve = *resolve;
    }
    return command;
}

// sig-file <patterns>: one JSON line per signature.
Parsed SigFile(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Sig;
    command.sigMode = Command::SigMode::File;
    command.request.op = op::Search;
    command.file = args.At(0);
    return command;
}

// sig-bytes <addr> [len]: live bytes to start a signature from.
Parsed SigBytes(const Args& args, const ParseContext& context) {
    Command command;
    uint64_t length = sig::DefaultBytes;
    if (!Address(args.At(0), context.refs, context.modules, command.request.address)) return Fail();
    if (!args.At(1).empty() && !Number(args.At(1), length)) return Fail("length must be a number");
    if (!length || length > sig::MaxPattern) return Fail("length must be 1.." + std::to_string(sig::MaxPattern));
    command.kind = Kind::Sig;
    command.sigMode = Command::SigMode::Bytes;
    command.request.op = op::Read;
    command.request.length = static_cast<uint32_t>(length);
    return command;
}
}
