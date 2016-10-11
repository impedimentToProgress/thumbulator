#ifndef SIMSUPPORT_HEADER
#define SIMSUPPORT_HEADER

#include <stdio.h>

#define RAM_START           0x40000000
#define RAM_SIZE            (1 << 23) // 8 MB
#define RAM_ADDRESS_MASK    (((~0) << 23) ^ (~0))
#define FLASH_START         0x0
#define FLASH_SIZE          (1 << 23) // 8 MB
#define FLASH_ADDRESS_MASK  (((~0) << 23) ^ (~0))
#define CPU_FREQ            24000000
#define MEMMAPIO_START      0x80000000
#define MEMMAPIO_SIZE       (4*19)
#define WATCHPOINT_ADDR     0x80000010

typedef __uint32_t u32;
typedef __uint64_t u64;
typedef __int32_t i32;
typedef __int64_t i64;
typedef __uint8_t u8;
typedef __uint16_t u16;
typedef char bool;

// Core CPU compenents
extern u32 ram[RAM_SIZE >> 2];
extern u32 flash[FLASH_SIZE >> 2];
extern bool takenBranch;    // Informs fetch that previous instruction caused a control flow change
extern void sim_exit(int);  // All sim ends lead through here
void cpu_reset();           // Resets the CPU according to the specification
char simLoadInsn(u32 address, u16 *value);  // All memory accesses one simulation starts should be through these interfaces
char simLoadData(u32 address, u32 *value);
char simLoadData_internal(u32 address, u32 *value, u32 falseRead); // falseRead says whether this is a read due to anything other than the program
char simStoreData(u32 address, u32 value);

// Controls whether the program output prints to the simulator's console or is not printed at all
#define DISABLE_PROGRAM_PRINTING 1

// Simulator debugging
#define PRINT_INST 0                                    // diss_printf(): disassembly printing?
#define PRINT_ALL_STATE 0                               // Print all registers after each instruction? Used for comparing to original Thumbulator.
#define PRINT_STATE_DIFF_INIT (0 & (PRINT_ALL_STATE))   // Print changed registers after each instruction?
#define PRINT_STORES_WITH_STATE (0 & (PRINT_ALL_STATE)) // Print memory written with state updates?
#define PRINT_ALL_MEM 0                                 // Print all memory accesses?
#define PRINT_FLASH_WRITES 0                            // Print all writes to flash?
#define PRINT_RAM_WRITES 0                              // Print all writes to ram?

// Simulator correctness checks: tradeoff speed for safety
#define MEM_CHECKS 0                                    // Check memory access alignment
#define VERIFY_BRANCHES_TAGGED 1                        // Make sure that all control flow changes come from known paths
#define THUMB_CHECK 1                                   // Verify that the PC stays in thumb mode

#define diff_printf(format, ...) do{ fprintf(stderr, "%08X:\t", cpu_get_pc() - 0x5); fprintf(stderr, format, __VA_ARGS__); } while(0)
#define diss_printf(format, ...) do{ if (PRINT_INST) { fprintf(stderr, "%08X:\t", cpu_get_pc() - 0x5); fprintf(stderr, format, __VA_ARGS__); } } while(0)

// Hooks to run code every time a GPR is accessed
#define HOOK_GPR_ACCESSES 1         // Currently set to see if stack crosses heap

// Macros for Ratchet
#define PRINT_CHECKPOINTS 0                 // Print checkpoint info
#define MEM_COUNT_INST 0                    // Track and report program loads, stores, and checkpoints
#define PRINT_MEM_OPS 1                     // Prints detailed info for each program-generated memory access (Clank)
#define INCREMENT_CYCLES(x) {\
  cycleCount += x;           \
  cyclesSinceReset += x;     \
  cyclesSinceCP += x;        \
  if(wdt_seed != 0) {        \
    wdt_val +=x;             \
    if(wdt_val >= wdt_seed) {\
      wdt_val = 0;           \
      cpu_set_except(16);    \
    }                        \
  }                          \
}

// Macros for Clank
#define REPORT_IDEM_BREAKS 0
#define IGNORE_ADDRESS 0x40000000
void reportAndReset(char pNumRegsPushed);   // Reports on status of current idempotent section, then clears buffers
struct ADDRESS_LIST {
    u32 address;
    struct ADDRESS_LIST *next;
};
typedef struct ADDRESS_LIST ADDRESS_LIST;


extern u64 cycleCount;
extern u64 insnCount;
extern u32 cyclesSinceReset;
extern u32 resetAfterCycles;
extern u64 wastedCycles;
extern u32 cyclesSinceCP;
extern u32 addrOfCP;
extern u32 addrOfRestoreCP;
extern u32 do_reset;
extern u32 wdt_val;
extern u32 wdt_seed;
extern u32 PRINT_STATE_DIFF;
#if MEM_COUNT_INST
  extern u32 store_count;
  extern u32 load_count;
  extern u32 cp_count;
#endif
#if HOOK_GPR_ACCESSES
    void do_nothing(void);
    void report_sp(void);
    void (* gprReadHooks[16])(void);
    void (* gprWriteHooks[16])(void);
#endif
char simValidMem(u32 address); // Interface for rsp (GDB) server


//struct MEMMAPIO {
//  u32 *cycleCountLSB;
//  u32 *cycleCountMSB;
//  u32 *cyclesSince;
//  u32 *resetAfter;
//
//  u32 *do_reset;
//  u32 *do_logging;
//};
//typedef struct MEMMAPIO MEMMAPIO;

#endif
