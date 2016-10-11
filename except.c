#include <stdlib.h>
#include "exmemwb.h"

// Clear pending exception after handled
// Used for watchdog timer exception
void check_except(void)
{
  int i;

  for(i=0; i<32; i++)
  {
    if(((cpu_get_except() >> i)&1) == 1)
    {
      except_enter(i);
      cpu_clear_except(i);
    }
  }
}

void except_enter(const u32 exceptID)
{
    // Do we need to align the stack frame
    u32 frame_align = 0;//(cpu_get_sp() & 0x4) >> 2;
    
    // Align the new SP to a frame
    cpu_set_sp((cpu_get_sp() - 0x20));// & ~0x4);
    u32 * frame_ptr = (u32 *)cpu_get_sp();
    
    // Exception can be mapped as a normal function call
    // so we need to backup the callee-saved registers for
    // the interrupted function
    simStoreData((u32)&frame_ptr[0], cpu_get_gpr(0));
    simStoreData((u32)&frame_ptr[1], cpu_get_gpr(1));
    simStoreData((u32)&frame_ptr[2], cpu_get_gpr(2));
    simStoreData((u32)&frame_ptr[3], cpu_get_gpr(3));
    simStoreData((u32)&frame_ptr[4], cpu_get_gpr(12));
    simStoreData((u32)&frame_ptr[5], cpu_get_lr());
    simStoreData((u32)&frame_ptr[6], cpu_get_pc() - 0x4);

    u32 psr = cpu_get_apsr();
    simStoreData((u32)&frame_ptr[7], (psr & 0xFFFFFC00) | (frame_align << 9) |  (psr & 0x1FF));

    // Encode the mode of the cpu at time of exception in LR value
    if(cpu_mode_is_handler())
        cpu_set_lr(0xFFFFFFF1);
    else
        if(cpu_stack_is_main())
            cpu_set_lr(0xFFFFFFF9);
        else
            cpu_set_lr(0xFFFFFFFD);

    // Put the cpu in exception handling mode
    cpu_mode_handler();
    cpu_set_ipsr(exceptID);
    cpu_stack_use_main();
    u32 handlerAddress = 0;
    simLoadData(exceptID << 2, &handlerAddress);
    cpu_set_pc(handlerAddress);
    
    // This counts as a branch
    takenBranch = 1;
}

void except_exit(const u32 pType)
{
    // Return to the mode and stack that were active when the exception started
    // Error if handler mode and process stack, stops simulation
    if((pType & 0xF) == 1)
    {
        cpu_mode_handler();
        cpu_stack_use_main();
    }
    else if((pType & 0xF) == 0x9)
    {
        cpu_mode_thread();
        cpu_stack_use_main();
    }
    else if((pType & 0xF) == 0xD)
    {
        cpu_mode_thread();
        cpu_stack_use_process();
    }
    else
    {
        fprintf(stderr, "ERROR: Invalid exception return\n");
        sim_exit(1);
    }

    cpu_set_ipsr(0);
  
    // Restore registers
    u32 * frame_ptr = (u32 *)cpu_get_sp();
    u32 value;
    
    simLoadData((u32)&frame_ptr[0], &value);
    cpu_set_gpr(0, value);
    
    simLoadData((u32)&frame_ptr[1], &value);
    cpu_set_gpr(1, value);
    
    simLoadData((u32)&frame_ptr[2], &value);
    cpu_set_gpr(2, value);
    
    simLoadData((u32)&frame_ptr[3], &value);
    cpu_set_gpr(3, value);
    
    simLoadData((u32)&frame_ptr[4], &value);
    cpu_set_gpr(12, value);
    
    simLoadData((u32)&frame_ptr[5], &value);
    cpu_set_lr(value);
    
    simLoadData((u32)&frame_ptr[6], &value);
    cpu_set_pc(value);
    
    simLoadData((u32)&frame_ptr[7], &value);
    cpu_set_apsr(value);
    
    // Set special-purpose registers
    cpu_set_sp((cpu_get_sp() + 0x20));// | ((cpu_get_apsr() > (9 - 2)) & 0x200));
    cpu_set_apsr(cpu_get_apsr() & 0xF0000000);
    cpu_set_ipsr(0);
    // Ignore epsr
    takenBranch = 1;
}
