// script --decode: the Kismet decoder (values/kismet.h) on synthetic bytecode
// shaped like a compiled Blueprint's (implicit casts, math calls, contexts,
// execution-flow stacks, an uber-graph stub). Pointers and names are made up.
#include "reagentlive/packs/ue/cli/values/kismet.h"
#include "doctest.h"
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace reagentlive::cli::kismet;

namespace {
// Little-endian bytecode, written opcode by opcode.
struct Bytes {
    std::vector<uint8_t> b;
    Bytes& op(uint8_t v) { b.push_back(v); return *this; }
    template<class T> Bytes& put(T v) {
        const auto at = b.size();
        b.resize(at + sizeof(T));
        memcpy(b.data() + at, &v, sizeof(T));
        return *this;
    }
    Bytes& ptr(uint64_t p) { return put(p); }
    Bytes& name(int32_t index, int32_t number = 0) { return put(index).put(index).put(number); }
    Bytes& local(uint64_t p) { return op(ex::LocalVariable).ptr(p); }
};

constexpr uint64_t Speed = 0x10000100, Wide = 0x10000200, Limit = 0x10000300, Result = 0x10000400, Out = 0x10000500,
    Vehicle = 0x10000600, Params = 0x10000700, Magnitude = 0x10000800, Items = 0x10000900;
constexpr uint64_t Greater = 0x20000100, Multiply = 0x20000200, Suppress = 0x20000300, Graph = 0x20000400,
    Tag = 0x20000500, Print = 0x20000600;
constexpr int32_t GetSpeed = 501, Alive = 502;

Symbols Names() {
    Symbols s;
    s.properties = {{Speed, {"Speed", "FloatProperty"}}, {Wide, {"SpeedDouble", "DoubleProperty"}},
        {Limit, {"Limit", "DoubleProperty"}}, {Result, {"Faster", "BoolProperty"}}, {Out, {"ReturnValue", "BoolProperty"}},
        {Vehicle, {"Vehicle", "ObjectProperty"}}, {Params, {"Parameters", "StructProperty"}},
        {Magnitude, {"Magnitude", "FloatProperty"}}, {Items, {"Items", "ArrayProperty"}}};
    s.objects = {{Greater, {"Greater_DoubleDouble", "KismetMathLibrary"}}, {Multiply, {"Multiply_DoubleDouble", "KismetMathLibrary"}},
        {Suppress, {"Suppress", "SuppressionComponent"}}, {Graph, {"ExecuteUbergraph_Mine", "Mine_C"}},
        {Tag, {"GameplayTag", ""}}, {Print, {"PrintString", "KismetSystemLibrary"}}};
    s.names = {{GetSpeed, "GetSpeedKPH"}, {Alive, "State.Alive"}};
    return s;
}

std::vector<std::string> Texts(const Decoded& d) {
    const auto symbols = Names();
    std::vector<std::string> out;
    for (const auto& s : d.statements) out.push_back(Render(s, symbols));
    return out;
}
}

TEST_CASE("a comparison through a context call, a cast and a math operator") {
    Bytes b;
    // Speed = Vehicle.GetSpeedKPH()
    b.op(ex::Let).ptr(Speed).local(Speed)
        .op(ex::Context).local(Vehicle).put<uint32_t>(14).ptr(Speed)
        .op(ex::VirtualFunction).name(GetSpeed).op(ex::EndFunctionParms);
    const uint32_t cast = static_cast<uint32_t>(b.b.size());
    // SpeedDouble = double(Speed); the destination property may be null.
    b.op(ex::Let).ptr(0).local(Wide).op(ex::Cast).op(cast::FloatToDouble).local(Speed);
    // Faster = SpeedDouble > Limit
    b.op(ex::LetBool).local(Result).op(ex::CallMath).ptr(Greater).local(Wide).local(Limit).op(ex::EndFunctionParms);
    b.op(ex::LetBool).op(ex::LocalOutVariable).ptr(Out).local(Result);
    b.op(ex::Return).op(ex::LocalOutVariable).ptr(Out);
    b.op(ex::EndOfScript);
    const auto decoded = Decode(b.b);
    CHECK(decoded.stopped.empty());
    REQUIRE(decoded.statements.size() == 6);
    CHECK(decoded.statements[1].offset == cast);
    CHECK(OpName(decoded.statements[2].op) == "LetBool");
    CHECK(Texts(decoded) == std::vector<std::string>{"Speed = Vehicle.GetSpeedKPH()", "SpeedDouble = double(Speed)",
        "Faster = SpeedDouble > Limit", "ReturnValue = Faster", "return ReturnValue", "end"});

    Operands operands;
    for (const auto& s : decoded.statements) Collect(s, operands);
    CHECK(operands.properties == std::set<uint64_t>{Speed, Wide, Limit, Result, Out, Vehicle});
    CHECK(operands.objects == std::set<uint64_t>{Greater});
    CHECK(operands.names == std::set<int32_t>{GetSpeed});
}

TEST_CASE("constants, struct members, nested operators and flow") {
    Bytes b;
    b.op(ex::PushExecutionFlow).put<uint32_t>(0x99);                                         // push +0x99
    b.op(ex::JumpIfNot).put<uint32_t>(0x40).op(ex::CallMath).ptr(Greater)                    // if !(a > 0.5) goto +0x40
        .op(ex::StructMemberContext).ptr(Magnitude).local(Params).op(ex::DoubleConst).put(0.5).op(ex::EndFunctionParms);
    b.op(ex::Let).ptr(Wide).local(Wide).op(ex::CallMath).ptr(Multiply)                        // nested: parenthesised
        .op(ex::CallMath).ptr(Multiply).local(Limit).op(ex::FloatConst).put(2.0f).op(ex::EndFunctionParms)
        .op(ex::IntConst).put<int32_t>(-3).op(ex::EndFunctionParms);
    b.op(ex::Context).local(Vehicle).put<uint32_t>(0).ptr(0)                                  // a call with a byte and a struct
        .op(ex::FinalFunction).ptr(Suppress).op(ex::ByteConst).op(5)
        .op(ex::StructConst).ptr(Tag).put<int32_t>(12).op(ex::NameConst).name(Alive)
        .op(ex::BitFieldConst).ptr(Result).op(1).op(ex::EndStructConst)
        .op(ex::EndFunctionParms);
    b.op(ex::FinalFunction).ptr(Print).op(ex::StringConst).put('h').put('i').op(0)
        .op(ex::ArrayGetByRef).local(Items).op(ex::IntOne).op(ex::True).op(ex::NoObject).op(ex::EndFunctionParms);
    b.op(ex::PopExecutionFlowIfNot).local(Result);
    b.op(ex::Jump).put<uint32_t>(0x10);
    b.op(ex::PopExecutionFlow);
    b.op(ex::EndOfScript);
    const auto decoded = Decode(b.b);
    CHECK(decoded.stopped.empty());
    CHECK(Texts(decoded) == std::vector<std::string>{"push +0x99", "if !(Parameters.Magnitude > 0.5) goto +0x40",
        "SpeedDouble = (Limit * 2.0) * -3", "Vehicle.Suppress(5, GameplayTag{'State.Alive', true})",
        "KismetSystemLibrary.PrintString(\"hi\", Items[1], true, None)", "if !Faster pop", "goto +0x10", "pop", "end"});
}

TEST_CASE("switch values, arrays and names with a number") {
    Bytes b;
    b.op(ex::Let).ptr(Wide).local(Wide).op(ex::SwitchValue).put<uint16_t>(2).put<uint32_t>(0x50).local(Speed)
        .op(ex::IntZero).put<uint32_t>(0x30).op(ex::DoubleConst).put(1.0)
        .op(ex::IntOne).put<uint32_t>(0x40).op(ex::DoubleConst).put(2.5)
        .op(ex::DoubleConst).put(0.0);
    b.op(ex::SetArray).local(Items).op(ex::NameConst).name(Alive, 3).op(ex::NameConst).name(7777).op(ex::EndArray);
    b.op(ex::EndOfScript);
    const auto decoded = Decode(b.b);
    CHECK(decoded.stopped.empty());
    CHECK(Texts(decoded) == std::vector<std::string>{"SpeedDouble = switch(Speed) {0: 1.0; 1: 2.5; default: 0.0}",
        "Items = ['State.Alive_2', '#7777']", "end"});
}

TEST_CASE("an unknown opcode or a cut operand stops the decode") {
    Bytes b;
    b.local(Speed).op(0x08).op(ex::EndOfScript);
    auto decoded = Decode(b.b);
    CHECK(decoded.statements.size() == 1);
    CHECK(decoded.stopped == "unknown 0x08 at +0x9");

    Bytes cut;
    cut.op(ex::Let).ptr(Speed).local(Speed).op(ex::IntConst).op(1);
    decoded = Decode(cut.b);
    CHECK(decoded.statements.empty());
    CHECK(decoded.stopped.starts_with("truncated at"));

    Bytes params; // A call whose parameter list never ends.
    params.op(ex::FinalFunction).ptr(Print).op(ex::IntOne);
    CHECK(Decode(params.b).stopped.starts_with("truncated at"));

    Bytes unknownCast;
    unknownCast.op(ex::Cast).op(0x09).local(Speed);
    CHECK(Decode(unknownCast.b).stopped == "unknown cast 0x09 at +0x0");
    CHECK(Decode(b.b, 100).stopped.starts_with("offset +0x64 is past the end"));
}

TEST_CASE("an uber graph's entries and one event read alone") {
    Bytes stub; // An event stub: call the graph with its entry offset, then return.
    stub.op(ex::LocalFinalFunction).ptr(Graph).op(ex::IntConst).put<int32_t>(0x0b).op(ex::EndFunctionParms)
        .op(ex::Return).op(ex::Nothing).op(ex::EndOfScript);
    std::vector<uint32_t> entries;
    for (const auto& s : Decode(stub.b).statements) Entries(s, Graph, entries);
    CHECK(entries == std::vector<uint32_t>{0x0b});
    entries.clear();
    for (const auto& s : Decode(stub.b).statements) Entries(s, Print, entries);
    CHECK(entries.empty());

    Bytes graph; // Two events: the dispatch at +0x0 and one after it.
    graph.op(ex::ComputedJump).local(Speed);                                      // +0x0
    graph.op(ex::Let).ptr(Speed).local(Speed).op(ex::IntOne);                     // never reached by an event read
    const auto second = static_cast<uint32_t>(graph.b.size());
    graph.op(ex::LetBool).local(Result).op(ex::True);
    graph.op(ex::PopExecutionFlow);
    graph.op(ex::Return).op(ex::Nothing);
    graph.op(ex::EndOfScript);
    const auto one = Decode(graph.b, second, true);
    CHECK(one.stopped.empty());
    CHECK(Texts(one) == std::vector<std::string>{"Faster = true", "pop"});
    CHECK(Decode(graph.b, 0, true).statements.size() == 1);
    CHECK(Texts(Decode(graph.b)).back() == "end");
}

TEST_CASE("unresolved operands keep their addresses") {
    Bytes b;
    b.op(ex::Let).ptr(0x30000000).op(ex::InstanceVariable).ptr(0x30000000).op(ex::ObjectConst).ptr(0x40000000);
    b.op(ex::CallMath).ptr(0x50000000).op(ex::VirtualFunction).name(9).op(ex::EndFunctionParms).op(ex::EndFunctionParms);
    const auto texts = Texts(Decode(b.b));
    CHECK(texts == std::vector<std::string>{"prop@0x30000000 = obj@0x40000000", "fn@0x50000000(#9())"});
}

TEST_CASE("an object-to-interface cast reads no class pointer") {
    // Public decoders read the cast byte, then the expression: no pointer.
    Bytes b;
    b.op(ex::Let).ptr(0).local(Vehicle).op(ex::Cast).op(cast::ObjectToInterface).local(Vehicle);
    b.op(ex::EndOfScript);
    const auto decoded = Decode(b.b);
    CHECK(decoded.stopped.empty());
    CHECK(Texts(decoded) == std::vector<std::string>{"Vehicle = interface(Vehicle)", "end"});
    Operands operands;
    for (const auto& s : decoded.statements) Collect(s, operands);
    CHECK(operands.objects.empty());
}

TEST_CASE("newer opcodes: field paths, instrumentation, transactions and markers") {
    Bytes b;
    b.op(ex::EndParmValue).op(ex::DeprecatedOp4A);
    b.op(ex::FieldPathConst).op(ex::PropertyConst).ptr(Speed);
    b.op(ex::InstrumentationEvent).op(7);
    b.op(ex::InstrumentationEvent).op(instrumentation::InlineEvent).name(Alive);
    b.op(ex::AutoRtfmTransact).put<int32_t>(3).put<uint32_t>(0x80)
        .op(ex::Let).ptr(Speed).local(Speed).op(ex::IntOne)
        .op(ex::AutoRtfmAbortIfNot).local(Result)
        .op(ex::AutoRtfmStopTransact).put<int32_t>(3).op(1);
    b.op(ex::AutoRtfmAbort);
    b.op(ex::EndOfScript);
    const auto decoded = Decode(b.b);
    CHECK(decoded.stopped.empty());
    CHECK(Texts(decoded) == std::vector<std::string>{"end parm value", "nop 4a", "field property Speed", "instrument 7",
        "instrument inline 'State.Alive'", "transact 3 {Speed = 1, if !Faster abort transact}", "abort transact", "end"});
    REQUIRE(decoded.statements.size() == 8);
    CHECK(decoded.statements[5].children.back().op == ex::AutoRtfmStopTransact);
    CHECK(OpName(ex::AutoRtfmTransact) == "AutoRtfmTransact");

    Bytes open; // A transaction without its stop is cut.
    open.op(ex::AutoRtfmTransact).put<int32_t>(1).put<uint32_t>(0).op(ex::IntOne);
    CHECK(Decode(open.b).stopped.starts_with("truncated at"));
}

TEST_CASE("string constants decode to UTF-8") {
    Bytes b; // Latin-1 "café"; UTF-16 "é€😀" and an unpaired high surrogate.
    b.op(ex::StringConst).op('c').op('a').op('f').op(0xE9).op(0);
    b.op(ex::UnicodeStringConst).put<uint16_t>(0xE9).put<uint16_t>(0x20AC).put<uint16_t>(0xD83D).put<uint16_t>(0xDE00)
        .put<uint16_t>(0xD800).put<uint16_t>('x').put<uint16_t>(0);
    const auto decoded = Decode(b.b);
    CHECK(decoded.stopped.empty());
    REQUIRE(decoded.statements.size() == 2);
    CHECK(decoded.statements[0].text == "caf\xC3\xA9");
    CHECK(decoded.statements[1].text == "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xEF\xBF\xBDx");
}
