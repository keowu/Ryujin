#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "RyujinCore.hh"

// MBA Obfuscation Pass
#include <z3++.h>
#include <Zydis/Zydis.h>
#include <asmjit/asmjit.h>
#include <cstdio>
#include <stdexcept>
#include <cstdint>
#include <random>

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

    // Traduzindo os registradores do Zydis para Registradores do ASMJIT
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

    std::printf("[RyujinMBAObfuscationPass] Processando equivalencia MBA em %s\n", proc->name.c_str());

    // Iniciando decoder
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    // Begin z3 context e unique block id
    z3::context ctx;
    uint64_t unique_id = 0;

    for (auto& block : proc->basic_blocks) {

        std::vector<std::vector<ZyanU8>> new_instructions;
        new_instructions.reserve(block.opcodes.size());

        for (auto& opcode : block.opcodes) {

            // Decodificando instruções com base nos opcodes armazenados em nossos basic blocks(sempre com o contexto atualizado)
            ZydisDecodedInstruction instruction{};
            std::vector<ZydisDecodedOperand> operands(ZYDIS_MAX_OPERAND_COUNT);
            std::memset(operands.data(), 0, operands.size() * sizeof(ZydisDecodedOperand));

            ZyanStatus status = ZydisDecoderDecodeFull(&decoder, opcode.data(), opcode.size(), &instruction, operands.data());
            if (!ZYAN_SUCCESS(status)) {

                new_instructions.push_back(opcode);

                continue;
            }


            // Checando se a instrução atual é um candidato para ter uma nova expression MBA(apenas com operações aritimeticas básicas)
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

                // Validação da semantica MBA...

                // Checando equivalencia(semantica) da expressão com o Z3 para garantir que a expression inserida não quebre posteriormente.
                std::string xs_name = "x_" + std::to_string(unique_id);
                std::string ys_name = "y_" + std::to_string(unique_id);
                unique_id++;

                z3::symbol sx = ctx.str_symbol(xs_name.c_str());
                z3::symbol sy = ctx.str_symbol(ys_name.c_str());

                z3::sort bv64 = ctx.bv_sort(64);
                z3::expr x = ctx.constant(sx, bv64);
                z3::expr y = ctx.constant(sy, bv64);

                z3::expr target = ctx.bv_val(0ULL, 64);

                switch (instruction.mnemonic) {

                    case ZYDIS_MNEMONIC_ADD: target = x + y; break;
                    case ZYDIS_MNEMONIC_SUB: target = x - y; break;
                    case ZYDIS_MNEMONIC_XOR: target = x ^ y; break;
                    case ZYDIS_MNEMONIC_AND: target = x & y; break;
                    case ZYDIS_MNEMONIC_OR:  target = x | y; break;
                    default: target = x;

                }

                // MBA's expressions para ofuscação
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

                // RNG Deterministico por unique_id para teste de expressions
                std::mt19937 gen(static_cast<uint32_t>(unique_id));
                std::uniform_int_distribution<size_t> dist(0, obf_variants.size() - 1);
                size_t variant_idx = dist(gen);
                z3::expr obf = obf_variants[variant_idx];

                z3::solver solver(ctx);

                // Verificando se a expression MBA é válida
                solver.add(obf != target);
                if (solver.check() != z3::unsat) {

                    std::cout << "[RyujinMBAObfuscationPass] Validação de expressões de mesma semantica com o Z3 retornou unsat para o procedimento. não tem uma equivalencia matematica para reescrever como mba de forma segura...\n";
                    new_instructions.push_back(opcode);

                    continue;
                }

                try {

                    // Insertion das expressions MBA...

                    // Preparando expressions MBA validadas anteriormente e gerando novas instruções para reproduzirem o mesmo resultado com a teoria de MBA
                    asmjit::JitRuntime rt;
                    asmjit::CodeHolder code;
                    code.init(rt.environment(), rt.cpuFeatures());
                    asmjit::x86::Assembler a(&code);

                    asmjit::x86::Gp dest64 = get_asm_reg_64(operands[0].reg.value);
                    asmjit::x86::Gp tmp_x = asmjit::x86::rcx;
                    asmjit::x86::Gp tmp_y = asmjit::x86::rdx;
                    asmjit::x86::Gp tmp_tmp = asmjit::x86::r8;
                    asmjit::x86::Gp tmp_extra = asmjit::x86::r9;

                    // Salvando contexto
                    a.push(asmjit::x86::rax);
                    a.push(asmjit::x86::rcx);
                    a.push(asmjit::x86::rdx);
                    a.push(asmjit::x86::r8);
                    a.push(asmjit::x86::r9);

                    a.mov(tmp_x, dest64);

                    if (is_src_reg) {

                        asmjit::x86::Gp src64 = get_asm_reg_64(operands[1].reg.value);
                        a.mov(tmp_y, src64);

                    }
                    else if (is_src_imm) {

                        uint64_t imm = operands[1].imm.value.u;
                        a.mov(tmp_y, imm);

                    }

                    // Sanity Check: Alinhando instruções para evitar desalinhamentos
                    a.align(asmjit::AlignMode::kCode, 16);

                    // Inserindo expressions MBA para cada mnemonic suportado
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
                        
                            // ~(~x + ~y) + 1  -> mesmo que x + y
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
                        
                            // ~(~x | ~y) => Igual x & y
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

                    // Recuperando contexto...
                    a.pop(asmjit::x86::r9);
                    a.pop(asmjit::x86::r8);
                    a.pop(asmjit::x86::rdx);
                    a.pop(asmjit::x86::rcx);
                    a.pop(asmjit::x86::rax);

                    // Gerando novos opcodes processados pelo algoritmo de MBA
                    asmjit::Section* section = code.sectionById(0);
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

                    // Apenas se a semantica foi inválidada ou alguma instrução não ser semanticamente esperada...
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


auto print_help() -> void {

    std::cout << R"(Ryujin Obfuscator CLI

Usage:
  RyujinConsole.exe --input <exe_path> --pdb <pdb_path> --output <output_path> [options]

Options:
  --input <exe>             Input binary to obfuscate (required)
  --pdb <pdb>               Path to the PDB file (required)
  --output <exe>            Output path for obfuscated binary (required)
  --virtualize              Enable virtualization
  --junk                    Add junk code
  --encrypt                 Encrypt obfuscated code
  --iat                     Enable IAT obfuscation
  --random-section          Use random PE section
  --keep-original           Keep original code (don't remove it)
  --AntiDebug               Inserts anti-debugging capabilities and terminates the protected binary if a debugger is detected.
  --Troll                   Crashes the entire OS if a debugger is detected (requires --AntiDebug).
  --AntiDump                Inserts anti-dump mechanisms that break the binary in memory, making dumps harder to analyze.
  --MemoryProtection        Protects obfuscated code against in-memory or on-disk patching.
  --HVPass                  Protect some parts of Ryujin using Microsoft Hypervisor APIs
  --procs <comma,separated,names>  Procedures to obfuscate (default: main, invoke_main, ...)

  --help                    Show this help message

In Action Usage Example:
    RyujinConsole.exe --input C:\\Users\\Keowu\\Documents\\GitHub\\Ryujin\\compiled\\release\\DemoObfuscation.exe --pdb C:\\Users\\Keowu\\Documents\\GitHub\\Ryujin\\compiled\\release\\RyujinConsole.pdb --output C:\\Users\\Keowu\\Documents\\GitHub\\Ryujin\\compiled\\release\\DemoObfuscation.ryujin.exe --virtualize --junk --encrypt --AntiDebug --troll --AntiDump --iat --HVPass --procs main,sub,subadd,sum,invoke_main,__scrt_common_main,j___security_init_cookie

)";

}

auto has_flag(const std::unordered_map<std::string, std::string>& args, const std::string& flag) -> bool {

    return args.find(flag) != args.end();
}

auto parse_args(int argc, char* argv[]) -> std::unordered_map<std::string, std::string> {

    std::unordered_map<std::string, std::string> options;
    for (int i = 1; i < argc; ++i) {
    
        std::string key = argv[i];
        if (key.rfind("--", 0) == 0) 
            if (i + 1 < argc && argv[i + 1][0] != '-') options[key] = argv[++i]; else options[key] = "true"; // Flag-only
        
    }

    return options;
}

auto main(int argc, char* argv[]) -> int {

    auto args = parse_args(argc, argv);

    if (has_flag(args, "--help") || argc == 1) {

        print_help();
        
        return 0;
    }

    auto input = args["--input"];
    auto pdb = args["--pdb"];
    auto output = args["--output"];

    if (input.empty() || pdb.empty() || output.empty()) {
        
        std::cerr << "Error: --input, --pdb, and --output are required.\n";
        print_help();
        
        return 0;
    }

    RyujinObfuscatorConfig config;
    config.m_isIgnoreOriginalCodeRemove = has_flag(args, "--keep-original");
    config.m_isJunkCode = has_flag(args, "--junk");
    config.m_isRandomSection = has_flag(args, "--random-section");
    config.m_isVirtualized = has_flag(args, "--virtualize");
    config.m_isIatObfuscation = has_flag(args, "--iat");
    config.m_isEncryptObfuscatedCode = has_flag(args, "--encrypt");
    config.m_isTrollRerversers = has_flag(args, "--troll");
    config.m_isAntiDebug = has_flag(args, "--AntiDebug");
    config.m_isAntiDump = has_flag(args, "--AntiDump");
    config.m_isMemoryProtection = has_flag(args, "--MemoryProtection");
    config.m_isHVPass = has_flag(args, "--HVPass");

    // Registering a new custom pass for invocation via callback
    config.RegisterCallback(RyujinCustomPassDemo);

    config.RegisterCallback(RyujinMBAObfuscationPass);

    if (has_flag(args, "--procs")) {
        auto rawList = args["--procs"];
        size_t start = 0;
        size_t end = 0;
        int index = 0;

        while ((end = rawList.find(',', start)) != std::string::npos && index < MAX_PROCEDURES) {
            auto procName = rawList.substr(start, end - start);
            strncpy_s(config.m_strProceduresToObfuscate.procedures[index], procName.c_str(), MAX_PROCEDURE_NAME_LEN - 1);
            ++index;
            start = end + 1;
        }

        if (index < MAX_PROCEDURES) {
            auto procName = rawList.substr(start);
            strncpy_s(config.m_strProceduresToObfuscate.procedures[index], procName.c_str(), MAX_PROCEDURE_NAME_LEN - 1);
            ++index;
        }

        config.m_strProceduresToObfuscate.procedureCount = index;
    }
    else {
        print_help();
        return 0;
    }

    auto bSuccess = config.RunRyujin(input, pdb, output, config);
    std::printf("Ryujin core returned: %d\n", bSuccess);

    return bSuccess;
}
