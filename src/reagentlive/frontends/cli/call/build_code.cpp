// reagentlive CLI: building the native code verbs (disasm, xrefs, touches, vtable, sig-*).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/call/build_internal.h"

namespace reagentlive::cli::build {
using namespace text;
// disasm: decoded in the console. function lists the whole function: count defaults to its cap.
Parsed Disasm(const Args& args, const ParseContext& context) {
    Command command;
    command.function = args.Has("function");
    const uint64_t most = command.function ? MaxFunctionDisasm : MaxDisasm;
    const uint64_t count = args.Int("count", command.function ? MaxFunctionDisasm : DefaultDisasm);
    if (auto why = Place(args, "addr", context, command, true); !why.empty()) return Fail(why);
    if (!count || count > most) return Fail("count must be 1.." + std::to_string(most));
    command.resume = args.Has("resume");
    command.file = args.Str("file"); // save's file.
    if (args.Has("save") && command.file.empty()) return Fail("file: save needs a file");
    if (!command.function && command.resume) return Fail("resume needs function");
    if (!command.function && !command.file.empty()) return Fail("save needs function");
    if (args.Has("callees")) {
        const uint64_t depth = args.Int("callees");
        if (!depth || depth > MaxCalleeDepth) return Fail("callees must be 1.." + std::to_string(MaxCalleeDepth));
        command.calleeDepth = static_cast<uint32_t>(depth);
    }
    command.labels = args.Str("labels");
    if (args.Has("labels") && command.labels.empty()) return Fail("labels needs a file");
    command.follow = args.Has("follow");
    command.kind = Kind::Disasm;
    command.limit = static_cast<uint32_t>(count);
    command.request.op = op::Read;
    return command;
}

// xrefs: relative fields and stored pointers aimed at addr.
Parsed Xrefs(const Args& args, const ParseContext& context) {
    Command command;
    if (auto why = Place(args, "addr", context, command, false); !why.empty()) return Fail(why);
    command.all = args.Has("all");
    command.target = args.Str("module");
    command.kind = Kind::Xrefs;
    command.request.op = op::Xrefs;
    return command;
}

// touches: functions near addr whose [reg + disp] operands use the offsets.
Parsed Touches(const Args& args, const ParseContext& context) {
    Command command;
    if (auto why = Place(args, "addr", context, command, false); !why.empty()) return Fail(why);
    command.range = args.Int("range", touch::DefaultRange);
    if (!command.range || command.range > touch::MaxRange) return Fail("range must be 1.." + std::to_string(touch::MaxRange));
    for (const auto& offset : args.At("offsets")) {
        const uint64_t n = Args::Number(offset);
        if (!n || n > UINT32_MAX || command.offsets.size() == touch::MaxOffsets)
            return Fail("offsets: up to " + std::to_string(touch::MaxOffsets) + ", each a number above 0");
        if (std::ranges::find(command.offsets, static_cast<uint32_t>(n)) == command.offsets.end())
            command.offsets.push_back(static_cast<uint32_t>(n));
    }
    command.readsOnly = args.Has("reads");
    command.bases = args.Strs("bases");
    command.kind = Kind::Touches;
    command.request.op = op::ReadBatch;
    return command;
}

// disp-scan: every instruction in the main module's code with one [reg + disp] form, a page at a time.
Parsed DispScan(const Args& args, const ParseContext& context) {
    Command command;
    const uint64_t disp = args.Int("disp");
    if (!disp || disp > UINT32_MAX) return Fail("disp is 1..0xffffffff");
    command.offsets = {static_cast<uint32_t>(disp)};
    command.access = args.Has("access") ? args.Str("access") : "write";
    if (args.Has("width")) {
        const uint64_t width = args.Int("width");
        if (width != 1 && width != 2 && width != 4 && width != 8 && width != 16 && width != 32 && width != 64)
            return Fail("width is the operand's bytes: 1, 2, 4, 8, 16, 32 or 64");
        if (command.access == "any") return Fail("width: a lea has none; give access write or read");
        command.width = static_cast<uint32_t>(width);
    }
    if (args.Has("imm")) command.imm = args.Int("imm");
    command.bases = args.Strs("bases");
    if (args.Has("start"))
        if (auto why = Place(args, "start", context, command, false); !why.empty()) return Fail(why);
    command.kind = Kind::DispScan;
    command.request.op = op::ReadBatch;
    return command;
}

// vtable: slots until the first that is not code.
Parsed Vtable(const Args& args, const ParseContext& context) {
    Command command;
    const uint64_t count = args.Int("count");
    if (auto why = Place(args, "addr", context, command, true); !why.empty()) return Fail(why);
    if (args.Has("count") && (!count || count > MaxRead / 8)) return Fail("count must be 1.." + std::to_string(MaxRead / 8));
    command.table = args.Has("table");
    command.bodies = args.Has("resolve");
    command.rtti = args.Has("rtti");
    if (args.Has("offset")) {
        command.offset = args.Int("offset");
        if (command.offset > 0x100000) return Fail("offset is 0..0x100000");
        if (command.table) return Fail("offset reads a table pointer; table says there is none");
    }
    command.kind = Kind::Vtable;
    command.limit = static_cast<uint32_t>(count);
    command.request.op = op::Read;
    return command;
}

// sig-check: one pattern, or a file of them (one JSON line per signature).
Parsed SigCheck(const Args& args, const ParseContext& context) {
    Command command;
    command.kind = Kind::Sig;
    command.sigMode = Command::SigMode::Check;
    command.request.op = op::Search;
    if (args.Has("file")) {
        if (args.Has("pattern") || args.Has("module") || args.Has("expect") || args.Has("offset") || args.Has("resolve"))
            return Fail("file takes no pattern or other key: each line names its own");
        command.sigMode = Command::SigMode::File;
        command.file = args.Str("file");
        return command;
    }
    if (!args.Has("pattern")) return Fail("pattern: a pattern, or file");
    auto pattern = sig::ParsePattern(args.Str("pattern"));
    if (!pattern) return Fail("pattern: " + pattern.error());
    command.pattern = std::move(*pattern);
    if (auto anchor = sig::ChooseAnchor(command.pattern); !anchor) return Fail("pattern: " + anchor.error());
    command.target = args.Str("module");
    if (args.Has("expect")) {
        auto place = value::Address(args.At("expect"), context.modules);
        if (!place) return Fail("expect: " + place.error());
        command.expect = place->address;
    }
    if (args.Has("offset")) {
        const uint64_t n = args.Int("offset");
        if (n > UINT32_MAX) return Fail("offset is a 32-bit number");
        if (!command.expect) return Fail("offset needs expect");
        if (args.Has("resolve")) return Fail("offset and resolve do not combine");
        command.offset = n;
    }
    if (args.Has("resolve")) {
        auto resolve = sig::ParseResolve(args.Str("resolve"));
        if (!resolve) return Fail("resolve: " + resolve.error());
        command.resolve = *resolve;
    }
    return command;
}

// labels-sign: signatures on this build's user and global names.
Parsed LabelsSign(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::LabelsSign;
    command.request.op = op::Search;
    command.file = args.Str("file");
    command.target = args.Str("module");
    command.all = args.Has("retry");
    return command;
}

// labels-port: other builds' signed names, found in this one.
Parsed LabelsPort(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::LabelsPort;
    command.request.op = op::Search;
    command.file = args.Str("file");
    command.target = args.Str("module");
    return command;
}

Parsed LabelsCheck(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::LabelsCheck;
    command.request.op = op::Search;
    command.file = args.Str("file");
    command.target = args.Str("module");
    return command;
}

// sig-bytes: live bytes to start a signature from; a longer len is cut to sig::MaxPattern.
Parsed SigBytes(const Args& args, const ParseContext& context) {
    Command command;
    uint64_t length = args.Int("len", sig::DefaultBytes);
    if (auto why = Place(args, "addr", context, command, false); !why.empty()) return Fail(why);
    if (!length) return Fail("len must be at least 1");
    command.capped = length > sig::MaxPattern;
    length = std::min<uint64_t>(length, sig::MaxPattern);
    command.kind = Kind::Sig;
    command.sigMode = Command::SigMode::Bytes;
    command.request.op = op::Read;
    command.request.length = static_cast<uint32_t>(length);
    return command;
}
}
