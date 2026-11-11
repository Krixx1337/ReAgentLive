// reagentlive CLI console. Parses a line, calls the client API, prints the result.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/results/call_text.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/values/json_output.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli {
uint32_t FindProcess(std::wstring_view exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    uint32_t pid = 0;
    const std::wstring wanted(exeName);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        if (!_wcsicmp(entry.szExeFile, wanted.c_str())) { pid = entry.th32ProcessID; break; }
    CloseHandle(snapshot);
    return pid;
}
}

namespace reagentlive::cli::detail {

std::span<const Module> LoadedModules(Session& session) {
    if (session.modules.empty() && !session.modulesListed) RefreshModules(session);
    return session.modules;
}

bool RefreshModules(Session& session) {
    Client& client = session.client;
    if (session.modulesListed) return false;
    session.modulesListed = true;
    if (auto modules = client.Modules(); modules) session.modules = std::move(*modules);
    return true;
}

namespace {
std::string HelpText() {
    char buffer[12288];
    std::snprintf(buffer, sizeof(buffer),
        "  find [text] [--class C] [--from N]   names containing text and/or instances of class C or a subclass;\n"
        "                                       24 per page (results become $1..$N), prints the next page's cursor;\n"
        "                                       only the first page counts the total\n"
        "  census [--class C] [--match text] [--changed] [--limit N] [--from N]\n"
        "                                       live instances per class (most first, pages of 100 rows, 0 = all;\n"
        "                                       --from next_cursor pages the stored census without a new scan);\n"
        "                                       --match keeps class names containing text; --changed prints only\n"
        "                                       classes whose count differs from the last census of that --class;\n"
        "                                       class refs become $1..$N\n"
        "  name-find <text> [--from N]          FName indices whose text contains text (gameplay tags, row names)\n"
        "  each <Class> <Field|[Owner:]Function> [--seconds N]\n"
        "                                       one value per live instance, read in one tick; functions must be\n"
        "                                       const or pure with outputs only; --seconds repeats and prints changes\n"
        "  read <addr> [len] [--as T]           hex dump, len 1..%zu (default 64); --as i8..u64|f32|f64|ptr adds\n"
        "                                       decoded values; --as \"u8 phase, pad 3, f64[3] start\" decodes a block\n"
        "                                       of mixed types as {name: value}\n"
        "  read-batch <addr> <len> [...]        read up to 8 ranges in one host tick\n"
        "  peek <addr> [count] [--label <file> [name]]\n"
        "                                       what 1..64 qwords hold (default 8): object, module (+rva), heap\n"
        "                                       or value (floats, double, name_hint); --label appends one\n"
        "                                       confirmed global to a labels file (default name FName_<hint>)\n"
        "  write <addr> <hex bytes> [--expect <hex>]\n"
        "                                       e.g. write $1+0x120 00 00 80 3f (max %zu bytes); --expect writes only\n"
        "                                       while memory still holds those bytes, else prints what it found\n"
        "  call <object> <[Class:]Function> [hex params]\n"
        "  call <object> <[Class:]Function> Name=value [...] | --typed\n"
        "                                       params must match the function's size; prints the block after the call;\n"
        "                                       kept: true when outputs own memory (handles, strings): the agent\n"
        "                                       reuses the block so the next call releases the old values\n"
        "  release <object> <[Class:]Function> [hex params]\n"
        "                                       one more call (zeroed inputs by default) that drops the kept block\n"
        "  base                                 exe base and size\n"
        "  modules                              loaded modules, exe first (also refreshes the cached list)\n"
        "  search <hex|u32|i32|u64|ptr|f32|f64|text> <value> [--wide] [--from A] [--to A] [--module M] [--align N]\n"
        "         [--writable] [--heap] [--paged] [--background] [--max N]\n"
        "                                       addresses holding the value in resident, cacheable memory;\n"
        "                                       text: quote it (\"a b; c\") to keep spaces, ';' and '--';\n"
        "                                       --heap: private memory only; --paged: also non-resident pages (slow);\n"
        "                                       --background: agent worker thread, no host-tick cost (host opt-in)\n"
        "                                       (typed values align to their size; default 64 hits); prints\n"
        "                                       a resume address while more of the range is left\n"
        "  pin <addr> <hex> [--seconds N|--ms N] [--expect <hex>]\n"
        "                                       write the bytes every tick (default 10 s, up to 60 s); $N+off\n"
        "                                       targets stop when that object is freed or replaced\n"
        "  pin list | pin clear [id]            pins with state and write counts; clear one or all\n"
        "  write <addr> <hex> --trial --owner <ref> [--object <addr>] --expect <hex> [--seconds N]\n"
        "                                       one write the agent undoes (default 10 s, up to 60 s); stops\n"
        "                                       as conflict if the game changes it, abandoned if the owner or\n"
        "                                       --object's vtable changes; owner: a live actor or component\n"
        "  write --trial list | write --trial clear [id]  states (active, restored, conflict, abandoned);\n"
        "                                       clear restores active trials\n"
        "  watch <seconds|Nms> <addr>:<len>... [--then call <object> <Function> [hex]]\n"
        "                                       sample up to 8 ranges every tick (up to 30 s) and print the\n"
        "                                       rows; --then runs the call right after sampling starts\n"
        "  watch --start <duration> [--every <duration>] [--follow <root>] <addr|offset>:<len>...\n"
        "                                       a capture the agent keeps (up to 60 min with --every); returns at\n"
        "                                       once; --follow continues on a host root's replacement (status lists\n"
        "                                       roots); watch --read: the rows so far; watch --stop: end it\n"
        "  fields <object|Class>                reflected properties by offset; a class or struct lists its own\n"
        "  dump <object|Class> [--match text] [--nonzero] [--changed [file]]\n"
        "                                       fields with their current values (use on instances, incl. Default__\n"
        "                                       objects); filters keep matching names, non-zero values, or fields\n"
        "                                       changed since snap (or a snapshot file)\n"
        "  label <object|Class> <offset> <type>[n] <Name>\n"
        "                                       name an unreflected member in <console>.native.txt; fields, dump\n"
        "                                       and get then show it (native, stale after a patch)\n"
        "  export-labels <file> [--module M] [--kinds exec,vtable,interface,global]\n"
        "                                       names for a static-analysis tool (JSON lines, one build): native\n"
        "                                       exec thunks, vtables, interface implementations, and the globals\n"
        "                                       peek --label put in the file (always kept); import with\n"
        "                                       import_labels.py. After another tool's mappings: interface,global\n"
        "  entries <object> <[Owner:]Field> [--from N]\n"
        "                                       one page of an array, set or map: elements, or key and value;\n"
        "                                       names and object pointers resolved; prints the next page's cursor\n"
        "  get <object> <Field[.Field...]>...   named fields, following object pointers and structs (up to 16 paths)\n"
        "  batch { <command>; <command>; ... }  up to 64 commands in as few host ticks as possible; one result each\n"
        "                                       plus same_tick; a $N from an earlier find in the batch works\n"
        "  sig --check <pattern> [--module M] [--expect <addr>] [--offset N | --resolve next,field[,size]]\n"
        "                                       is a byte signature (\"48 8b ?? 89\" or \"0x48, 0x8B, -1, 0x89\", up to\n"
        "                                       96 bytes) unique in the module (default: the exe), and does it land\n"
        "                                       at --expect (+ --offset when it starts inside the function; with\n"
        "                                       --resolve, expect is the target); reports mismatches, diverges_at\n"
        "                                       and warnings for unmasked addresses\n"
        "  sig --check --file <patterns>        one JSON line per signature; names the first that holds\n"
        "  sig --bytes <addr> [len]             live bytes (default 64, up to 96) in both forms, with\n"
        "                                       address fields wildcarded in suggested\n"
        "  disasm <addr> [count] [--function] [--follow]\n"
        "                                       1..64 instructions (default 24, up to 512 bytes) decoded from live\n"
        "                                       bytes, with targets, string hints and address fields; --function\n"
        "                                       lists the containing function (exception table); --follow steps\n"
        "                                       through up to 3 unconditional direct jumps\n"
        "  xrefs <addr> [--module M] [--all]    what points here: rel32 fields (call, jmp, jcc, lea, load, store,\n"
        "                                       rip) and stored pointers, each with its function; --all adds\n"
        "                                       unknown hits (likely chance bytes)\n"
        "  touches <addr> <offset>... [--range N]\n"
        "                                       functions within N bytes of addr (default 0x80000) whose\n"
        "                                       [reg + disp] operands use the offsets (up to 8), most offsets\n"
        "                                       first, writes marked: the code behind an unreflected member\n"
        "  vtable <addr> [count] [--offset N] [--table] [--resolve]\n"
        "                                       an object's virtual table: slots until the first entry\n"
        "                                       that is not code (up to 512); --offset: the table pointer at\n"
        "                                       addr+N (an interface part); --table: addr is the table;\n"
        "                                       --resolve: where jump stubs lead\n"
        "  region <addr>                        what the memory is: code, rodata, data (in an image, by\n"
        "                                       section), mapped, private, reserved, free; base, size, protection\n"
        "  <addr>->0x10->0x8                    a pointer chain in read, peek, vtable, disasm, region: each ->off\n"
        "                                       but the last reads a pointer; read --as cstr|wstr reads text\n"
        "  describe <address|$N>                object name, class and reusable ref\n"
        "  name <index>...                      FName comparison indices as text (FName #N in dumps)\n"
        "  func <object|class> <[Owner:]Function>  reflected metadata and exec entry address\n"
        "  func <object|class> <[Owner:]Function> --script\n"
        "                                       a Blueprint function's bytecode: the names, functions and\n"
        "                                       properties right after their opcodes, each confirmed live\n"
        "  refs <name|#index>                   Blueprint functions whose bytecode names it (EX_NameConst)\n"
        "  funcs <object|class> [text] [--from N]   list functions on the class chain\n"
        "  params <object|class> <[Owner:]Function>  reflected parameter signature\n"
        "  snap <ref> [file]                    save a baseline for dump --changed (file: across runs)\n"
        "  record <ref> <seconds> <file> <field>...  sample 1..8 named fields for 1..10 seconds after countdown\n"
        "  status                               agent pid and tick rate\n"
        "  quit\n"
        "  Numbers: 0x hex or decimal. <addr> accepts $N, $N+offset, base+offset and <module>+offset\n"
        "  (game.exe+0x1a2b3c, offset inside the image). Addresses inside a module print module and rva.\n"
        "  Refs: $N from the last find, or the printed literal 0x<address>#<index>:<serial> (works across runs).\n"
        "  A bare class or struct name works where <object|Class> is shown; the first lookup scans the table.\n"
        "  One console per agent: a second one is refused as busy.\n"
        "  Any command ending in --timing adds trips, wait_ms and total_ms to its output.\n"
        "  Struct values print as {Name: value}; --meta at the end of a line adds their member layout.\n"
        "  Class refs, field tables, the module list and $N persist for the agent instance in %%TEMP%%\\reagentlive-<pid>.cache.\n"
        "  One-shot: <console> [pid] -c \"find X; dump $1\" emits JSON Lines.\n", MaxRead, MaxWrite);
    return buffer;
}

// census, or with --changed only the classes whose count differs from the
// last census under the same --class. Either way the new census becomes the
// baseline; --match and --limit only shape what is printed.
void RunCensus(Session& session, const Command& command, std::vector<Ref>& refs, std::string_view jsonCommand) {
    const Request& request = command.request;
    const std::string filter(request.length ? reinterpret_cast<const char*>(request.data) : "");
    const CensusPage page{command.from, command.limit};
    ue::CensusResult census;
    int64_t ms = 0;
    if (page.from) {
        // A later page: the stored census, sliced; no scan, and the baselines stay.
        if (const auto problem = CensusPageProblem(session.census, session.censusBefore, filter, command.changed);
            !problem.empty()) return JsonError(session, jsonCommand, "validation", problem);
        census = StoredCensus(*session.census);
    } else {
        if (command.changed)
            if (const auto problem = CensusBaselineProblem(session.census, filter); !problem.empty())
                return JsonError(session, jsonCommand, "validation", problem);
        const auto started = Clock::now();
        auto scanned = ue::Census(session.client, filter);
        if (!scanned) return CommandError(session, jsonCommand, scanned.error());
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        census = std::move(*scanned);
        session.censusBefore = std::move(session.census);
        session.census = MakeCensusBaseline(filter, census);
    }
    refs.clear();
    Json result;
    if (command.changed) {
        auto changes = CensusChanges(*session.censusBefore, census);
        KeepMatching(changes, command.match, [](const CensusChange& c) -> const std::string& { return c.name; });
        const auto [first, shown] = CensusSlice(changes.size(), page);
        for (const auto& change : std::span(changes).subspan(first, shown)) refs.push_back(change.ref);
        result = CensusChangedJson(*session.censusBefore, census, changes, page, ms);
    } else {
        auto rows = census.rows;
        KeepMatching(rows, command.match, [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
        const auto [first, shown] = CensusSlice(rows.size(), page);
        for (const auto& row : std::span(rows).subspan(first, shown)) refs.push_back(row.type.ref);
        result = CensusJson(filter, census, rows, page, ms);
    }
    JsonResult(session, jsonCommand, std::move(result));
}

void Run(Session& session, const Command& original, std::vector<Ref>& refs, StructCache& structs,
         const std::optional<Snapshot>& baseline, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    const Request& request = command.request;
    if (command.kind == Kind::Base) {
        const auto modules = LoadedModules(session);
        if (modules.empty()) return JsonError(session, jsonCommand, "unavailable", "base unknown: modules unavailable");
        return JsonResult(session, jsonCommand, ModuleJson(modules.front()));
    }
    if (!client.Supports(request.op))
        return JsonError(session, jsonCommand, "unsupported", "agent does not support this operation");
    if (command.kind == Kind::Each) return RunEach(session, command, refs, structs, jsonCommand);
    if (!command.className.empty()) {
        auto type = ResolveClass(session, command.className);
        if (!type) return CommandError(session, jsonCommand, type.error());
        ue::SetRef(command.request, *type);
    }
    if (command.kind == Kind::Dump) return PrintDump(session, ue::GetRef(request), refs, structs, command, baseline);
    if (command.kind == Kind::Script) {
        if (!client.Supports(ue::op::Script)) return JsonError(session, jsonCommand, "unsupported", "agent does not support --script");
        const auto object = ue::GetRef(request);
        auto function = ue::Function(client, object, request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto script = ue::Script(client, Ref{static_cast<uintptr_t>(function->address), -1, 0});
        if (!script) return CommandError(session, jsonCommand, script.error());
        return JsonResult(session, jsonCommand, ScriptJson(object, function->owner + ":" + function->name, *script));
    }
    if (command.kind == Kind::Refs) {
        auto index = static_cast<int32_t>(request.args[0]);
        std::string name = request.text;
        if (!index) { // Exact text: walk the name pool's matches until it appears.
            for (uint32_t from = 0;;) {
                auto found = ue::NameFind(client, name, from);
                if (!found) return CommandError(session, jsonCommand, found.error());
                for (const auto& match : found->names) if (match.name == name) { index = match.index; break; }
                if (index || !found->next) break;
                from = found->next;
            }
            if (!index) return JsonError(session, jsonCommand, "validation", "no pooled name is exactly '" + name + "'");
        } else if (auto names = ue::Names(client, std::span(&index, 1)); names && !names->front().empty()) {
            name = names->front();
        }
        auto found = ue::Refs(client, index);
        if (!found) return CommandError(session, jsonCommand, found.error());
        return JsonResult(session, jsonCommand, RefsJson(index, name, *found));
    }
    // release without a block: zeroed inputs of the function's size. A call
    // without one: the same, when the call fills every parameter.
    std::optional<ue::Signature> callSignature;
    if (request.op == ue::op::Call && command.kind != Kind::TypedCall && !request.length) {
        auto function = ue::Function(client, ue::GetRef(request), request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto signature = ue::Parameters(client, function->address);
        if (!signature) return CommandError(session, jsonCommand, signature.error());
        if (signature->size > ue::MaxParams) return JsonError(session, jsonCommand, "validation", "parameter block too large");
        if (command.kind != Kind::Release)
            if (const auto refusal = calls::ZeroBlockRefusal(*signature); !refusal.empty())
                return JsonError(session, jsonCommand, "validation", refusal);
        command.request.length = signature->size;
        memset(command.request.data, 0, signature->size);
        callSignature = std::move(*signature);
    }
    if (request.op == ue::op::Parameters || command.kind == Kind::TypedCall) {
        const auto object = ue::GetRef(request);
        auto function = ue::Function(client, object, request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto signature = ue::Parameters(client, function->address);
        if (!signature) return CommandError(session, jsonCommand, signature.error());
        if (command.kind == Kind::TypedCall)
            return RunTypedCall(session, command, function->address, *signature, structs, jsonCommand);
        return PrintParameters(session, *function, *signature, object);
    }
    switch (request.op) {
    case op::Read: {
        auto bytes = client.Read(request.address, request.length);
        // A string may end before an unreadable page: read up to the page end.
        if (!bytes && StringView(command.view) && 0x1000 - (request.address & 0xfff) < request.length)
            bytes = client.Read(request.address, static_cast<uint32_t>(0x1000 - (request.address & 0xfff)));
        if (!bytes) return CommandError(session, jsonCommand, bytes.error());
        const auto modules = LoadedModules(session);
        Json result = {{"address", format_detail::Pointer(request.address)}, {"size", bytes->size()},
            {"hex", format_detail::Hex(*bytes, bytes->size())}};
        AddLocation(result, request.address, modules);
        if (bytes->size() == 4) {
            result["views"] = {{"i32", JsonNumber<int32_t>(*bytes)}, {"u32", JsonNumber<uint32_t>(*bytes)},
                {"f32", JsonNumber<float>(*bytes)}};
        } else if (bytes->size() == 8) {
            uint64_t address{};
            format_detail::Load(*bytes, address);
            result["views"] = {{"i64", JsonNumber<int64_t>(*bytes)}, {"u64", address},
                {"ptr", address ? Json(format_detail::Pointer(address)) : Json(nullptr)},
                {"f64", JsonNumber<double>(*bytes)}};
            AddLocation(result["views"], address, modules, "ptr_");
        }
        if (StringView(command.view)) {
            const auto decoded = command.view == "wstr" ? native::WideString(*bytes) : native::CString(*bytes);
            result["text"] = decoded.text;
            result["length"] = decoded.length;
            result["terminated"] = decoded.terminated;
        } else if (ElementSize(command.view)) {
            result["values"] = DecodeValues(*bytes, command.view);
        } else if (!command.view.empty()) {
            if (auto parsed = layout::Parse(command.view)) result["values"] = layout::Decode(*parsed, *bytes);
        }
        return JsonResult(session, jsonCommand, std::move(result));
    }
    case op::ReadBatch: {
        auto batch = client.ReadBatch(command.reads);
        if (!batch) return CommandError(session, jsonCommand, batch.error());
        const auto modules = LoadedModules(session);
        Json items = Json::array();
        for (size_t i = 0; i < batch->items.size(); ++i) {
            const auto& item = batch->items[i];
            const auto& read = command.reads[i];
            Json row = {{"address", format_detail::Pointer(read.address)}, {"size", read.length},
                {"ok", item.status == Status::Ok}};
            AddLocation(row, read.address, modules);
            if (item.status == Status::Ok) row["hex"] = format_detail::Hex(item.bytes, item.bytes.size());
            else row["error"] = {{"status", static_cast<uint32_t>(item.status)}, {"message", StatusText(item.status)}};
            items.push_back(std::move(row));
        }
        return JsonResult(session, jsonCommand, {{"tick", batch->tick}, {"items", std::move(items)}});
    }
    case op::Write: {
        std::vector<uint8_t> found;
        auto written = request.flags & WriteExpect ?
            client.WriteIf(request.address, std::span(request.data, request.length),
                           std::span(request.data + request.length, request.length), &found) :
            client.Write(request.address, std::span(request.data, request.length));
        if (!written) {
            if (found.empty()) return CommandError(session, jsonCommand, written.error());
            session.failed = true;
            return PrintJson(session, {{"command", jsonCommand}, {"ok", false}, {"error", {{"kind", "agent"},
                {"message", written.error().message}, {"reason", written.error().reason},
                {"found", format_detail::Hex(found, found.size())}}}});
        }
        Json result = {{"address", format_detail::Pointer(request.address)}, {"size", request.length}};
        AddLocation(result, request.address, LoadedModules(session));
        return JsonResult(session, jsonCommand, std::move(result));
    }
    case op::Search: {
        SearchQuery query;
        query.pattern.assign(request.data, request.data + request.length);
        query.from = request.address;
        query.to = request.args[0];
        query.alignment = SearchAlignment(request.flags);
        query.writableOnly = request.flags & SearchWritable;
        query.privateOnly = request.flags & SearchPrivate;
        query.paged = request.flags & SearchPaged;
        query.background = request.flags & SearchBackground;
        query.maxHits = static_cast<uint32_t>(request.args[1]);
        if (!command.target.empty()) {
            const auto* match = text::ModuleNamed(command.target, LoadedModules(session));
            if (!match && RefreshModules(session)) match = text::ModuleNamed(command.target, LoadedModules(session));
            if (!match) return JsonError(session, jsonCommand, "module", "no loaded module with that name");
            query.from = match->base;
            query.to = match->base + match->size;
        }
        auto found = client.Search(query, std::max(client.Timeout(), std::chrono::milliseconds(180000)));
        if (!found) return CommandError(session, jsonCommand, found.error());
        const auto modules = LoadedModules(session);
        Json hits = Json::array();
        for (uint64_t hit : found->hits) {
            Json row = {{"address", format_detail::Pointer(hit)}};
            AddLocation(row, hit, modules);
            hits.push_back(std::move(row));
        }
        return JsonResult(session, jsonCommand, {{"hits", std::move(hits)}, {"count", found->hits.size()},
            {"resume", found->resume ? Json(format_detail::Pointer(found->resume)) : Json(nullptr)}});
    }
    case op::Modules: {
        auto modules = client.Modules();
        if (!modules) return CommandError(session, jsonCommand, modules.error());
        Json rows = Json::array();
        for (const auto& module : *modules) rows.push_back(ModuleJson(module));
        session.modules = std::move(*modules);
        session.modulesListed = true;
        return JsonResult(session, jsonCommand, {{"modules", std::move(rows)}});
    }
    case ue::op::Find: {
        const std::string_view className = request.length ? reinterpret_cast<const char*>(request.data) : "";
        const auto from = static_cast<uint32_t>(request.args[1]);
        auto found = ue::Find(client, request.text, {className, from});
        if (!found) return CommandError(session, jsonCommand, found.error());
        refs.clear();
        for (const auto& object : found->objects) refs.push_back(object.ref);
        return JsonResult(session, "find", FindJson(*found, from));
    }
    case ue::op::Call: {
        bool kept = false;
        auto block = ue::Call(client, ue::GetRef(request), request.text, std::span(request.data, request.length), 0,
            request.flags, &kept);
        if (!block) return CommandError(session, jsonCommand, block.error());
        auto record = CallJson(request, *block, command.kind == Kind::Release ? "release" : "raw", kept);
        if (kept) {
            // Named from the parameters: fetched now unless the zeroed block already did.
            if (!callSignature)
                if (auto function = ue::Function(client, ue::GetRef(request), request.text); function)
                    if (auto signature = ue::Parameters(client, function->address); signature) callSignature = std::move(*signature);
            if (callSignature) record["kept_reason"] = calls::KeptReason(*callSignature, structs);
        }
        return JsonResult(session, jsonCommand, std::move(record));
    }
    case ue::op::Fields: return PrintFields(session, ue::GetRef(request), command.className.empty(), jsonCommand);
    case ue::op::Names: {
        std::vector<int32_t> indices(request.length / 4);
        memcpy(indices.data(), request.data, request.length);
        auto names = ue::Names(client, indices);
        if (!names) return CommandError(session, jsonCommand, names.error());
        return JsonResult(session, jsonCommand, NamesJson(indices, *names));
    }
    case ue::op::Census: return RunCensus(session, command, refs, jsonCommand);
    case ue::op::NameFind: {
        const auto from = static_cast<uint32_t>(request.args[1]);
        auto found = ue::NameFind(client, request.text, from);
        if (!found) return CommandError(session, jsonCommand, found.error());
        return JsonResult(session, jsonCommand, NameFindJson(*found, from));
    }
    case ue::op::Function: return PrintFunction(session, ue::GetRef(request), request.text, jsonCommand);
    case ue::op::Functions: {
        auto functions = ue::Functions(client, ue::GetRef(request), request.text, request.length);
        if (!functions) return CommandError(session, jsonCommand, functions.error());
        return JsonResult(session, jsonCommand, FunctionsJson(ue::GetRef(request), *functions, request.length));
    }
    case ue::op::Describe: {
        auto object = ue::Describe(client, ue::GetRef(request));
        if (!object) return CommandError(session, jsonCommand, object.error());
        RememberRef(refs, object->ref);
        return JsonResult(session, jsonCommand, ObjectJson(*object));
    }
    }
    JsonError(session, jsonCommand, "unsupported", "unsupported operation");
}

// Commands that make several round trips run held, so they finish in as few
// host ticks as the agent's budget allows. Single requests and timed loops
// (watch, record, each --seconds) do not: a hold would only add one wait.
bool Held(const Command& command) {
    switch (command.kind) {
    case Kind::Dump: case Kind::Snap: case Kind::Get: case Kind::TypedCall: case Kind::Release:
    case Kind::Script: case Kind::Peek: case Kind::Entries: case Kind::ExportLabels: case Kind::Label: return true;
    case Kind::Each: return !command.seconds;
    case Kind::Remote: break;
    default: return false;
    }
    if (!command.className.empty()) return true;
    const uint32_t op = command.request.op;
    if (op == ue::op::Call && !command.request.length) return true; // Looks up the function's size first.
    return op == ue::op::Fields || op == ue::op::Names || op == ue::op::Census ||
        op == ue::op::Functions || op == ue::op::Parameters || op == ue::op::Function || op == reagentlive::op::Modules;
}

bool Execute(Session& session, const std::string& line, bool inBatch);
bool TakeFlag(std::string& line, std::string_view flag);

void RunBatch(Session& session, const Command& command, std::string_view jsonCommand) {
    std::vector<Json> results;
    std::vector<BatchItemTicks> ticks;
    uint64_t first = 0, last = 0;
    {
        Client::Hold hold(session.client);
        session.capture = &results;
        for (const auto& item : command.fields) {
            const size_t printed = results.size();
            BatchItemTicks at{hold.LastTick()};
            hold.BeginSpan();
            Execute(session, item, true);
            if (hold.SpanFirstTick()) {
                at.first = hold.SpanFirstTick();
                at.last = hold.LastTick();
            }
            if (results.size() > printed) {
                ticks.resize(results.size());
                ticks.back() = at;
            }
        }
        session.capture = nullptr;
        first = hold.FirstTick();
        last = hold.LastTick();
    }
    Json result = BatchJson(std::move(results), ticks, first, last);
    if (!TickAccurate(session)) result["tick_accurate"] = false;
    JsonResult(session, jsonCommand, std::move(result));
}

// One command line; true on quit. Inside a batch the batch's hold covers it.
bool Execute(Session& session, const std::string& original, bool inBatch) {
    Client& client = session.client;
    // --meta covers this line (a batch's covers its items); the previous state returns after it.
    struct MetaScope {
        Session& session;
        bool previous;
        ~MetaScope() { session.meta = previous; }
    } metaScope{session, session.meta};
    std::string line = original;
    if (TakeFlag(line, "--meta")) session.meta = true;
    std::string_view rest = line;
    const auto jsonCommand = text::Next(rest);
    if (jsonCommand.empty()) return false;
    Json envelope;
    if (session.config.hostCommand && session.config.hostCommand(client, line, envelope)) {
        if (!envelope.is_object() || !envelope.contains("ok") || !envelope["ok"].is_boolean()) {
            JsonError(session, jsonCommand, "host", "host command returned no result");
        } else {
            envelope["command"] = jsonCommand;
            session.failed |= !envelope["ok"].get<bool>();
            PrintJson(session, std::move(envelope));
        }
        return false;
    }
    // Module addresses (base+0x10, game.exe+0x10) need the list; a name it
    // lacks may be a module loaded since it was read.
    const bool moduleAddress = line.find('+') != std::string::npos || line.find("base") != std::string::npos;
    auto parsed = Parse(line, session.refs, moduleAddress ? LoadedModules(session) : std::span<const Module>{});
    if (!parsed && moduleAddress && RefreshModules(session)) parsed = Parse(line, session.refs, LoadedModules(session));
    if (!parsed) {
        JsonError(session, jsonCommand, "syntax", parsed.error());
        return false;
    }
    if (inBatch && parsed->kind == Kind::Each && parsed->seconds) {
        JsonError(session, jsonCommand, "syntax", "each --seconds is not allowed in a batch");
        return false;
    }
    // A pointer chain: resolved here, then the command runs at its end.
    session.chain.reset();
    if (!parsed->chain.empty()) {
        auto chain = client.ResolveChain(parsed->request.address, parsed->chain);
        if (!chain) {
            CommandError(session, jsonCommand, chain.error());
            return false;
        }
        Json json = ChainJson(parsed->request.address, *chain, LoadedModules(session));
        if (chain->broken) {
            session.failed = true;
            Json error = {{"kind", "chain"}, {"message", "hop " + std::to_string(*chain->broken) + ": " + chain->reason},
                {"chain", std::move(json)}};
            PrintJson(session, {{"command", jsonCommand}, {"ok", false}, {"error", std::move(error)}});
            return false;
        }
        parsed->request.address = chain->address;
        session.chain = std::move(json);
    }
    switch (parsed->kind) {
    case Kind::Quit:
        JsonResult(session, jsonCommand, {{"quit", true}});
        return true;
    case Kind::Help: {
        auto help = HelpText();
        help += session.config.hostHelp;
        JsonResult(session, jsonCommand, {{"usage", help}});
        return false;
    }
    case Kind::Status: PrintStatus(session, jsonCommand); return false;
    case Kind::Pin: RunPin(session, *parsed, jsonCommand); return false;
    case Kind::Trial: RunTrial(session, *parsed, jsonCommand); return false;
    case Kind::Watch: RunWatch(session, *parsed, session.refs, LoadedModules(session), jsonCommand); return false;
    case Kind::Batch: RunBatch(session, *parsed, jsonCommand); return false;
    case Kind::Sig: RunSig(session, *parsed, jsonCommand); return false;
    case Kind::Disasm: RunDisasm(session, *parsed, jsonCommand); return false;
    case Kind::Xrefs: RunXrefs(session, *parsed, jsonCommand); return false;
    case Kind::Touches: RunTouches(session, *parsed, jsonCommand); return false;
    case Kind::Vtable: RunVtable(session, *parsed, jsonCommand); return false;
    case Kind::Region: RunRegion(session, *parsed, jsonCommand); return false;
    default: break;
    }
    std::optional<Client::Hold> hold;
    if (!inBatch && Held(*parsed)) hold.emplace(client);
    if (parsed->kind == Kind::Label) {
        RunLabel(session, *parsed, jsonCommand);
        return false;
    }
    if (parsed->kind == Kind::ExportLabels) {
        RunExportLabels(session, *parsed, jsonCommand);
        return false;
    }
    if (parsed->kind == Kind::Entries) {
        RunEntries(session, *parsed, jsonCommand);
        return false;
    }
    if (parsed->kind == Kind::Peek) {
        RunPeek(session, *parsed, jsonCommand);
        return false;
    }
    if (parsed->kind == Kind::Get) {
        if (!client.Supports(ue::op::Fields)) JsonError(session, jsonCommand, "unsupported", "get needs an agent with fields");
        else RunGet(session, *parsed, session.structs, session.refs, jsonCommand);
        return false;
    }
    if (parsed->kind == Kind::Snap || parsed->kind == Kind::Record) {
        if (!client.Supports(ue::op::ReadObject) || !client.Supports(ue::op::Fields)) {
            JsonError(session, jsonCommand, "unsupported", "this command needs an agent with fields and object reads");
            return false;
        }
        const auto object = ue::GetRef(parsed->request);
        if (parsed->kind == Kind::Record) Record(session, object, *parsed, session.structs, jsonCommand);
        else Snap(session, object, parsed->file, session.baseline, jsonCommand);
        return false;
    }
    Run(session, *parsed, session.refs, session.structs, session.baseline, jsonCommand);
    return false;
}

std::filesystem::path CachePath(uint32_t pid) {
    wchar_t folder[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, folder);
    if (!length || length > MAX_PATH) return {};
    return std::filesystem::path(folder) / (L"reagentlive-" + std::to_wstring(pid) + L".cache");
}

void LoadSessionCache(Session& session, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto data = LoadCache(text);
    if (!data || data->owner != session.client.AgentOwner()) return; // Another agent instance: start fresh.
    for (auto& [name, ref] : data->classes) session.classRefs.push_back({std::move(name), ref, false});
    for (auto& [type, fields] : data->tables) session.tables[type] = std::move(fields);
    session.refs = std::move(data->refs);
    session.modules = std::move(data->modules);
    for (auto& [base, layout] : data->layouts) session.layouts[base] = std::move(layout);
    session.census = std::move(data->census);
    session.censusBefore = std::move(data->censusBefore);
}

void SaveSessionCache(const Session& session, const std::filesystem::path& path) {
    CacheData data;
    data.owner = session.client.AgentOwner();
    if (!data.owner) return;
    for (const auto& entry : session.classRefs)
        if (data.classes.size() < MaxCachedClasses) data.classes.emplace_back(entry.name, entry.ref);
    for (const auto& [type, fields] : session.tables)
        if (data.tables.size() < MaxCachedTables) data.tables.emplace_back(type, fields);
    for (const auto& ref : session.refs) {
        if (!ref.address) break; // Not expressible as a literal ref; $N past it would shift.
        data.refs.push_back(ref);
    }
    data.modules.assign(session.modules.begin(), session.modules.end());
    for (const auto& [base, layout] : session.layouts)
        if (std::ranges::any_of(session.modules, [base](const Module& m) { return m.base == base; }))
            data.layouts.emplace_back(base, layout);
    data.census = session.census;
    data.censusBefore = session.censusBefore;
    const auto temporary = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << SaveCache(data);
        if (!file) return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}

// A console flag (--timing, --meta) at the end of a line: strips it and says so.
bool TakeFlag(std::string& line, std::string_view flag) {
    const auto trimmed = text::Trim(line);
    if (!trimmed.ends_with(flag) || (trimmed.size() > flag.size() && trimmed[trimmed.size() - flag.size() - 1] != ' '))
        return false;
    line = std::string(text::Trim(trimmed.substr(0, trimmed.size() - flag.size())));
    return true;
}
}
}

namespace reagentlive::cli {
using namespace detail;

int RunConsole(const ConsoleConfig& config, int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring_view(argv[1]) == L"--replay") {
        if (argc == 3) return Replay(argv[2]);
        std::puts("Usage: <console> --replay <file>");
        return 1;
    }
    constexpr const char* usage = "Usage: <console> [pid] [-c \"command; command ...\"] [--names <file>]";
    // Before a session exists: print the envelope directly.
    const auto printStartup = [](const Json& envelope) {
        std::puts(envelope.dump(-1, ' ', true, Json::error_handler_t::replace).c_str());
        std::fflush(stdout);
    };
    const auto startupError = [&](std::string_view message) {
        printStartup(ErrorEnvelope("startup", "startup", message));
        return 1;
    };
    Names names;
    uint32_t pid = 0;
    std::filesystem::path namesFile;
    bool namesRequired = false;
    if (!config.namesFile.empty()) {
        wchar_t module[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        if (length && length < MAX_PATH) namesFile = std::filesystem::path(module).parent_path() / config.namesFile;
    }
    std::optional<std::vector<std::string>> oneShot;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"-c" && i + 1 < argc && !oneShot) {
            const std::wstring_view wide(argv[++i]);
            std::string commands;
            for (const wchar_t c : wide) {
                if (c > 0x7f) return startupError("-c commands must be ASCII");
                commands += static_cast<char>(c);
            }
            oneShot = SplitCommands(commands);
        } else if (arg == L"--names" && i + 1 < argc) {
            namesFile = argv[++i];
            namesRequired = true;
        } else if (!pid && !arg.empty() && std::ranges::all_of(arg, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
            pid = static_cast<uint32_t>(_wtoi(argv[i]));
        } else {
            return startupError(usage);
        }
    }
    if (oneShot && oneShot->empty()) return startupError("provide commands via -c");
    if (!pid) pid = FindProcess(config.exeName);
    if (!pid) {
        return startupError(std::string("Target not running. ") + usage);
    }
    // Native members: <console>.native.txt beside the console, hand-owned (the
    // names file may be regenerated; nothing writes into it).
    std::filesystem::path nativeFile;
    NativeNames native;
    {
        wchar_t module[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        if (length && length < MAX_PATH) nativeFile = std::filesystem::path(module).replace_extension(L".native.txt");
        if (std::ifstream file(nativeFile); !nativeFile.empty() && file) {
            std::string why;
            if (const size_t bad = native.Load(file, &why))
                return startupError("malformed native file at line " + std::to_string(bad) + ": " + why);
        }
    }
    if (!namesFile.empty()) {
        std::ifstream file(namesFile);
        if (!file && namesRequired) return startupError("cannot open names file");
        if (file) {
            if (const size_t bad = names.Load(file))
                return startupError("malformed names file at line " + std::to_string(bad));
        }
    }
    auto client = Client::Attach(std::wstring(config.sectionPrefix) + std::to_wstring(pid), config.layout);
    if (!client) {
        printStartup(ErrorEnvelope("startup", client.error()));
        return 1;
    }
    client->SetWaitNotice([](std::chrono::milliseconds waited, bool ticking) {
        std::fprintf(stderr, "  waiting %lld s (%s)\n", static_cast<long long>(waited.count() / 1000),
            ticking ? "agent ticking, request running" : "agent not ticking");
        std::fflush(stderr);
    });
    Session session(*client, config);
    session.names = std::move(names);
    session.native = std::move(native);
    session.nativeFile = std::move(nativeFile);
    session.lastBeat = {client->Heartbeat(), Clock::now()};
    const auto cachePath = CachePath(pid);
    if (!cachePath.empty()) LoadSessionCache(session, cachePath);
    size_t next = 0;
    std::string line;
    const auto nextLine = [&] {
        if (oneShot) {
            if (next == oneShot->size()) return false;
            line = (*oneShot)[next++];
            return true;
        }
        return static_cast<bool>(std::getline(std::cin, line));
    };
    while (nextLine()) {
        // --timing and --meta in either order; Execute takes --meta.
        bool timing = false, meta = false;
        for (bool more = true; more;) {
            more = false;
            if (TakeFlag(line, "--timing")) timing = more = true;
            if (TakeFlag(line, "--meta")) meta = more = true;
        }
        if (meta) line += " --meta";
        if (timing) session.timing = Timing{&*client, client->Stats(), Clock::now()};
        const bool quit = Execute(session, line, false);
        session.timing.reset();
        if (quit) break;
    }
    if (!cachePath.empty()) SaveSessionCache(session, cachePath);
    return session.failed ? 1 : 0;
}
}
