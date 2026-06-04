#include "pch.h"
#include "apihook.h"

#pragma pack(push, 1)
struct CallJmpInstr
{
	BYTE opcode;
	DWORD rel32;
};
#pragma pack(pop)

// Maximum bytes we'll copy from a function prologue
const BYTE MAX_HOOK_SIZE = 32;

/*
* Minimal x86 (32-bit) instruction length decoder.
* Computes the length of the instruction at 'code'.
* Handles all common MSVC-generated prologue instructions.
*/
static BYTE GetInstructionLength(BYTE* code)
{
	BYTE opcode = code[0];
	BYTE prefixLen = 0;

	// Handle prefix bytes (segment override, operand size, etc.)
	if (opcode == 0x64 || opcode == 0x65 || opcode == 0x66 ||
		opcode == 0x67 || opcode == 0xF0 || opcode == 0xF2 || opcode == 0xF3) {
		prefixLen = 1;
		opcode = code[1];
	}

	BYTE len = prefixLen + 1; // prefix + opcode

	// push/pop register: 50-5F
	if (opcode >= 0x50 && opcode <= 0x5F) return len;
	// nop
	if (opcode == 0x90) return len;
	// ret
	if (opcode == 0xC3) return len;
	// ret imm16
	if (opcode == 0xC2) return len + 2;
	// int3
	if (opcode == 0xCC) return len;
	// push imm8
	if (opcode == 0x6A) return len + 1;
	// push imm32
	if (opcode == 0x68) return len + 4;
	// jmp rel32 / call rel32
	if (opcode == 0xE9 || opcode == 0xE8) return len + 4;
	// jmp rel8 / short jcc
	if (opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F)) return len + 1;
	// mov reg, imm32: B8-BF
	if (opcode >= 0xB8 && opcode <= 0xBF) return len + 4;
	// mov reg, imm8: B0-B7
	if (opcode >= 0xB0 && opcode <= 0xB7) return len + 1;
	// xchg eax,reg: 91-97
	if (opcode >= 0x91 && opcode <= 0x97) return len;

	// Opcodes with ModR/M byte
	bool hasModRM =
		(opcode >= 0x00 && opcode <= 0x03) ||
		(opcode >= 0x08 && opcode <= 0x0B) ||
		(opcode >= 0x10 && opcode <= 0x13) ||
		(opcode >= 0x18 && opcode <= 0x1B) ||
		(opcode >= 0x20 && opcode <= 0x23) ||
		(opcode >= 0x28 && opcode <= 0x2B) ||
		(opcode >= 0x30 && opcode <= 0x33) ||
		(opcode >= 0x38 && opcode <= 0x3B) ||
		(opcode >= 0x80 && opcode <= 0x8F) ||
		(opcode >= 0xC0 && opcode <= 0xC1) ||
		(opcode >= 0xC6 && opcode <= 0xC7) ||
		(opcode >= 0xD0 && opcode <= 0xD3) ||
		(opcode >= 0xF6 && opcode <= 0xF7) ||
		(opcode >= 0xFE && opcode <= 0xFF) ||
		(opcode == 0x8D) || // lea
		(opcode == 0x84 || opcode == 0x85); // test

	if (hasModRM) {
		BYTE modrm = code[len];
		BYTE mod = (modrm >> 6) & 3;
		BYTE rm = modrm & 7;
		len++; // ModR/M byte

		if (mod == 0) {
			if (rm == 5) len += 4; // disp32
			else if (rm == 4) len++; // SIB
		} else if (mod == 1) {
			len += 1; // disp8
			if (rm == 4) len++; // SIB
		} else if (mod == 2) {
			len += 4; // disp32
			if (rm == 4) len++; // SIB
		}
		// mod==3: register-register, no displacement, no SIB

		// Immediate data
		if (opcode == 0x80 || opcode == 0x83 || opcode == 0xC0 || opcode == 0xC1) {
			len += 1; // imm8
		} else if (opcode == 0x81) {
			len += 4; // imm32
		} else if (opcode == 0x82) {
			len += 1; // imm8
		} else if (opcode == 0xC6) {
			len += 1; // mov r/m8, imm8
		} else if (opcode == 0xC7) {
			len += 4; // mov r/m32, imm32
		} else if (opcode == 0xF6 && (modrm & 0x38) == 0) {
			len += 1; // test r/m8, imm8
		} else if (opcode == 0xF7 && (modrm & 0x38) == 0) {
			len += 4; // test r/m32, imm32
		}
		return len;
	}

	// 2-byte opcodes (0F xx)
	if (opcode == 0x0F) {
		BYTE opcode2 = code[len];
		len++; // second opcode byte
		BYTE modrm = code[len];
		BYTE mod = (modrm >> 6) & 3;
		BYTE rm = modrm & 7;
		len++; // ModR/M

		if (mod == 0) {
			if (rm == 5) len += 4;
			else if (rm == 4) len++;
		} else if (mod == 1) {
			len += 1;
			if (rm == 4) len++;
		} else if (mod == 2) {
			len += 4;
			if (rm == 4) len++;
		}

		// Some 0F instructions have immediates
		if (opcode2 == 0xBA) len += 1; // bt/bts/btr/btc imm8
		if (opcode2 == 0xA4 || opcode2 == 0xAC) len += 1; // shld/shrd imm8
		if (opcode2 == 0xC2) len += 1; // cmpss

		return len;
	}

	// Fallback: assume 1 byte (conservative for unknown opcodes)
	return len;
}

/*
* Compute the number of bytes to copy for a hook trampoline.
* Must cover at least 5 bytes (size of a jmp rel32 instruction),
* ending on an instruction boundary.
*/
static BYTE ComputeHookSize(void* proc)
{
	BYTE totalSize = 0;
	BYTE* p = (BYTE*)proc;
	while (totalSize < 5) {
		BYTE instrLen = GetInstructionLength(p);
		if (instrLen == 0) instrLen = 1;
		totalSize += instrLen;
		p += instrLen;
		if (totalSize > MAX_HOOK_SIZE) break; // safety
	}
	return totalSize;
}

/*
* If the copied bytes start with a jump (0xe9), recalculate the relative offset
* because the jump target is relative to the original address, not the trampoline.
*/
void recalculateRel32IfIsJump(void* dest, void* source)
{
	CallJmpInstr* mayBeJump = (CallJmpInstr*)dest;
	if (mayBeJump->opcode == 0xe9)
	{
		mayBeJump->rel32 = (DWORD)source - (DWORD)dest + mayBeJump->rel32;
	}
}

/*
* Save the first N bytes of a function into a trampoline buffer,
* then add a jump back to the original function at offset N.
*/
BYTE saveOldFunction(void* proc, void* old, BYTE size)
{
	CopyMemory(old, proc, size);
	recalculateRel32IfIsJump(old, proc);
	CallJmpInstr* instr = (CallJmpInstr*)((BYTE*)old + size);
	instr->opcode = 0xe9;
	instr->rel32 = (DWORD)((BYTE*)proc + size) - (DWORD)((BYTE*)old + size + 5);
	return size;
}

void* splice(void* splicedFunctionAddress, void* hookFunction)
{
	BYTE hookSize = ComputeHookSize(splicedFunctionAddress);

	DWORD oldProtect;
	VirtualProtect((DWORD*)splicedFunctionAddress, hookSize, PAGE_EXECUTE_READWRITE, &oldProtect);
	void* oldFunction = malloc(255);
	*(DWORD*)oldFunction = (DWORD)splicedFunctionAddress;
	*((BYTE*)oldFunction + 4) = saveOldFunction((DWORD*)((BYTE*)splicedFunctionAddress), (DWORD*)((BYTE*)oldFunction + 5), hookSize);

	// Write jump at the start of the original function.
	// If hookSize > 5, fill remaining bytes with NOPs after the jmp.
	CallJmpInstr* instr = (CallJmpInstr*)((BYTE*)splicedFunctionAddress);
	instr->opcode = 0xe9;
	instr->rel32 = (DWORD)hookFunction - (DWORD)splicedFunctionAddress - 5;
	// NOP out remaining bytes if hookSize > 5
	for (BYTE i = 5; i < hookSize; i++) {
		*((BYTE*)splicedFunctionAddress + i) = 0x90; // NOP
	}
	VirtualProtect((DWORD*)splicedFunctionAddress, hookSize, oldProtect, &oldProtect);

	return (DWORD*)((BYTE*)oldFunction + 5);
}

BOOL restore(void*& oldProc)
{
	if (oldProc != 0 && *((BYTE*)(*(DWORD*)((BYTE*)oldProc - 5))) == 0xe9) {
		void* proc = (DWORD*)(*(DWORD*)((BYTE*)oldProc - 5));
		DWORD size = (BYTE)(*(DWORD*)((BYTE*)oldProc - 1));
		DWORD oldProtect;
		VirtualProtect(proc, size, PAGE_EXECUTE_READWRITE, &oldProtect);
		CopyMemory(proc, oldProc, size);
		recalculateRel32IfIsJump(proc, oldProc);
		VirtualProtect(proc, size, oldProtect, &oldProtect);
		free((DWORD*)((BYTE*)oldProc - 5));
		oldProc = 0;
		return TRUE;
	}
	return FALSE;
}
