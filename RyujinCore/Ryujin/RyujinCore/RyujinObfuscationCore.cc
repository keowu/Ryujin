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

	// Initializing AsmJit
	asmjit::JitRuntime runtime;

	for (auto& block : m_proc.basic_blocks) {

		// Vector to store the opcodes related to the current context basic block
		std::vector<std::vector<ZyanU8>> new_instructions;

		for (auto& opcode : block.opcodes) {

			// Saving all original opcodes of the basic block
			std::vector<ZyanU8> new_opcodes;

			for (auto individual_opcode : opcode)
				new_opcodes.push_back(individual_opcode);

			// Adding them to the main control vector
			new_instructions.push_back(new_opcodes);

			//Storing Nop-Spacing
			std::vector<ZyanU8> gen_opcodes;

			// Initializing AsmJit
			asmjit::CodeHolder code;
			code.init(runtime.environment());
			asmjit::x86::Assembler a(&code);

			// Inserting nop-spacing technique
			for (auto i = 0; i < MAX_PADDING_SPACE_INSTR; i++) a.nop();

			// Flush flatten
			code.flatten();

			// Getting the result from JIT
			auto section = code.sectionById(0);
			const auto buf = section->buffer().data();
			auto size = section->buffer().size();

			// Storing each new generated opcode
			for (auto i = 0; i < size; ++i) gen_opcodes.push_back(buf[i]);

			// Storing in the main vector of the block
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

	for (auto& block : m_obfuscated_bb) {

		for (auto& instr : block.instructions) {

			if (instr.instruction.info.meta.category == ZYDIS_CATEGORY_CALL && instr.instruction.operands->type == ZYDIS_OPERAND_TYPE_MEMORY) {

				// Finding the block info related to the obfuscated opcode
				auto block_info = findBlockId(instr.instruction.info.opcode, instr.instruction.operands->mem.disp.value, 2, sizeof(uint32_t));

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
				uint32_t iat_target_rva = (next_instruction_address + orInstr.instruction.operands->mem.disp.value) - m_ProcImageBase;

				/*
					Let's obfuscate our RVA
				*/
				// Generating two random bytes for the key
				std::mt19937 rng(std::random_device{}());
				// A single random value of 2 bytes (uint16_t)
				std::uniform_int_distribution<uint16_t> dist(0, 0xFFFF);
				uint16_t xorKey = dist(rng);

				// Obfsucate the RVA with a XOR
				iat_target_rva ^= xorKey;

				// Obfuscate PEB offset from automatic scan
				unsigned char PebGsOffset  = 0x60 ^ (xorKey & 0xFF);
				unsigned char ImageBasePeb = 0x10 ^ (xorKey & 0xFF);

				// A new vector to store our corrected IAT
				std::vector<ZyanU8> new_iat_call;

				//Begin ASMJIT configuration
				asmjit::JitRuntime runtime;
				asmjit::CodeHolder code;
				code.init(runtime.environment());
				asmjit::x86::Assembler a(&code);

				// Using `rdgsbase rax` to store the base address of the GS segment in RAX -> rdgsbase rax
				a.emit(asmjit::x86::Inst::kIdRdgsbase, asmjit::x86::rax);

				// Adding the obfuscated offset of the PEB in the GS segment -> add rax, PebGsOffset
				a.add(asmjit::x86::rax, PebGsOffset);

				// Undoing the XOR operation with the obfuscated RAX value and the XOR key -> xor rax, lastByteXorKey
				a.xor_(asmjit::x86::rax, asmjit::imm(xorKey & 0xFF));

				// Accessing the resulting address to retrieve the PEB instance -> mov rax, [rax]
				a.mov(asmjit::x86::rax, asmjit::x86::ptr(asmjit::x86::rax));

				// Adding the obfuscated offset of the ImageBase field in the PEB -> add rax, ImageBasePeb
				a.add(asmjit::x86::rax, ImageBasePeb);

				// Undoing the XOR operation with the obfuscated value and the XOR key -> xor rax, lastByteXorKey
				a.xor_(asmjit::x86::rax, asmjit::imm(xorKey & 0xFF));

				// Accessing the resulting address to retrieve the PEB+ImageBase instance -> mov rax, [rax]
				a.mov(asmjit::x86::rax, asmjit::x86::ptr(asmjit::x86::rax));

				// Adding the RVA that points to the entry in the IAT -> add rax, imm32 -> Adding the offset of the IAT entry
				a.add(asmjit::x86::rax, asmjit::imm(iat_target_rva));
				
				// Undoing the XOR operation with the obfuscated value and the XOR key -> xor rax, xorKey
				a.xor_(asmjit::x86::rax, asmjit::imm(xorKey));

				// mov rax, [rax] -> retrieving the resolved address for the IAT entry by the OS loader
				a.mov(asmjit::x86::rax, asmjit::x86::ptr(asmjit::x86::rax));

				// call rax -> Calling the IAT
				a.call(asmjit::x86::rax);

				// Obtaining the new section buffer
				auto& opcodeBuffer = code.sectionById(0)->buffer();
				// Obtaining the pointer to the buffer of raw opcode data generated
				const auto pOpcodeBuffer = opcodeBuffer.data();
				// Reserving space in the IAT vector
				new_iat_call.reserve(opcodeBuffer.size());

				// Storing each opcode individually in the vector for our new IAT call
				for (auto i = 0; i < opcodeBuffer.size(); ++i) new_iat_call.push_back(static_cast<ZyanU8>(pOpcodeBuffer[i]));

				// Replacing opcodes of the call in question with the new ones
				data.assign(new_iat_call.begin(), new_iat_call.end());

				std::printf("[OK] Obfuscating IAT CALL: %s\n", instr.instruction.text);

			}

		}

	}

	return;
}

void RyujinObfuscationCore::insertJunkCode() {

	// Initializing AsmJit
	asmjit::JitRuntime runtime;
	
	for (auto& block : m_proc.basic_blocks) {

		// New vector to load the updated opcodes for the given block
		std::vector<std::vector<ZyanU8>> new_instructions;
		
		for (auto& opcode : block.opcodes) {

			// Saving all original opcodes of the basic block
			std::vector<ZyanU8> new_opcodes;

			// Storing the original opcodes of the procedure
			for (auto individual_opcode : opcode) new_opcodes.push_back(individual_opcode);

			// Inserting original opcodes into the control vector
			new_instructions.push_back(new_opcodes);

			// Generating junk code
			std::vector<ZyanU8> junk_opcodes;

			// Initializing AsmJit
			asmjit::CodeHolder code;
			code.init(runtime.environment());
			asmjit::x86::Assembler a(&code);

			// Let's iterate over all registers not used by the procedure to generate junk code
			for (auto reg : m_unusedRegisters) {

				// Nop-Spacing technique for alignment
				for (auto i = 0; i < MAX_PADDING_SPACE_INSTR; i++) a.nop();

				// Junk code insertion technique
				for (auto i = 0; i < MAX_JUNK_GENERATION_ITERATION; i++) {

					/*
						Converting ZydisRegister to GP Register based on it's own ID.
					*/
					// Only x64 registers
					if (ZydisRegisterGetClass(reg) != ZYDIS_REGCLASS_GPR64) continue;
					
					// Converting ZydisRegister to GB Register Index
					auto idx = ZydisRegisterGetId(reg);
					
					// Ignore stack unused registers, if the feature for extracting unused register fail
					if (idx == 4 /*RSP*/ || idx == 5 /*RBP*/) continue;

					// Converting GB Register Index to a GB Register
					auto regx = a.gpz(uint32_t(idx));

					/*
						Generating Junk Code instructions randomly
					*/

					// Generating random value for obfuscation
					std::random_device rd;
					std::mt19937 gen(rd());

					std::uniform_int_distribution<uint32_t> quantity_dist(0, 69);     // Instructions per block
					std::uniform_int_distribution<uint32_t> opcode_dist(0, 37);       // 37 supported instructions
					std::uniform_int_distribution<uint32_t> imm_dist(1, 100);         // Range for randomizing immediate values
					std::uniform_int_distribution<uint32_t> shift_dist(0, 69);        // Range for randomizing bitwise values

					// Junk Code In
					a.push(regx);
					a.pushf();

					// Generating number of instructions for the junk code block
					auto numInstructions = quantity_dist(gen);

					for (auto i = 0; i < numInstructions; ++i) {

						/*
							Generating random values for the opcode, immediate value, and displacement value (bitwise).
						*/
						auto opcode = opcode_dist(gen);
						auto value = imm_dist(gen);
						auto shift = shift_dist(gen);

						// Choosing an opcode to use for randomizing the junk code
						switch (opcode) {

							case 0:  a.add(regx, value); break;
							case 1:  a.sub(regx, value); break;
							case 2:  a.imul(regx, value); break;
							case 3:  a.xor_(regx, value); break;
							case 4:  a.or_(regx, value); break;
							case 5:  a.and_(regx, value); break;
							case 6:  a.not_(regx); break;
							case 7:  a.neg(regx); break;
							case 8:  a.shl(regx, shift); break;
							case 9:  a.shr(regx, shift); break;
							case 10: a.sar(regx, shift); break;
							case 11: a.rol(regx, shift); break;
							case 12: a.ror(regx, shift); break;
							case 13: a.inc(regx); break;
							case 14: a.dec(regx); break;
							case 15: a.test(regx, value); break;
							case 16: a.cmp(regx, value); break;
							case 17: a.lea(regx, asmjit::x86::ptr(regx, value)); break;
							case 18: a.nop(); break;
							case 19: a.add(regx, regx); break;

							/*
								Additional instructions contributions coming directly from VMProtect mutation
								(https://keowu.re/posts/Analyzing-Mutation-Coded-VM-Protect-and-Alcatraz-English/#analyzing-techniques-and-mutation-of-vm-protect)
							*/
							case 20: a.bt(regx, value); break;
							case 21: a.bts(regx, value); break;
							case 22: a.btc(regx, value); break;
							case 23: a.movzx(regx.r32(), regx.r8()); break;
							case 24: a.movsx(regx.r32(), regx.r8()); break;
							case 25: a.movsxd(regx, regx.r32()); break;
							case 26: a.cmovs(regx, regx); break;
							case 27: a.cmovp(regx, regx); break;
							case 28: a.sal(regx, shift); break;
							case 29: a.rcl(regx, 1); break;
							case 30: a.rcr(regx, 1); break;
							case 31: a.stc(); break;
							case 32: a.clc(); break;
							case 33: a.cmc(); break;
							case 34: a.cdqe(); break;
							case 35: a.cbw(); break;
							case 36: a.sbb(regx, value); break;
							case 37: a.bsf(regx, regx); break;

							default: break;
						}
					}

					// Junk Code Out
					a.popf();
					a.pop(regx);

				}

			}

			// AsmJit Flush flatten
			code.flatten();

			// Getting the result of opcodes generated via JIT to add to our junk opcodes in the current iteration context
			auto section = code.sectionById(0);
			const auto buf = section->buffer().data();
			auto size = section->buffer().size();
			for (auto i = 0; i < size; ++i) junk_opcodes.push_back(buf[i]);

			// Adding the newly processed opcodes to the global instruction vector
			new_instructions.push_back(junk_opcodes);

		}

		// Overwriting opcodes with the new obfuscated ones
		block.opcodes.clear();
		block.opcodes.assign(new_instructions.begin(), new_instructions.end());
	
	}

}

void RyujinObfuscationCore::insertVirtualization() {

	/*
		1 - Convert the procedure's instructions and their basic blocks into the VM's bytecode (each instruction generates 8 bytes of bytecode).
		2 - Replace the instruction with a call to the VM's interpretation routine and pass the bytecodes via RCX. (Take into account saving the register and stack contexts.)
		3 - Be able to continue execution without issues, integrating the VM routine with the original code that is to be executed and not obfuscated.
		4 - This routine should insert only the VM stub and bytecode. After that, there will be a processing step before saving and fixing relocations, so we can identify the virtualization routine pattern and insert the real address of the VM interpreter to make it work.

		Basically, this is a single-VM that:
			Analyzes the instruction in question, extracts its opcode and maps it to the VM's opcode, extracts its immediates and stores everything in a single set.
		Example:
			0x48 -> mov -> bytecode
			rbx -> bytecode
			10 -> value

		Example output:
			0x112210

		Which will be assigned to the value of RCX:

			push rcx
			mov rcx, 112210h
			call vmentry (but a symbolic value, since the immediate offset wouldn't be inserted here)
			-> rax result goes to the register in question that would continue the execution flow or receive the result, in this example: rbx
			pop rcx

		In this way, the code would continue.
	*/

	/*
		Ryujin MiniVM Logic Begin
	*/
	// Is it a candidate instruction to be virtualized by the minivm?
	auto isValidToSRyujinMiniVm = [&](RyujinInstruction instr) {

		return instr.instruction.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER && instr.instruction.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
			// Ignoring registers and stack operations
			(instr.instruction.operands[0].reg.value != ZYDIS_REGISTER_RSP && instr.instruction.operands[0].reg.value != ZYDIS_REGISTER_RBP);
	};

	// Let's map the Zydis register to ASMJIT
	auto mapZydisToAsmjitGp = [&](ZydisRegister zydisReg) -> asmjit::x86::Gp {

		switch (zydisReg) {

			// RAX family
			case ZYDIS_REGISTER_AL:
			case ZYDIS_REGISTER_AH:
			case ZYDIS_REGISTER_AX:
			case ZYDIS_REGISTER_EAX:
			case ZYDIS_REGISTER_RAX: return asmjit::x86::rax;

			// RBX family
			case ZYDIS_REGISTER_BL:
			case ZYDIS_REGISTER_BH:
			case ZYDIS_REGISTER_BX:
			case ZYDIS_REGISTER_EBX:
			case ZYDIS_REGISTER_RBX: return asmjit::x86::rbx;

			// RCX family
			case ZYDIS_REGISTER_CL:
			case ZYDIS_REGISTER_CH:
			case ZYDIS_REGISTER_CX:
			case ZYDIS_REGISTER_ECX:
			case ZYDIS_REGISTER_RCX: return asmjit::x86::rcx;

			// RDX family
			case ZYDIS_REGISTER_DL:
			case ZYDIS_REGISTER_DH:
			case ZYDIS_REGISTER_DX:
			case ZYDIS_REGISTER_EDX:
			case ZYDIS_REGISTER_RDX: return asmjit::x86::rdx;

			// RSI family
			case ZYDIS_REGISTER_SIL:
			case ZYDIS_REGISTER_SI:
			case ZYDIS_REGISTER_ESI:
			case ZYDIS_REGISTER_RSI: return asmjit::x86::rsi;

			// RDI family
			case ZYDIS_REGISTER_DIL:
			case ZYDIS_REGISTER_DI:
			case ZYDIS_REGISTER_EDI:
			case ZYDIS_REGISTER_RDI: return asmjit::x86::rdi;

			// RBP family
			case ZYDIS_REGISTER_BPL:
			case ZYDIS_REGISTER_BP:
			case ZYDIS_REGISTER_EBP:
			case ZYDIS_REGISTER_RBP: return asmjit::x86::rbp;

			// RSP family
			case ZYDIS_REGISTER_SPL:
			case ZYDIS_REGISTER_SP:
			case ZYDIS_REGISTER_ESP:
			case ZYDIS_REGISTER_RSP: return asmjit::x86::rsp;

			// R8 family
			case ZYDIS_REGISTER_R8B:
			case ZYDIS_REGISTER_R8W:
			case ZYDIS_REGISTER_R8D:
			case ZYDIS_REGISTER_R8: return asmjit::x86::r8;

			// R9 family
			case ZYDIS_REGISTER_R9B:
			case ZYDIS_REGISTER_R9W:
			case ZYDIS_REGISTER_R9D:
			case ZYDIS_REGISTER_R9: return asmjit::x86::r9;

			// R10 family
			case ZYDIS_REGISTER_R10B:
			case ZYDIS_REGISTER_R10W:
			case ZYDIS_REGISTER_R10D:
			case ZYDIS_REGISTER_R10: return asmjit::x86::r10;

			// R11 family
			case ZYDIS_REGISTER_R11B:
			case ZYDIS_REGISTER_R11W:
			case ZYDIS_REGISTER_R11D:
			case ZYDIS_REGISTER_R11: return asmjit::x86::r11;

			// R12 family
			case ZYDIS_REGISTER_R12B:
			case ZYDIS_REGISTER_R12W:
			case ZYDIS_REGISTER_R12D:
			case ZYDIS_REGISTER_R12: return asmjit::x86::r12;

			// R13 family
			case ZYDIS_REGISTER_R13B:
			case ZYDIS_REGISTER_R13W:
			case ZYDIS_REGISTER_R13D:
			case ZYDIS_REGISTER_R13: return asmjit::x86::r13;

			// R14 family
			case ZYDIS_REGISTER_R14B:
			case ZYDIS_REGISTER_R14W:
			case ZYDIS_REGISTER_R14D:
			case ZYDIS_REGISTER_R14: return asmjit::x86::r14;

			// R15 family
			case ZYDIS_REGISTER_R15B:
			case ZYDIS_REGISTER_R15W:
			case ZYDIS_REGISTER_R15D:
			case ZYDIS_REGISTER_R15: return asmjit::x86::r15;

			default: break;
		}

	};

	// Let's translate an instruction to the MiniVm bytecode from Ryujin
	auto translateToMiniVmBytecode = [&](ZydisRegister reg, ZyanU8 op, ZyanU64 value) {

		ZyanU64 miniVmByteCode = 0;

		switch (reg) {

			case ZYDIS_REGISTER_EAX:
			case ZYDIS_REGISTER_RAX: {

				miniVmByteCode = 0x33; // reg = RAX
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RBX: {

				miniVmByteCode = 0x34; // reg = RBX
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RCX: {
			
				miniVmByteCode = 0x35; // reg = RCX
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RDX: {
			
				miniVmByteCode = 0x36; // reg = RDX
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RSI: {
			
				miniVmByteCode = 0x37; // reg = RSI
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RDI: {
			
				miniVmByteCode = 0x38; // reg = RDI
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RBP: {
			
				miniVmByteCode = 0x39; // reg = RBP
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_RSP: {
			
				miniVmByteCode = 0x40; // reg = RSP
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R8: {

				miniVmByteCode = 0x41; // reg = R8
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R9: { 

				miniVmByteCode = 0x42; // reg = R9
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R10: { 

				miniVmByteCode = 0x43; // reg = R10
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R11: { 
				
				miniVmByteCode = 0x44; // reg = R11
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R12: { 
				
				miniVmByteCode = 0x45; // reg = R12
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R13: { 
				
				miniVmByteCode = 0x46; // reg = R13
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R14: { 
				
				miniVmByteCode = 0x47; // reg = R14
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}
			case ZYDIS_REGISTER_R15: { 
			
				miniVmByteCode = 0x48; // reg = R15
				miniVmByteCode <<= 8;
				miniVmByteCode |= op; // OP TYPE
				miniVmByteCode <<= 8;
				miniVmByteCode |= value; // valor

				break;
			}

			default: break;
		}

		return miniVmByteCode;
	};

	// Initializing the asmjit runtime
	asmjit::JitRuntime runtime;

	for (auto& block : m_proc.basic_blocks) {
		
		for (auto& instr : block.instructions) {
			
			// Vector to store the MiniVm opcodes from Ryujin
			std::vector<ZyanU8> minivm_enter;

			// Operand type
			ZyanU8 opType = 0;

			// Finding the block info for the current opcode
			auto block_info = findBlockId(instr.instruction.info.opcode, instr.instruction.operands[1].imm.value.u, 2, sizeof(unsigned char));

			// If not found
			if (block_info.first == -1 || block_info.second == -1) continue;

			// Retrieving the original opcodes of the instruction we're working on
			auto& data = m_proc.basic_blocks[block_info.first].opcodes[block_info.second];

			// Checking for operands that are candidates to be virtualized by the minivm
			if (instr.instruction.info.mnemonic == ZYDIS_MNEMONIC_ADD && isValidToSRyujinMiniVm(instr)) opType = 1;
			else if (instr.instruction.info.mnemonic == ZYDIS_MNEMONIC_SUB && isValidToSRyujinMiniVm(instr)) opType = 2;
			else if (instr.instruction.info.mnemonic == ZYDIS_MNEMONIC_IMUL && isValidToSRyujinMiniVm(instr)) opType = 3;
			else if (instr.instruction.info.mnemonic == ZYDIS_MNEMONIC_DIV && isValidToSRyujinMiniVm(instr)) opType = 4;

			// Is there a new VM Operator?
			if (opType != 0) {

				/*
					Encrypting the PEB acessing fields
				*/
				std::mt19937 rng(std::random_device{}());
				// A single random value of 2 bytes (uint16_t)
				std::uniform_int_distribution<uint16_t> dist(0, 0xFFFFF);
				uint16_t xorKey = dist(rng);

				// Obfuscate PEB offset from automatic scan
				// Xoring PEB Offset
				unsigned char PebGsOffset = 0x60 ^ (xorKey & 0xFF);
				// Xoring ImageBase offset
				unsigned char ImageBasePeb = 0x10 ^ (xorKey & 0xFF);
				// Xoring MiniVM Bytecode
				ZyanU64 vmByteCode = translateToMiniVmBytecode(instr.instruction.operands[0].reg.value, opType, instr.instruction.operands[1].imm.value.u) ^ xorKey;

				// Initializing asmjit to generate our minivm instructions
				asmjit::CodeHolder code;
				code.init(runtime.environment());
				asmjit::x86::Assembler a(&code);

				// Saving the current value of RCX
				a.push(asmjit::x86::rcx);
				// Saving the current value of RDX
				a.push(asmjit::x86::rdx);
				// Storing in the first argument RCX the value of the register from the first operand of the mathematical operation
				a.mov(asmjit::x86::rcx, mapZydisToAsmjitGp(instr.instruction.operands[0].reg.value));
				// Storing in the second argument RDX the value of the bytecode sequence to be interpreted by the Ryujin MiniVM
				a.mov(asmjit::x86::rdx, vmByteCode);
				// Xor key for mini vmbytecode
				a.xor_(asmjit::x86::rdx, asmjit::imm(xorKey));
				// Using `rdgsbase rax` to store the base address of the GS segment in RAX
				a.emit(asmjit::x86::Inst::kIdRdgsbase, asmjit::x86::rax);
				// Adding to RAX the offset value for the PEB Xored
				a.add(asmjit::x86::rax, PebGsOffset);
				// Undoing the XOR operation with the obfuscated RAX value and the XOR key -> xor rax, lastByteXorKey
				a.xor_(asmjit::x86::rax, asmjit::imm(xorKey & 0xFF));
				// Accessing the resulting address to retrieve the PEB instance -> mov rax, [rax]
				a.mov(asmjit::x86::rax, asmjit::x86::ptr(asmjit::x86::rax));
				// Adding the obfuscated offset of the ImageBase xored field in the PEB -> add rax, ImageBasePeb
				a.add(asmjit::x86::rax, ImageBasePeb);
				// Undoing the XOR operation with the obfuscated value and the XOR key -> xor rax, lastByteXorKey
				a.xor_(asmjit::x86::rax, asmjit::imm(xorKey & 0xFF));
				// Accessing the "ImageBase" address in the PEB to obtain the actual value
				a.mov(asmjit::x86::rax, asmjit::x86::ptr(asmjit::x86::rax));
				// Adding to the "ImageBase" value a "default" offset that will later be overwritten by the actual offset of the MiniVM enter
				a.add(asmjit::x86::rax, asmjit::imm(0x88));
				// Calling the MiniVMEnter procedure to execute
				a.call(asmjit::x86::rax);
				// Storing the result of the MiniVM execution stored in RAX into the correct register to continue the normal execution flow
				a.mov(mapZydisToAsmjitGp(instr.instruction.operands[0].reg.value), asmjit::x86::rax);
				// Restoring the original value of RDX
				a.pop(asmjit::x86::rdx);
				// Restoring the original value of RCX
				a.pop(asmjit::x86::rcx);

				// Retrieving from ASMJIT’s JIT the resulting opcodes generated by our algorithm
				auto& opcodeBuffer = code.sectionById(0)->buffer();
				const auto pOpcodeBuffer = opcodeBuffer.data();
				minivm_enter.reserve(opcodeBuffer.size());

				// Storing each individual opcode in our minivm vector
				for (auto i = 0; i < opcodeBuffer.size(); ++i) minivm_enter.push_back(static_cast<ZyanU8>(pOpcodeBuffer[i]));

				// Overwriting old opcodes with the new ones
				data.assign(minivm_enter.begin(), minivm_enter.end());

				std::printf("[!] Inserting a new MiniVm ByteCode on %s\n", instr.instruction.text);

			}

		}
	
	}

}

void RyujinObfuscationCore::insertAntiDebug() {

	BOOL isInserted{ FALSE };

	for (auto& block : m_proc.basic_blocks) {

		for (auto& instr : block.instructions) {

			if (!isInserted) {

				// 1º Inserir a stub que vai carregar o shellcode via stack
				// 2º usar virtual alloc
				// 3º criar uma thread escondida do debugger para executar o shellcode com o antidebug ou antidebug + trollreversers
				// ACESSAR PEB RECUPERAR ESSES MODULOS MANUALMENTE ? sad. mas é parecido como o Themida e suas detecções funcionam.

				if (this->m_config.m_isTrollRerversers) {

					// IstrollReversers é o antidebug convencional mas com a capacidade de trigar tela azul via hard error

					std::printf("Run m_isAntiDebug + m_isTrollRerversers\n");

				}
				else {

					// Is Antidebug é o antidebug convencional que só encerrara a execução completa do binário protegido
				
					std::printf("Run m_isAntiDebug\n");
				
				}

				isInserted = TRUE;
			}

		}
	
	}

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

	if (m_config.m_isAntiDebug) {

		// Insert AntiDebug
		this->insertAntiDebug();

		// Update our basic blocks context to rela 1-1 for the new obfuscated opcodes.
		this->updateBasicBlocksContext();

	}

	if (m_config.m_isVirtualized) {

		// Insert Virtualization
		insertVirtualization();

		//Update our basic blocks context to rely 1-1 for the new obfuscated opcodes.
		this->updateBasicBlocksContext();

	}

	//Obfuscate IAT for the configured procedures
	if (m_config.m_isIatObfuscation) {

		// Obfuscate IAT
		obfuscateIat();

		//Update our basic blocks context to rely 1-1 for the new obfuscated opcodes.
		this->updateBasicBlocksContext();

	}

	if (m_config.m_isJunkCode) {

		// Insert junk code
		insertJunkCode();

		//Update our basic blocks context to rely 1-1 for the new obfuscated opcodes.
		this->updateBasicBlocksContext();

	}

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

void RyujinObfuscationCore::InsertMiniVmEnterProcedureAddress(uintptr_t imageBase, uintptr_t virtualAddress, std::vector<unsigned char>& new_opcodes) {

	//Inserting Ryujin MiniVm Address on each vm entry reference
	if (m_config.m_isVirtualized) {
		
		// Data and sizes of the opcodes to be worked on
		auto size = new_opcodes.size();
		auto data = new_opcodes.data();

		// Signature of the pattern that we must replace with the referenced RVA of our MiniVmEntry
		unsigned char ucSignature[]{ 0x48, 0x05, 0x88, 0x00, 0x00, 0x00 };

		// Let's search for the pattern to replace
		for (auto i = 0; i < size; i++)

			// If we find it
			if (std::memcmp(&*(data + i), ucSignature, 6) == 0) {

				// Just log it
				std::printf("[OK] Inserting MiniVmEnter at %llx\n", imageBase + virtualAddress + i);
				
				// We will remove the value 0x88 and ensure there are no other offsets
				std::memset(&*(data + i + 2), 0, 4);

				// Finally, we will insert our new RVA for the MiniVmEntry procedure
				std::memcpy(&*(data + i + 2), &virtualAddress, sizeof(uint32_t));
			
			}
	
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