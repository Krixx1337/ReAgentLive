// reagentlive core agent: what its files share (agent.cpp, agent_search.cpp). Internal.
#pragma once
#include <stdint.h>

namespace reagentlive::detail {
// A steady clock in microseconds.
int64_t Microseconds();
// Inside the agent's own request and response copies or its section view:
// a search hit there is the agent's copy of the pattern, not a result.
bool InAgentState(uintptr_t address);
// At the start of every tick: the search job's slice when it runs on the
// tick (no worker), or taking its end from the worker.
void TickSearchJob();
// The threads the agent has started: only the search worker, and only when
// the host sets Host::backgroundSearch. For tests.
uint32_t ThreadsStarted();
}
