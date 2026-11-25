// reagentlive core agent: the native call recorder (protocol.h,
// NativeCallsStart) on the core's recorder slots (core/recorder.h). One code
// pointer, a vtable entry in the object's private copy (core/vtable_copy.h)
// or a writable cell, is swapped for slot I's generic thunk
// (core/native_thunks.asm), which saves the argument registers, calls
// reagentlive_native_record and jumps to the original. The recorder calls
// nothing: no allocation, lock, log or system call; the thread id comes from
// the TEB and the time from the time-stamp counter. Its one guarded copy
// faults safely under SEH.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstring>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/recorder.h"
#include "reagentlive/core/vtable_copy.h"

// The thunks (native_thunks.asm), one per slot.
extern "C" {
void reagentlive_native_thunk_0();
void reagentlive_native_thunk_1();
void reagentlive_native_thunk_2();
void reagentlive_native_thunk_3();
void reagentlive_native_thunk_4();
void reagentlive_native_thunk_5();
void reagentlive_native_thunk_6();
void reagentlive_native_thunk_7();
}

namespace reagentlive::native {
// What the thunk saved, as it lays it out at its rsp + 32.
struct Frame {
    uint64_t rcx, rdx, r8, r9, rax, r10, r11, pad;
    alignas(16) uint64_t xmm[6][2];
    uint64_t pad2, returnAddress;
};
namespace {
static_assert(offsetof(Frame, xmm) == 64 && offsetof(Frame, returnAddress) == 168, "native_thunks.asm's frame");
static_assert(offsetof(recorder::Slot, original) == 24 && sizeof(recorder::Slot) == 168,
    "native_thunks.asm's SLOT_ORIGINAL and SLOT_SIZE");
static_assert(MaxRecorderSlots == 8, "native_thunks.asm assembles one thunk per slot");

using ThunkFn = void (*)();
constexpr ThunkFn Thunks[MaxRecorderSlots] = {&reagentlive_native_thunk_0, &reagentlive_native_thunk_1,
    &reagentlive_native_thunk_2, &reagentlive_native_thunk_3, &reagentlive_native_thunk_4, &reagentlive_native_thunk_5,
    &reagentlive_native_thunk_6, &reagentlive_native_thunk_7};
// The kind's setup words in a slot.
constexpr unsigned SetupRegister = 0, SetupBytes = 1;

// A vtable recording's entry, given back when its cell is restored.
vtable::Entry g_entries[MaxRecorderSlots];
void GiveEntry(uint32_t index) { vtable::Give(g_entries[index]); }

uint64_t RegisterValue(const Frame& frame, uint64_t which) {
    switch (static_cast<NativeRegister>(which)) {
    case NativeRegister::Rcx: return frame.rcx;
    case NativeRegister::Rdx: return frame.rdx;
    case NativeRegister::R8: return frame.r8;
    case NativeRegister::R9: return frame.r9;
    case NativeRegister::None: break;
    }
    return 0;
}

bool ExecutablePage(DWORD protect) {
    return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// A cell inside a mapped image: refused unless its section is writable and
// not code, and it is not in the import address table. Empty: allowed.
const char* ImageCellRefusal(uint64_t cell, const void* base) {
    const auto* image = static_cast<const uint8_t*>(base);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!GuardedCopy(&dos, image, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !GuardedCopy(&nt, image + dos.e_lfanew, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE)
        return "the cell is in an image whose headers cannot be read";
    const uint64_t rva = cell - reinterpret_cast<uint64_t>(image);
    const auto& iat = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    if (iat.Size && rva >= iat.VirtualAddress && rva < uint64_t{iat.VirtualAddress} + iat.Size)
        return "the cell is in an import address table";
    const uint8_t* sections = image + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!GuardedCopy(&section, sections + i * sizeof(section), sizeof(section))) break;
        const uint64_t size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        if (rva < section.VirtualAddress || rva >= section.VirtualAddress + size) continue;
        if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) return "the cell is in a code section";
        if (!(section.Characteristics & IMAGE_SCN_MEM_WRITE)) return "the cell is in a read-only section";
        return nullptr;
    }
    return "the cell is in no section of its image";
}

// Why a cell cannot be swapped, or null.
const char* CellRefusal(uint64_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return "the cell is not committed memory";
    if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return "the cell is on a guard or no-access page";
    if (ExecutablePage(info.Protect)) return "the cell is on an executable page";
    if (!(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY))) return "the cell is on a read-only page (never made writable)";
    if (vtable::InCopy(address)) return "the cell is in a vtable copy: record the entry with the vtable target";
    const auto slots = reinterpret_cast<uint64_t>(recorder::g_slots);
    if (address >= slots && address < slots + sizeof(recorder::Slot) * MaxRecorderSlots) return "the cell is the agent's own";
    if (info.Type == MEM_IMAGE) return ImageCellRefusal(address, info.AllocationBase);
    return nullptr;
}

const char* TakenText(vtable::Taken taken) {
    switch (taken) {
    case vtable::Taken::Unreadable: return "the object's vptr cannot be read or written";
    case vtable::Taken::Foreign: return "the vptr points outside a loaded module (another owner's copy)";
    case vtable::Taken::NotCode: return "the entry is past the table's code run or not in its module's code";
    case vtable::Taken::TooLong: return "the table's code run reaches the copy's cap";
    case vtable::Taken::Busy: return "the entry is taken already (recorded, or another user of the copy)";
    case vtable::Taken::Changed: return "the vptr changed while installing";
    case vtable::Taken::Full: return "every vtable copy is in use or in its grace";
    case vtable::Taken::NoMemory: return "no memory for the vtable copy";
    case vtable::Taken::Ok: break;
    }
    return "refused";
}

Progress Refused(Response& response, const char* why, uint64_t detail = 0) {
    Refuse(response, Status::Refused, why, detail);
    return Progress::Done;
}
}
}

// Called by every thunk with its slot and the saved registers: records the
// call when the slot is recording this kind. Calls nothing.
extern "C" void reagentlive_native_record(uint32_t index, const reagentlive::native::Frame* frame) {
    using namespace reagentlive;
    recorder::Slot& slot = recorder::g_slots[index];
    if (recorder::Enter(slot, NativeCallsKind, frame->rcx)) {
        const recorder::Pending row = recorder::Claim(slot, frame->rcx, static_cast<int64_t>(__rdtsc()));
        auto* out = reinterpret_cast<volatile uint64_t*>(recorder::Payload(row)); // Stores, never a library copy.
        out[0] = frame->rcx;
        out[1] = frame->rdx;
        out[2] = frame->r8;
        out[3] = frame->r9;
        for (unsigned i = 0; i < 4; ++i) out[4 + i] = frame->xmm[i][0];
        out[8] = frame->returnAddress;
        uint32_t size = sizeof(NativeCall);
        uint8_t tag = 0;
        if (const auto bytes = static_cast<uint32_t>(slot.setup[native::SetupBytes])) {
            const uint64_t source = native::RegisterValue(*frame, slot.setup[native::SetupRegister]);
            auto* copy = recorder::Payload(row) + sizeof(NativeCall);
            bool copied = false;
            __try {
                __movsb(copy, reinterpret_cast<const unsigned char*>(source), bytes);
                copied = true;
            } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                copied = false;
            }
            if (copied) {
                size += bytes;
                tag = NativeCopied;
            }
            recorder::Count(slot, copied ? NativeCopiedCount : NativeFaultedCount);
        }
        recorder::Publish(row, size, tag);
    }
    // The thunk reads slot.original after this returns: the reuse grace covers it.
    recorder::Leave(slot);
}

namespace reagentlive {
Progress NativeCallsStart(const Request& request, Response& response, bool, const Host&) {
    using namespace native;
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    NativeCallsSpec spec{};
    memcpy(&spec, request.data, sizeof(spec));
    const bool dry = (spec.flags & NativeCallsDry) != 0;
    const bool viaVtable = spec.target == static_cast<uint32_t>(NativeTarget::Vtable);
    recorder::Sweep();
    NativeCallsStarted started{};
    uint32_t owner = 0;
    void* volatile* cell = nullptr;
    void* original = nullptr;
    if (viaVtable) {
        // Checked first without writing: dry stops here, and a recorded or
        // non-code entry is refused before any copy is made.
        void** table = nullptr;
        void* entry = nullptr;
        const auto* vptr = reinterpret_cast<const uint8_t*>(spec.address) + spec.offset;
        if (!GuardedCopy(&table, vptr, sizeof(table)) || !table || !GuardedCopy(&entry, table + spec.index, sizeof(entry)))
            return Refused(response, "the object's vptr or the entry cannot be read");
        if (recorder::IsThunk(entry, owner)) return Refused(response, "already recorded by slot", owner);
        if (!vtable::InCode(entry)) return Refused(response, "the entry is not code in a loaded module", reinterpret_cast<uint64_t>(entry));
        started = {reinterpret_cast<uint64_t>(table + spec.index), reinterpret_cast<uint64_t>(entry), 0,
            reinterpret_cast<uint64_t>(table), MaxRecorderSlots, 0};
    } else {
        if (const char* why = CellRefusal(spec.address)) return Refused(response, why);
        if (!GuardedCopy(&original, reinterpret_cast<const void*>(spec.address), sizeof(original)))
            return Refused(response, "the cell cannot be read");
        if (recorder::IsThunk(original, owner)) return Refused(response, "already recorded by slot", owner);
        if (!vtable::InCode(original))
            return Refused(response, "the cell's value is not code in a loaded module", reinterpret_cast<uint64_t>(original));
        cell = reinterpret_cast<void* volatile*>(spec.address);
        started = {spec.address, reinterpret_cast<uint64_t>(original), 0, 0, MaxRecorderSlots, 0};
    }
    const uint32_t index = recorder::Pick();
    if (index == MaxRecorderSlots) return Refused(response, "every recording slot is busy or just stopped; stop one or retry in a second");
    started.slot = index;
    started.thunk = reinterpret_cast<uint64_t>(Thunks[index]);
    if (!dry) {
        vtable::Entry entry{};
        if (viaVtable) {
            const auto taken = vtable::Take(reinterpret_cast<void*>(spec.address), spec.offset, spec.index, entry);
            if (taken != vtable::Taken::Ok) return Refused(response, TakenText(taken));
            cell = entry.cell;
            original = entry.original;
            started.cell = reinterpret_cast<uint64_t>(entry.cell);
            started.original = reinterpret_cast<uint64_t>(entry.original);
            started.count = entry.count;
            g_entries[index] = entry;
        }
        recorder::Slot& slot = recorder::g_slots[index];
        slot.setup[SetupRegister] = spec.copyRegister;
        slot.setup[SetupBytes] = spec.copyBytes;
        const recorder::Arm arm{NativeCallsKind, static_cast<uint32_t>(sizeof(NativeCall)) + spec.copyBytes, cell, original,
            reinterpret_cast<void*>(Thunks[index]), spec.address, spec.filter, spec.milliseconds, recorder::RecorderClock::Tsc,
            viaVtable ? &GiveEntry : nullptr};
        switch (recorder::Start(index, arm)) {
        case recorder::Started::Armed: break;
        case recorder::Started::NoMemory:
            if (viaVtable) vtable::Give(entry);
            return Refused(response, "no memory for the recording");
        case recorder::Started::Changed:
            if (viaVtable) vtable::Give(entry);
            return Refused(response, "the pointer changed while starting");
        }
    }
    memcpy(response.data, &started, sizeof(started));
    response.length = sizeof(started);
    return Progress::Done;
}
}
