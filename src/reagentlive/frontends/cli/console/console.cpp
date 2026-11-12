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
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

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
        "  Core (any process):\n"
        "  read <addr> [len] [--as T]           hex dump, len 1..%zu (default 64); --as i8..u64|f32|f64|ptr adds\n"
        "                                       decoded values; --as \"u8 phase, pad 3, f64[3] start\" decodes a block\n"
        "                                       of mixed types as {name: value}\n"
        "  read-batch <addr> <len> [...]        read up to 8 ranges in one host tick\n"
        "  peek <addr> [count] [--label <file> [name]]\n"
        "                                       what 1..64 qwords hold (default 8): object (with a pack), module\n"
        "                                       (+rva), heap or value (floats, double, name_hint); --label appends one\n"
        "                                       confirmed global to a labels file (default name FName_<hint>)\n"
        "  write <addr> <hex bytes> [--expect <hex>]\n"
        "                                       e.g. write $1+0x120 00 00 80 3f (max %zu bytes); --expect writes only\n"
        "                                       while memory still holds those bytes, else prints what it found\n"
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
        "                                       --object's vtable changes; owner: a live actor or component (Unreal pack)\n"
        "  write --trial list | write --trial clear [id]  states (active, restored, conflict, abandoned);\n"
        "                                       clear restores active trials\n"
        "  watch <seconds|Nms> <addr>:<len>... [--then call <object> <Function> [hex]]\n"
        "                                       sample up to 8 ranges every tick (up to 30 s) and print the\n"
        "                                       rows; --then runs the call (Unreal pack) right after sampling starts\n"
        "  watch --start <duration> [--every <duration>] [--follow <root>] <addr|offset>:<len>...\n"
        "                                       a capture the agent keeps (up to 60 min with --every); returns at\n"
        "                                       once; --follow continues on a host root's replacement (status lists\n"
        "                                       roots); watch --read: the rows so far; watch --stop: end it\n"
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
        "  status                               agent pid, tick rate and the packs the host offers\n"
        "  quit\n"
        "  Numbers: 0x hex or decimal. <addr> accepts $N, $N+offset, base+offset and <module>+offset\n"
        "  (game.exe+0x1a2b3c, offset inside the image). Addresses inside a module print module and rva.\n"
        "  One console per agent: a second one is refused as busy.\n"
        "  Any command ending in --timing adds trips, wait_ms and total_ms to its output.\n"
        "  The module list, $N and the pack's lookups persist for the agent instance in %%TEMP%%\\reagentlive-<pid>.cache.\n"
        "  One-shot: <console> [pid] -c \"status; modules\" emits JSON Lines.\n", MaxRead, MaxWrite);
    return buffer;
}

void Run(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const Request& request = command.request;
    if (command.kind == Kind::Base) {
        const auto modules = LoadedModules(session);
        if (modules.empty()) return JsonError(session, jsonCommand, "unavailable", "base unknown: modules unavailable");
        return JsonResult(session, jsonCommand, ModuleJson(modules.front()));
    }
    if (!client.Supports(request.op))
        return JsonError(session, jsonCommand, "unsupported", "agent does not support this operation");
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
    }
    JsonError(session, jsonCommand, "unsupported", "unsupported operation");
}

// Commands that make several round trips run held, so they finish in as few
// host ticks as the agent's budget allows. Single requests and timed loops
// (watch, record, each --seconds) do not: a hold would only add one wait.
bool Held(const Command& command) {
    if (command.kind == Kind::Peek) return true;
    return command.kind == Kind::Remote && command.request.op == reagentlive::op::Modules;
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
        help += PackHelp();
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
    if (!inBatch && (Held(*parsed) || (PackCommand(*parsed) && PackHeld(*parsed)))) hold.emplace(client);
    if (parsed->kind == Kind::Peek) {
        RunPeek(session, *parsed, jsonCommand);
        return false;
    }
    if (PackCommand(*parsed)) {
        RunPackCommand(session, *parsed, jsonCommand);
        return false;
    }
    Run(session, *parsed, jsonCommand);
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
    session.refs = std::move(data->refs);
    session.modules = std::move(data->modules);
    for (auto& [base, layout] : data->layouts) session.layouts[base] = std::move(layout);
    LoadPackCache(session, data->packs);
}

void SaveSessionCache(const Session& session, const std::filesystem::path& path) {
    CacheData data;
    data.owner = session.client.AgentOwner();
    if (!data.owner) return;
    for (const auto& ref : session.refs) {
        if (!ref.address) break; // Not expressible as a literal ref; $N past it would shift.
        data.refs.push_back(ref);
    }
    data.modules.assign(session.modules.begin(), session.modules.end());
    for (const auto& [base, layout] : session.layouts)
        if (std::ranges::any_of(session.modules, [base](const Module& m) { return m.base == base; }))
            data.layouts.emplace_back(base, layout);
    data.packs = SavePackCache(session);
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
    session.pack = MakePackState(session);
    if (const auto problem = LoadPackFiles(session); !problem.empty()) return startupError(problem);
    session.names = std::move(names);
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
