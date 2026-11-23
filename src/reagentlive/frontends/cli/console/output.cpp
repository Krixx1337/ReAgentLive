// reagentlive CLI console: JSON output: records, errors, `timing`, output budgets and batch capture.
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
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/results/budget.h"
#include "reagentlive/frontends/cli/results/verbs.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {


namespace {
// The usage log's view of a printed record: its size and whether it was refused.
void Count(Session& session, const Json& record, size_t size) {
    if (!session.line) return;
    session.line->out += size;
    if (session.line->ok && record.contains("ok") && record["ok"].is_boolean() && !record["ok"].get<bool>()) {
        session.line->ok = false;
        const auto& error = record.contains("error") ? record["error"] : Json();
        session.line->refused = error.is_object() && error.contains("kind") && error["kind"].is_string() ?
            error["kind"].get<std::string>() : "error";
    }
}
std::string Text(const Json& record) { return record.dump(-1, ' ', true, Json::error_handler_t::replace); }

// The whole envelope of a line that must not run again, in the results
// folder; the last 16 are kept. Its path, or empty when it cannot be written.
constexpr size_t KeptResults = 16;
std::string KeepResult(const Session& session, const std::string& text) {
    if (session.results.empty()) return {};
    std::error_code error;
    std::filesystem::create_directories(session.results, error);
    if (error) return {};
    static uint32_t sequence = 0;
    const auto name = std::format("{:%Y%m%dT%H%M%S}-{}-{}.json",
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()), GetCurrentProcessId(), ++sequence);
    const auto path = session.results / name;
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !(file << text << '\n')) return {};
    }
    std::vector<std::filesystem::directory_entry> kept;
    for (const auto& entry : std::filesystem::directory_iterator(session.results, error))
        if (entry.is_regular_file(error) && entry.path().extension() == ".json") kept.push_back(entry);
    if (kept.size() > KeptResults) {
        std::ranges::sort(kept, {}, [](const auto& entry) {
            std::error_code ignored;
            return entry.last_write_time(ignored);
        });
        for (size_t i = 0; i + KeptResults < kept.size(); ++i) std::filesystem::remove(kept[i].path(), error);
    }
    return path.string();
}

// A result past the line's budget: a digest instead (results/budget.h).
std::optional<Json> Digested(const Session& session, Json& record, size_t chars) {
    const LineInput& input = session.input;
    budget::Line line;
    line.verb = session.current;
    line.call = input.call;
    line.limit = input.budget;
    line.reads = input.reads;
    if (line.verb && input.call) {
        auto args = CheckCall(*line.verb, input.call->contains("args") ? (*input.call)["args"] : Json());
        if (args) line.args = std::move(*args);
    }
    return budget::Replace(record, chars, line, [&](const std::string& text) { return KeepResult(session, text); });
}
}

void PrintJson(Session& session, Json record) {
    const auto budgeted = [&](std::string& text) {
        if (!session.input.budget || text.size() <= session.input.budget || !record.contains("result")) return;
        if (auto digest = Digested(session, record, text.size())) {
            if (session.line) session.line->digest += text.size();
            record = std::move(*digest);
            text = Text(record);
        }
    };
    if (session.capture) {
        std::string text = Text(record);
        budgeted(text);
        if (session.line && session.usage.record) Count(session, record, text.size() + 1);
        session.capture->push_back(std::move(record));
        return;
    }
    if (session.timing) {
        const auto stats = session.timing->client->Stats();
        const auto total = std::chrono::duration<double, std::milli>(Clock::now() - session.timing->began).count();
        record["timing"] = {{"trips", stats.trips - session.timing->start.trips},
            {"wait_ms", static_cast<double>(stats.waitMicroseconds - session.timing->start.waitMicroseconds) / 1000.0},
            {"total_ms", total}};
    }
    std::string text = Text(record);
    budgeted(text);
    Count(session, record, text.size() + 1);
    std::puts(text.c_str());
    std::fflush(stdout);
}

void JsonResult(Session& session, std::string_view command, Json result) {
    session.chain.reset();
    if (session.current) {
        const auto type = session.current->result ? session.current->result() : schema::Of<schema::Nothing>();
        if (auto problem = schema::Problem(type, result); !problem.empty())
            return JsonError(session, command, ErrorKind::Internal, "the result does not match help " +
                std::string(session.current->name) + ": " + problem);
    }
    PrintJson(session, ResultEnvelope(command, std::move(result)));
}

void JsonError(Session& session, std::string_view command, ErrorKind kind, std::string_view message) {
    session.failed = true;
    PrintJson(session, ErrorEnvelope(command, kind, message));
}

void JsonRefusal(Session& session, std::string_view command, Json error) {
    session.failed = true;
    PrintJson(session, {{"command", command}, {"ok", false}, {"error", std::move(error)}});
}

void CommandError(Session& session, std::string_view jsonCommand, const Error& error) {
    session.failed = true;
    PrintJson(session, ErrorEnvelope(jsonCommand, error));
}
}
