; reagentlive core agent: the native call recorder's thunks, one per recorder
; slot (core/agent_native_calls.cpp). Each saves every register a call may
; pass or rely on (rcx, rdx, r8, r9, rax, r10, r11, xmm0-5), hands them to
; the C++ recorder, restores them and jumps to the slot's original pointer,
; so the original runs on the caller's stack exactly as called. Only
; RIP-relative addressing: no base relocation, no import.
;
; Frame (rsp after the prolog, 16-byte aligned):
;   [rsp +   0]  home space for the recorder's call
;   [rsp +  32]  rcx, rdx, r8, r9, rax, r10, r11 (NativeFrame starts here)
;   [rsp +  96]  xmm0..xmm5
;   [rsp + 200]  the caller's return address
; SLOT_SIZE and SLOT_ORIGINAL follow recorder::Slot (static_asserts in
; agent_native_calls.cpp); THUNKS follows MaxRecorderSlots.

SLOT_SIZE       EQU 168
SLOT_ORIGINAL   EQU 24
FRAME_SIZE      EQU 200

EXTERN reagentlive_recorder_slots:BYTE
EXTERN reagentlive_native_record:PROC

.code

NATIVE_THUNK MACRO index
reagentlive_native_thunk_&index& PROC FRAME
    sub rsp, FRAME_SIZE
    .allocstack FRAME_SIZE
    .endprolog
    mov [rsp + 32], rcx
    mov [rsp + 40], rdx
    mov [rsp + 48], r8
    mov [rsp + 56], r9
    mov [rsp + 64], rax
    mov [rsp + 72], r10
    mov [rsp + 80], r11
    movdqa xmmword ptr [rsp + 96], xmm0
    movdqa xmmword ptr [rsp + 112], xmm1
    movdqa xmmword ptr [rsp + 128], xmm2
    movdqa xmmword ptr [rsp + 144], xmm3
    movdqa xmmword ptr [rsp + 160], xmm4
    movdqa xmmword ptr [rsp + 176], xmm5
    mov ecx, index
    lea rdx, [rsp + 32]
    call reagentlive_native_record
    movdqa xmm0, xmmword ptr [rsp + 96]
    movdqa xmm1, xmmword ptr [rsp + 112]
    movdqa xmm2, xmmword ptr [rsp + 128]
    movdqa xmm3, xmmword ptr [rsp + 144]
    movdqa xmm4, xmmword ptr [rsp + 160]
    movdqa xmm5, xmmword ptr [rsp + 176]
    mov rcx, [rsp + 32]
    mov rdx, [rsp + 40]
    mov r8, [rsp + 48]
    mov r9, [rsp + 56]
    mov rax, [rsp + 64]
    mov r10, [rsp + 72]
    mov r11, [rsp + 80]
    add rsp, FRAME_SIZE
    jmp qword ptr [reagentlive_recorder_slots + index * SLOT_SIZE + SLOT_ORIGINAL]
reagentlive_native_thunk_&index& ENDP
ENDM

NATIVE_THUNK 0
NATIVE_THUNK 1
NATIVE_THUNK 2
NATIVE_THUNK 3
NATIVE_THUNK 4
NATIVE_THUNK 5
NATIVE_THUNK 6
NATIVE_THUNK 7

END
