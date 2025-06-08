#include "RyujinObfuscationCore.hh"

RyujinObfuscationCore::RyujinObfuscationCore(const RyujinObfuscatorConfig& config, const RyujinProcedure& proc, uintptr_t ProcImageBase) {

	m_proc = proc;
	m_config = config;
	m_ProcImageBase = ProcImageBase;

	if (!extractUnusedRegisters())
		throw std::exception("No registers avaliable for obfuscation...");



}

RyujinProcedure RyujinObfuscationCore::getProcessedProc() {

	return this->m_proc;
}

BOOL RyujinObfuscationCore::extractUnusedRegisters() {

	std::vector<ZydisRegister> candidateRegs = {

			ZYDIS_REGISTER_RAX,
			ZYDIS_REGISTER_RCX,
			ZYDIS_REGISTER_RDX,
			ZYDIS_REGISTER_RBX,
			ZYDIS_REGISTER_RSP,
			ZYDIS_REGISTER_RBP,
			ZYDIS_REGISTER_RSI,
			ZYDIS_REGISTER_RDI,
			ZYDIS_REGISTER_R8,
			ZYDIS_REGISTER_R9,
			ZYDIS_REGISTER_R10,
			ZYDIS_REGISTER_R11,
			ZYDIS_REGISTER_R12,
			ZYDIS_REGISTER_R13,
			ZYDIS_REGISTER_R14,
			ZYDIS_REGISTER_R15,

	};

	std::set<ZydisRegister> usedRegs;

	for (auto blocks : m_proc.basic_blocks) {

		for (auto instr : blocks.instructions) {

			for (auto i = 0; i < instr.instruction.info.operand_count; ++i) {

				const ZydisDecodedOperand& op = instr.instruction.operands[i];

				if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) usedRegs.insert(op.reg.value);
				else if (op.type == ZYDIS_OPERAND_TYPE_POINTER) {
				
					if (op.mem.base != ZYDIS_REGISTER_NONE) usedRegs.insert(op.mem.base);
					if (op.mem.index != ZYDIS_REGISTER_NONE) usedRegs.insert(op.mem.index);
				
				}

			}

		}

	}

	ZydisRegister freeReg = ZYDIS_REGISTER_NONE;
	for (auto reg : candidateRegs)
		if (usedRegs.count(reg) == 0) m_unusedRegisters.push_back(reg);

	return m_unusedRegisters.size() >= 2; //Theres unused regs for be used by us ?
}

void RyujinObfuscationCore::addPaddingSpaces() {

	asmjit::JitRuntime runtime;

	for (auto& block : m_proc.basic_blocks) {

		std::vector<std::vector<ZyanU8>> new_instructions;

		for (auto& opcode : block.opcodes) {

			std::vector<ZyanU8> new_opcodes;

			for (auto individual_opcode : opcode) {

				new_opcodes.push_back(individual_opcode);

			}

			new_instructions.push_back(new_opcodes);

			//Inserindo junkcode
			std::vector<ZyanU8> gen_opcodes;

			asmjit::CodeHolder code;
			code.init(runtime.environment());
			asmjit::x86::Assembler a(&code);

			for (auto i = 0; i < MAX_PADDING_SPACE_INSTR; i++) a.nop();

			code.flatten();

			auto section = code.sectionById(0);
			const uint8_t* buf = section->buffer().data();
			size_t size = section->buffer().size();

			for (size_t i = 0; i < size; ++i) {
				gen_opcodes.push_back(buf[i]);
			}

			new_instructions.push_back(gen_opcodes);

		}

		//Overrite the original opcodes with new ones
		block.opcodes.clear();
		block.opcodes.assign(new_instructions.begin(), new_instructions.end());

	}

}

void RyujinObfuscationCore::obfuscateIat() {

	/*
		Unexpected Ryujin requires at least one unused register in the current procedure to deobfuscate the IAT during runtime
	*/
	if (m_unusedRegisters.size() == 0) return;

	auto findBlockId = [&](ZyanU8 uopcode, ZyanI64 value) -> std::pair<int, int> {

		int block_id = 0;
		int opcode_id = 0;

		for (auto& block : m_proc.basic_blocks) {

			opcode_id = 0;

			for (auto& opcode : block.opcodes) {

				auto data = opcode.data();
				auto size = opcode.size();

				if (data[0] == uopcode) { //0xFF ?

					if (std::memcmp(&*(data + 2), &value, sizeof(uint32_t)) == 0) // Is it the same memory immediate?

						return std::make_pair(block_id, opcode_id);
				}

				opcode_id++;
			
			}

			block_id++;
		}

		return std::make_pair(-1, -1);
	};

	for (auto& block : m_obfuscated_bb) {

		for (auto& instr : block.instructions) {

			if (instr.instruction.info.meta.category == ZYDIS_CATEGORY_CALL && instr.instruction.operands->type == ZYDIS_OPERAND_TYPE_MEMORY) {

				// Finding the block info related to the obfuscated opcode
				auto block_info = findBlockId(instr.instruction.info.opcode, instr.instruction.operands->mem.disp.value);

				// Call to an invalid IAT in the list of basic blocks
				if (block_info.first == -1 || block_info.second == -1) continue;

				// Retrieving the original opcodes where the opcodes have already been updated and obfuscated
				auto& data = m_proc.basic_blocks[block_info.first].opcodes[block_info.second];

				// Retrieving the "INSTRUCTION" from the basic block for our IAT call related to this context we're working with
				auto orInstr = m_proc.basic_blocks[block_info.first].instructions.back(); // A call [IAT] will always be the last entry

				/*
					Let's calculate the IAT address that stores the resolved address for the given CALL
				*/
				// Calculating the VA of the next instruction after the "CALL" to the IAT in the original section
				const uintptr_t next_instruction_address = orInstr.addressofinstruction + orInstr.instruction.info.length;

				// Calculating the target address of the IAT using the memory immediate from the original instruction
				const uint32_t iat_target_rva = (next_instruction_address + orInstr.instruction.operands->mem.disp.value) - m_ProcImageBase;

				// A new vector to store our corrected IAT
				std::vector<ZyanU8> new_iat_call;

				unsigned char pebRecoverModuleBase[24]{
					0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, // mov rax, gs:60h
					0x48, 0x8B, 0x40, 0x10, // mov rax, qword ptr ds:[rax+0x10]
					0x48, 0x05, 0x00, 0x00, 0x00, 0x00, // add     rax, 22228h
					0x48, 0x8B, 0x00, //mov rax, qword ptr ds:[rax] -> Recover the IAT jump value stored in the data segment
					0xFF, 0xD0//call rax
				};

				std::memcpy(&*(pebRecoverModuleBase + 15), &iat_target_rva, sizeof(uint32_t));

				new_iat_call.insert(new_iat_call.end(), std::begin(pebRecoverModuleBase), std::end(pebRecoverModuleBase));
				
				// Replacing opcodes of the call in question with the new ones
				data.assign(new_iat_call.begin(), new_iat_call.end());

				std::printf("[OK] Obfuscating IAT CALL: %s\n", instr.instruction.text);

			}

		}

	}

	return;
}

void RyujinObfuscationCore::updateBasicBlocksContext() {

	auto new_obfuscated_opcodes = getProcessedProc().getUpdateOpcodes();
	auto bb = new RyujinBasicBlockerBuilder(ZYDIS_MACHINE_MODE_LONG_64, ZydisStackWidth_::ZYDIS_STACK_WIDTH_64);
	m_obfuscated_bb = bb->createBasicBlocks(new_obfuscated_opcodes.data(), static_cast<size_t>(new_obfuscated_opcodes.size()), m_proc.address);

}

BOOL RyujinObfuscationCore::Run() {

	//Add padding spaces
	addPaddingSpaces();

	//Update basic blocks view based on the new obfuscated 
	this->updateBasicBlocksContext();

	//Obfuscate IAT for the configured procedures
	if (m_config.m_isIatObfuscation) {

		//First obfuscate IAT
		obfuscateIat();

		//Update our basic blocks context to rely 1-1 for the new obfuscated opcodes.
		this->updateBasicBlocksContext();

	}

	/*

	if (config.m_isVirtualized) todoAction();
	if (config.m_isJunkCode) todoAction();
	*/

	return TRUE;
}

uint32_t RyujinObfuscationCore::findOpcodeOffset(const uint8_t* data, size_t dataSize, const void* opcode, size_t opcodeSize) {
	
	if (opcodeSize == 0 || dataSize < opcodeSize) return 0;

	for (size_t i = 0; i <= dataSize - opcodeSize; ++i) if (std::memcmp(data + i, opcode, opcodeSize) == 0) return static_cast<uint32_t>(i);

	return 0;
}

std::vector<uint8_t> RyujinObfuscationCore::fix_branch_near_far_short(uint8_t original_opcode, uint64_t jmp_address, uint64_t target_address) {
	
	static const std::unordered_map<uint8_t, uint8_t> SHORT_TO_NEAR = {

		{ 0x70, 0x80 }, { 0x71, 0x81 }, { 0x72, 0x82 }, { 0x73, 0x83 },
		{ 0x74, 0x84 }, { 0x75, 0x85 }, { 0x76, 0x86 }, { 0x77, 0x87 },
		{ 0x78, 0x88 }, { 0x79, 0x89 }, { 0x7A, 0x8A }, { 0x7B, 0x8B },
		{ 0x7C, 0x8C }, { 0x7D, 0x8D }, { 0x7E, 0x8E }, { 0x7F, 0x8F }
	
	};

	std::vector<uint8_t> result;

	// Tries to handle as a short jump (2 bytes)
	const int short_length = 2;
	const int64_t short_disp = static_cast<int64_t>(target_address) - (jmp_address + short_length);

	if (short_disp >= -128 && short_disp <= 127) {
		
		result.push_back(original_opcode);
		result.push_back(static_cast<uint8_t>(short_disp));
		
		return result;
	}

	// If it is not a conditional jump, returns the original
	auto it = SHORT_TO_NEAR.find(original_opcode);
	if (it == SHORT_TO_NEAR.end()) {

		result.push_back(original_opcode);
		
		return result; // Does not apply conversion
	}

	// Handles as a near jump (6 bytes)
	const uint8_t near_opcode = it->second;
	const int near_length = 6;
	const int64_t near_disp = static_cast<int64_t>(target_address) - (jmp_address + near_length);

	if (near_disp < INT32_MIN || near_disp > INT32_MAX) throw std::exception("[X] Offset exceeds the limit of a 32-bit signed integer.");

	result.push_back(0x0F);
	result.push_back(near_opcode);

	const uint32_t raw_disp = static_cast<uint32_t>(near_disp);
	result.push_back((raw_disp >> 0) & 0xFF);
	result.push_back((raw_disp >> 8) & 0xFF);
	result.push_back((raw_disp >> 16) & 0xFF);
	result.push_back((raw_disp >> 24) & 0xFF);

	return result;
}

void RyujinObfuscationCore::applyRelocationFixupsToInstructions(uintptr_t imageBase, DWORD virtualAddress, std::vector<unsigned char>& new_opcodes) {

	/*
		Creating a new basic block for our obfuscated code
	*/
	auto bb = new RyujinBasicBlockerBuilder(ZYDIS_MACHINE_MODE_LONG_64, ZydisStackWidth_::ZYDIS_STACK_WIDTH_64);
	m_obfuscated_bb = bb->createBasicBlocks(new_opcodes.data(), static_cast<size_t>(new_opcodes.size()), imageBase + virtualAddress);

	//The current block id that we're working with
	int block_id = 0;

	for (auto& block : m_proc.basic_blocks) {

		for (auto& instruction : block.instructions) {

			//Fixing all Call to a immediate(No IAT) values from our obfuscated opcodes -> CALL IMM
			if (instruction.instruction.info.meta.category == ZYDIS_CATEGORY_CALL && instruction.instruction.operands->type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {

				//References for the data and size of the vector with the obfuscated opcodes
				auto size = new_opcodes.size();
				auto data = new_opcodes.data();

				//Getting the immediate value of the original "CALL"
				const uint32_t immediateValue = instruction.instruction.operands[0].imm.value.u;

				/*
					Creating a signature for the opcode from the original section so that we can
					scan the obfuscated region using the correct instruction offset and recalculate its displacement.
				*/
				unsigned char ucOpcodeSignature[5]{ instruction.instruction.info.opcode };
				std::memcpy(&*(ucOpcodeSignature + 1), &immediateValue, sizeof(immediateValue));

				//Finding the offset of the "CALL" using the opcode signature in the obfuscated section
				const uint32_t offset = findOpcodeOffset(data, size, &ucOpcodeSignature, 5);

				//Calculating the VA (Virtual Address) of the "CALL" in the obfuscated section
				uint32_t obfuscated_call_va = imageBase + virtualAddress + offset;

				/*
					Calculating the new immediate offset to fix the relocation of the new obfuscated "CALL" instruction
				*/
				// Calculate address of the next instruction (CALL instruction + its length)
				const uintptr_t next_instruction_address = instruction.addressofinstruction + instruction.instruction.info.length;

				// Get the relative displacement from the first operand (signed 32-bit integer)
				const int32_t displacement = static_cast<int32_t>(instruction.instruction.operands[0].imm.value.s);

				// Calculate absolute target address
				const uintptr_t target_address = next_instruction_address + displacement;

				//Calculating the new immediate value for the "CALL" instruction using the VA addresses of the obfuscated section
				uint32_t new_immediate_reloc = static_cast<uint32_t>(target_address) - (obfuscated_call_va + instruction.instruction.info.length); //length == 5

				//Fixing the relocation of the "CALL" instruction in the obfuscated region
				std::memcpy(&*(data + offset + 1), &new_immediate_reloc, sizeof(uint32_t));

				std::printf("[OK] Fixing CALL IMM -> %s from 0x%X to 0x%X\n", instruction.instruction.text, immediateValue, new_immediate_reloc);

			} 
			//Fixing all Call to a memory(IAT) values from our obfuscated opcodes -> CALL [MEMORY]
			else if (instruction.instruction.info.meta.category == ZYDIS_CATEGORY_CALL && instruction.instruction.operands->type == ZYDIS_OPERAND_TYPE_MEMORY && !m_config.m_isIatObfuscation) {

				// References for the vector's data and size with the obfuscated opcodes
				auto size = new_opcodes.size();
				auto data = new_opcodes.data();

				// Obtaining the memory immediate value for the "CALL"
				const uint32_t memmory_immediate = instruction.instruction.operands->mem.disp.value;

				// Creating a signature to search for the offset in the obfuscated opcodes
				unsigned char ucOpcodeSignature[6]{ 0xFF, 0x15 };
				std::memcpy(&*(ucOpcodeSignature + 2), &memmory_immediate, sizeof(memmory_immediate));

				// Finding the offset of the "CALL" memory using the opcode signature in the obfuscated section
				const uint32_t offset = findOpcodeOffset(data, size, &ucOpcodeSignature, 6);

				// If we don't find the signature, it might not be an IAT... requiring future handling.
				if (offset == 0) {

					std::printf("[X] Invalid IAT call or call to a custom address detected.....\n");

					continue;
				}

				// Calculating the VA (Virtual Address) of the "CALL" instruction to the IAT in the obfuscated section
				const uintptr_t obfuscated_call_iat_va = ((imageBase + virtualAddress + offset));

				// Calculating the VA of the next instruction after the "CALL" to the IAT in the original section
				const uintptr_t next_instruction_address = instruction.addressofinstruction + instruction.instruction.info.length;

				// Calculating the target address of the IAT using the memory immediate from the original instruction
				const uintptr_t iat_target_address = next_instruction_address + memmory_immediate;

				// Calculating new RIP (Instruction Pointer) for the obfuscated instruction
				uintptr_t new_rip = obfuscated_call_iat_va + instruction.instruction.info.length;

				// Calculating the displacement from the new position to the IAT address
				const uint32_t new_memory_immediate_iat = iat_target_address - new_rip;

				// Fixing the relocation of the "CALL" instruction in the obfuscated region to the new memory immediate
				std::memcpy(&*(data + offset + 2), &new_memory_immediate_iat, sizeof(uint32_t));

				std::printf("[OK] Fixing IAT Call -> %s from 0x%X to 0x%X\n", instruction.instruction.text, obfuscated_call_iat_va, new_memory_immediate_iat);

			}
			// Searching for MOV and LEA instructions that have the second operand as memory-relative
			else if ((instruction.instruction.info.mnemonic == ZYDIS_MNEMONIC_LEA || instruction.instruction.info.mnemonic == ZYDIS_MNEMONIC_MOV) && instruction.instruction.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY) {

				const ZydisDecodedOperandMem* mem = &instruction.instruction.operands[1].mem;

				//Looking for: lea reg, [MEMORY] and mov reg, [MEMORY]
				if (mem->base == ZYDIS_REGISTER_RIP && mem->index == ZYDIS_REGISTER_NONE && mem->disp.has_displacement) {

					// References for data and vector size with obfuscated opcodes
					auto size = new_opcodes.size();
					auto data = new_opcodes.data();

					// Getting the memory immediate offset value to build the signature
					const uint32_t memmory_immediate_offset = mem->disp.value;

					// Creating a signature to search for the offset in the obfuscated opcodes
					unsigned char ucOpcodeSignature[7]{ 0 };
					std::memcpy(&ucOpcodeSignature, reinterpret_cast<void*>(instruction.addressofinstruction), 3); // 3 BYTES do opcode relativo ao LEA ou MOV
					std::memcpy(&*(ucOpcodeSignature + 3), &memmory_immediate_offset, sizeof(memmory_immediate_offset));

					// Finding the offset of the "LEA" or "MOV" that uses memory-relative addressing
					const ZyanI64 offset = findOpcodeOffset(data, size, &ucOpcodeSignature, 7);

					// If we don't find any offset, there may be an issue or bug.
					if (offset == 0) {

						std::printf("[X] Invalid lea reference or uknown lea detected.....\n");

						continue;
					}

					// Retrieving the instruction address in the original section
					const uintptr_t original_address = instruction.addressofinstruction;

					// Calculating new address in the obfuscated section
					const uintptr_t obfuscated_va_address = ((imageBase + virtualAddress + offset));

					/*
						Calculating new displacement for the immediate value
					*/
					// Calculating the address of the instruction following the original instruction
					const uintptr_t original_rip = original_address + instruction.instruction.info.length;
					
					// Calculating the original target address of the original instruction
					const uintptr_t target_original = original_rip + memmory_immediate_offset;

					// Calculating the address of the instruction following the obfuscated instruction
					const uintptr_t new_obfuscated_rip = obfuscated_va_address + instruction.instruction.info.length;

					// New memory immediate value for the instruction
					const uintptr_t new_memory_immediate = target_original - new_obfuscated_rip;

					// Fixing the immediate value for the "LEA" or "MOV" instruction with the corrected relative immediate value
					std::memcpy(&*(data + offset + 3), &new_memory_immediate, sizeof(uint32_t)); // 3 bytes for the size of the LEA or MOV opcode

					std::printf("[OK] Fixing -> %s - from %X to %X\n", instruction.instruction.text, mem->disp.value, new_memory_immediate);

				}

			}
			else if (instruction.instruction.info.meta.category == ZYDIS_CATEGORY_COND_BR || instruction.instruction.info.meta.category == ZYDIS_CATEGORY_UNCOND_BR) {
			
				// References for data and vector size with obfuscated opcodes
				auto size = new_opcodes.size();
				auto data = new_opcodes.data();

				/*
					Finding the address of the currently analyzed branch instruction within the set of obfuscated basic blocks
				*/
				uintptr_t obfuscated_jmp_address = 0;
				auto basic_block_obfuscated_ctx = m_obfuscated_bb.at(block_id);
				for (auto& inst : basic_block_obfuscated_ctx.instructions)

					if (inst.instruction.info.opcode == instruction.instruction.info.opcode && inst.instruction.operands[0].imm.value.u == instruction.instruction.operands[0].imm.value.u) {
						obfuscated_jmp_address = inst.addressofinstruction;
						break;
					}

				/*
					Based on the branch's destination address, we’ll search for the block ID in our vector so that we can
					synchronize both the obfuscated and original blocks to work on a fix.
				*/
				auto address_branch = instruction.addressofinstruction + instruction.instruction.info.length + instruction.instruction.operands[0].imm.value.u;
				uint32_t local_block_id = 0;
				for (auto& block : m_proc.basic_blocks) {

					if (address_branch >= block.start_address && address_branch <= block.end_address)
						break;

					local_block_id++;
				}

				//Calculating our new branch immediate offset
				auto basic_block_original = m_proc.basic_blocks.at(local_block_id);
				auto basic_block_obfuscated = m_obfuscated_bb.at(local_block_id);

				/*
					Normally, obfuscated and deobfuscated blocks are 1-to-1, and we just need to get the address relative
					to the first instruction of the block in question so we can determine the jump address for the new obfuscated region.
				*/
				auto obfuscated_target_address = basic_block_obfuscated.instructions.at(0).addressofinstruction;

				/*
					Let's fix our new branch. Previously it was a "near" jump, but now it will be "far" depending on the jump length.
					This procedure will perform the calculation and generate a far or near branch depending on the need
					and the output of the obfuscated code.
				*/
				auto corrected = fix_branch_near_far_short(instruction.instruction.info.opcode, obfuscated_jmp_address, obfuscated_target_address);

				// Creating a signature for the original branch in the obfuscated opcode to be fixed
				unsigned char ucSignature[2]{ 0, 0 };
				std::memcpy(ucSignature, reinterpret_cast<void*>(instruction.addressofinstruction), 2);
				// Finding the correct offset of the opcode to apply the patch
				const uint32_t offset = findOpcodeOffset(data, size, &ucSignature, 2);

				// Clearing the branch so we can insert the new branch with the corrected opcode and its offset
				std::memset(&*(data + offset), 0x90, 9); // Equivalent to -> branch + offset and possibly some add reg, value -> we have space because "addPaddingSpaces" into this section.
				
				// Patching the cleared region with the new branch, now fully fixed and with the newly calculated jump displacement
				std::memcpy(&*(data + offset), corrected.data(), corrected.size());

				std::printf("[OK] Fixing %s -> %X -> id: %X\n", instruction.instruction.text, instruction.instruction.operands[0].imm.value.u, block_id);
				
			}

		}

		//Increment block index
		block_id++;

	}

}

void RyujinObfuscationCore::removeOldOpcodeRedirect(uintptr_t newMappedPE, std::size_t szMapped, uintptr_t newObfuscatedAddress, bool isIgnoreOriginalCodeRemove) {

	/*
		Creating signatures to search for the opcode in the PE mapped from disk.
		We will use findOpcodeOffset to find the exact offset of the procedure's start
		in the unmapped region with the SEC_IMAGE flag.
	*/
	unsigned char ucSigature[10]{ 0 };
	std::memcpy(ucSigature, reinterpret_cast<void*>(m_proc.address), 10);
	auto offsetz = findOpcodeOffset(reinterpret_cast<unsigned char*>(newMappedPE), szMapped, &ucSigature, 10);

	// Based on the obfuscation configuration, some users can decide to not remove the original code from the original procedure after obfuscation.
	if (!isIgnoreOriginalCodeRemove) std::memset(reinterpret_cast<void*>(newMappedPE + offsetz), 0x90, m_proc.size); // Removing all the opcodes from the original procedure and replacing them with NOP instructions.

	/*
		Creating a new JMP opcode in such a way that it can be added to the old region that was completely replaced by NOP,
		thus redirecting execution to the new obfuscated code.
	*/
	unsigned char ucOpcodeJmp[5]{
		0xE9, 0, 0, 0, 0, //JMP imm
	};

	/*	
		Calculating the new displacement between the original code region and the target obfuscated opcode,
		calculating the relative immediate offset.
	*/
	const uint32_t offset = newObfuscatedAddress - (m_proc.address + 5);

	//Replacing the jump opcode with the new relative immediate displacement value.
	std::memcpy(&*(ucOpcodeJmp + 1), &offset, sizeof(uint32_t));

	//Inserting the new jump opcode into the original cleaned function to redirect execution to the fully obfuscated code.
	std::memcpy(reinterpret_cast<void*>(newMappedPE + offsetz), ucOpcodeJmp, 5);

}

RyujinObfuscationCore::~RyujinObfuscationCore() {

}