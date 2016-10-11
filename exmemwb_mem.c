#include <stdlib.h>
#include "exmemwb.h"
#include "decode.h"


///--- Load/store multiple operations --------------------------------------------///

// LDM - Load multiple registers from the stack
u32 ldm()
{
    diss_printf("ldm r%u!, {0x%X}\n", decoded.rN, decoded.reg_list);

    u32 numLoaded = 0;
    u32 rNWritten = (1 << decoded.rN) & decoded.reg_list;
    u32 address = cpu_get_gpr(decoded.rN);
    
    for(int i = 0; i < 8; ++i)
    {
        int mask = 1 << i;
        if(decoded.reg_list & mask)
        {
            u32 data = 0;
            simLoadData(address, &data);
            cpu_set_gpr(i, data);
            address += 4;
            ++numLoaded;
        }
    }
    
    if(rNWritten == 0)
        cpu_set_gpr(decoded.rN, address);
    
    return 1 + numLoaded;
}

// STM - Store multiple registers to the stack
u32 stm()
{
    diss_printf("stm r%u!, {0x%X}\n", decoded.rN, decoded.reg_list);
    
    u32 numStored = 0;
    u32 address = cpu_get_gpr(decoded.rN);
    
    for(int i = 0; i < 8; ++i)
    {
        int mask = 1 << i;
        if(decoded.reg_list & mask)
        {
            if(i == decoded.rN && numStored == 0)
            {
                fprintf(stderr, "Error: Malformed instruction!\n");
                sim_exit(1);
            }
                
            u32 data = cpu_get_gpr(i);
            simStoreData(address, data);
            address += 4;
            ++numStored;
        }
    }
    
    cpu_set_gpr(decoded.rN, address);
    
    return 1 + numStored;
}

///--- Stack operations --------------------------------------------///

// Pop multiple reg values from the stack and update SP
u32 pop()
{    
	diss_printf("pop {0x%X}\n", decoded.reg_list);
    
    u32 numLoaded = 0;
    u32 address = cpu_get_sp();
    
    for(int i = 0; i < 16; ++i)
    {
        int mask = 1 << i;
        if(decoded.reg_list & mask)
        {
            u32 data = 0;
            simLoadData(address, &data);
            cpu_set_gpr(i, data);
            ++numLoaded;
            if(i == 15)
                takenBranch = 1;
            address += 4;
        }
        
        // Skip constant 0s
        if(i == 7)
            i = 14;
    }
    
    cpu_set_sp(address);
    
    return 1 + numLoaded + takenBranch ? TIMING_PC_UPDATE : 0;
}

// Push multiple reg values to the stack and update SP
u32 push()
{
    diss_printf("push {0x%4.4X}\n", decoded.reg_list);
    
    u32 numStored = 0;
    u32 address = cpu_get_sp();
    
    for(int i = 14; i >= 0; --i)
    {
        int mask = 1 << i;
        if(decoded.reg_list & mask)
        {
            address -= 4;
            u32 data = cpu_get_gpr(i);
            simStoreData(address, data);
            ++numStored;
        }
        
        // Skip constant 0s
        if(i == 14)
            i = 8;
    }
    
    cpu_set_sp(address);
    
    return 1 + numStored;
}

///--- Single load operations --------------------------------------------///


// LDR - Load from offset from register
u32 ldr_i()
{
	diss_printf("ldr r%u, [r%u, #0x%X]\n", decoded.rD, decoded.rN, decoded.imm << 2);

	u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = zeroExtend32(decoded.imm << 2);
    u32 effectiveAddress = base + offset;
    
    u32 result = 0;
    simLoadData(effectiveAddress, &result);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDR - Load from offset from SP
u32 ldr_sp()
{
	diss_printf("ldr r%u, [SP, #0x%X]\n", decoded.rD, decoded.imm << 2);
    
	u32 base = cpu_get_sp();
    u32 offset = zeroExtend32(decoded.imm << 2);
    u32 effectiveAddress = base + offset;
    
    u32 result = 0;
    simLoadData(effectiveAddress, &result);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDR - Load from offset from PC
u32 ldr_lit()
{
	diss_printf("ldr r%u, [PC, #%d]\n", decoded.rD, decoded.imm << 2);
    
	u32 base = cpu_get_pc() & 0xFFFFFFFC;
    u32 offset = zeroExtend32(decoded.imm << 2);
    u32 effectiveAddress = base + offset;
    
    u32 result = 0;
    simLoadData(effectiveAddress, &result);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDR - Load from an offset from a reg based on another reg value
u32 ldr_r()
{
    diss_printf("ldr r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);

    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    
    u32 result = 0;
    simLoadData(effectiveAddress, &result);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDRB - Load byte from offset from register
u32 ldrb_i()
{
	diss_printf("ldrb r%u, [r%u, #0x%X]\n", decoded.rD, decoded.rN, decoded.imm);
    
	u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = zeroExtend32(decoded.imm);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    
    u32 result = 0;
    simLoadData(effectiveAddressWordAligned, &result);
    
    // Select the correct byte
    switch (effectiveAddress & 0x3) {
        case 0:
            break;
        case 1:
            result >>= 8;
            break;
        case 2:
            result >>= 16;
            break;
        case 3:
            result >>= 24;
    }
    
    result = zeroExtend32(result & 0xFF);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDRB - Load byte from an offset from a reg based on another reg value
u32 ldrb_r()
{
    diss_printf("ldrb r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    
    u32 result = 0;
    simLoadData(effectiveAddressWordAligned, &result);
    
    // Select the correct byte
    switch (effectiveAddress & 0x3) {
        case 0:
            break;
        case 1:
            result >>= 8;
            break;
        case 2:
            result >>= 16;
            break;
        case 3:
            result >>= 24;
    }
    
    result = zeroExtend32(result & 0xFF);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDRH - Load halfword from offset from register
u32 ldrh_i()
{
	diss_printf("ldrh r%u, [r%u, #0x%X]\n", decoded.rD, decoded.rN, decoded.imm);
    
	u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = zeroExtend32(decoded.imm << 1);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    
    u32 result = 0;
    simLoadData(effectiveAddressWordAligned, &result);

    // Select the correct halfword
    switch (effectiveAddress & 0x2) {
        case 0:
            break;
        default:
            result >>= 16;
            break;
    }
    
    result = zeroExtend32(result & 0xFFFF);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDRH - Load halfword from an offset from a reg based on another reg value
u32 ldrh_r()
{
    diss_printf("ldrh r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    
    u32 result = 0;
    simLoadData(effectiveAddressWordAligned, &result);

    // Select the correct halfword
    switch (effectiveAddress & 0x2) {
        case 0:
            break;
        default:
            result >>= 16;
            break;
    }
    
    result = zeroExtend32(result & 0xFFFF);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDRSB - Load signed byte from an offset from a reg based on another reg value
u32 ldrsb_r()
{
    diss_printf("ldrsb r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    
    u32 result = 0;
    simLoadData(effectiveAddressWordAligned, &result);
    
    // Select the correct byte
    switch (effectiveAddress & 0x3) {
        case 0:
            break;
        case 1:
            result >>= 8;
            break;
        case 2:
            result >>= 16;
            break;
        case 3:
            result >>= 24;
    }
    
    result = signExtend32(result & 0xFF, 8);
    
    cpu_set_gpr(decoded.rD, result);
    
    return TIMING_MEM;
}

// LDRSH - Load signed halfword from an offset from a reg based on another reg value
u32 ldrsh_r()
{
    diss_printf("ldrsh r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    
    u32 result = 0;
    simLoadData(effectiveAddressWordAligned, &result);
    
    // Select the correct halfword
    switch (effectiveAddress & 0x2) {
        case 0:
            break;
        default:
            result >>= 16;
    }
    
    result = signExtend32(result & 0xFFFF, 16);
    
    cpu_set_gpr(decoded.rD, result);

    return TIMING_MEM;
}

///--- Single store operations --------------------------------------------///

// STR - Store to offset from register
u32 str_i()
{
	diss_printf("str r%u, [r%u, #%d]\n", decoded.rD, decoded.rN, decoded.imm << 2);
    
	u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = zeroExtend32(decoded.imm << 2);
    u32 effectiveAddress = base + offset;
    
    simStoreData(effectiveAddress, cpu_get_gpr(decoded.rD));
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddress, cpu_get_gpr(decoded.rD));
    #endif
    
    return TIMING_MEM;
}

// STR - Store to offset from SP
u32 str_sp()
{
	diss_printf("str r%u, [SP, #%d]\n", decoded.rD, decoded.imm << 2);
    
	u32 base = cpu_get_sp();
    u32 offset = zeroExtend32(decoded.imm << 2);
    u32 effectiveAddress = base + offset;
    
    simStoreData(effectiveAddress, cpu_get_gpr(decoded.rD));
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddress, cpu_get_gpr(decoded.rD));
    #endif
    
    return TIMING_MEM;
}

// STR - Store to an offset from a reg based on another reg value
u32 str_r()
{
    diss_printf("str r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    
    simStoreData(effectiveAddress, cpu_get_gpr(decoded.rD));
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddress, cpu_get_gpr(decoded.rD));
    #endif
    
    return TIMING_MEM;
}

// STRB - Store byte to offset from register
u32 strb_i()
{
	diss_printf("strb r%u, [r%u, #0x%X]\n", decoded.rD, decoded.rN, decoded.imm);
    
	u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = zeroExtend32(decoded.imm);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    u32 data = cpu_get_gpr(decoded.rD) & 0xFF;
    
    u32 orig;
    simLoadData_internal(effectiveAddressWordAligned, &orig, 1);
    
    // Select the correct byte
    switch (effectiveAddress & 0x3) {
        case 0:
            orig = (orig & 0xFFFFFF00) | (data << 0);
            break;
        case 1:
            orig = (orig & 0xFFFF00FF) | (data << 8);
            break;
        case 2:
            orig = (orig & 0xFF00FFFF) | (data << 16);
            break;
        case 3:
            orig = (orig & 0x00FFFFFF) | (data << 24);
    }
    
    simStoreData(effectiveAddressWordAligned, orig);
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddressWordAligned, orig);
    #endif
    
    return TIMING_MEM;
}

// STRB - Store byte to an offset from a reg based on another reg value
u32 strb_r()
{
    diss_printf("strb r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    u32 data = cpu_get_gpr(decoded.rD) & 0xFF;
    
    u32 orig;
    simLoadData_internal(effectiveAddressWordAligned, &orig, 1);
    
    // Select the correct byte
    switch (effectiveAddress & 0x3) {
        case 0:
            orig = (orig & 0xFFFFFF00) | (data << 0);
            break;
        case 1:
            orig = (orig & 0xFFFF00FF) | (data << 8);
            break;
        case 2:
            orig = (orig & 0xFF00FFFF) | (data << 16);
            break;
        case 3:
            orig = (orig & 0x00FFFFFF) | (data << 24);
    }
    
    simStoreData(effectiveAddressWordAligned, orig);
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddressWordAligned, orig);
    #endif
    
    return TIMING_MEM;
}

// STRH - Store halfword to offset from register
u32 strh_i()
{
	diss_printf("strh r%u, [r%u, #0x%X]\n", decoded.rD, decoded.rN, decoded.imm);
    
	u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = zeroExtend32(decoded.imm << 1);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    u32 data = cpu_get_gpr(decoded.rD) & 0xFFFF;
    
    u32 orig;
    simLoadData_internal(effectiveAddressWordAligned, &orig, 1);
    
    // Select the correct byte
    switch (effectiveAddress & 0x2) {
        case 0:
            orig = (orig & 0xFFFF0000) | (data << 0);
            break;
        default:
            orig = (orig & 0x0000FFFF) | (data << 16);
    }
    
    simStoreData(effectiveAddressWordAligned, orig);
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddressWordAligned, orig);
    #endif
    
    return TIMING_MEM;
}

// STRH - Store halfword to an offset from a reg based on another reg value
u32 strh_r()
{
    diss_printf("strh r%u, [r%u, r%u]\n", decoded.rD, decoded.rN, decoded.rM);
    
    u32 base = cpu_get_gpr(decoded.rN);
    u32 offset = cpu_get_gpr(decoded.rM);
    u32 effectiveAddress = base + offset;
    u32 effectiveAddressWordAligned = effectiveAddress & ~0x3;
    u32 data = cpu_get_gpr(decoded.rD) & 0xFFFF;
    
    u32 orig;
    simLoadData_internal(effectiveAddressWordAligned, &orig, 1);
    
    // Select the correct byte
    switch (effectiveAddress & 0x2) {
        case 0:
            orig = (orig & 0xFFFF0000) | (data << 0);
            break;
        default:
            orig = (orig & 0x0000FFFF) | (data << 16);
    }
    
    simStoreData(effectiveAddressWordAligned, orig);
    
    #if PRINT_STORES_WITH_STATE
        printf("write: %08X %08X\n", effectiveAddressWordAligned, orig);
    #endif
    
    return TIMING_MEM;
}
