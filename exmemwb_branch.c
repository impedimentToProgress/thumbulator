#include <stdlib.h>
#include "exmemwb.h"
#include "decode.h"
#include "except.h"

///--- Compare operations --------------------------------------------///

u32 cmn()
{
    diss_printf("cmns r%u, r%u\n", decoded.rM, decoded.rN);

    u32 opA = cpu_get_gpr(decoded.rM);
    u32 opB = cpu_get_gpr(decoded.rN);
    u32 result = opA + opB;

    do_nflag(result);
    do_zflag(result);
    do_cflag(opA, opB, 0);
    do_vflag(opA, opB, result);
    
    return 1;
}

u32 cmp_i()
{
    diss_printf("cmp r%u, #0x%02X\n", decoded.rD, decoded.imm);

    u32 opA = cpu_get_gpr(decoded.rD);
    u32 opB = ~zeroExtend32(decoded.imm);
    u32 result = opA + opB + 1;

    do_nflag(result);
    do_zflag(result);
    do_cflag(opA, opB, 1);
    do_vflag(opA, opB, result);

    return 1;
}

u32 cmp_r()
{
    diss_printf("cmp r%u, r%u\n", decoded.rD, decoded.rM); // rN to rD due to decoding

    u32 opA = cpu_get_gpr(decoded.rD);
    u32 opB = ~zeroExtend32(cpu_get_gpr(decoded.rM));
    u32 result = opA + opB + 1;

    do_nflag(result);
    do_zflag(result);
    do_cflag(opA, opB, 1);
    do_vflag(opA, opB, result);

    return 1;
}

// TST - Test for matches
u32 tst()
{
    diss_printf("tst r%u, r%u\n", decoded.rN, decoded.rD); // Switch operands to ease decoding
    
    u32 opA = cpu_get_gpr(decoded.rD);
    u32 opB = cpu_get_gpr(decoded.rM);
    u32 result = opA & opB;
    
    do_nflag(result);
    do_zflag(result);
    
    return 1;
}

///--- Branch operations --------------------------------------------///

// B - Unconditional branch
u32 b()
{
    u32 offset = signExtend32(decoded.imm << 1, 12);
    
    diss_printf("B 0x%08X\n", offset);
    
    u32 result = offset + cpu_get_pc();
    cpu_set_pc(result);
    takenBranch = 1;
    
    return TIMING_BRANCH;
}

// B - Conditional branch
u32 b_c()
{
    diss_printf("Bcc 0x%08X\n", decoded.imm);
    u32 taken = 0;
    
    switch(decoded.cond)
    {
		case 0x0: // b eq, z set
			diss_printf("beq 0x%08X\n", decoded.imm);
			if(cpu_get_flag_z())
                taken = 1;
			break;
		case 0x1: // b ne, z clear
			diss_printf("bne 0x%08X\n", decoded.imm);
			if(!cpu_get_flag_z())
                taken = 1;
			break;
		case 0x2: // b cs, c set
			diss_printf("bcs 0x%08X\n", decoded.imm);
			if(cpu_get_flag_c())
                taken = 1;
			break;
		case 0x3: // b cc, c clear
			diss_printf("bcc 0x%08X\n", decoded.imm);
			if(!cpu_get_flag_c())
                taken = 1;
			break;
		case 0x4: // b mi, n set
			diss_printf("bmi 0x%08X\n", decoded.imm);
			if(cpu_get_flag_n())
                taken = 1;
			break;
		case 0x5: // b pl, n clear
			diss_printf("bpl 0x%08X\n", decoded.imm);
			if(!cpu_get_flag_n())
                taken = 1;
			break;
		case 0x6: // b vs, v set
			diss_printf("bvs 0x%08X\n", decoded.imm);
			if(cpu_get_flag_v())
                taken = 1;
			break;
		case 0x7: // b vc, v clear
			diss_printf("bvc 0x%08X\n", decoded.imm);
			if(!cpu_get_flag_v())
                taken = 1;
			break;
		case 0x8: // b hi, c set z clear
			diss_printf("bhi 0x%08X\n", decoded.imm);
			if(cpu_get_flag_c() && !cpu_get_flag_z())
                taken = 1;
			break;
		case 0x9: // b ls, c clear or z set
			diss_printf("bls 0x%08X\n", decoded.imm);
			if(cpu_get_flag_z() || !cpu_get_flag_c())
                taken = 1;
			break;
		case 0xA: // b ge, N  ==  V
			diss_printf("bge 0x%08X\n", decoded.imm);
			if(cpu_get_flag_n() == cpu_get_flag_v())
                taken = 1;
			break;
		case 0xB: // b lt, N ! =  V
			diss_printf("blt 0x%08X\n", decoded.imm);
			if(cpu_get_flag_n() != cpu_get_flag_v())
                taken = 1;
			break;
		case 0xC: // b gt, Z == 0 and N  ==  V
			diss_printf("bgt 0x%08X\n", decoded.imm);
			if(!cpu_get_flag_z() && (cpu_get_flag_n() == cpu_get_flag_v()))
                taken = 1;
			break;
		case 0xD: // b le, Z == 1 or N ! =  V
			diss_printf("ble 0x%08X\n", decoded.imm);
			if(cpu_get_flag_z() || (cpu_get_flag_n() != cpu_get_flag_v()))
                taken = 1;
            break;
        default:
            fprintf(stderr, "Error: Malformed instruction!");
            sim_exit(1);
    }
    
    if(taken == 0)
    {
        return 1;
    }
    
    u32 offset = signExtend32(decoded.imm << 1, 9);
    u32 pc = cpu_get_pc();
    u32 result = offset + pc;
    cpu_set_pc(result);
    takenBranch = 1;
    
    return TIMING_BRANCH;
}

// BLX - Unconditional branch and link with switch to ARM mode
u32 blx()
{
    diss_printf("blx r%u\n", decoded.rM);
    
    u32 address = cpu_get_gpr(decoded.rM);
    
    if((address & 0x1) == 0)
    {
        fprintf(stderr, "Error: Interworking not supported: 0x%8.8X\n", address);
        sim_exit(1);
    }
    
    cpu_set_lr(cpu_get_pc() - 0x2);
    cpu_set_pc(address);
    takenBranch = 1;
    
    return TIMING_BRANCH;
}

// BX - Unconditional branch with switch to ARM mode
// Also may be used as exception return
u32 bx()
{
    diss_printf("bx r%u\n", decoded.rM);
    
    u32 address = cpu_get_gpr(decoded.rM);
    
    if((address & 0x1) == 0)
    {
        fprintf(stderr, "Error: Interworking not supported: 0x%8.8X\n", address);
        sim_exit(1);
    }
    
    // Check for exception return
    if((address >> 28) == 0xF)
        except_exit(address);
    else
        cpu_set_pc(address);
    
    takenBranch = 1;
    
    return TIMING_BRANCH;
}

// BL - Unconditional branch and link
// 32 bit instruction
u32 bl()
{
    u32 result = signExtend32(decoded.imm << 1, 25);
    
    diss_printf("bl 0x%08X\n", result);
    
    result += cpu_get_pc();
    
    cpu_set_lr(cpu_get_pc());
    cpu_set_pc(result);
    takenBranch = 1;
    
    return TIMING_BRANCH_LINK;
}
