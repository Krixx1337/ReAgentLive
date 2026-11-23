// reagentlive CLI, Unreal pack: Kismet bytecode (UStruct::Script) as
// statements. Decode reads the bytes into expression trees, Operands lists the
// pointers and names they hold, Render prints them with resolved symbols.
// Opcodes and operand sizes are UE5's public Script.h and ScriptCore.cpp: a
// property or object operand is an 8-byte pointer, a script name 12 bytes
// (comparison index, display index, number), a skip or jump offset 4 bytes.
// An opcode not listed stops the decode; nothing is guessed. Pure; tested.
#pragma once
#include <cstdint>
#include <cstring>
#include <format>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reagentlive::cli::kismet {
// EExprToken values this decoder reads.
namespace ex {
constexpr uint8_t LocalVariable = 0x00, InstanceVariable = 0x01, DefaultVariable = 0x02, Return = 0x04, Jump = 0x06,
    JumpIfNot = 0x07, Assert = 0x09, Nothing = 0x0B, NothingInt32 = 0x0C, Let = 0x0F, BitFieldConst = 0x11, ClassContext = 0x12,
    MetaCast = 0x13, LetBool = 0x14, EndFunctionParms = 0x16, Self = 0x17, Skip = 0x18, Context = 0x19,
    ContextFailSilent = 0x1A, VirtualFunction = 0x1B, FinalFunction = 0x1C, IntConst = 0x1D, FloatConst = 0x1E,
    StringConst = 0x1F, ObjectConst = 0x20, NameConst = 0x21, RotationConst = 0x22, VectorConst = 0x23,
    ByteConst = 0x24, IntZero = 0x25, IntOne = 0x26, True = 0x27, False = 0x28, TextConst = 0x29, NoObject = 0x2A,
    TransformConst = 0x2B, IntConstByte = 0x2C, NoInterface = 0x2D, DynamicCast = 0x2E, StructConst = 0x2F,
    EndStructConst = 0x30, SetArray = 0x31, EndArray = 0x32, PropertyConst = 0x33, UnicodeStringConst = 0x34,
    Int64Const = 0x35, UInt64Const = 0x36, DoubleConst = 0x37, Cast = 0x38, SetSet = 0x39, EndSet = 0x3A,
    SetMap = 0x3B, EndMap = 0x3C, SetConst = 0x3D, EndSetConst = 0x3E, MapConst = 0x3F, EndMapConst = 0x40,
    Vector3fConst = 0x41, StructMemberContext = 0x42, LetMulticastDelegate = 0x43, LetDelegate = 0x44,
    LocalVirtualFunction = 0x45, LocalFinalFunction = 0x46, LocalOutVariable = 0x48, InstanceDelegate = 0x4B,
    PushExecutionFlow = 0x4C, PopExecutionFlow = 0x4D, ComputedJump = 0x4E, PopExecutionFlowIfNot = 0x4F,
    Breakpoint = 0x50, InterfaceContext = 0x51, ObjToInterfaceCast = 0x52, EndOfScript = 0x53,
    CrossInterfaceCast = 0x54, InterfaceToObjCast = 0x55, WireTracepoint = 0x5A, SkipOffsetConst = 0x5B,
    AddMulticastDelegate = 0x5C, ClearMulticastDelegate = 0x5D, Tracepoint = 0x5E, LetObj = 0x5F,
    LetWeakObjPtr = 0x60, BindDelegate = 0x61, RemoveMulticastDelegate = 0x62, CallMulticastDelegate = 0x63,
    LetValueOnPersistentFrame = 0x64, ArrayConst = 0x65, EndArrayConst = 0x66, SoftObjectConst = 0x67,
    CallMath = 0x68, SwitchValue = 0x69, ArrayGetByRef = 0x6B, ClassSparseDataVariable = 0x6C, EndParmValue = 0x15,
    DeprecatedOp4A = 0x4A, InstrumentationEvent = 0x6A, FieldPathConst = 0x6D, AutoRtfmTransact = 0x70,
    AutoRtfmStopTransact = 0x71, AutoRtfmAbortIfNot = 0x72, AutoRtfmAbort = 0x73;
}
// EScriptInstrumentation::Type (EX_InstrumentationEvent's operand); an inline
// event carries a script name after it.
namespace instrumentation {
constexpr uint8_t InlineEvent = 4;
}
// ECastToken (EX_Cast's first operand).
namespace cast {
constexpr uint8_t ObjectToInterface = 0x00, ObjectToBool = 0x01, InterfaceToBool = 0x02, DoubleToFloat = 0x03,
    FloatToDouble = 0x04;
}
// EBlueprintTextLiteralType (EX_TextConst's first operand).
namespace text {
constexpr uint8_t Empty = 0, Localized = 1, Invariant = 2, LiteralString = 3, StringTableEntry = 4;
}

struct ScriptName { int32_t index = 0, display = 0, number = 0; };

// One expression. Which members hold what depends on `op` (Decode's cases).
struct Expr {
    uint32_t offset = 0;      // Of the opcode byte.
    uint8_t op = 0;
    uint8_t sub = 0;          // Cast token, text literal type, assert's debug flag.
    uint64_t pointer = 0;     // Property, function, class or object; Context: the r-value property.
    uint64_t pointer2 = 0;    // MapConst: the value property.
    ScriptName name{};
    int64_t integer = 0;      // Integer constants, counts, struct size, assert line.
    uint64_t uinteger = 0;    // UInt64Const.
    uint32_t target = 0;      // Jump, push, skip and switch offsets.
    double values[10] = {};   // Float and double constants, vectors, rotators, transforms.
    std::string text;         // String constants (UTF-8).
    std::vector<Expr> children;
};

struct Decoded {
    std::vector<Expr> statements;
    std::string stopped;      // Why the decode stopped early ("unknown 0xNN at +0xOFF"); empty at the end.
};

namespace detail {
class Reader {
public:
    Reader(std::span<const uint8_t> bytes, uint32_t at) : bytes_(bytes), at_(at) {}
    uint32_t At() const { return at_; }
    bool Failed() const { return !error_.empty(); }
    const std::string& Error() const { return error_; }
    bool Done() const { return at_ >= bytes_.size(); }
    uint8_t Peek() const { return at_ < bytes_.size() ? bytes_[at_] : 0; }

    template<class T> T Read() {
        T value{};
        if (Failed()) return value;
        if (bytes_.size() - at_ < sizeof(T) || at_ > bytes_.size()) { Fail(std::format("truncated at +0x{:x}", at_)); return value; }
        memcpy(&value, bytes_.data() + at_, sizeof(T));
        at_ += sizeof(T);
        return value;
    }
    ScriptName Name() { ScriptName n; n.index = Read<int32_t>(); n.display = Read<int32_t>(); n.number = Read<int32_t>(); return n; }
    // EX_StringConst: Latin-1 bytes, one code point each.
    std::string Ansi() {
        std::string out;
        while (!Failed()) {
            const uint32_t c = Read<uint8_t>();
            if (!c) break;
            Utf8(out, c);
        }
        return out;
    }
    // EX_UnicodeStringConst: UTF-16; a surrogate pair is one code point, an
    // unpaired surrogate becomes U+FFFD.
    std::string Utf16() {
        std::string out;
        while (!Failed()) {
            uint32_t c = Read<uint16_t>();
            if (!c) break;
            if (c >= 0xD800 && c <= 0xDBFF && at_ <= bytes_.size() && bytes_.size() - at_ >= 2) {
                uint16_t low = 0;
                memcpy(&low, bytes_.data() + at_, 2);
                if (low >= 0xDC00 && low <= 0xDFFF) { at_ += 2; c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00); }
            }
            if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
            Utf8(out, c);
        }
        return out;
    }
    static void Utf8(std::string& out, uint32_t c) {
        if (c < 0x80) { out += static_cast<char>(c); return; }
        if (c < 0x800) { out += static_cast<char>(0xC0 | c >> 6); out += static_cast<char>(0x80 | (c & 0x3F)); return; }
        if (c < 0x10000) out += static_cast<char>(0xE0 | c >> 12);
        else { out += static_cast<char>(0xF0 | c >> 18); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); }
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    void Fail(std::string why) { if (error_.empty()) error_ = std::move(why); }

    // One expression; false (with Error()) when it cannot be read.
    bool Expression(Expr& e, unsigned depth = 0) {
        if (Failed()) return false;
        if (depth > 256) { Fail(std::format("nested too deep at +0x{:x}", at_)); return false; }
        e.offset = at_;
        e.op = Read<uint8_t>();
        if (Failed()) return false;
        const auto child = [&]() {
            e.children.emplace_back();
            return Expression(e.children.back(), depth + 1);
        };
        const auto until = [&](uint8_t end) {
            while (!Failed()) {
                if (Done()) { Fail(std::format("truncated at +0x{:x}", at_)); return false; }
                if (Peek() == end) { ++at_; return true; }
                if (!child()) return false;
            }
            return false;
        };
        using namespace ex;
        switch (e.op) {
        case LocalVariable: case InstanceVariable: case DefaultVariable: case LocalOutVariable:
        case ClassSparseDataVariable: case PropertyConst: case ObjectConst:
            e.pointer = Read<uint64_t>(); break;
        case Return: case ComputedJump: case PopExecutionFlowIfNot: case InterfaceContext: case SoftObjectConst:
        case ClearMulticastDelegate: case FieldPathConst: case AutoRtfmAbortIfNot:
            child(); break;
        case Jump: case PushExecutionFlow: case SkipOffsetConst:
            e.target = Read<uint32_t>(); break;
        case JumpIfNot: case Skip:
            e.target = Read<uint32_t>(); child(); break;
        case Assert:
            e.integer = Read<uint16_t>(); e.sub = Read<uint8_t>(); child(); break;
        case Nothing: case EndOfScript: case Self: case IntZero: case IntOne: case True: case False: case NoObject:
        case NoInterface: case PopExecutionFlow: case Breakpoint: case WireTracepoint: case Tracepoint:
        case EndParmValue: case DeprecatedOp4A: case AutoRtfmAbort:
            break;
        case InstrumentationEvent:
            e.sub = Read<uint8_t>();
            if (e.sub == instrumentation::InlineEvent) e.name = Name();
            break;
        case AutoRtfmTransact: // Its body runs to an EX_AutoRtfmStopTransact, kept as the last child.
            e.integer = Read<int32_t>(); e.target = Read<uint32_t>();
            while (child() && e.children.back().op != AutoRtfmStopTransact) {}
            break;
        case AutoRtfmStopTransact:
            e.integer = Read<int32_t>(); e.sub = Read<uint8_t>(); break;
        case NothingInt32: case IntConst:
            e.integer = Read<int32_t>(); break;
        case Let:
            e.pointer = Read<uint64_t>(); child() && child(); break;
        case LetValueOnPersistentFrame: case StructMemberContext: case MetaCast: case DynamicCast:
        case ObjToInterfaceCast: case CrossInterfaceCast: case InterfaceToObjCast:
            e.pointer = Read<uint64_t>(); child(); break;
        case LetBool: case LetObj: case LetWeakObjPtr: case LetDelegate: case LetMulticastDelegate:
        case AddMulticastDelegate: case RemoveMulticastDelegate: case ArrayGetByRef:
            child() && child(); break;
        case ClassContext: case Context: case ContextFailSilent:
            if (!child()) break;
            e.target = Read<uint32_t>();
            e.pointer = Read<uint64_t>();
            child();
            break;
        case VirtualFunction: case LocalVirtualFunction:
            e.name = Name(); until(EndFunctionParms); break;
        case FinalFunction: case LocalFinalFunction: case CallMath: case CallMulticastDelegate:
            e.pointer = Read<uint64_t>(); until(EndFunctionParms); break;
        case FloatConst: e.values[0] = Read<float>(); break;
        case DoubleConst: e.values[0] = Read<double>(); break;
        case StringConst: e.text = Ansi(); break;
        case UnicodeStringConst: e.text = Utf16(); break;
        case NameConst: case InstanceDelegate: e.name = Name(); break;
        case RotationConst: case VectorConst: for (int i = 0; i < 3; ++i) e.values[i] = Read<double>(); break;
        case Vector3fConst: for (int i = 0; i < 3; ++i) e.values[i] = Read<float>(); break;
        case TransformConst: for (int i = 0; i < 10; ++i) e.values[i] = Read<double>(); break;
        case ByteConst: case IntConstByte: e.integer = Read<uint8_t>(); break;
        case BitFieldConst: e.pointer = Read<uint64_t>(); e.integer = Read<uint8_t>(); break; // A bool member's value.
        case Int64Const: e.integer = Read<int64_t>(); break;
        case UInt64Const: e.uinteger = Read<uint64_t>(); break;
        case TextConst:
            e.sub = Read<uint8_t>();
            switch (e.sub) {
            case text::Empty: break;
            case text::Localized: child() && child() && child(); break;
            case text::Invariant: case text::LiteralString: child(); break;
            case text::StringTableEntry: e.pointer = Read<uint64_t>(); child() && child(); break;
            default: Fail(std::format("unknown text literal 0x{:02x} at +0x{:x}", e.sub, e.offset)); break;
            }
            break;
        case Cast:
            e.sub = Read<uint8_t>();
            if (e.sub > cast::FloatToDouble) { Fail(std::format("unknown cast 0x{:02x} at +0x{:x}", e.sub, e.offset)); break; }
            child();
            break;
        case StructConst:
            e.pointer = Read<uint64_t>(); e.integer = Read<int32_t>(); until(EndStructConst); break;
        case SetArray:
            child() && until(EndArray); break;
        case SetSet: case SetMap:
            if (!child()) break;
            e.integer = Read<int32_t>();
            until(e.op == SetSet ? EndSet : EndMap);
            break;
        case ArrayConst: case SetConst:
            e.pointer = Read<uint64_t>(); e.integer = Read<int32_t>(); until(e.op == ArrayConst ? EndArrayConst : EndSetConst); break;
        case MapConst:
            e.pointer = Read<uint64_t>(); e.pointer2 = Read<uint64_t>(); e.integer = Read<int32_t>(); until(EndMapConst); break;
        case BindDelegate:
            e.name = Name(); child() && child(); break;
        case SwitchValue: {
            const auto cases = Read<uint16_t>();
            e.target = Read<uint32_t>();
            e.integer = cases;
            if (!child()) break;
            for (uint16_t i = 0; i < cases && !Failed(); ++i) {
                if (!child()) break;
                Read<uint32_t>(); // Offset of the next case.
                child();
            }
            child();
            break;
        }
        default:
            Fail(std::format("unknown 0x{:02x} at +0x{:x}", e.op, e.offset));
            return false;
        }
        return !Failed();
    }

private:
    std::span<const uint8_t> bytes_;
    uint32_t at_;
    std::string error_;
};

inline bool EndsEvent(uint8_t op) {
    return op == ex::Return || op == ex::PopExecutionFlow || op == ex::EndOfScript || op == ex::Jump ||
        op == ex::ComputedJump;
}
}

// Statements from `from` to EX_EndOfScript (or the end of the bytes). An
// event (an uber-graph entry) stops after its first return, pop, goto,
// computed goto or end, so it reads alone.
inline Decoded Decode(std::span<const uint8_t> bytes, uint32_t from = 0, bool event = false) {
    Decoded out;
    detail::Reader reader(bytes, from);
    if (from >= bytes.size()) { out.stopped = std::format("offset +0x{:x} is past the end (+0x{:x})", from, bytes.size()); return out; }
    while (!reader.Done()) {
        Expr e;
        if (!reader.Expression(e)) { out.stopped = reader.Error(); break; }
        out.statements.push_back(std::move(e));
        const uint8_t op = out.statements.back().op;
        if (op == ex::EndOfScript || (event && detail::EndsEvent(op))) break;
    }
    return out;
}

// What an expression tree refers to, for the caller to resolve.
struct Operands {
    std::set<uint64_t> properties, objects; // objects: functions, classes, structs and constants.
    std::set<int32_t> names;
};
inline void Collect(const Expr& e, Operands& out) {
    using namespace ex;
    switch (e.op) {
    case LocalVariable: case InstanceVariable: case DefaultVariable: case LocalOutVariable:
    case ClassSparseDataVariable: case PropertyConst: case LetValueOnPersistentFrame: case StructMemberContext:
    case ArrayConst: case SetConst: case BitFieldConst:
        out.properties.insert(e.pointer); break;
    case MapConst: out.properties.insert(e.pointer); out.properties.insert(e.pointer2); break;
    case Let: case Context: case ContextFailSilent: case ClassContext:
        if (e.pointer) out.properties.insert(e.pointer);
        break;
    case FinalFunction: case LocalFinalFunction: case CallMath: case CallMulticastDelegate: case ObjectConst:
    case MetaCast: case DynamicCast: case ObjToInterfaceCast: case CrossInterfaceCast: case InterfaceToObjCast:
    case StructConst:
        out.objects.insert(e.pointer); break;
    case TextConst: if (e.pointer) out.objects.insert(e.pointer); break;
    case NameConst: case VirtualFunction: case LocalVirtualFunction: case InstanceDelegate: case BindDelegate:
        out.names.insert(e.name.index); break;
    case InstrumentationEvent: if (e.sub == instrumentation::InlineEvent) out.names.insert(e.name.index); break;
    default: break;
    }
    for (const auto& child : e.children) Collect(child, out);
}

// Resolved operands. A property's `name` is its readable name where one is
// known (else its reflected one); objects carry their outer's name.
struct Property { std::string name, type; };
struct Object { std::string name, owner; };
struct Symbols {
    std::unordered_map<uint64_t, Property> properties;
    std::unordered_map<uint64_t, Object> objects;
    std::unordered_map<int32_t, std::string> names;
};

namespace detail {
inline std::string Real(double value, bool single) {
    std::string out = single ? std::format("{}", static_cast<float>(value)) : std::format("{}", value);
    if (out.find_first_of(".einf") == std::string::npos) out += ".0";
    return out;
}
inline std::string Quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out + "\"";
}

// KismetMathLibrary functions of two arguments shown as operators: the name's
// part before the first underscore (Multiply_DoubleDouble, BooleanAND).
inline std::string_view Operator(std::string_view function) {
    static constexpr std::pair<std::string_view, std::string_view> Table[] = {
        {"Add", "+"}, {"Subtract", "-"}, {"Multiply", "*"}, {"Divide", "/"}, {"Percent", "%"},
        {"Less", "<"}, {"Greater", ">"}, {"LessEqual", "<="}, {"GreaterEqual", ">="},
        {"EqualEqual", "=="}, {"NotEqual", "!="}, {"BooleanAND", "&&"}, {"BooleanOR", "||"}, {"BooleanXOR", "^"},
        {"And", "&"}, {"Or", "|"}, {"Xor", "^"}};
    const auto stem = function.substr(0, function.find('_'));
    for (const auto& [name, op] : Table) if (stem == name) return op;
    return {};
}

class Printer {
public:
    explicit Printer(const Symbols& symbols) : symbols_(symbols) {}

    std::string Property(uint64_t pointer) const {
        if (const auto found = symbols_.properties.find(pointer); found != symbols_.properties.end()) return found->second.name;
        return std::format("prop@0x{:x}", pointer);
    }
    std::string ObjectName(uint64_t pointer) const {
        if (!pointer) return "None";
        if (const auto found = symbols_.objects.find(pointer); found != symbols_.objects.end()) return found->second.name;
        return std::format("obj@0x{:x}", pointer);
    }
    std::string Function(uint64_t pointer, bool owner) const {
        const auto found = symbols_.objects.find(pointer);
        if (found == symbols_.objects.end()) return std::format("fn@0x{:x}", pointer);
        if (!owner || found->second.owner.empty()) return found->second.name;
        return found->second.owner + "." + found->second.name;
    }
    std::string Name(const ScriptName& name) const {
        const auto found = symbols_.names.find(name.index);
        std::string text = found != symbols_.names.end() && !found->second.empty() ? found->second : "#" + std::to_string(name.index);
        if (name.number) text += "_" + std::to_string(name.number - 1);
        return text;
    }

    std::string Arguments(std::span<const Expr> args) const {
        std::string out;
        for (const auto& arg : args) {
            if (!out.empty()) out += ", ";
            out += Text(arg);
        }
        return out;
    }
    std::string List(std::span<const Expr> items, std::string_view open, std::string_view close) const {
        return std::string(open) + Arguments(items) + std::string(close);
    }
    // An operand of an operator: parenthesised when it is an operator itself.
    std::string Operand(const Expr& e) const {
        const std::string text = Text(e);
        return IsOperator(e) ? "(" + text + ")" : text;
    }
    bool IsOperator(const Expr& e) const {
        if (e.op != ex::CallMath || e.children.size() != 2) return false;
        const auto found = symbols_.objects.find(e.pointer);
        return found != symbols_.objects.end() && found->second.owner == "KismetMathLibrary" &&
            !Operator(found->second.name).empty();
    }
    std::string Condition(const Expr& e) const {
        const std::string text = Text(e);
        return text.find(' ') == std::string::npos ? "!" + text : "!(" + text + ")";
    }
    static std::string Target(uint32_t offset) { return std::format("+0x{:x}", offset); }

    // An expression in a context: calls name the function without its owner.
    std::string Member(const Expr& e) const {
        using namespace ex;
        switch (e.op) {
        case FinalFunction: case LocalFinalFunction: case CallMath:
            return Function(e.pointer, false) + List(e.children, "(", ")");
        default: return Text(e);
        }
    }

    std::string Text(const Expr& e) const {
        using namespace ex;
        const auto& c = e.children;
        const auto at = [&](size_t i) { return i < c.size() ? Text(c[i]) : std::string("?"); };
        switch (e.op) {
        case LocalVariable: case InstanceVariable: case LocalOutVariable: case ClassSparseDataVariable:
            return Property(e.pointer);
        case DefaultVariable: return "default." + Property(e.pointer);
        case PropertyConst: return "property " + Property(e.pointer);
        case Return:
            return c.empty() || c[0].op == Nothing ? "return" : "return " + at(0);
        case Jump: return "goto " + Target(e.target);
        case JumpIfNot: return "if " + (c.empty() ? "?" : Condition(c[0])) + " goto " + Target(e.target);
        case Skip: return "skip " + Target(e.target) + " " + at(0);
        case Assert: return "assert(" + at(0) + ")";
        case Nothing: return "nop";
        case NothingInt32: return std::format("nop {}", e.integer);
        case Let: case LetBool: case LetObj: case LetWeakObjPtr: case LetDelegate: case LetMulticastDelegate:
            return at(0) + " = " + at(1);
        case LetValueOnPersistentFrame: return Property(e.pointer) + " = " + at(0);
        case AddMulticastDelegate: return at(0) + " += " + at(1);
        case RemoveMulticastDelegate: return at(0) + " -= " + at(1);
        case ClearMulticastDelegate: return at(0) + ".Clear()";
        case ClassContext: case Context: case ContextFailSilent:
            return (c.empty() ? std::string("?") : Operand(c[0])) + "." + (c.size() > 1 ? Member(c[1]) : "?");
        case InterfaceContext: return at(0);
        case StructMemberContext: return (c.empty() ? std::string("?") : Operand(c[0])) + "." + Property(e.pointer);
        case VirtualFunction: case LocalVirtualFunction: return Name(e.name) + List(c, "(", ")");
        case FinalFunction: case LocalFinalFunction: return Function(e.pointer, true) + List(c, "(", ")");
        case CallMath: {
            if (IsOperator(e)) return Operand(c[0]) + " " + std::string(Operator(symbols_.objects.at(e.pointer).name)) + " " + Operand(c[1]);
            if (c.size() == 1) {
                const auto found = symbols_.objects.find(e.pointer);
                if (found != symbols_.objects.end() && found->second.owner == "KismetMathLibrary" && found->second.name == "Not_PreBool")
                    return Condition(c[0]);
            }
            return Function(e.pointer, true) + List(c, "(", ")");
        }
        case CallMulticastDelegate:
            return (c.empty() ? std::string("?") : Operand(c[0])) + ".Broadcast" + List(std::span(c).subspan(c.empty() ? 0 : 1), "(", ")");
        case Self: return "self";
        case IntZero: return "0";
        case IntOne: return "1";
        case True: return "true";
        case BitFieldConst: return e.integer ? "true" : "false";
        case False: return "false";
        case NoObject: case NoInterface: return "None";
        case IntConst: case ByteConst: case IntConstByte: case Int64Const: return std::to_string(e.integer);
        case UInt64Const: return std::to_string(e.uinteger);
        case FloatConst: return Real(e.values[0], true);
        case DoubleConst: return Real(e.values[0], false);
        case StringConst: case UnicodeStringConst: return Quoted(e.text);
        case NameConst: return "'" + Name(e.name) + "'";
        case ObjectConst: return ObjectName(e.pointer);
        case SoftObjectConst: return "soft " + at(0);
        case SkipOffsetConst: return Target(e.target);
        case VectorConst: case Vector3fConst:
            return std::format("Vector({}, {}, {})", Real(e.values[0], e.op == Vector3fConst), Real(e.values[1], e.op == Vector3fConst),
                               Real(e.values[2], e.op == Vector3fConst));
        case RotationConst:
            return std::format("Rotator({}, {}, {})", Real(e.values[0], false), Real(e.values[1], false), Real(e.values[2], false));
        case TransformConst: {
            std::string out = "Transform(";
            for (int i = 0; i < 10; ++i) out += (i ? ", " : "") + Real(e.values[i], false);
            return out + ")";
        }
        case TextConst:
            switch (e.sub) {
            case text::Empty: return "Text()";
            case text::StringTableEntry: return "Text(" + ObjectName(e.pointer) + ", " + Arguments(c) + ")";
            default: return "Text(" + Arguments(c) + ")";
            }
        case Cast:
            switch (e.sub) {
            case cast::DoubleToFloat: return "float(" + at(0) + ")";
            case cast::FloatToDouble: return "double(" + at(0) + ")";
            case cast::ObjectToBool: case cast::InterfaceToBool: return "bool(" + at(0) + ")";
            default: return "interface(" + at(0) + ")";
            }
        case MetaCast: return "CastClass<" + ObjectName(e.pointer) + ">(" + at(0) + ")";
        case DynamicCast: case ObjToInterfaceCast: case CrossInterfaceCast: case InterfaceToObjCast:
            return "Cast<" + ObjectName(e.pointer) + ">(" + at(0) + ")";
        case StructConst: return ObjectName(e.pointer) + List(c, "{", "}");
        case SetArray: return at(0) + " = " + List(std::span(c).subspan(c.empty() ? 0 : 1), "[", "]");
        case SetSet: return at(0) + " = " + List(std::span(c).subspan(c.empty() ? 0 : 1), "{", "}");
        case SetMap: {
            std::string out = at(0) + " = {";
            for (size_t i = 1; i + 1 < c.size(); i += 2) out += (i > 1 ? ", " : "") + Text(c[i]) + ": " + Text(c[i + 1]);
            return out + "}";
        }
        case ArrayConst: return List(c, "[", "]");
        case SetConst: return List(c, "{", "}");
        case MapConst: {
            std::string out = "{";
            for (size_t i = 0; i + 1 < c.size(); i += 2) out += (i ? ", " : "") + Text(c[i]) + ": " + Text(c[i + 1]);
            return out + "}";
        }
        case ArrayGetByRef: return Operand(c.empty() ? Expr{} : c[0]) + "[" + at(1) + "]";
        case SwitchValue: {
            // Children: the index, a value and result per case, the default.
            std::string out = "switch(" + at(0) + ") {";
            size_t i = 1;
            for (int64_t n = 0; n < e.integer && i + 1 < c.size(); ++n, i += 2) out += Text(c[i]) + ": " + Text(c[i + 1]) + "; ";
            return out + "default: " + at(i) + "}";
        }
        case InstanceDelegate: return "delegate '" + Name(e.name) + "'";
        case BindDelegate: return at(0) + " = Bind(" + at(1) + ", '" + Name(e.name) + "')";
        case PushExecutionFlow: return "push " + Target(e.target);
        case PopExecutionFlow: return "pop";
        case PopExecutionFlowIfNot: return "if " + (c.empty() ? "?" : Condition(c[0])) + " pop";
        case ComputedJump: return "goto " + at(0);
        case EndOfScript: return "end";
        case Breakpoint: return "breakpoint";
        case Tracepoint: return "tracepoint";
        case WireTracepoint: return "wire tracepoint";
        case EndParmValue: return "end parm value";
        case DeprecatedOp4A: return "nop 4a";
        case FieldPathConst: return "field " + at(0);
        case InstrumentationEvent:
            return e.sub == instrumentation::InlineEvent ? "instrument inline '" + Name(e.name) + "'" : std::format("instrument {}", e.sub);
        case AutoRtfmTransact: {
            const size_t body = c.empty() || c.back().op != AutoRtfmStopTransact ? c.size() : c.size() - 1;
            return std::format("transact {} {{", e.integer) + Arguments(std::span(c).first(body)) + "}";
        }
        case AutoRtfmStopTransact: return std::format("stop transact {} mode {}", e.integer, e.sub);
        case AutoRtfmAbortIfNot: return "if " + (c.empty() ? "?" : Condition(c[0])) + " abort transact";
        case AutoRtfmAbort: return "abort transact";
        default: return std::format("op 0x{:02x}", e.op);
        }
    }

private:
    const Symbols& symbols_;
};
}

// A statement as text, e.g. "Speed = Vehicle.GetSpeedKPH()" or "if !(A > B) goto +0x58".
inline std::string Render(const Expr& statement, const Symbols& symbols) {
    return detail::Printer(symbols).Text(statement);
}

// The opcode's Script.h name without its EX_ prefix ("" for one not decoded).
inline std::string_view OpName(uint8_t op) {
    using namespace ex;
    switch (op) {
    case LocalVariable: return "LocalVariable"; case InstanceVariable: return "InstanceVariable";
    case DefaultVariable: return "DefaultVariable"; case Return: return "Return"; case Jump: return "Jump";
    case JumpIfNot: return "JumpIfNot"; case Assert: return "Assert"; case Nothing: return "Nothing";
    case NothingInt32: return "NothingInt32"; case Let: return "Let"; case BitFieldConst: return "BitFieldConst"; case ClassContext: return "ClassContext";
    case MetaCast: return "MetaCast"; case LetBool: return "LetBool"; case Self: return "Self"; case Skip: return "Skip";
    case Context: return "Context"; case ContextFailSilent: return "Context_FailSilent";
    case VirtualFunction: return "VirtualFunction"; case FinalFunction: return "FinalFunction";
    case IntConst: return "IntConst"; case FloatConst: return "FloatConst"; case StringConst: return "StringConst";
    case ObjectConst: return "ObjectConst"; case NameConst: return "NameConst"; case RotationConst: return "RotationConst";
    case VectorConst: return "VectorConst"; case ByteConst: return "ByteConst"; case IntZero: return "IntZero";
    case IntOne: return "IntOne"; case True: return "True"; case False: return "False"; case TextConst: return "TextConst";
    case NoObject: return "NoObject"; case TransformConst: return "TransformConst"; case IntConstByte: return "IntConstByte";
    case NoInterface: return "NoInterface"; case DynamicCast: return "DynamicCast"; case StructConst: return "StructConst";
    case SetArray: return "SetArray"; case PropertyConst: return "PropertyConst";
    case UnicodeStringConst: return "UnicodeStringConst"; case Int64Const: return "Int64Const";
    case UInt64Const: return "UInt64Const"; case DoubleConst: return "DoubleConst"; case Cast: return "Cast";
    case SetSet: return "SetSet"; case SetMap: return "SetMap"; case SetConst: return "SetConst"; case MapConst: return "MapConst";
    case Vector3fConst: return "Vector3fConst"; case StructMemberContext: return "StructMemberContext";
    case LetMulticastDelegate: return "LetMulticastDelegate"; case LetDelegate: return "LetDelegate";
    case LocalVirtualFunction: return "LocalVirtualFunction"; case LocalFinalFunction: return "LocalFinalFunction";
    case LocalOutVariable: return "LocalOutVariable"; case InstanceDelegate: return "InstanceDelegate";
    case PushExecutionFlow: return "PushExecutionFlow"; case PopExecutionFlow: return "PopExecutionFlow";
    case ComputedJump: return "ComputedJump"; case PopExecutionFlowIfNot: return "PopExecutionFlowIfNot";
    case Breakpoint: return "Breakpoint"; case InterfaceContext: return "InterfaceContext";
    case ObjToInterfaceCast: return "ObjToInterfaceCast"; case EndOfScript: return "EndOfScript";
    case CrossInterfaceCast: return "CrossInterfaceCast"; case InterfaceToObjCast: return "InterfaceToObjCast";
    case WireTracepoint: return "WireTracepoint"; case SkipOffsetConst: return "SkipOffsetConst";
    case AddMulticastDelegate: return "AddMulticastDelegate"; case ClearMulticastDelegate: return "ClearMulticastDelegate";
    case Tracepoint: return "Tracepoint"; case LetObj: return "LetObj"; case LetWeakObjPtr: return "LetWeakObjPtr";
    case BindDelegate: return "BindDelegate"; case RemoveMulticastDelegate: return "RemoveMulticastDelegate";
    case CallMulticastDelegate: return "CallMulticastDelegate"; case LetValueOnPersistentFrame: return "LetValueOnPersistentFrame";
    case ArrayConst: return "ArrayConst"; case SoftObjectConst: return "SoftObjectConst"; case CallMath: return "CallMath";
    case SwitchValue: return "SwitchValue"; case ArrayGetByRef: return "ArrayGetByRef";
    case ClassSparseDataVariable: return "ClassSparseDataVariable"; case EndParmValue: return "EndParmValue";
    case DeprecatedOp4A: return "DeprecatedOp4A"; case InstrumentationEvent: return "InstrumentationEvent";
    case FieldPathConst: return "FieldPathConst"; case AutoRtfmTransact: return "AutoRtfmTransact";
    case AutoRtfmStopTransact: return "AutoRtfmStopTransact"; case AutoRtfmAbortIfNot: return "AutoRtfmAbortIfNot";
    case AutoRtfmAbort: return "AutoRtfmAbort";
    default: return {};
    }
}

// Uber-graph entries in an event stub: calls of `ubergraph` whose first
// argument is an integer constant (the entry offset).
inline void Entries(const Expr& e, uint64_t ubergraph, std::vector<uint32_t>& out) {
    const bool call = e.op == ex::FinalFunction || e.op == ex::LocalFinalFunction;
    if (call && e.pointer == ubergraph && !e.children.empty() && e.children[0].op == ex::IntConst && e.children[0].integer >= 0)
        out.push_back(static_cast<uint32_t>(e.children[0].integer));
    for (const auto& child : e.children) Entries(child, ubergraph, out);
}
}
