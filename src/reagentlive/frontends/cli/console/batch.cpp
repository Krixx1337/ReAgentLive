// reagentlive CLI console: batch (calls in one hold) and run (a plan file's steps).
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "reagentlive/frontends/cli/call/plan.h"
#include "reagentlive/version.h"
#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
void RunBatch(Session& session, const Command& command, std::string_view jsonCommand) {
    std::vector<Json> results;
    std::vector<BatchItemTicks> ticks;
    uint64_t first = 0, last = 0;
    // A plan's batch step: its envelope goes to the plan's capture, not stdout.
    std::vector<Json>* outer = session.capture;
    {
        Client::Hold hold(session.client);
        session.capture = &results;
        for (const auto& item : command.fields) {
            const size_t printed = results.size();
            BatchItemTicks at{hold.LastTick()};
            hold.BeginSpan();
            Execute(session, ReadLine(item), true);
            if (hold.SpanFirstTick()) {
                at.first = hold.SpanFirstTick();
                at.last = hold.LastTick();
            }
            if (results.size() > printed) {
                ticks.resize(results.size());
                ticks.back() = at;
            }
        }
        session.capture = outer;
        first = hold.FirstTick();
        last = hold.LastTick();
    }
    auto result = MakeBatch(std::move(results), ticks, first, last);
    if (!TickAccurate(session)) result.tickAccurate = false;
    Emit(session, jsonCommand, std::move(result));
}

// run: a plan's steps, each run as its own call (its pack checked, its hold
// its own) with its envelope captured into a line; then the report.
void RunPlan(Session& session, const Command& command, std::string_view jsonCommand) {
    std::ifstream in(command.file, std::ios::binary);
    if (!in) return JsonError(session, jsonCommand, ErrorKind::File, "plan: cannot read " + command.file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto steps = plan::Parse(text);
    if (!steps) return JsonError(session, jsonCommand, ErrorKind::Schema, steps.error());
    std::ofstream out;
    if (!command.target.empty()) {
        out.open(command.target, std::ios::binary | std::ios::trunc);
        if (!out) return JsonError(session, jsonCommand, ErrorKind::File, "out: cannot write " + command.target);
    }
    const auto began = Clock::now();
    const auto since = [](Clock::time_point from) {
        return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
    };
    const auto tenths = [](double ms) { return static_cast<double>(static_cast<int64_t>(ms * 10 + 0.5)) / 10; };
    plan::Tally tally;
    shape::Run result;
    std::vector<Json> rows;
    const auto write = [&](const Json& line) {
        ++result.lines;
        // Flushed per line: a background run's lines are read while it runs.
        if (out.is_open()) out << line.dump(-1, ' ', true, Json::error_handler_t::replace) << '\n' << std::flush;
        else rows.push_back(line);
    };
    std::vector<Json>* outer = session.capture;
    ProgressLine progress("steps", steps->size());
    for (size_t index = 0; index < steps->size(); ++index) {
        progress.Done(index);
        const plan::Step& step = (*steps)[index];
        plan::Tally::StepTime time{index + 1, step.verb};
        const size_t runs = step.Runs();
        const auto stepBegan = Clock::now();
        for (size_t run = 0; run < runs; ++run) {
            // A repeat keeps its pace from the step's start, not from the last call's end.
            if (run) {
                const auto due = stepBegan + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(step.every * static_cast<double>(run)));
                if (const auto now = Clock::now(); due > now) std::this_thread::sleep_until(due);
            }
            std::vector<Json> records;
            session.capture = &records;
            const auto callBegan = Clock::now();
            Execute(session, ReadLine(step.call.dump()), false);
            const double ms = since(callBegan);
            session.capture = outer;
            // A step that printed nothing is a console bug: its line says so, and the run's
            // usage line counts it as refused, so usage shows it.
            if (records.empty() && session.line && session.line->ok) {
                session.line->ok = false;
                session.line->refused = KindName(ErrorKind::Internal);
            }
            Json envelope = records.empty() ? Json{{"command", step.verb}, {"ok", false},
                {"error", {{"kind", KindName(ErrorKind::Internal)}, {"message", "the step printed no result"}}}} :
                std::move(records.back());
            Json line = {{"step", index + 1}};
            if (runs > 1) line["run"] = run + 1;
            line["at_ms"] = tenths(since(began));
            line["elapsed_ms"] = tenths(ms);
            for (auto& [key, value] : envelope.items()) line[key] = std::move(value);
            if (step.expect) {
                const auto why = plan::Mismatch(*step.expect, line);
                line["expect"] = why.empty() ? "met" : "failed";
                if (!why.empty()) line["expect_failed"] = why;
                ++(why.empty() ? tally.expectMet : tally.expectFailed);
            }
            tally.Count(line);
            ++time.calls;
            time.ms += ms;
            write(line);
        }
        tally.steps.push_back(std::move(time));
    }
    progress.Done(steps->size());
    shape::RunReport& report = result.report;
    report.steps = steps->size();
    report.calls = tally.calls;
    report.ok = tally.ok;
    report.failed = tally.failed;
    if (!tally.refused.empty()) report.refused = tally.refused;
    report.expectMet = tally.expectMet;
    report.expectFailed = tally.expectFailed;
    report.ms = tenths(since(began));
    for (const auto& time : tally.steps) report.timings.push_back({time.step, time.verb, time.calls, tenths(time.ms)});
    report.console = Version;
    report.commit = ConsoleCommit();
    report.agent = session.client.AgentBuild();
    report.tableHash = session.tableHash;
    if (out.is_open()) {
        out << Json{{"report", ToJson(report)}}.dump(-1, ' ', true, Json::error_handler_t::replace) << '\n';
        out.flush();
        if (!out) return JsonError(session, jsonCommand, ErrorKind::File, "out: cannot write " + command.target);
        result.file = command.target;
    } else {
        result.rows = std::move(rows);
    }
    Emit(session, jsonCommand, std::move(result));
}
}
