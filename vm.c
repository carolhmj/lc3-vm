#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/* memory definition */
uint16_t memory[UINT16_MAX];

enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};

uint16_t reg[R_COUNT];

/* opcodes */
enum {
    OP_BR = 0, /*branch*/
    OP_ADD,    /*add*/ 
    OP_LD,     /*load*/ 
    OP_ST,     /*store*/ 
    OP_JSR,    /*jump register*/   
    OP_AND,    /*bitwise and*/ 
    OP_LDR,    /*load register*/ 
    OP_STR,    /*store register*/ 
    OP_RTI,    /*unused*/ 
    OP_NOT,    /*bitwise not*/
    OP_LDI,    /*load indirect*/ 
    OP_STI,    /*store indirect*/ 
    OP_JMP,    /*jump*/ 
    OP_RES,    /*reserved (unused)*/
    OP_LEA,    /*load effective address*/ 
    OP_TRAP    /*execute trap*/ 
};

/* condition flags */
enum {
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2
};

uint16_t sign_extend(uint16_t x, int bit_count);
void update_flags(uint16_t r);

int main(int argc, char const *argv[]) {
    /* load arguments */
    /* setup */
    /* start PC */
    enum {PC_START = 0x3000};
    reg[R_PC] = PC_START;
    int running = 1;
    while (running) {
        /* fetch instruction */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op) {
            case OP_ADD: 
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //First operand (SR1)
                uint16_t r1 = (instr >> 6) & 0x7;
                //Immediate mode
                uint16_t imm_flag = (instr >> 5) & 0x1;
                
                if (imm_flag) {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] + imm5;
                } else {
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] + reg[r2];    
                }

                update_flags(r0);
                break;
            case OP_AND:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //First operand (SR1)
                uint16_t r1 = (instr >> 6) & 0x7;
                //Immediate mode
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag) {
                    //Sign-extended immediate field
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] & imm5;
                } else {
                    //Second operand (SR2)
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] & reg[r2]; 
                }

                update_flags(r0);
                break;
            case OP_NOT:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //BaseR register
                uint16_t base_r = (instr >> 6) & 0x7;

                reg[r0] = ~reg[base_r];
                update_flags(r0);
                break;        
            case OP_BR:
                //Get condition that will be tested
                uint16_t n = (instr >> 11) & 0x1;
                uint16_t z = (instr >> 10) & 0x1;
                uint16_t p =  (instr >> 9) & 0x1;

                //Get condition from condition codes
                uint16_t N = reg[R_COND] == FL_NEG;
                uint16_t Z = reg[R_COND] == FL_ZRO;
                uint16_t P = reg[R_COND] == FL_POS;
                
                if ((n & N) | (z & Z) | (p & P)) {
                    reg[R_PC] += sign_extend(instr & 0x1FF, 9);        
                }
                break;
            case OP_JMP:
                //BaseR register
                uint16_t base_r = (instr >> 6) & 0x7;
                
                //Special case: return
                if (base_r == 0x7) {
                    reg[R_PC] = reg[R_R7];
                } else {
                    reg[R_PC] = reg[base_r];
                }
                break;
            case OP_JSR: 
                //PC offset flag
                uint16_t off_flag = (instr >> 11) & 0x1;

                reg[R_R7] = reg[R_PC];
                //Offset from PC mode
                if (off_flag) {
                    uint16_t pc_offset11 = instr & 0x7FF;
                    reg[R_PC] = reg[R_PC] + sign_extend(pc_offset11, 11);
                } else { //Register mode
                    uint16_t base_r = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[base_r];
                }
                break;
            case OP_LD:
                //Destination register
                uint16_t r0 = (instr >> 9) & 0x7;
                //PC offset
                uint16_t pc_offset9 = instr & 0x1FF;

                reg[r0] = mem_read(reg[R_PC] + sign_extend(pc_offset9, 9));
                update_flags(r0);
                break;
            case OP_LDI:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //PC offset
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                reg[r0] = mem_read(mem_read(reg[R_PC]+pc_offset));
                update_flags(r0);
                break;
            case OP_LDR:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Base register (baseR)
                uint16_t br = (instr >> 6) & 0x7;

                //Offset
                uint16_t off = sign_extend(instr & 0x1f, 6);
                reg[r0] = mem_read(reg[br] + off);

                update_flags(r0);
                break;
            case OP_LEA:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Offset
                uint16_t off = sign_extend(instr & 0x1ff, 9);

                reg[r0] = mem_read(reg[R_PC] + off);
                update_flags(r0);
                break;
            case OP_ST:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Offset
                uint16_t off = sign_extend(instr & 0x1ff, 9);            
                
                mem_write(reg[R_PC] + off, reg[r0]);
                break;
            case OP_STI:
                break;
            case OP_STR:
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Base register (baseR)
                uint16_t br = (instr >> 6) & 0x7;

                //Offset
                uint16_t off = sign_extend(instr & 0x1f, 6);
                
                mem_write(reg[br] + off, reg[r0]);
                break;
            case OP_TRAP:
                //Store PC's contents here so we can return from the calling routine
                reg[R_R7] = reg[R_PC];
                uint16_t trapvect = sign_extend(instr & 0xff, 8);

                reg[R_PC] = mem_read(trapvect);
                break;
            case OP_RES:
            case OP_RTI:
            default:
                break;                                            
        }
    }
    return 0;
}

uint16_t sign_extend(uint16_t x, int bit_count) {
    if ((x >> (bit_count-1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

void update_flags(uint16_t r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;
    } else if (reg[r] >> 15) {
        reg[R_COND] = FL_NEG;
    } else {
        reg[R_COND] = FL_POS;
    }
}