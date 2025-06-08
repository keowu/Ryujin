#pragma once
#include <Windows.h>
#include <vector>
#include <set>
#include <cstdint>
#include <unordered_map>
#include <asmjit/asmjit.h>
#include <Zydis/Zydis.h>
#include <Zydis/SharedTypes.h>
#include "../Models/RyujinProcedure.hh"
#include "../Models/RyujinObfuscatorConfig.hh"
#include "../RyujinCore/BasicBlockerBuilder.hh"

class RyujinObfuscationCore {

private:
	const int MAX_PADDING_SPACE_INSTR = 15;
	std::vector<ZydisRegister> m_unusedRegisters;
	std::vector<RyujinBasicBlock> m_obfuscated_bb;
	uintptr_t m_ProcImageBase;
	RyujinProcedure m_proc;
	RyujinObfuscatorConfig m_config;
	BOOL extractUnusedRegisters();
	void updateBasicBlocksContext();
	void addPaddingSpaces();
	void obfuscateIat();
	std::vector<uint8_t> fix_branch_near_far_short(uint8_t original_opcode, uint64_t jmp_address, uint64_t target_address);
	uint32_t findOpcodeOffset(const uint8_t* data, size_t dataSize, const void* opcode, size_t opcodeSize);

public:
	RyujinObfuscationCore(const RyujinObfuscatorConfig& config, const RyujinProcedure& proc, uintptr_t ProcImageBase);
	void applyRelocationFixupsToInstructions(uintptr_t imageBase, DWORD virtualAddress, std::vector<unsigned char>& new_opcodes);
	void removeOldOpcodeRedirect(uintptr_t newMappedPE, std::size_t szMapped, uintptr_t newObfuscatedAddress, bool isIgnoreOriginalCodeRemove = false);
	BOOL Run();
	RyujinProcedure getProcessedProc();
	~RyujinObfuscationCore();

};