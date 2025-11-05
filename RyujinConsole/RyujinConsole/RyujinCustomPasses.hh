#include <z3++.h>
#include <Zydis/Zydis.h>
#include <asmjit/asmjit.h>
#include <cstdio>
#include <stdexcept>
#include <cstdint>
#include <random>

namespace RyujinCustomPasses {

    void RyujinCustomPassDemo(RyujinProcedure* proc) {

        /*
                This is a sample callback that demonstrates how Ryujin users can register callbacks during its operation/integration to be used with the RyujinCore engine.
                This callback provides direct access to the RyujinProcedure class, which encapsulates all the logic of basic blocks and information about each obfuscated procedure,
                allowing modifications and extensions limited only by the implementer's creativity.
                In this example, the registered callback is used solely to display relevant information about each procedure to be obfuscated, as a demonstration.

                A sample output:
                ----------------------------------------------
                RyujinCustomPassDemo get called for subadd
                subadd has 31 bytes, resides on 0x7ff6c7fd1100, with 1 basic blocks.
                Instructions:
                mov [rsp+0x08], ecx
                mov eax, [rsp+0x08]
                add eax, 0x0A
                mov [rsp+0x08], eax
                mov eax, [rsp+0x08]
                sub eax, 0x02
                mov [rsp+0x08], eax
                mov eax, [rsp+0x08]
                ret
                ----------------------------------------------
        */

        std::printf("----------------------------------------------\n");
        std::printf("RyujinCustomPassDemo get called for %s\n", proc->name.c_str());
        std::printf("%s has %lld bytes, resides on 0x%llx, with %llx basic blocks.\n", proc->name.c_str(), proc->size, proc->address, proc->basic_blocks.size());

        std::printf("Instructions:\n");

        for (auto& block : proc->basic_blocks)
            for (auto& inst : block.instructions)
                std::printf("%s\n", inst.instruction.text);

        std::printf("----------------------------------------------\n");

    }

    void RyujinMBAObfuscationPass(RyujinProcedure* proc) {

        /*
            This is a custom obfuscation pass callback that adds MBA-Linear support to the Ryujin Core, allowing
            mathematical operators in certain procedures with the "mba_" prefix to be obfuscated. This ensures
            equivalence between the original and obfuscated expressions, increasing analysis difficulty without
            breaking the core execution logic. SMT SOLVER - Z3-C
        
            Currently supports the following linear equivalence instructions: SUB, ADD, XOR, AND, and OR.
        */
        // Translating Zydis registers to ASMJIT registers
        auto get_asm_reg_64 = [&](ZydisRegister z_reg) -> asmjit::x86::Gp {

            switch (z_reg) {

                case ZYDIS_REGISTER_RAX: return asmjit::x86::rax;
                case ZYDIS_REGISTER_RBX: return asmjit::x86::rbx;
                case ZYDIS_REGISTER_RCX: return asmjit::x86::rcx;
                case ZYDIS_REGISTER_RDX: return asmjit::x86::rdx;
                case ZYDIS_REGISTER_RSI: return asmjit::x86::rsi;
                case ZYDIS_REGISTER_RDI: return asmjit::x86::rdi;
                case ZYDIS_REGISTER_R8:  return asmjit::x86::r8;
                case ZYDIS_REGISTER_R9:  return asmjit::x86::r9;
                case ZYDIS_REGISTER_R10: return asmjit::x86::r10;
                case ZYDIS_REGISTER_R11: return asmjit::x86::r11;
                case ZYDIS_REGISTER_R12: return asmjit::x86::r12;
                case ZYDIS_REGISTER_R13: return asmjit::x86::r13;
                case ZYDIS_REGISTER_R14: return asmjit::x86::r14;
                case ZYDIS_REGISTER_R15: return asmjit::x86::r15;

                case ZYDIS_REGISTER_EAX: return asmjit::x86::rax;
                case ZYDIS_REGISTER_EBX: return asmjit::x86::rbx;
                case ZYDIS_REGISTER_ECX: return asmjit::x86::rcx;
                case ZYDIS_REGISTER_EDX: return asmjit::x86::rdx;
                case ZYDIS_REGISTER_ESI: return asmjit::x86::rsi;
                case ZYDIS_REGISTER_EDI: return asmjit::x86::rdi;
                case ZYDIS_REGISTER_R8D: return asmjit::x86::r8;
                case ZYDIS_REGISTER_R9D: return asmjit::x86::r9;
                case ZYDIS_REGISTER_R10D: return asmjit::x86::r10;
                case ZYDIS_REGISTER_R11D: return asmjit::x86::r11;
                case ZYDIS_REGISTER_R12D: return asmjit::x86::r12;
                case ZYDIS_REGISTER_R13D: return asmjit::x86::r13;
                case ZYDIS_REGISTER_R14D: return asmjit::x86::r14;
                case ZYDIS_REGISTER_R15D: return asmjit::x86::r15;

            }

            // Fallback
            return asmjit::x86::rax;
        };

        if (proc->name.find("mba_") == std::string::npos) return;

        std::printf("[RyujinMBAObfuscationPass] Processando equivalencia MBA em %s\n", proc->name.c_str());

        // Starting decoder
        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

        // Begin z3 context and unique block id
        z3::context ctx;
        uint64_t unique_id = 0;

        for (auto& block : proc->basic_blocks) {

            std::vector<std::vector<ZyanU8>> new_instructions;
            new_instructions.reserve(block.opcodes.size());

            for (auto& opcode : block.opcodes) {

                // Decoding instructions based on the opcodes stored in our basic blocks (always with the context updated)
                ZydisDecodedInstruction instruction{};
                std::vector<ZydisDecodedOperand> operands(ZYDIS_MAX_OPERAND_COUNT);
                std::memset(operands.data(), 0, operands.size() * sizeof(ZydisDecodedOperand));

                auto status = ZydisDecoderDecodeFull(&decoder, opcode.data(), opcode.size(), &instruction, operands.data());
                if (!ZYAN_SUCCESS(status)) {

                    new_instructions.push_back(opcode);

                    continue;
                }


                // Checking if the current instruction is a candidate to have a new MBA expression (only with basic arithmetic operations)
                bool isMbaRewritten = false;

                if ((instruction.mnemonic == ZYDIS_MNEMONIC_ADD || instruction.mnemonic == ZYDIS_MNEMONIC_SUB || instruction.mnemonic == ZYDIS_MNEMONIC_XOR ||
                    instruction.mnemonic == ZYDIS_MNEMONIC_AND || instruction.mnemonic == ZYDIS_MNEMONIC_OR)
                    && instruction.operand_count_visible == 2
                    && operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER) {

                    auto is_src_reg = (operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER);
                    auto is_src_imm = (operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE);
                    auto is_src_mem = (operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY);

                    if (is_src_mem) {

                        new_instructions.push_back(opcode);

                        continue;
                    }

                    // Validating MBA semantics...

                    // Checking semantic equivalence of the expression with Z3 to ensure the inserted expression won't break later.
                    std::string xs_name = "x_" + std::to_string(unique_id);
                    std::string ys_name = "y_" + std::to_string(unique_id);
                    unique_id++;

                    auto sx = ctx.str_symbol(xs_name.c_str());
                    auto sy = ctx.str_symbol(ys_name.c_str());

                    auto bv64 = ctx.bv_sort(64);
                    auto x = ctx.constant(sx, bv64);
                    auto y = ctx.constant(sy, bv64);

                    auto target = ctx.bv_val(0ULL, 64);

                    switch (instruction.mnemonic) {

                        case ZYDIS_MNEMONIC_ADD: target = x + y; break;
                        case ZYDIS_MNEMONIC_SUB: target = x - y; break;
                        case ZYDIS_MNEMONIC_XOR: target = x ^ y; break;
                        case ZYDIS_MNEMONIC_AND: target = x & y; break;
                        case ZYDIS_MNEMONIC_OR:  target = x | y; break;
                        default: target = x;

                    }

                    // MBA expressions for obfuscation
                    std::vector<z3::expr> obf_variants;
                    switch (instruction.mnemonic) {

                    case ZYDIS_MNEMONIC_ADD:

                        obf_variants.push_back((x ^ y) + z3::shl(x & y, ctx.bv_val(1ULL, 64)));
                        obf_variants.push_back((x | y) + (x & y));
                        obf_variants.push_back(~(~x + ~y) + ctx.bv_val(1ULL, 64));

                        break;
                    case ZYDIS_MNEMONIC_SUB:

                        obf_variants.push_back(x + (~y + ctx.bv_val(1ULL, 64)));
                        obf_variants.push_back((x ^ y) - z3::shl(~x & y, ctx.bv_val(1ULL, 64)));
                        obf_variants.push_back(~(y - ctx.bv_val(1ULL, 64)) - ~x);

                        break;
                    case ZYDIS_MNEMONIC_XOR:

                        obf_variants.push_back((x + y) - z3::shl(x & y, ctx.bv_val(1ULL, 64)));
                        obf_variants.push_back((x | y) - (x & y));
                        obf_variants.push_back(~(x & y) - ~(x | y) + ctx.bv_val(2ULL, 64));

                        break;
                    case ZYDIS_MNEMONIC_AND:

                        obf_variants.push_back((x + y) - (x | y));
                        obf_variants.push_back(~(~x | ~y));
                        obf_variants.push_back((x ^ y) + (x | y) - (x + y));

                        break;
                    case ZYDIS_MNEMONIC_OR:

                        obf_variants.push_back(x + y - (x & y));
                        obf_variants.push_back(~(~x & ~y));
                        obf_variants.push_back((x & y) + (x ^ y));

                        break;

                    }

                    // Sanity Check
                    if (obf_variants.empty()) {

                        new_instructions.push_back(opcode);

                        continue;
                    }

                    // Deterministic RNG by unique_id for expression testing
                    std::mt19937 gen(static_cast<uint32_t>(unique_id));
                    std::uniform_int_distribution<size_t> dist(0, obf_variants.size() - 1);
                    size_t variant_idx = dist(gen);
                    auto obf = obf_variants[variant_idx];

                    z3::solver solver(ctx);

                    // Verifying if the MBA expression is valid
                    solver.add(obf != target);
                    if (solver.check() != z3::unsat) {

                        std::cout << "[RyujinMBAObfuscationPass] Validation of semantically equivalent expressions with Z3 returned unsat for the procedure. There is no mathematical equivalence to safely rewrite it as an MBA...\n";
                        new_instructions.push_back(opcode);

                        continue;
                    }

                    try {

                        // Insertion of the MBA expressions...

                        // Preparing previously validated MBA expressions and generating new instructions to reproduce the same result using MBA theory
                        asmjit::JitRuntime rt;
                        asmjit::CodeHolder code;
                        code.init(rt.environment(), rt.cpuFeatures());
                        asmjit::x86::Assembler a(&code);

                        auto dest64 = get_asm_reg_64(operands[0].reg.value);
                        auto tmp_x = asmjit::x86::rcx;
                        auto tmp_y = asmjit::x86::rdx;
                        auto tmp_tmp = asmjit::x86::r8;
                        auto tmp_extra = asmjit::x86::r9;

                        // Save context
                        a.push(asmjit::x86::rax);
                        a.push(asmjit::x86::rcx);
                        a.push(asmjit::x86::rdx);
                        a.push(asmjit::x86::r8);
                        a.push(asmjit::x86::r9);

                        a.mov(tmp_x, dest64);

                        if (is_src_reg) {

                            auto src64 = get_asm_reg_64(operands[1].reg.value);
                            a.mov(tmp_y, src64);

                        }
                        else if (is_src_imm) {

                            auto imm = operands[1].imm.value.u;
                            a.mov(tmp_y, imm);

                        }

                        // Sanity Check: Aligning instructions to avoid misalignments
                        a.align(asmjit::AlignMode::kCode, 16);

                        // Inserting MBA expressions for each supported mnemonic
                        if (instruction.mnemonic == ZYDIS_MNEMONIC_ADD) {

                            if (variant_idx == 0) {

                                // (x ^ y) + shl(x & y,1)
                                a.mov(tmp_tmp, tmp_x);      // tmp_tmp = x
                                a.and_(tmp_tmp, tmp_y);     // tmp_tmp = x & y
                                a.shl(tmp_tmp, 1);          // tmp_tmp = (x & y) << 1
                                a.xor_(tmp_extra, tmp_x);   // tmp_extra = x
                                a.xor_(tmp_extra, tmp_y);   // tmp_extra = x ^ y
                                a.add(tmp_extra, tmp_tmp);  // tmp_extra = (x ^ y) + shl(x & y,1)
                                a.mov(tmp_x, tmp_extra);

                            }
                            else if (variant_idx == 1) {

                                // (x | y) + (x & y)
                                a.mov(tmp_tmp, tmp_x);
                                a.or_(tmp_tmp, tmp_y);      // tmp_tmp = x | y
                                a.mov(tmp_extra, tmp_x);
                                a.and_(tmp_extra, tmp_y);   // tmp_extra = x & y
                                a.add(tmp_tmp, tmp_extra);  // tmp_tmp = (x | y) + (x & y)
                                a.mov(tmp_x, tmp_tmp);

                            }
                            else {

                                // ~(~x + ~y) + 1  -> same as x + y
                                a.mov(tmp_tmp, tmp_x);
                                a.not_(tmp_tmp);            // tmp_tmp = ~x
                                a.mov(tmp_extra, tmp_y);
                                a.not_(tmp_extra);          // tmp_extra = ~y
                                a.add(tmp_tmp, tmp_extra);  // tmp_tmp = ~x + ~y
                                a.not_(tmp_tmp);            // tmp_tmp = ~(~x + ~y)
                                a.add(tmp_tmp, 1);          // +1
                                a.mov(tmp_x, tmp_tmp);

                            }

                            a.mov(dest64, tmp_x);
                        }
                        else if (instruction.mnemonic == ZYDIS_MNEMONIC_SUB) {

                            if (variant_idx == 0) {

                                // x + (~y + 1)  == x - y
                                a.mov(tmp_tmp, tmp_y);      // tmp_tmp = y
                                a.not_(tmp_tmp);            // tmp_tmp = ~y
                                a.add(tmp_tmp, 1);          // tmp_tmp = ~y + 1
                                a.add(tmp_x, tmp_tmp);      // x = x + (~y + 1)

                            }
                            else if (variant_idx == 1) {

                                // (x ^ y) - shl((~x & y),1)
                                a.mov(tmp_tmp, tmp_x);      // tmp_tmp = x
                                a.xor_(tmp_extra, tmp_x);   // tmp_extra = x
                                a.xor_(tmp_extra, tmp_y);   // tmp_extra = x ^ y
                                a.mov(tmp_tmp, tmp_x);
                                a.not_(tmp_tmp);            // tmp_tmp = ~x
                                a.and_(tmp_tmp, tmp_y);     // tmp_tmp = (~x & y)
                                a.shl(tmp_tmp, 1);          // tmp_tmp = shl(~x & y,1)
                                a.sub(tmp_extra, tmp_tmp);  // tmp_extra = (x ^ y) - shl(...)
                                a.mov(tmp_x, tmp_extra);

                            }
                            else {

                                // ~(y - 1) - ~x
                                a.mov(tmp_tmp, tmp_y);      // tmp_tmp = y
                                a.sub(tmp_tmp, 1);          // tmp_tmp = y - 1
                                a.not_(tmp_tmp);            // tmp_tmp = ~(y - 1)
                                a.mov(tmp_extra, tmp_x);    // tmp_extra = x
                                a.not_(tmp_extra);          // tmp_extra = ~x
                                a.sub(tmp_tmp, tmp_extra);  // tmp_tmp = ~(y-1) - ~x
                                a.mov(tmp_x, tmp_tmp);

                            }

                            a.mov(dest64, tmp_x);
                        }
                        else if (instruction.mnemonic == ZYDIS_MNEMONIC_XOR) {

                            if (variant_idx == 0) {

                                // (x + y) - shl(x & y,1)
                                a.mov(tmp_extra, tmp_x);    // tmp_extra = x
                                a.add(tmp_extra, tmp_y);    // tmp_extra = x + y
                                a.mov(tmp_tmp, tmp_x);
                                a.and_(tmp_tmp, tmp_y);     // tmp_tmp = x & y
                                a.shl(tmp_tmp, 1);          // tmp_tmp = shl(x & y,1)
                                a.sub(tmp_extra, tmp_tmp);  // tmp_extra = (x + y) - shl(...)
                                a.mov(tmp_x, tmp_extra);

                            }
                            else if (variant_idx == 1) {

                                // (x | y) - (x & y)
                                a.mov(tmp_tmp, tmp_x);
                                a.or_(tmp_tmp, tmp_y);      // tmp_tmp = x | y
                                a.mov(tmp_extra, tmp_x);
                                a.and_(tmp_extra, tmp_y);   // tmp_extra = x & y
                                a.sub(tmp_tmp, tmp_extra);  // tmp_tmp = (x | y) - (x & y)
                                a.mov(tmp_x, tmp_tmp);

                            }
                            else {

                                // ~(x & y) - ~(x | y) + 2
                                a.mov(tmp_tmp, tmp_x);
                                a.and_(tmp_tmp, tmp_y);     // tmp_tmp = x & y
                                a.not_(tmp_tmp);            // tmp_tmp = ~(x & y)
                                a.mov(tmp_extra, tmp_x);
                                a.or_(tmp_extra, tmp_y);    // tmp_extra = x | y
                                a.not_(tmp_extra);          // tmp_extra = ~(x | y)
                                a.sub(tmp_tmp, tmp_extra);  // tmp_tmp = ~(x&y) - ~(x|y)
                                a.add(tmp_tmp, 2);          // +2
                                a.mov(tmp_x, tmp_tmp);

                            }

                            a.mov(dest64, tmp_x);
                        }
                        else if (instruction.mnemonic == ZYDIS_MNEMONIC_AND) {

                            if (variant_idx == 0) {

                                // (x + y) - (x | y)
                                a.mov(tmp_extra, tmp_x);
                                a.add(tmp_extra, tmp_y);    // tmp_extra = x + y
                                a.mov(tmp_tmp, tmp_x);
                                a.or_(tmp_tmp, tmp_y);      // tmp_tmp = x | y
                                a.sub(tmp_extra, tmp_tmp);  // tmp_extra = (x + y) - (x | y)
                                a.mov(tmp_x, tmp_extra);

                            }
                            else if (variant_idx == 1) {

                                // ~(~x | ~y) => same as x & y
                                a.mov(tmp_tmp, tmp_x);
                                a.not_(tmp_tmp);            // tmp_tmp = ~x
                                a.mov(tmp_extra, tmp_y);
                                a.not_(tmp_extra);          // tmp_extra = ~y
                                a.or_(tmp_tmp, tmp_extra);  // tmp_tmp = ~x | ~y
                                a.not_(tmp_tmp);            // tmp_tmp = ~(~x | ~y)
                                a.mov(tmp_x, tmp_tmp);

                            }
                            else {

                                // (x ^ y) + (x | y) - (x + y)
                                a.mov(tmp_tmp, tmp_x);
                                a.xor_(tmp_tmp, tmp_y);     // tmp_tmp = x ^ y
                                a.mov(tmp_extra, tmp_x);
                                a.or_(tmp_extra, tmp_y);    // tmp_extra = x | y
                                a.add(tmp_tmp, tmp_extra);  // tmp_tmp = (x ^ y) + (x | y)
                                a.mov(tmp_extra, tmp_x);
                                a.add(tmp_extra, tmp_y);    // tmp_extra = x + y
                                a.sub(tmp_tmp, tmp_extra);  // tmp_tmp = ... - (x + y)
                                a.mov(tmp_x, tmp_tmp);

                            }

                            a.mov(dest64, tmp_x);
                        }
                        else if (instruction.mnemonic == ZYDIS_MNEMONIC_OR) {

                            if (variant_idx == 0) {

                                // x + y - (x & y)
                                a.mov(tmp_extra, tmp_x);
                                a.add(tmp_extra, tmp_y);    // tmp_extra = x + y
                                a.mov(tmp_tmp, tmp_x);
                                a.and_(tmp_tmp, tmp_y);     // tmp_tmp = x & y
                                a.sub(tmp_extra, tmp_tmp);  // tmp_extra = x + y - (x & y)
                                a.mov(tmp_x, tmp_extra);

                            }
                            else if (variant_idx == 1) {

                                // ~(~x & ~y) => x | y
                                a.mov(tmp_tmp, tmp_x);
                                a.not_(tmp_tmp);            // tmp_tmp = ~x
                                a.mov(tmp_extra, tmp_y);
                                a.not_(tmp_extra);          // tmp_extra = ~y
                                a.and_(tmp_tmp, tmp_extra); // tmp_tmp = ~x & ~y
                                a.not_(tmp_tmp);            // tmp_tmp = ~(~x & ~y)
                                a.mov(tmp_x, tmp_tmp);

                            }
                            else {

                                // (x & y) + (x ^ y)
                                a.mov(tmp_tmp, tmp_x);
                                a.and_(tmp_tmp, tmp_y);     // tmp_tmp = x & y
                                a.mov(tmp_extra, tmp_x);
                                a.xor_(tmp_extra, tmp_y);   // tmp_extra = x ^ y
                                a.add(tmp_tmp, tmp_extra);  // tmp_tmp = (x & y) + (x ^ y)
                                a.mov(tmp_x, tmp_tmp);

                            }

                            a.mov(dest64, tmp_x);
                        }

                        // Retrieving context...
                        a.pop(asmjit::x86::r9);
                        a.pop(asmjit::x86::r8);
                        a.pop(asmjit::x86::rdx);
                        a.pop(asmjit::x86::rcx);
                        a.pop(asmjit::x86::rax);

                        // Generating new opcodes processed by the MBA algorithm
                        auto section = code.sectionById(0);
                        if (!section || section->buffer().empty()) {

                            new_instructions.push_back(opcode);

                            continue;
                        }

                        const auto& buf = section->buffer();
                        std::vector<ZyanU8> generated(buf.begin(), buf.end());
                        new_instructions.push_back(std::move(generated));
                        isMbaRewritten = true;
                    }
                    catch (const std::exception& ex) {

                        // Only if the semantics were invalidated or some instruction is not semantically expected...
                        std::cerr << "[RyujinMBAObfuscationPass] assembly exception: " << ex.what() << ".\n";
                        new_instructions.push_back(opcode);

                        continue;
                    }
                }

                if (!isMbaRewritten) new_instructions.push_back(opcode);

            }

            // Overwriting opcodes with the new obfuscated ones
            block.opcodes.clear();
            block.opcodes.assign(new_instructions.begin(), new_instructions.end());
        }

    }

};