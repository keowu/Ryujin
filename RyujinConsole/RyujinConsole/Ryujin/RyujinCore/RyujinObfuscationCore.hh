#pragma once
#include <Windows.h>
#include <vector>
#include <set>
#include <cstdint>
#include <random>
#include <functional>
#include <unordered_map>
#include <asmjit/asmjit.h>
#include <Zydis/Zydis.h>
#include <Zydis/SharedTypes.h>
#include "../Models/RyujinProcedure.hh"
#include "../Models/RyujinObfuscatorConfig.hh"
#include "../RyujinCore/BasicBlockerBuilder.hh"

class RyujinObfuscationCore {

private:
	const int MAX_PADDING_SPACE_INSTR = 7;
	const int MAX_JUNK_GENERATION_ITERATION = 5;
	std::vector<ZydisRegister> m_unusedRegisters;
	std::vector<RyujinBasicBlock> m_obfuscated_bb;
	uintptr_t m_ProcImageBase;
	RyujinProcedure m_proc;
	RyujinObfuscatorConfig m_config;
	BOOL extractUnusedRegisters();
	void updateBasicBlocksContext();
	void addPaddingSpaces();
	void obfuscateIat();
	void insertJunkCode();
	void insertVirtualization();
	std::vector<uint8_t> fix_branch_near_far_short(uint8_t original_opcode, uint64_t jmp_address, uint64_t target_address);
	uint32_t findOpcodeOffset(const uint8_t* data, size_t dataSize, const void* opcode, size_t opcodeSize);

	inline std::pair<int, int> findBlockId(ZyanU8 uopcode, ZyanI64 value, ZyanU8 idx, ZyanU8 szData) {

		int block_id = 0;
		int opcode_id = 0;

		for (auto& block : m_proc.basic_blocks) {

			opcode_id = 0;

			for (auto& opcode : block.opcodes) {

				auto data = opcode.data();
				auto size = opcode.size();

				if (data[0] == uopcode)  //0xFF ?

					if (std::memcmp(&*(data + idx), &value, szData) == 0) // Is it the same memory immediate?

						return std::make_pair(block_id, opcode_id);


				opcode_id++;

			}

			block_id++;
		}

		return std::make_pair(-1, -1);
	};

public:
	RyujinObfuscationCore(const RyujinObfuscatorConfig& config, const RyujinProcedure& proc, uintptr_t ProcImageBase);
	void applyRelocationFixupsToInstructions(uintptr_t imageBase, DWORD virtualAddress, std::vector<unsigned char>& new_opcodes);
	void removeOldOpcodeRedirect(uintptr_t newMappedPE, std::size_t szMapped, uintptr_t newObfuscatedAddress, bool isIgnoreOriginalCodeRemove = false);
	void InsertMiniVmEnterProcedureAddress(uintptr_t imageBase, uintptr_t virtualAddress, std::vector<unsigned char>& new_opcodes);
	BOOL Run();
	RyujinProcedure getProcessedProc();
	~RyujinObfuscationCore();

};