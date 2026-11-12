// reagentlive-table: the verb table without a console or an agent. Prints
// what `help --json` returns for the core's and the Unreal pack's rows (the
// Python client's tests read it), and writes or checks the page generated
// from it.
//   reagentlive-table                      the `help --json` result
//   reagentlive-table --markdown           docs/commands/results.md as text
//   reagentlive-table --write <file>       write that page
//   reagentlive-table --check <file>       exit 1 when the file differs (stale)
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/results/docs.h"
#include "reagentlive/frontends/cli/results/ue/verbs_ue.h"
#include "reagentlive/frontends/cli/results/verbs.h"

int main(int argc, char** argv) {
    using namespace reagentlive::cli;
    const Origin pack[] = {UnrealOrigin};
    const auto verbs = TableRows(UnrealVerbs(), pack, {});
    const auto origins = TableOrigins(pack);
    auto help = MakeHelpTable(verbs, origins);
    help.tableHash = TableHash(verbs, origins);
    const Json table = ToJson(help);
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode.empty()) {
        std::puts(table.dump().c_str());
        return 0;
    }
    const std::string page = ResultsMarkdown(table);
    if (mode == "--markdown") {
        std::fputs(page.c_str(), stdout);
        return 0;
    }
    if ((mode == "--write" || mode == "--check") && argc == 3) {
        if (mode == "--write") {
            std::ofstream file(argv[2], std::ios::binary | std::ios::trunc);
            file << page;
            return file ? 0 : 1;
        }
        std::ifstream file(argv[2], std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::erase(text, '\r');
        if (text == page) return 0;
        std::fprintf(stderr, "%s is stale: run reagentlive-table --write %s\n", argv[2], argv[2]);
        return 1;
    }
    std::fputs("usage: reagentlive-table [--markdown | --write <file> | --check <file>]\n", stderr);
    return 2;
}
