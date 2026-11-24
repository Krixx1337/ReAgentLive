// reagentlive CLI: one builder per verb, named in the verb table's rows
// (results/verbs.h, packs/ue/cli/results/verbs_ue.h). Each turns a call's
// checked args into a Command; an error about one key starts with that key.
#pragma once
#include <span>
#include <string>
#include <string_view>
#include "reagentlive/frontends/cli/call/build.h"

namespace reagentlive::cli::build {
inline Parsed Fail(std::string why) { return Parsed(std::unexpect, std::move(why)); }

// An addr argument (with `chains`, also a chain) into the request's address
// and the command's chain; why not, or empty.
inline std::string Place(const Args& args, std::string_view key, const ParseContext& context, Command& command, bool chains) {
    auto place = value::Address(args.At(key), context.modules, chains);
    if (!place) return std::string(key) + ": " + place.error();
    command.request.address = place->address;
    command.chain = std::move(place->chain);
    return {};
}

// Core (build.cpp, build_memory.cpp, build_code.cpp).
Parsed Help(const Args& args, const ParseContext& context);
Parsed Status(const Args& args, const ParseContext& context);
Parsed Quit(const Args& args, const ParseContext& context);
Parsed Usage(const Args& args, const ParseContext& context);
Parsed Batch(const Args& args, const ParseContext& context);
Parsed Run(const Args& args, const ParseContext& context);
Parsed Read(const Args& args, const ParseContext& context);
Parsed MemSnap(const Args& args, const ParseContext& context);
Parsed MemDiff(const Args& args, const ParseContext& context);
Parsed Peek(const Args& args, const ParseContext& context);
Parsed Write(const Args& args, const ParseContext& context);
Parsed TimedWrites(const Args& args, const ParseContext& context);
Parsed Modules(const Args& args, const ParseContext& context);
Parsed Search(const Args& args, const ParseContext& context);
Parsed SearchJob(const Args& args, const ParseContext& context);
Parsed WatchStart(const Args& args, const ParseContext& context);
Parsed WatchRead(const Args& args, const ParseContext& context);
Parsed WatchStop(const Args& args, const ParseContext& context);
Parsed Disasm(const Args& args, const ParseContext& context);
Parsed Xrefs(const Args& args, const ParseContext& context);
Parsed Touches(const Args& args, const ParseContext& context);
Parsed Vtable(const Args& args, const ParseContext& context);
Parsed SigCheck(const Args& args, const ParseContext& context);
Parsed LabelsSign(const Args& args, const ParseContext& context);
Parsed LabelsPort(const Args& args, const ParseContext& context);
Parsed LabelsCheck(const Args& args, const ParseContext& context);
Parsed DispScan(const Args& args, const ParseContext& context);
Parsed SigBytes(const Args& args, const ParseContext& context);

// Unreal pack (packs/ue/cli/call/build_unreal.cpp).
Parsed Find(const Args& args, const ParseContext& context);
Parsed Call(const Args& args, const ParseContext& context);
Parsed CallPure(const Args& args, const ParseContext& context);
Parsed Release(const Args& args, const ParseContext& context);
Parsed Funcs(const Args& args, const ParseContext& context);
Parsed Func(const Args& args, const ParseContext& context);
Parsed Script(const Args& args, const ParseContext& context);
Parsed Enum(const Args& args, const ParseContext& context);
Parsed GameThread(const Args& args, const ParseContext& context);
Parsed Name(const Args& args, const ParseContext& context);
Parsed Describe(const Args& args, const ParseContext& context);
Parsed MemberName(const Args& args, const ParseContext& context);
Parsed LabelsExport(const Args& args, const ParseContext& context);
Parsed LayoutExport(const Args& args, const ParseContext& context);
Parsed LayoutCheck(const Args& args, const ParseContext& context);
Parsed Entries(const Args& args, const ParseContext& context);
Parsed Rows(const Args& args, const ParseContext& context);
Parsed AssetsExport(const Args& args, const ParseContext& context);
Parsed Get(const Args& args, const ParseContext& context);
Parsed LineTrace(const Args& args, const ParseContext& context);
Parsed CallsStart(const Args& args, const ParseContext& context);
Parsed CallsRead(const Args& args, const ParseContext& context);
Parsed CallsStop(const Args& args, const ParseContext& context);
Parsed Census(const Args& args, const ParseContext& context);
Parsed NameRefs(const Args& args, const ParseContext& context);
Parsed NameFind(const Args& args, const ParseContext& context);
Parsed Each(const Args& args, const ParseContext& context);
Parsed EachCall(const Args& args, const ParseContext& context);
Parsed Owner(const Args& args, const ParseContext& context);
Parsed Route(const Args& args, const ParseContext& context);
}
