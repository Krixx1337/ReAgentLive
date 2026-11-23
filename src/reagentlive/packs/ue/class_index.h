// reagentlive Unreal pack: the class index find --cls and each walk instead of
// the whole object table (pure, tested). Per table slot it keeps the object
// last seen there and that object's class. Each query starts with a pass that
// compares every slot's pointer with the kept one and reads the header of a
// changed slot only, so the index follows the table without a full rebuild;
// a full rebuild runs once the last is RebuildMs old (an object freed and
// replaced at the same address in the same slot is the one change a pass
// cannot see). Which classes match a name is kept per name until a rebuild.
#pragma once
#include <stdint.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reagentlive::ue::index {
constexpr uint64_t RebuildMs = 60000;
// Names whose class verdicts are kept at once; more clears them.
constexpr size_t MaxNames = 16;

// Source: what a pass reads, as a type with
//   int32_t Count();                                      the table's slot count
//   const void* Object(int32_t slot);                     the slot's object pointer (null: empty)
//   const void* ClassOf(const void* object, int32_t slot); its class when it is a readable object
//                                                         of this slot, else null
//   bool Named(const void* type, std::string_view name);  the class or a parent is named `name`
// Spent: bool(), true once this tick's share is used.
// Containers may throw std::bad_alloc; callers catch it and Clear().
class ClassIndex {
public:
    // A new query for `name`: a full rebuild when the last is RebuildMs old
    // (or there was none), else a pass over changed slots.
    void Start(std::string_view name, uint64_t nowMs) {
        if (!builtMs_ || nowMs - builtMs_ >= RebuildMs) {
            Clear();
            builtMs_ = nowMs ? nowMs : 1;
        }
        cursor_ = 0;
        passed_ = false;
        if (verdicts_.size() >= MaxNames && !verdicts_.contains(std::string(name))) verdicts_.clear();
        active_ = &verdicts_[std::string(name)];
        name_.assign(name);
    }
    // Brings the index up to date, then the verdicts for the query's name.
    // False while work is left: call again with the same source.
    template<class Source, class Spent> bool Step(Source& source, Spent&& spent) {
        if (!passed_) {
            const int32_t count = std::max(source.Count(), 0);
            if (objects_.size() != static_cast<size_t>(count)) {
                objects_.resize(static_cast<size_t>(count), nullptr);
                classOf_.resize(static_cast<size_t>(count), 0);
            }
            for (unsigned checked = 0; cursor_ < count; ++cursor_, ++checked) {
                if ((checked & 255) == 255 && spent()) return false;
                const void* object = source.Object(cursor_);
                const auto slot = static_cast<size_t>(cursor_);
                if (object == objects_[slot]) continue;
                const void* type = object ? source.ClassOf(object, cursor_) : nullptr;
                classOf_[slot] = type ? Id(type) + 1 : 0;
                objects_[slot] = object; // Last: a slot cut short by a fault is read again.
            }
            passed_ = true;
        }
        auto& verdicts = *active_;
        for (unsigned checked = 0; verdicts.size() < classes_.size(); ++checked) {
            if ((checked & 15) == 15 && spent()) return false;
            verdicts.push_back(source.Named(classes_[verdicts.size()], name_) ? 1 : 0);
        }
        return true;
    }
    // The first slot in [from, end) whose kept class matches the query's
    // name; `end` when none. After Step returned true.
    int32_t Next(int32_t from, int32_t end) const {
        const auto& verdicts = *active_;
        const int32_t last = std::min(end, static_cast<int32_t>(classOf_.size()));
        for (int32_t slot = std::max(from, 0); slot < last; ++slot) {
            const uint32_t id = classOf_[static_cast<size_t>(slot)];
            if (id && id <= verdicts.size() && verdicts[id - 1]) return slot;
        }
        return end;
    }
    void Clear() {
        objects_.clear();
        classOf_.clear();
        classes_.clear();
        ids_.clear();
        verdicts_.clear();
        active_ = &verdicts_[name_];
        builtMs_ = 0;
        cursor_ = 0;
        passed_ = false;
    }
    size_t Classes() const { return classes_.size(); }

private:
    uint32_t Id(const void* type) {
        const auto [found, added] = ids_.try_emplace(type, static_cast<uint32_t>(classes_.size()));
        if (added) classes_.push_back(type);
        return found->second;
    }
    std::vector<const void*> objects_;  // Per slot: the object the last pass saw.
    std::vector<uint32_t> classOf_;     // Per slot: its class id + 1; 0 for none.
    std::vector<const void*> classes_;  // Class id -> class.
    std::unordered_map<const void*, uint32_t> ids_;
    std::unordered_map<std::string, std::vector<uint8_t>> verdicts_; // Per name: matches per class id.
    std::vector<uint8_t>* active_ = nullptr;
    std::string name_;
    uint64_t builtMs_ = 0;
    int32_t cursor_ = 0;
    bool passed_ = false;
};
}
