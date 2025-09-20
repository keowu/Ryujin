#include "BasicBlockerBuilder.hh"

bool RyujinBasicBlockerBuilder::isControlFlow(const ZydisDecodedInstruction& instruction) const {

	return (instruction.meta.category == ZYDIS_CATEGORY_CALL ||
		instruction.meta.category == ZYDIS_CATEGORY_RET ||
		instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
		instruction.meta.category == ZYDIS_CATEGORY_COND_BR ||
		instruction.meta.category == ZYDIS_CATEGORY_SYSTEM ||
		instruction.meta.category == ZYDIS_CATEGORY_INTERRUPT ||
		instruction.meta.category == ZYDIS_CATEGORY_SYSCALL);
}

RyujinBasicBlockerBuilder::RyujinBasicBlockerBuilder(ZydisMachineMode machine, ZydisStackWidth stack) {

	::ZydisDecoderInit(

		_Inout_ &decoder,
		_In_ machine,
		_In_ stack

	);

}

std::vector<RyujinBasicBlock> RyujinBasicBlockerBuilder::createBasicBlocks(const unsigned char* chOpcode, size_t szOpcode, uintptr_t start_address) {

	std::vector<RyujinBasicBlock> blocks;

	std::size_t offset = 0;
	uintptr_t curr_addr = start_address;

	while (offset < szOpcode) {

		RyujinBasicBlock block;
		block.start_address = start_address;

		std::size_t inner = offset;
		while (inner < szOpcode) {

			ZydisDisassembledInstruction instr;
			if (!ZYAN_SUCCESS(::ZydisDisassembleIntel(
				
				_In_ ZYDIS_MACHINE_MODE_LONG_64,
				_In_ curr_addr + (inner - offset),
				_In_ chOpcode + inner,
				_In_ szOpcode - inner,
				_Out_ &instr

			))) {

				std::printf(
					
					"RyujinBasicBlockerBuilder::createBasicBlocks: Zydis disam failed %llx\n",
					inner
				
				);

				break;
			}

			//Storing the instruction relative to the basic block
			RyujinInstruction inst;
			inst.addressofinstruction = curr_addr + (inner - offset);
			inst.instruction = instr;

			block.instructions.push_back(
				
				inst
			
			);

			//Storing the original opcodes from basic block
			block.opcodes.emplace_back(

				chOpcode + inner,
				chOpcode + inner + instr.info.length

			);

			inner += instr.info.length;

			//Theres a branch ? so let's preparate the next block
			if (isControlFlow(
				
				instr.info
			
			)) break;

		}

		block.end_address = curr_addr + (inner - offset);
		blocks.push_back(std::move(block));

		curr_addr = block.end_address;
		offset = inner;
	}

	return blocks;
}