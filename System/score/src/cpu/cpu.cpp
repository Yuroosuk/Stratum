#include "cpu.h"

const char* GPR_NAMES[32] = {
    "zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
    "s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
    "a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
    "s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
};

CPU_state cpu = {};

const char* CPU_state::get_reg_name(int reg_idx)const
{
    if(reg_idx>=0 && reg_idx < 32)
    {
        return GPR_NAMES[reg_idx];
    }
    return "unknown";
}