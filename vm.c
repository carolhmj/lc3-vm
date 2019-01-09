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

/* Trap codes */
enum {
    TRAP_GETC = 0x20, /*get char from keyboard*/
    TRAP_OUT = 0x21, /*output char*/
    TRAP_PUTS = 0x22, /*output word string*/
    TRAP_IN = 0x23, /*input string*/
    TRAP_PUTSP = 0x24, /*output byte string*/
    TRAP_HALT = 0x25 /*halt program*/
};

enum {
    MR_KBSR = 0xfe00, /*keyboard status*/
    MR_KBDR = 0xfe02 /*keyboard data*/
};

struct termios original_tio;

uint16_t sign_extend(uint16_t x, int bit_count);
void update_flags(uint16_t r);
void read_image_file(FILE* file);
int read_image(const char* image_path);
uint16_t swap16(uint16_t x);
void mem_write(uint16_t address, uint16_t val);
uint16_t mem_read(uint16_t address);
uint16_t check_key();
void disable_input_buffering();
void restore_input_buffering();
void handle_interrupt(int signal);
uint16_t check_key();

int main(int argc, char const *argv[]) {
    /* load arguments */
    if (argc < 2) {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j) {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }
    /* setup */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
    /* start PC */
    enum {PC_START = 0x3000};
    reg[R_PC] = PC_START;
    int running = 1;
    while (running) {
        /* fetch instruction */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op) {
            case OP_ADD: { 
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
            }
            case OP_AND: {
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
            }
            case OP_NOT: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //BaseR register
                uint16_t base_r = (instr >> 6) & 0x7;

                reg[r0] = ~reg[base_r];
                update_flags(r0);
                break;
            }        
            case OP_BR: {
                uint16_t off = sign_extend(instr & 0x1FF, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;
                
                if (cond_flag & reg[R_COND]) {
                    reg[R_PC] += off;        
                }
                break;
            }
            case OP_JMP: {
                //BaseR register
                uint16_t base_r = (instr >> 6) & 0x7;

                //Return is when base_7 is R7
                reg[R_PC] = reg[base_r];            
                break;
            }
            case OP_JSR: { 
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
            }
            case OP_LD: {
                //Destination register
                uint16_t r0 = (instr >> 9) & 0x7;
                //PC offset
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

                reg[r0] = mem_read(reg[R_PC] + pc_offset9);
                update_flags(r0);
                break;
            }
            case OP_LDI: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //PC offset
                uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);
                reg[r0] = mem_read(mem_read(reg[R_PC]+pc_offset));
                update_flags(r0);
                break;
            }
            case OP_LDR: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Base register (baseR)
                uint16_t br = (instr >> 6) & 0x7;

                //Offset
                uint16_t off = sign_extend(instr & 0x1f, 6);
                reg[r0] = mem_read(reg[br] + off);

                update_flags(r0);
                break;
            }
            case OP_LEA: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Offset
                uint16_t off = sign_extend(instr & 0x1ff, 9);

                reg[r0] = reg[R_PC] + off;
                update_flags(r0);
                break;
            }
            case OP_ST: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Offset
                uint16_t off = sign_extend(instr & 0x1ff, 9);            
                
                mem_write(reg[R_PC] + off, reg[r0]);
                break;
            }
            case OP_STI: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Offset
                uint16_t off = sign_extend(instr & 0x1ff, 9);            
                
                mem_write(mem_read(reg[R_PC] + off), reg[r0]);
                break;
            }
            case OP_STR: {
                //Destination register (DR)
                uint16_t r0 = (instr >> 9) & 0x7;
                //Base register (baseR)
                uint16_t br = (instr >> 6) & 0x7;

                //Offset
                uint16_t off = sign_extend(instr & 0x3f, 6);
                
                mem_write(reg[br] + off, reg[r0]);
                break;
            }
            case OP_TRAP: {
                switch (instr & 0xff) {
                    case TRAP_GETC: {
                        uint16_t c = getchar() & 0xff;
                        reg[R_R0] = c;    
                        break;
                    }
                    case TRAP_OUT: {
                        putchar(reg[R_R0]);
                        fflush(stdout);
                        break;
                    }
                    case TRAP_PUTS: {
                        uint16_t* c = memory + reg[R_R0];
                        while (*c) {
                            putc((char)*c, stdout);
                            ++c;
                        }
                        fflush(stdout);
                        break;
                    }
                    case TRAP_IN: {
                        putchar('>');
                        uint16_t c = getchar() & 0xff;
                        putchar(c);
                        reg[R_R0] = c;
                        break;
                    }
                    case TRAP_PUTSP: {
                        uint16_t* c = memory + reg[R_R0];
                        while (*c) {
                            putc((char)(*c & 0xff), stdout);
                            putc((char)(*c & 0xff00), stdout);
                            ++c;
                        }
                        fflush(stdout);
                        break;
                    }
                    case TRAP_HALT: {
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        break;
                    }                    
                }
                break;
            }
            case OP_RES:
            case OP_RTI:
            default:
                abort();
                break;                                            
        }
    }
    restore_input_buffering();
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

void read_image_file(FILE* file) {
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    while (read-- > 0) {
        *p = swap16(*p);
        ++p;
    } 
}

int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; }
    read_image_file(file);
    fclose(file);
    return 1;
}

uint16_t swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}

void mem_write(uint16_t address, uint16_t val) {
    memory[address] = val;
}

uint16_t mem_read(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = 1 << 15;
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

void disable_input_buffering() {
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

uint16_t check_key() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}