// reagentlive CLI: one parser per verb, named in the verb table's rows
// (results/verbs.h, results/ue/verbs_ue.h). Each turns a line's sorted
// arguments into a Command; an empty error means "show the usage line".
#pragma once
#include <span>
#include <string>
#include <string_view>
#include "reagentlive/frontends/cli/syntax/parse.h"

namespace reagentlive::cli::parse {
inline Parsed Fail(std::string why = {}) { return Parsed(std::unexpect, std::move(why)); }

// Core (parse.cpp, parse_memory.cpp, parse_code.cpp).
Parsed Help(const Args& args, const ParseContext& context);
Parsed Status(const Args& args, const ParseContext& context);
Parsed Quit(const Args& args, const ParseContext& context);
Parsed Stats(const Args& args, const ParseContext& context);
Parsed Batch(const Args& args, const ParseContext& context);
Parsed Read(const Args& args, const ParseContext& context);
Parsed Peek(const Args& args, const ParseContext& context);
Parsed ReadBatch(const Args& args, const ParseContext& context);
Parsed Write(const Args& args, const ParseContext& context);
Parsed Trial(const Args& args, const ParseContext& context);
Parsed TrialList(const Args& args, const ParseContext& context);
Parsed TrialClear(const Args& args, const ParseContext& context);
Parsed Base(const Args& args, const ParseContext& context);
Parsed Modules(const Args& args, const ParseContext& context);
Parsed Search(const Args& args, const ParseContext& context);
Parsed Pin(const Args& args, const ParseContext& context);
Parsed PinList(const Args& args, const ParseContext& context);
Parsed PinClear(const Args& args, const ParseContext& context);
Parsed Watch(const Args& args, const ParseContext& context);
Parsed WatchStart(const Args& args, const ParseContext& context);
Parsed WatchRead(const Args& args, const ParseContext& context);
Parsed WatchStop(const Args& args, const ParseContext& context);
Parsed Disasm(const Args& args, const ParseContext& context);
Parsed Xrefs(const Args& args, const ParseContext& context);
Parsed Touches(const Args& args, const ParseContext& context);
Parsed Region(const Args& args, const ParseContext& context);
Parsed Vtable(const Args& args, const ParseContext& context);
Parsed SigCheck(const Args& args, const ParseContext& context);
Parsed SigFile(const Args& args, const ParseContext& context);
Parsed SigBytes(const Args& args, const ParseContext& context);

// Unreal pack (ue/parse_unreal.cpp).
Parsed Find(const Args& args, const ParseContext& context);
Parsed Call(const Args& args, const ParseContext& context);
Parsed Release(const Args& args, const ParseContext& context);
Parsed Funcs(const Args& args, const ParseContext& context);
Parsed Func(const Args& args, const ParseContext& context);
Parsed Script(const Args& args, const ParseContext& context);
Parsed Params(const Args& args, const ParseContext& context);
Parsed Name(const Args& args, const ParseContext& context);
Parsed Describe(const Args& args, const ParseContext& context);
Parsed Snap(const Args& args, const ParseContext& context);
Parsed Record(const Args& args, const ParseContext& context);
Parsed Fields(const Args& args, const ParseContext& context);
Parsed Dump(const Args& args, const ParseContext& context);
Parsed Label(const Args& args, const ParseContext& context);
Parsed ExportLabels(const Args& args, const ParseContext& context);
Parsed Entries(const Args& args, const ParseContext& context);
Parsed Get(const Args& args, const ParseContext& context);
Parsed Census(const Args& args, const ParseContext& context);
Parsed CensusDiff(const Args& args, const ParseContext& context);
Parsed Refs(const Args& args, const ParseContext& context);
Parsed NameFind(const Args& args, const ParseContext& context);
Parsed Each(const Args& args, const ParseContext& context);
}
