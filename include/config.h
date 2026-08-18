#pragma once

// NOTE: activates MemState DRAM instead of UF for reading
#define USE_DRAM_MEMSTATE true

// TODO: need to change all usage sites
#define TEMP_DECODE BoolConst(true)
#define TEMP_OPCODE 0x01
#define TEMP_BIT_WIDTH 128
#define TEMP_LARGEST_ADDR_WIDTH 256
