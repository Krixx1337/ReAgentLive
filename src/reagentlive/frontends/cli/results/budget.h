// reagentlive CLI: output budgets. A result past its line's budget is not
// printed: the envelope carries a digest instead, with the result's size,
// its largest parts and the flags that would narrow the line. Pure: the
// console decides the budget, prints, and writes files (console/output.cpp).
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/verb.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli::budget {
// A tool caller's default: what one result may take of an agent's context.
constexpr size_t ToolBudget = 25000;
// The verb flags that narrow a result, as the digest names them.
inline constexpr std::string_view Narrowing[] = {"--fields", "--where", "--match", "--limit", "--nonzero", "--own", "--cls"};

// Characters per path of a result, as the console prints it. Array elements
// fold into one path (`rows[].Price`), so a path's size is all its elements'.
struct Node {
    std::string path;           // Empty for the root.
    std::string key;            // Its last segment: an object key, or "[]" for an array's elements.
    size_t chars = 0;
    size_t count = 0;           // An array's elements, over every array folded into it.
    bool array = false, object = false;
    size_t parent = SIZE_MAX;
    std::vector<size_t> children;
};

class Profile {
public:
    explicit Profile(const Json& value) {
        nodes_.push_back({});
        Walk(value, 0);
    }
    const std::vector<Node>& Nodes() const { return nodes_; }
    size_t Chars() const { return nodes_.front().chars; }

    // The result's main list: the largest array of objects with more than
    // one element, or SIZE_MAX.
    size_t Rows() const {
        size_t best = SIZE_MAX;
        for (size_t i = 1; i < nodes_.size(); ++i) {
            const auto& node = nodes_[i];
            if (!node.array || node.count < 2 || node.children.empty() || !nodes_[node.children.front()].object) continue;
            if (best == SIZE_MAX || node.chars > nodes_[best].chars) best = i;
        }
        return best;
    }

    // The largest parts outside the main list's own frame, `limit` at most:
    // from the largest path down to the child holding most of it, no two
    // overlapping.
    std::vector<std::pair<std::string, size_t>> Largest(size_t limit) const {
        std::set<size_t> frame; // The root, the main list and its elements, and what holds them.
        if (const size_t rows = Rows(); rows != SIZE_MAX) {
            frame.insert(rows);
            frame.insert(nodes_[rows].children.front());
            for (size_t up = rows; up != SIZE_MAX; up = nodes_[up].parent) frame.insert(up);
        }
        frame.insert(0);
        std::vector<size_t> order;
        for (size_t i = 1; i < nodes_.size(); ++i)
            if (!frame.contains(i)) order.push_back(i);
        std::ranges::stable_sort(order, [&](size_t a, size_t b) { return nodes_[a].chars > nodes_[b].chars; });
        std::vector<size_t> picked;
        for (size_t i : order) {
            if (picked.size() >= limit) break;
            for (bool deeper = true; deeper;) {
                deeper = false;
                for (size_t child : nodes_[i].children)
                    if (nodes_[child].chars * 10 >= nodes_[i].chars * 6) { i = child; deeper = true; break; }
            }
            const bool overlaps = std::ranges::any_of(picked, [&](size_t p) { return Within(i, p) || Within(p, i); });
            if (!overlaps) picked.push_back(i);
        }
        std::vector<std::pair<std::string, size_t>> out;
        for (size_t i : picked) out.emplace_back(nodes_[i].path, nodes_[i].chars);
        return out;
    }

    // What `--fields` can name under the main list, and the characters each
    // takes over all rows: member paths as RowReader reads them (an array's
    // header and the `rows` level of nested arrays dropped). Sorted by path.
    std::map<std::string, size_t> Fields() const {
        std::map<std::string, size_t> out;
        const size_t rows = Rows();
        if (rows == SIZE_MAX) return out;
        size_t base = nodes_[rows].children.front();
        // A table row's members sit under its `value` (rows); a call's rows are the members.
        for (size_t child : nodes_[base].children)
            if (nodes_[child].key == "value" && nodes_[child].object) { base = child; break; }
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (!nodes_[i].children.empty() || !Within(i, base) || i == base) continue;
            if (Header(nodes_[i].parent)) continue;
            std::vector<std::string_view> keys;
            for (size_t up = i; up != base; up = nodes_[up].parent) {
                const auto& node = nodes_[up];
                if (node.key == "[]") continue;
                if (node.key == "rows" && Header(node.parent)) continue;
                keys.push_back(node.key);
            }
            if (keys.empty()) continue;
            std::string path;
            for (auto key = keys.rbegin(); key != keys.rend(); ++key) path += (path.empty() ? "" : ".") + std::string(*key);
            out[path] += nodes_[i].chars;
        }
        return out;
    }

private:
    static std::string Text(const Json& value) { return value.dump(-1, ' ', true, Json::error_handler_t::replace); }

    size_t Child(size_t parent, const std::string& key) {
        const std::string path = nodes_[parent].path + (key == "[]" || nodes_[parent].path.empty() ? "" : ".") + key;
        if (const auto found = index_.find(path); found != index_.end()) return found->second;
        Node node;
        node.path = path;
        node.key = key;
        node.parent = parent;
        nodes_.push_back(std::move(node));
        const size_t at = nodes_.size() - 1;
        nodes_[parent].children.push_back(at);
        index_.emplace(path, at);
        return at;
    }

    // The value's printed size, added to `node` and below.
    size_t Walk(const Json& value, size_t node) {
        size_t size = 0;
        if (value.is_object()) {
            nodes_[node].object = true;
            size = 2 + (value.empty() ? 0 : value.size() - 1);
            for (const auto& [key, member] : value.items()) {
                const size_t child = Child(node, key);
                const size_t keySize = Text(Json(key)).size() + 1;
                size += keySize + Walk(member, child);
                nodes_[child].chars += keySize; // The key belongs to its member.
            }
        } else if (value.is_array()) {
            nodes_[node].array = true;
            nodes_[node].count += value.size();
            size = 2 + (value.empty() ? 0 : value.size() - 1);
            if (!value.empty()) {
                const size_t elements = Child(node, "[]");
                for (const auto& element : value) size += Walk(element, elements);
            }
        } else {
            size = Text(value).size();
        }
        nodes_[node].chars += size;
        return size;
    }

    bool Within(size_t node, size_t ancestor) const {
        for (; node != SIZE_MAX; node = nodes_[node].parent)
            if (node == ancestor) return true;
        return false;
    }
    // An array as RowReader prints it: {count, rows, ...}.
    bool Header(size_t node) const {
        if (node == SIZE_MAX || !nodes_[node].object) return false;
        bool count = false, rows = false;
        for (size_t child : nodes_[node].children) {
            count |= nodes_[child].key == "count";
            rows |= nodes_[child].key == "rows";
        }
        return count && rows;
    }

    std::vector<Node> nodes_;
    std::map<std::string, size_t> index_;
};

// A flag the narrower line sets: its value, or none for a switch.
struct Change {
    std::string flag;
    std::optional<std::string> value;
    bool operator==(const Change&) const = default;
};

namespace detail {
inline bool HasFlag(const Verb& verb, std::string_view name) {
    return std::ranges::any_of(verb.flags, [&](const Flag& flag) { return flag.name == name; });
}
// Members that fit in `room` characters, top down: a member kept whole when
// it fits (smallest first), else its own members tried the same way. Field
// paths, sorted; `sum` gets their characters.
inline std::vector<std::string> Pick(const std::map<std::string, size_t>& fields, size_t room, size_t& sum) {
    const auto under = [](const std::string& path, const std::string& prefix) {
        return prefix.empty() || path == prefix || (path.starts_with(prefix) && path[prefix.size()] == '.');
    };
    // The members one level below `prefix`, with their characters.
    const auto level = [&](const std::string& prefix) {
        std::map<std::string, size_t> out;
        for (const auto& [path, chars] : fields) {
            if (!under(path, prefix) || path == prefix) continue;
            const size_t from = prefix.empty() ? 0 : prefix.size() + 1;
            const size_t dot = path.find('.', from);
            out[path.substr(0, dot)] += chars;
        }
        return out;
    };
    std::vector<std::string> kept;
    sum = 0;
    std::vector<std::string> pending{""};
    while (!pending.empty()) {
        const auto prefix = std::move(pending.front());
        pending.erase(pending.begin());
        const auto members = level(prefix);
        std::vector<std::pair<std::string, size_t>> bySize(members.begin(), members.end());
        std::ranges::stable_sort(bySize, {}, &std::pair<std::string, size_t>::second);
        for (const auto& [member, chars] : bySize) {
            if (sum + chars <= room) {
                kept.push_back(member);
                sum += chars;
            } else if (!fields.contains(member)) {
                pending.push_back(member); // A struct or array: try its members.
            }
        }
    }
    if (kept.empty() && !fields.empty()) { // Nothing fits: the smallest, on a smaller page.
        const auto smallest = std::ranges::min_element(fields, {}, &std::pair<const std::string, size_t>::second);
        kept.push_back(smallest->first);
        sum = smallest->second;
    }
    std::ranges::sort(kept);
    return kept;
}
}

// The flags that narrow a line whose result took `chars` of a `limit`
// budget, from its profile and the verb's own flags; empty when the verb
// has none that would help. `given` is the line's flags as given.
inline std::vector<Change> Narrow(const Profile& profile, const Verb& verb, const Args& given, size_t limit) {
    std::vector<Change> changes;
    const size_t target = limit / 4 * 3; // Room for the envelope and a rounding error.
    const size_t rows = profile.Rows();
    const auto& nodes = profile.Nodes();
    const size_t frame = rows == SIZE_MAX ? profile.Chars() : profile.Chars() - nodes[rows].chars;
    const size_t room = target > frame ? target - frame : 0;
    if (rows != SIZE_MAX && nodes[rows].count) {
        const size_t shown = nodes[rows].count;
        size_t kept = nodes[rows].chars;
        if (detail::HasFlag(verb, "--fields")) {
            const auto fields = profile.Fields();
            size_t sum = 0;
            const auto chosen = detail::Pick(fields, room, sum);
            if (!chosen.empty() && sum < kept) {
                std::string list;
                for (const auto& path : chosen) list += (list.empty() ? "" : ",") + path;
                changes.push_back({"--fields", list});
                kept = sum + shown * 2;
            }
        }
        if (kept > room && detail::HasFlag(verb, "--limit")) {
            const size_t perRow = std::max<size_t>(1, kept / shown);
            const size_t fit = std::max<size_t>(1, room / perRow);
            if (fit < shown) changes.push_back({"--limit", std::to_string(fit)});
        }
    }
    if (changes.empty() && detail::HasFlag(verb, "--nonzero") && !given.Has("--nonzero")) changes.push_back({"--nonzero", {}});
    else if (changes.empty() && detail::HasFlag(verb, "--own") && !given.Has("--own")) changes.push_back({"--own", {}});
    return changes;
}

// The verb's narrowing flags, as `help` names them: the digest's `narrow`.
inline std::vector<std::string> NarrowingFlags(const Verb& verb) {
    std::vector<std::string> out;
    for (const auto& flag : verb.flags)
        if (std::ranges::find(Narrowing, flag.name) != std::end(Narrowing)) out.emplace_back(flag.name);
    return out;
}

// A typed line's arguments with the changes applied, as a line again. `refs`
// turns $N into literal refs (the line runs again later, after $N moved);
// nullopt when a $N has no ref or the verb takes the rest of the line.
inline std::optional<std::string> TypedLine(const Verb& verb, Args args, const std::vector<Change>& changes,
                                            const std::vector<std::string>& refs) {
    if (std::ranges::any_of(verb.positionals, [](const Positional& p) { return p.arity == Arity::Rest; })) return std::nullopt;
    for (const auto& change : changes) {
        std::erase_if(args.flags, [&](const Args::Given& given) { return given.flag == change.flag; });
        const auto row = std::ranges::find(verb.flags, change.flag, &Flag::name);
        if (row == verb.flags.end()) return std::nullopt;
        Args::Given given{row->name, {}, {}};
        if (change.value) given.values.push_back(*change.value);
        args.flags.push_back(std::move(given));
    }
    bool missing = false;
    const auto literal = [&](const std::string& token) {
        if (token.size() < 2 || token[0] != '$' || token[1] < '0' || token[1] > '9') return token;
        size_t end = 1;
        while (end < token.size() && token[end] >= '0' && token[end] <= '9') ++end;
        const size_t n = std::stoul(token.substr(1, end - 1));
        if (!n || n > refs.size()) { missing = true; return token; }
        return refs[n - 1] + token.substr(end);
    };
    std::string line(verb.name);
    for (const auto& positional : args.positionals) line += " " + literal(positional);
    for (const auto& given : args.flags) {
        line += " " + std::string(given.flag);
        for (const auto& value : given.values) line += " " + literal(value);
        if (!given.rest.empty()) line += " " + given.rest;
    }
    if (missing) return std::nullopt;
    return line;
}

// A JSON call with the changes applied to its args.
inline Json CallLine(Json call, const Verb& verb, const std::vector<Change>& changes) {
    if (!call.contains("args") || !call["args"].is_object()) call["args"] = Json::object();
    for (const auto& change : changes) {
        const auto row = std::ranges::find(verb.flags, change.flag, &Flag::name);
        if (row == verb.flags.end()) continue;
        const std::string key(FlagKey(*row));
        if (!change.value) call["args"][key] = true;
        else if (row->type == "int") call["args"][key] = std::stoull(*change.value);
        else call["args"][key] = *change.value;
    }
    return call;
}

// The envelope that stands for a result past its budget. `next` is the
// narrower line (typed or a JSON call's text); `file` holds the whole
// envelope when the line must not run again.
inline Json Digest(const Profile& profile, size_t chars, size_t limit, const Verb* verb,
                   std::optional<std::string> next, std::optional<std::string> file) {
    Json digest = {{"chars", chars}, {"budget", limit}};
    if (const size_t rows = profile.Rows(); rows != SIZE_MAX)
        digest["rows"] = {{"path", profile.Nodes()[rows].path}, {"count", profile.Nodes()[rows].count}};
    Json largest = Json::array();
    for (const auto& [path, size] : profile.Largest(5)) largest.push_back({{"path", path}, {"chars", size}});
    digest["largest"] = std::move(largest);
    if (verb)
        if (auto flags = NarrowingFlags(*verb); !flags.empty()) digest["narrow"] = std::move(flags);
    if (next) digest["next"] = std::move(*next);
    if (file) digest["file"] = std::move(*file);
    return digest;
}

// The line as the budget sees it (the console fills it: console/output.cpp).
struct Line {
    const Verb* verb = nullptr;
    std::optional<Args> args;           // As given; nullopt when they do not parse again.
    std::optional<Json> call;           // A JSON call; else a typed line.
    std::vector<std::string> refs;      // $1..$N as literal text when the line began.
    std::string flags;                  // The line flags a typed rerun keeps.
    size_t limit = 0;                   // The budget.
    bool reads = true;                  // Safe to run again.
};

// The envelope that stands for `record`, `chars` printed past the line's
// budget: a digest with a narrower line to run, or, when the line must not
// run again, with the file `keep(text)` wrote the whole envelope to.
// nullopt when keep wrote none: the record prints whole, so a write's result
// is never lost.
template<class Keep>
std::optional<Json> Replace(Json& record, size_t chars, const Line& line, Keep&& keep) {
    const Profile profile(record["result"]);
    std::optional<std::string> next, file;
    if (!line.reads) {
        file = keep(record.dump(-1, ' ', true, Json::error_handler_t::replace));
        if (file->empty()) return std::nullopt;
    } else if (line.verb && line.args) {
        const auto changes = Narrow(profile, *line.verb, *line.args, line.limit);
        if (!changes.empty() && line.call) {
            next = CallLine(*line.call, *line.verb, changes).dump(-1, ' ', true, Json::error_handler_t::replace);
        } else if (!changes.empty()) {
            next = TypedLine(*line.verb, *line.args, changes, line.refs);
            if (next && !line.flags.empty()) *next += " " + line.flags;
        }
    }
    Json out = {{"command", record["command"]}, {"ok", true},
        {"digest", Digest(profile, chars, line.limit, line.verb, std::move(next), std::move(file))}};
    if (record.contains("timing")) out["timing"] = std::move(record["timing"]);
    return out;
}

// The folder of one agent instance's kept results, beside its cache file:
// keyed by target pid and the agent's owner id (new on every injection), so
// a game that reuses the pid never sees an earlier one's.
inline std::string ResultsPrefix(uint32_t pid) { return "reagentlive-" + std::to_string(pid) + "-"; }
inline std::string ResultsName(uint32_t pid, uint32_t owner) {
    return ResultsPrefix(pid) + std::to_string(owner) + ".results";
}
}
