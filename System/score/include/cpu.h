#pragma once
#include <cstdint>

struct CPU_state {
    uint32_t gpr[32];
    uint32_t pc;

    const char* get_reg_name(int reg_idx)const;
};

extern const char* GPR_NAMES[32];

extern CPU_state cpu;