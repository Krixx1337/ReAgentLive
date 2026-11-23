// reagentlive-table: the verb table without a console or an agent. Prints
// what `help --json` returns for the core's and the Unreal pack's rows (the
// Python client's tests read it), and writes or checks the pages generated
// from it.
//   reagentlive-table                              the `help --json` result
//   reagentlive-table --markdown                   docs/commands/results.md as text
//   reagentlive-table --write <file>               write that page
//   reagentlive-table --check <file>               exit 1 when the file differs (stale)
//   reagentlive-table --write-reference <file>     docs/commands/reference.md, likewise
//   reagentlive-table --check-reference <file>
//   reagentlive-table --check-calls                one call per stdin line, checked as a console
//                                                  checks it before it runs (a console's --check)
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/results/docs.h"
#include "reagentlive/packs/ue/cli/results/verbs_ue.h"
#include "reagentlive/frontends/cli/results/verbs.h"

int main(int argc, char** argv) {
    using namespace reagentlive::cli;
    const Origin pack[] = {UnrealOrigin};
    const auto verbs = TableRows(UnrealVerbs(), {});
    const auto origins = TableOrigins(pack);
    auto help = MakeHelpTable(verbs, origins);
    help.tableHash = TableHash(verbs, origins);
    const Json table = ToJson(help);
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode.empty()) {
        std::puts(table.dump().c_str());
        return 0;
    }
    if (mode == "--check-calls") {
        int failed = 0;
        for (std::string line; std::getline(std::cin, line);) {
            if (line.find_first_not_of(" 	") == std::string::npos) continue;
            const Json call = Json::parse(line, nullptr, false);
            const std::string name = call.is_object() && call.contains("verb") && call["verb"].is_string() ?
                call["verb"].get<std::string>() : "call";
            Json envelope = ResultEnvelope(name, {{"checked", true}});
            if (call.is_discarded()) envelope = ErrorEnvelope("call", ErrorKind::Schema, "not JSON");
            else if (auto refused = CheckWithoutTarget(verbs, call))
                envelope = Json{{"command", name}, {"ok", false}, {"error", std::move(*refused)}};
            failed |= envelope["ok"].get<bool>() ? 0 : 1;
            std::puts(envelope.dump(-1, ' ', true, Json::error_handler_t::replace).c_str());
        }
        return failed;
    }
    // What the reference adds to the table: the general notes, the view keys, the error kinds.
    Json extra = {{"notes", GeneralNotes()}, {"view", Json::array()}, {"errors", Json::array()}};
    for (const auto& note : UnrealNotes()) extra["notes"].push_back(note);
    for (const auto& key : ViewKeys()) extra["view"].push_back(ArgJson(key));
    for (const auto& kind : ErrorKinds)
        extra["errors"].push_back({{"kind", kind.name}, {"meaning", kind.meaning}, {"message", kind.message}});
    const bool reference = mode == "--write-reference" || mode == "--check-reference";
    const std::string page = reference ? ReferenceMarkdown(table, extra) : ResultsMarkdown(table);
    if (mode == "--markdown") {
        std::fputs(page.c_str(), stdout);
        return 0;
    }
    if ((mode == "--write" || mode == "--check" || reference) && argc == 3) {
        if (mode == "--write" || mode == "--write-reference") {
            std::ofstream file(argv[2], std::ios::binary | std::ios::trunc);
            file << page;
            return file ? 0 : 1;
        }
        std::ifstream file(argv[2], std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::erase(text, '\r');
        if (text == page) return 0;
        std::fprintf(stderr, "%s is stale: run reagentlive-table %s %s\n", argv[2],
            reference ? "--write-reference" : "--write", argv[2]);
        return 1;
    }
    std::fputs("usage: reagentlive-table [--markdown | --write <file> | --check <file> | --write-reference <file> | "
               "--check-reference <file> | --check-calls]\n", stderr);
    return 2;
}
