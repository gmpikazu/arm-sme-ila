#pragma once

// NOTE: activates MemState DRAM instead of UF for reading
#define USE_DRAM_MEMSTATE true
#define BIG_DRAM_ADDR_WIDTH 128 // very large, to see if MemState will break or not

// TODO: need to change all usage sites
#define TEMP_DECODE BoolConst(true)
#define TEMP_OPCODE 0x01 // some random number, used by ALL instructions
#define TEMP_BIT_WIDTH 128
#define TEMP_LARGEST_ADDR_WIDTH 256
