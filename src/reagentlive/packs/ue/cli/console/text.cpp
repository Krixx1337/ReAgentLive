// reagentlive CLI console, Unreal pack: FText display strings. A text value
// holds a pointer to engine-private text data whose layout changes between
// engine builds (a localized text keeps no inline string), so the string comes
// from the engine itself: KismetTextLibrary.Conv_TextToString, through call,
// on the class default object. The function returns an FString the engine
// allocates; the agent keeps that block and carries it into the next call,
// whose assignment frees the old string, so a session holds at most one.
#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr const char* Converter = "KismetTextLibrary.Conv_TextToString";
constexpr uint32_t MaxTextChars = 4096; // Longer strings are cut and say so.

// The text data pointer: an FText with none is not a value the engine made.
uint64_t TextData(std::span<const uint8_t> bytes) {
    uint64_t pointer = 0;
    if (bytes.size() >= sizeof(pointer)) memcpy(&pointer, bytes.data(), sizeof(pointer));
    return pointer;
}
}

void TextBatch::Add(const ue::Field& field, std::span<const uint8_t> bytes) {
    if (field.type != "TextProperty" || bytes.empty()) return;
    const std::vector<uint8_t> value(bytes.begin(), bytes.end());
    if (std::ranges::find(texts_, value) == texts_.end()) texts_.push_back(value);
}

void TextBatch::Resolve(Session& session) {
    strings_.assign(texts_.size(), std::nullopt);
    errors_.assign(texts_.size(), {});
    if (texts_.empty()) return;
    Client& client = session.client;
    if (!client.Supports(ue::op::Call) || !client.Supports(ue::op::Parameters)) {
        why_ = "the host offers no engine calls; text needs " + std::string(Converter);
        return;
    }
    auto object = DefaultObject(session, "Default__KismetTextLibrary");
    if (!object) { why_ = object.error().message; return; }
    auto function = ue::Function(client, *object, "Conv_TextToString");
    if (!function) { why_ = function.error().message; return; }
    auto signature = ue::Parameters(client, function->address);
    if (!signature) { why_ = signature.error().message; return; }
    const ue::Field* input = nullptr;
    const ue::Field* output = nullptr;
    for (const auto& parameter : signature->parameters) {
        if (parameter.type == "TextProperty" && ue::InputParameter(parameter.parameterFlags)) input = &parameter;
        if (parameter.type == "StrProperty" && (parameter.parameterFlags & ue::Return)) output = &parameter;
    }
    if (!input || !output || output->size != sizeof(ue::ArrayHeader) || signature->size > ue::MaxParams ||
        size_t{input->offset} + input->size > signature->size || size_t{output->offset} + output->size > signature->size) {
        why_ = std::string("unexpected ") + Converter + " signature";
        return;
    }
    const Client::Hold hold(client);
    std::vector<uint8_t> block(signature->size);
    for (size_t i = 0; i < texts_.size(); ++i) {
        const auto& value = texts_[i];
        if (value.size() != input->size) { errors_[i] = "text is " + std::to_string(value.size()) + " bytes; " +
            Converter + " takes " + std::to_string(input->size); continue; }
        if (!TextData(value)) { errors_[i] = "no text data (zeroed text)"; continue; }
        std::ranges::fill(block, uint8_t{0});
        memcpy(block.data() + input->offset, value.data(), value.size());
        auto result = ue::Call(client, *object, "Conv_TextToString", block, function->address, 0);
        if (!result) {
            // A refusal for every text alike (not on the game thread): said once.
            if (result.error().kind == Error::Kind::Agent && result.error().status == Status::Refused && i == 0) {
                why_ = result.error().message;
                return;
            }
            errors_[i] = result.error().message;
            continue;
        }
        if (result->size() != block.size()) { errors_[i] = "incomplete call result"; continue; }
        ue::ArrayHeader header{};
        memcpy(&header, result->data() + output->offset, sizeof(header));
        if (header.count <= 0 || !header.data) { strings_[i] = ""; continue; }
        const uint32_t chars = std::min<uint32_t>(static_cast<uint32_t>(header.count), MaxTextChars);
        auto bytes = client.ReadRange(header.data, size_t{chars} * 2);
        if (!bytes) { errors_[i] = bytes.error().message; continue; }
        auto decoded = native::WideString(*bytes).text;
        if (static_cast<uint32_t>(header.count) > MaxTextChars + 1) decoded += "...";
        strings_[i] = std::move(decoded);
    }
}

Json TextBatch::Value(const ue::Field& field, std::span<const uint8_t> bytes, Json raw, bool meta) const {
    if (field.type != "TextProperty") return raw;
    const std::vector<uint8_t> value(bytes.begin(), bytes.end());
    const auto at = std::ranges::find(texts_, value);
    const auto slot = static_cast<size_t>(at - texts_.begin());
    if (at != texts_.end() && slot < strings_.size() && strings_[slot])
        return meta ? Json{{"text", *strings_[slot]}, {"via", Converter}} : Json(*strings_[slot]);
    const std::string why = !why_.empty() ? why_ : at != texts_.end() && slot < errors_.size() ? errors_[slot] :
        "text not resolved";
    if (raw.is_object()) raw["text_error"] = why;
    return raw;
}
}
