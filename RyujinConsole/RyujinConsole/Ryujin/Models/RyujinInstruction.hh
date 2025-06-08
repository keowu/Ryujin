#pragma once

struct RyujinInstruction {

    ZydisDisassembledInstruction instruction;
    uintptr_t addressofinstruction;

};