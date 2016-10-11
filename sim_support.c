#include <stdlib.h>
#if MD5
   #include <openssl/md5.h>
#endif
#include "sim_support.h"
#include "exmemwb.h"
#include "rsp-server.h"

u64 cycleCount = 0;
u64 insnCount = 0;
u64 wastedCycles = 0;
u32 cyclesSinceReset = 0;
u32 cyclesSinceCP = 0;
u32 resetAfterCycles = 0;
u32 addrOfCP = 0;
u32 addrOfRestoreCP = 0;
u32 do_reset = 0;
u32 wdt_seed = 0;
u32 wdt_val = 0;
u32 md5[4] = {0,0,0,0,0};
u32 PRINT_STATE_DIFF = PRINT_STATE_DIFF_INIT;
#if MEM_COUNT_INST
  u32 store_count = 0;
  u32 load_count = 0;
  u32 cp_count = 0;
#endif
u32 ram[RAM_SIZE >> 2];
u32 flash[FLASH_SIZE >> 2];
bool takenBranch = 0;
ADDRESS_LIST addressReadBeforeWriteList = {0, NULL};
ADDRESS_LIST addressWriteBeforeReadList = {0, NULL};
ADDRESS_LIST addressConflictList = {0, NULL};
int addressConflicts = 0;
int addressConflictsStack = 0;
int addressWrites = 0;
int addressReads = 0;

// Reserve a space inside the simulator for variables that GDB and python can use to control the simulator
// Essentially creates a new block of addresses on the bus of the processor that only the debug read and write commands can access
//MEMMAPIO mmio = {.cycleCountLSB = &cycleCount, .cycleCountMSB = &cycleCount+4,
//                 .cyclesSince = &cyclesSinceReset, .resetAfter = &resetAfterCycles};
u32* mmio[] = {&cycleCount, ((u32*)&cycleCount)+1, &wastedCycles, ((u32*)&wastedCycles)+1,
  &cyclesSinceReset, &cyclesSinceCP, &addrOfCP, &addrOfRestoreCP, 
  &resetAfterCycles, &do_reset, &PRINT_STATE_DIFF, &wdt_seed, 
  &wdt_val, &(md5[0]), &(md5[1]), &(md5[2]), 
  &(md5[3]), &(md5[4])};

// Reset CPU state in accordance with B1.5.5 and B3.2.2
void cpu_reset(void)
{
  // Initialize the special-purpose registers
  cpu.apsr = 0;       // No flags set
  cpu.ipsr = 0;       // No exception number
  cpu.espr = ESPR_T;  // Thumb mode
  cpu.primask = 0;    // No except priority boosting
  cpu.control = 0;    // Priv mode and main stack
  cpu.sp_main = 0;    // Stack pointer for exception handling
  cpu.sp_process = 0; // Stack pointer for process

  // Clear the general purpose registers
  memset(cpu.gpr, 0, sizeof(cpu.gpr));

  // Set the reserved GPRs
  cpu.gpr[GPR_LR] = 0;

  // May need to add logic to send writes and reads to the
  // correct stack pointer
  // Set the stack pointers
  simLoadData(0, &cpu.sp_main);
  cpu.sp_main &= 0xFFFFFFFC;
  cpu.sp_process = 0;
  cpu_set_sp(cpu.sp_main);

  // Set the program counter to the address of the reset exception vector
  u32 startAddr;
  simLoadData(0x4, &startAddr);
  cpu_set_pc(startAddr);

  // No pending exceptions
  cpu.exceptmask = 0;

  // Check for attempts to go to ARM mode
  if((cpu_get_pc() & 0x1) == 0)
  {
    printf("Error: Reset PC to an ARM address 0x%08X\n", cpu_get_pc());
    sim_exit(1);
  }

  // Reset the systick unit
  systick.control = 0x4;
  systick.reload = 0x0;
  systick.value = 0x0;
  systick.calib = CPU_FREQ/100 | 0x80000000;

  // Reset counters
  wastedCycles += cyclesSinceCP;
  cyclesSinceReset = 0;
  cyclesSinceCP = 0;
  wdt_val = 0;
}

void sim_command(void)
{
  // Reset the CPU
  if(do_reset != 0)
  {
    cpu_reset();
    cpu_set_pc(cpu_get_pc() + 0x4);
    do_reset = 0;
  }

  // Compute MD5 of memory
#if MD5
  if(md5[0] != 0)
  {
    printf("Computing MD5!");
    MD5_CTX c;
    unsigned char digest[16];
    int length;
    char *ptr;

    MD5_Init(&c);

    // Do RAM first
    length = RAM_SIZE-1;
    ptr = (char*) ram;
    while (length > 0) {
      if (length > 512) {
        MD5_Update(&c, ptr, 512);
      } else {
        MD5_Update(&c, ptr, length);
      }
      length -= 512;
      ptr += 512;
    }
    
    // Do flash next 
    length = FLASH_SIZE-1;
    ptr = (char*) flash;
    while (length > 0) {
      if (length > 512) {
        MD5_Update(&c, ptr, 512);
      } else {
        MD5_Update(&c, ptr, length);
      }
      length -= 512;
      ptr += 512;
    }

    //// Now low registers
    //length = 8*4;
    //ptr = (char*) cpu.gpr;
    //MD5_Update(&c, ptr, length);
    
    // PC, SP, LR
    length = 3*4;
    ptr = (char*) &(cpu.gpr[13]);
    MD5_Update(&c, ptr, length);

    MD5_Final((unsigned char*) &(md5[1]), &c);

    md5[0]=0;
  }
#endif

}


#if HOOK_GPR_ACCESSES
  void do_nothing(void){;}

  void report_sp(void)
  {
    if(cpu_get_sp() < 0X40010000)
    {
      fprintf(stderr, "SP crosses heap: 0x%8.8X\n", cpu_get_sp());
      fprintf(stderr, "PC: 0x%8.8X\n", cpu_get_pc());
    }
  }

  void (* gprReadHooks[16])(void) = { \
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing\
  };

  void (* gprWriteHooks[16])(void) = { \
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    do_nothing,\
    report_sp,\
    do_nothing\
  };
#endif

// Returns 1 if the passed list of addresses contains the passed
// address, returns 0 otherwise
char containsAddress(const ADDRESS_LIST *pList, const u32 pAddress)
{
  if(pList->address == pAddress)
    return 1;

  if(pList->next == NULL)
    return 0;

  // Tail recursion FTW
  return containsAddress(pList->next, pAddress);
}

// Adds the passed address to the end of the list
// Returns 0 if already present, 1 if added to end
char addAddress(const ADDRESS_LIST *pList, const u32 pAddress)
{
  ADDRESS_LIST *temp = pList;
  ADDRESS_LIST *temp_prev;

  // Go to the end of the list
  do
  {
    if(temp->address == pAddress)
      return 0;
    temp_prev = temp;
    temp = temp->next;
  } while(temp != NULL);

  // Create a new entry and link to it
  temp_prev->next = malloc(sizeof(ADDRESS_LIST));
  temp_prev->next->address = pAddress;
  temp_prev->next->next = NULL;

  return 1;
}

// Clears the list
void clearList(ADDRESS_LIST *pList)
{
  if(pList->next == NULL)
    return;

  clearList(pList->next);

  free(pList->next);

  pList->next = NULL;
}

// Called by branch and links
int insnsPerConflict = 0;
void reportAndReset(char pNumRegsPushed)
{
  if(!REPORT_IDEM_BREAKS)
    return;

  insnsPerConflict = cycleCount - insnsPerConflict;
  fprintf(stderr, "%d,%d,%d,%d,%d,%d,%d\n", addressWrites, addressReads, addressConflicts, addressConflicts - addressConflictsStack, insnsPerConflict, pNumRegsPushed, cpu_get_pc());
  addressConflicts = 0;
  addressConflictsStack = 0;
  addressWrites = 0;
  addressReads = 0;
  insnsPerConflict = cycleCount;
  clearList(&addressConflictList);
  clearList(&addressReadBeforeWriteList);
  clearList(&addressWriteBeforeReadList);
}

// Memory access functions assume that RAM has a higher address than Flash
char simLoadInsn(u32 address, u16 *value)
{
  u32 fromMem;

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      fprintf(stderr, "Error: ILR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    if(REPORT_IDEM_BREAKS)
    {
      // Add addresses to the read list if they weren't written to first
      if(!containsAddress(&addressWriteBeforeReadList, address))
        addressReads += addAddress(&addressReadBeforeWriteList, address);
    }

    fromMem = ram[(address & RAM_ADDRESS_MASK) >> 2];
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: ILF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    fromMem = flash[(address & FLASH_ADDRESS_MASK) >> 2];
  }
    
  // Data 32-bits, but instruction 16-bits
  *value = ((address & 0x2) != 0) ? (u16)(fromMem >> 16) : (u16)fromMem;
    
  return 0;
}

// Normal interface for a program to load from memory
// Increments load counter
char simLoadData(u32 address, u32 *value)
{
  #if MEM_COUNT_INST
    ++load_count;
  #endif
  return simLoadData_internal(address, value, 0);
}

char simLoadData_internal(u32 address, u32 *value, u32 falseRead)
{
  #if MEM_CHECKS
    if((address & 0x3) != 0)
    {
      fprintf(stderr, "Unalinged data memory read: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
        *value = 0;
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01)
      {
        *value = ((u32 *)&systick)[(address >> 2) & 0x3];
        if(address == 0xE000E010)
          systick.control &= 0x00010000;

        return 0;
      }

      fprintf(stderr, "Error: DLR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    // Add addresses to the read list if they weren't written to first
    if(REPORT_IDEM_BREAKS && !falseRead)
    {
      if(!containsAddress(&addressWriteBeforeReadList, address))
        addressReads += addAddress(&addressReadBeforeWriteList, address);
    }

    *value = ram[(address & RAM_ADDRESS_MASK) >> 2];
      
    #if PRINT_MEM_OPS
      if(!falseRead) printf("%llu\t%llu\tR\t%8.8X\t%d\n", cycleCount, insnCount, address, *value);
    #endif

#if PRINT_ALL_MEM
    fprintf(stderr, "%8.8X: Ram read at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, *value);
#endif
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DLF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    *value = flash[(address & FLASH_ADDRESS_MASK) >> 2];

    #if PRINT_MEM_OPS
      if(!falseRead) printf("%llu\t%llu\tR\t%8.8X\t%d\n", cycleCount, insnCount, address, *value);
    #endif
      
#if PRINT_ALL_MEM
    fprintf(stderr, "%8.8X: Flash read at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, *value);
#endif
  }

  return 0;
}

char simStoreData(u32 address, u32 value)
{
  unsigned int word;

  #if MEM_CHECKS
    if((address & 0x3) != 0) // Thumb-mode requires LSB = 1
    {
      fprintf(stderr, "Unalinged data memory write: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
#if !DISABLE_PROGRAM_PRINTING
        printf("%c", value & 0xFF);
        fflush(stdout);
#endif
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01 && address != 0xE000E01C)
      {
        if(address == 0xE000E010)
        {
          systick.control = (value & 0x1FFFD) | 0x4; // No external tick source, no interrupt
          if(value & 0x2)
            fprintf(stderr, "ERROR: SYSTICK interrupts not implemented...ignoring\n");
        }
        else if(address == 0xE000E014)
          systick.reload = value & 0xFFFFFF;
        else if(address == 0xE000E018)
          systick.value = 0; // Reads clears current value

        return 0;
      }

      // Check for cycle count
      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START+MEMMAPIO_SIZE))
      {
        word = *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]);
        word &= ~(0xff << (8*(address%4)));
        word |= (value << (8*(address%4)));
        *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]) = word;

        // If the variable updated is a request to reset, then do it
        if(do_reset != 0)
        {
          cpu_reset();
          cpu_set_pc(cpu_get_pc() + 0x4);
          do_reset = 0;
        }

        return 0;
      }

      fprintf(stderr, "Error: DSR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    if(REPORT_IDEM_BREAKS && address != IGNORE_ADDRESS)
    {
      // Conflict if we are writting to an address that was read
      // from before being written to
      if(containsAddress(&addressReadBeforeWriteList, address))
      {
        fprintf(stderr, "Error: Idempotency violation: address=0x%8.8X, pc=0x%8.8X\n", address, cpu_get_pc());
        int addressConflictsOrig = addressConflicts;

        addressConflicts += addAddress(&addressConflictList, address);
        if(address >= (cpu_get_sp() - 0x20) /*0x40001C00*/ && addressConflicts != addressConflictsOrig)
          ++addressConflictsStack;
      }
      // Only track new write addresses that were not read from first
      else
        addressWrites += addAddress(&addressWriteBeforeReadList, address);
    }

    rsp_check_watch(address);

    #if PRINT_RAM_WRITES || PRINT_ALL_MEM
      fprintf(stderr, "%8.8X: Ram write at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, value);
    #endif

    #if PRINT_MEM_OPS
      printf("%llu\t%llu\tW\t%8.8X\t%d\t%d\n", cycleCount, insnCount, address, ram[(address & RAM_ADDRESS_MASK) >> 2], value);
    #endif

    ram[(address & RAM_ADDRESS_MASK) >> 2] = value;
    
    #if MEM_COUNT_INST
      ++store_count;
    #endif
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DSF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    rsp_check_watch(address);

    #if PRINT_FLASH_WRITES || PRINT_ALL_MEM
      fprintf(stderr, "%8.8X: Flash write at 0x%8.8X=0x%8.8X\n", cpu_get_pc()-4, address, value);
    #endif

    #if PRINT_MEM_OPS
      printf("%llu\t%llu\tW\t%8.8X\t%d\t%d\n", cycleCount, insnCount, address, flash[(address & FLASH_ADDRESS_MASK) >> 2], value);
    #endif
      
    flash[(address & FLASH_ADDRESS_MASK) >> 2] = value;
      
    #if MEM_COUNT_INST
      ++store_count;
    #endif
  }

  return 0;
}

//
// Code below here is used by GDB interface
//

char simDebugRead(u32 address, unsigned char* value)
{
  unsigned int word;

  #if MEM_CHECKS
    if((address & 0x3) != 0)
    {
      fprintf(stderr, "Unalinged data memory read: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif
    
  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
        *value = 0;
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01)
      {
        *value = ((u32 *)&systick)[(address >> 2) & 0x3];
        if(address == 0xE000E010)
          systick.control &= 0x00010000;

        return 0;
      }

      // Check for cycle count
      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START+MEMMAPIO_SIZE))
      {
        //word = *((u32 *) (((void*)mmio)+((address & 0xfffffffc)-MEMMAPIO_START))) & 0xffffffff;
        word = *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]);
        //if(address < (MEMMAPIO_START+4))
        //  word = cyclesSinceReset & 0xffffffff;
        //else if(address < (MEMMAPIO_START+8))
        //  word = resetAfterCycles & 0xffffffff;
        //else if(address < (MEMMAPIO_START+12))
        //  word = cycleCount & 0xffffffff;
        //else if(address < (MEMMAPIO_START+16))
        //  word = (cycleCount >> 32) & 0xffffffff;

        *value = (word >> (8*(address%4)));
        return 0;
      }

      fprintf(stderr, "Error: DLR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = ram[(address & RAM_ADDRESS_MASK) >> 2]; 
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DLF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = flash[(address & FLASH_ADDRESS_MASK) >> 2];
  }

  *value = (word >> (8*(address %4))) & 0xff;

  return 0;
}

char simDebugWrite(u32 address, unsigned char value)
{
  unsigned int word;

  #if MEM_CHECKS
    if((address & 0x3) != 0)
    {
      fprintf(stderr, "Unalinged data memory read: 0x%8.8X\n", address);
      sim_exit(1);
    }
  #endif

  if(address >= RAM_START)
  {
    if(address >= (RAM_START + RAM_SIZE))
    {
      // Check for UART
      if(address == 0xE0000000)
      {
        return 0;
      }

      // Check for systick
      if((address >> 4) == 0xE000E01)
      {
        if(address == 0xE000E010)
          systick.control &= 0x00010000;

        return 0;
      }

      // Check for cycle count
      if(address >= MEMMAPIO_START && address <= (MEMMAPIO_START+MEMMAPIO_SIZE))
      {
        word = *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]);
        word &= ~(0xff << (8*(address%4)));
        word |= (value << (8*(address%4)));
        *(mmio[((address & 0xfffffffc)-MEMMAPIO_START >> 2)]) = word;

        sim_command();

        return 0;
      }


      fprintf(stderr, "Error: DLR Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = ram[(address & RAM_ADDRESS_MASK) >> 2]; 
    word &= ~(0xff << (8*(address%4)));
    word |= (value << (8*(address%4)));
    ram[(address & RAM_ADDRESS_MASK) >> 2] = word; 
  }
  else
  {
    if(address >= (FLASH_START + FLASH_SIZE))
    {
      fprintf(stderr, "Error: DLF Memory access out of range: 0x%8.8X, pc=%x\n", address, cpu_get_pc());
      sim_exit(1);
    }

    word = flash[(address & FLASH_ADDRESS_MASK) >> 2];
    word &= ~(0xff << (8*(address%4)));
    word |= (value << (8*(address%4)));
    flash[(address & FLASH_ADDRESS_MASK) >> 2] = word;
  }

  return 0;
}

char simValidMem(u32 address)
{
  if((address >= RAM_START && address <= (RAM_START+RAM_SIZE)) ||
      (address >= FLASH_START && address <= (FLASH_START+FLASH_SIZE))||
      (address >= MEMMAPIO_START && address < MEMMAPIO_START+MEMMAPIO_SIZE))
    return 1;
  else
    return 0;
}

