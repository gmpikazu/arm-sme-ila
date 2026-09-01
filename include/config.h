#pragma once

// NOTE: activates MemState DRAM instead of UF for reading
#define USE_DRAM_MEMSTATE true // true means use DRAM MemState, DRAM_UF is ignored
#define BIG_DRAM_ADDR_WIDTH 128 // very large, to see if MemState will timeout or not

#define FAULTS_ADDR_WIDTH 8 // big enough to prevent fault count overflow, adjust as needed

/* NOTE: TEMP_OPCODE only works with UnrollPathConn 
 * since exactly one instruction is unrolled at each step
 * UnrollMonoConn needs one opcode per instruction
 */
#define TEMP_OPCODE 0xBAD // some random number, used by ALL instructions
#define CMD_ADDR_WIDTH 32 // must be big enough to fit the opcodes
