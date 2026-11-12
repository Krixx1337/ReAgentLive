// reagentlive CLI console: JSON output: records, errors, --timing and batch capture.
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
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {


void PrintJson(Session& session, Json record) {
    if (session.capture) {
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
    std::puts(record.dump(-1, ' ', true, Json::error_handler_t::replace).c_str());
    std::fflush(stdout);
}

void JsonResult(Session& session, std::string_view command, Json result) {
    session.chain.reset();
    if (session.current) {
        const auto type = session.current->result ? session.current->result() : schema::Of<schema::Nothing>();
        if (auto problem = schema::Problem(type, result); !problem.empty())
            return JsonError(session, command, "internal", "the result does not match help " +
                std::string(session.current->name) + ": " + problem);
    }
    PrintJson(session, ResultEnvelope(command, std::move(result)));
}

void JsonError(Session& session, std::string_view command, std::string_view kind, std::string_view message) {
    session.failed = true;
    PrintJson(session, ErrorEnvelope(command, kind, message));
}

void CommandError(Session& session, std::string_view jsonCommand, const Error& error) {
    session.failed = true;
    PrintJson(session, ErrorEnvelope(jsonCommand, error));
}
}
