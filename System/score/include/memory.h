#pragma once
#include <cstdint>

uint32_t pmem_read(uint32_t addr, int len);
void pmem_write(uint32_t addr, int len, uint32_t data);