// reagentlive CLI: what a call's parameters mean for the console, in words:
// whether a zeroed block can stand in for a missing one, why `each` refuses a
// function, and why the agent kept a block. Pure; tests cover it.
#pragma once
#include <string>
#include <string_view>
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli::calls {
// An output the call fills: the return value, or an out parameter that is not
// passed by reference (a reference may be an input: const& or in/out).
inline bool FilledByCall(uint8_t flags) {
    return (flags & ue::Return) || ((flags & ue::Output) && !(flags & ue::Reference));
}

// "GameplayTag" for a struct field, else the property class without "Property".
inline std::string TypeName(const ue::Field& field) {
    if (!field.structName.empty()) return field.structName;
    std::string_view type = field.type;
    if (type.ends_with("Property")) type.remove_suffix(8);
    return type == "Str" ? "String" : std::string(type);
}

// What in the value owns memory, as far as its layout shows: "an array",
// "Tags (an array)", or empty when nothing in it does.
template<class Lookup>
std::string OwnedPart(const ue::Field& field, Lookup&& lookup, unsigned depth = 0) {
    const std::string_view type = field.type;
    if (type == "ArrayProperty") return "an array";
    if (type == "StrProperty" || type == "TextProperty") return "a string";
    if (type == "MapProperty") return "a map";
    if (type == "SetProperty") return "a set";
    if (type != "StructProperty" || !field.structType.address || depth >= 3) return {};
    const auto* members = lookup(field.structType);
    if (!members) return {};
    for (const auto& member : *members)
        if (auto part = OwnedPart(member, lookup, depth + 1); !part.empty())
            return depth ? part : member.name + " (" + part + ")";
    return {};
}

// Why a parameter is not plain data, said of the parameter itself:
// "ReturnValue is a GameplayTagContainer, which owns TagArray (an array)".
template<class Lookup>
std::string NotPlain(const ue::Field& field, Lookup&& lookup) {
    const auto part = OwnedPart(field, lookup);
    const auto what = field.name + " is a " + TypeName(field);
    if (part.empty()) return what + ", which the engine does not mark plain data (it may need destroying)";
    if (field.type == "StructProperty") return what + ", which owns " + part;
    return what + ", which owns memory";
}

// Why `each` cannot call a function with these parameters; empty when it can.
// Every message is about the parameter it names.
template<class Lookup>
std::string EachRefusal(const ue::Signature& signature, Lookup&& lookup) {
    for (const auto& parameter : signature.parameters) {
        const auto flags = parameter.parameterFlags;
        if (!FilledByCall(flags))
            return parameter.name + " is an input" + (flags & ue::Reference ? " (passed by reference)" : "") +
                "; each calls only functions with no inputs: use call";
        if (!(flags & ue::Plain)) return NotPlain(parameter, lookup) + "; each reads only plain values: use call";
    }
    return {};
}

// Why a call with no block needs one; empty when every parameter is filled
// by the call, so a zeroed block of the function's size will do.
inline std::string ZeroBlockRefusal(const ue::Signature& signature) {
    for (const auto& parameter : signature.parameters)
        if (!FilledByCall(parameter.parameterFlags))
            return parameter.name + " is an input: pass the block (params shows the layout) or Name=value";
    return {};
}

// Why the agent kept a block: the first output that is not plain data.
template<class Lookup>
std::string KeptReason(const ue::Signature& signature, Lookup&& lookup) {
    for (const auto& parameter : signature.parameters)
        if ((parameter.parameterFlags & (ue::Output | ue::Return)) && !(parameter.parameterFlags & ue::Plain))
            return NotPlain(parameter, lookup) +
                "; the agent keeps the block so the engine frees the old value on the next call (release drops it)";
    return {};
}
}
