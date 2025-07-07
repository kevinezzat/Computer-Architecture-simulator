
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define INSTR_MEM_SIZE 1024
#define DATA_MEM_SIZE 2048
#define REG_COUNT 64

// Registers, PC, SREG
uint8_t registers[REG_COUNT] = {0};
uint16_t PC = 0;
uint8_t SREG = 0;

// Memories
uint16_t instr_mem[INSTR_MEM_SIZE] = {0}; // 16-bit each
uint8_t data_mem[DATA_MEM_SIZE] = {0};   // 8-bit each

// Pipeline stages
typedef struct {
    uint16_t instruction;
    int valid;
    uint8_t opcode, r1, r2, imm; // Decoded fields
    uint8_t r1_val, r2_val;      // Register values
    uint16_t addr;               // PC at fetch
} PipelineStage;

// Initialize stages to zero
PipelineStage IF = {0, 0, 0, 0, 0, 0, 0, 0, 0};
PipelineStage ID = {0, 0, 0, 0, 0, 0, 0, 0, 0};
PipelineStage EX = {0, 0, 0, 0, 0, 0, 0, 0, 0};

// Global variables
int cycle = 1;
int flush = 0; // For branch/jump
int program_size = 0; // Number of instructions loaded
int fetched_all = 0;  // Flag to indicate all instructions fetched

// Load instruction memory from program.txt
void loadProgram(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open program.txt\n");
        exit(1);
    }

    // Clear instruction memory
    memset(instr_mem, 0, sizeof(instr_mem));
    program_size = 0;

    char line[50];
    int addr = 0;
    while (fgets(line, 50, file) && addr < INSTR_MEM_SIZE) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        char op[10];
        int r1, r2;
        int imm;

        // Parse instruction
        if (sscanf(line, "%s R%d R%d", op, &r1, &r2) == 3) {
            // R-Format: ADD, SUB, MUL, EOR, BR
            uint16_t opcode = 0;
            if (strcmp(op, "ADD") == 0) opcode = 0;
            else if (strcmp(op, "SUB") == 0) opcode = 1;
            else if (strcmp(op, "MUL") == 0) opcode = 2;
            else if (strcmp(op, "EOR") == 0) opcode = 6;
            else if (strcmp(op, "BR") == 0) opcode = 7;
            else {
                printf("Unknown instruction: %s\n", line);
                exit(1);
            }
            if (r1 >= REG_COUNT || r2 >= REG_COUNT) {
                printf("Invalid register in %s: R%d or R%d\n", op, r1, r2);
                exit(1);
            }
            instr_mem[addr] = (opcode << 12) | (r1 << 6) | r2;
            addr++;
        }
        else if (sscanf(line, "%s R%d %d", op, &r1, &imm) == 3) {
            // I-Format: MOVI, BEQZ, ANDI, SAL, SAR, LDR, STR
            uint16_t opcode = 0;
            if (strcmp(op, "MOVI") == 0) opcode = 3;
            else if (strcmp(op, "BEQZ") == 0) opcode = 4;
            else if (strcmp(op, "ANDI") == 0) opcode = 5;
            else if (strcmp(op, "SAL") == 0) opcode = 8;
            else if (strcmp(op, "SAR") == 0) opcode = 9;
            else if (strcmp(op, "LDR") == 0) opcode = 10;
            else if (strcmp(op, "STR") == 0) opcode = 11;
            else {
                printf("Unknown instruction: %s\n", line);
                exit(1);
            }
            if (r1 >= REG_COUNT) {
                printf("Invalid register in %s: R%d\n", op, r1);
                exit(1);
            }
            // Allow 0 to 63 for LDR/STR, -32 to 31 for others
            if (opcode == 10 || opcode == 11) {
                if (imm < 0 || imm > 63) {
                    printf("Invalid immediate for %s: %d (must be 0 to 63)\n", op, imm);
                    exit(1);
                }
            }
            else {
                if (imm < -32 || imm > 31) {
                    printf("Invalid immediate for %s: %d (must be -32 to 31)\n", op, imm);
                    exit(1);
                }
            }
            instr_mem[addr] = (opcode << 12) | (r1 << 6) | (imm & 0x3F);
            addr++;
        }
        else {
            printf("Invalid instruction: %s\n", line);
            exit(1);
        }
    }
    program_size = addr;
    fclose(file);
}

// Decode instruction
void decode(PipelineStage *stage) {
    if (!stage->valid) return;

    stage->opcode = (stage->instruction >> 12) & 0xF; // Bits 15-12
    stage->r1 = (stage->instruction >> 6) & 0x3F;    // Bits 11-6
    stage->r2 = stage->instruction & 0x3F;           // Bits 5-0
    stage->imm = stage->r2;                          // Store immediate
    stage->r1_val = registers[stage->r1];
    if (stage->opcode <= 2 || stage->opcode == 6 || stage->opcode == 7) {
        // R-Format: ADD, SUB, MUL, EOR, BR
        stage->r2_val = registers[stage->r2];
    }
    else {
        stage->r2_val = 0; // Clear for I-Format
    }
}

// Execute instruction
void execute(PipelineStage *stage) {
    if (!stage->valid) return;

    uint8_t result = 0;
    int branch = 0;
    uint16_t branch_addr = 0;

    // Sign-extend immediate for most I-Format instructions
    int8_t imm = (stage->imm & 0x20) ? (stage->imm | 0xC0) : stage->imm;
    // Use unsigned immediate for LDR/STR
    uint8_t mem_addr = stage->imm & 0x3F; // 0 to 63

    switch (stage->opcode) {
        case 0: // ADD
            result = stage->r1_val + stage->r2_val;
            registers[stage->r1] = result;
            // Carry flag
            if ((uint16_t)(stage->r1_val + stage->r2_val) > 255) SREG |= (1 << 3);
            else SREG &= ~(1 << 3);
            // Overflow flag
            if ((stage->r1_val < 128 && stage->r2_val < 128 && result >= 128) ||
                (stage->r1_val >= 128 && stage->r2_val >= 128 && result < 128)) {
                SREG |= (1 << 2);
            }
            else SREG &= ~(1 << 2);
            // Negative, Zero flags
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            // Sign flag (N XOR V)
            SREG = (((SREG >> 1) & 1) ^ ((SREG >> 2) & 1)) ? (SREG | (1 << 4)) : (SREG & ~(1 << 4));
            printf("  [EX] ADD R%d = %d + %d = %d (C=%d, V=%d, N=%d, S=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, stage->r2_val, result,
                   (SREG >> 3) & 1, (SREG >> 2) & 1, (SREG >> 1) & 1, (SREG >> 4) & 1, SREG & 1);
            break;
        case 1: // SUB
            result = stage->r1_val - stage->r2_val;
            registers[stage->r1] = result;
            // Overflow flag
            if ((stage->r1_val < 128 && stage->r2_val >= 128 && result >= 128) ||
                (stage->r1_val >= 128 && stage->r2_val < 128 && result < 128)) {
                SREG |= (1 << 2);
            }
            else SREG &= ~(1 << 2);
            // Negative, Zero flags
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            // Sign flag (N XOR V)
            SREG = (((SREG >> 1) & 1) ^ ((SREG >> 2) & 1)) ? (SREG | (1 << 4)) : (SREG & ~(1 << 4));
            printf("  [EX] SUB R%d = %d - %d = %d (V=%d, N=%d, S=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, stage->r2_val, result,
                   (SREG >> 2) & 1, (SREG >> 1) & 1, (SREG >> 4) & 1, SREG & 1);
            break;
        case 2: // MUL
            result = stage->r1_val * stage->r2_val;
            registers[stage->r1] = result;
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            printf("  [EX] MUL R%d = %d * %d = %d (N=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, stage->r2_val, result,
                   (SREG >> 1) & 1, SREG & 1);
            break;
        case 3: // MOVI
            result = imm;
            registers[stage->r1] = result;
            printf("  [EX] MOVI R%d = %d\n", stage->r1, result);
            break;
        case 4: // BEQZ
            if (stage->r1_val == 0) {
                branch = 1;
                branch_addr = stage->addr + 1 + imm;
            }
            printf("  [EX] BEQZ R%d=%d, imm=%d, branch=%d to %d\n",
                   stage->r1, stage->r1_val, imm, branch, branch_addr);
            break;
        case 5: // ANDI
            result = stage->r1_val & imm;
            registers[stage->r1] = result;
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            printf("  [EX] ANDI R%d = %d & %d = %d (N=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, imm, result,
                   (SREG >> 1) & 1, SREG & 1);
            break;
        case 6: // EOR
            result = stage->r1_val ^ stage->r2_val;
            registers[stage->r1] = result;
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            printf("  [EX] EOR R%d = %d ^ %d = %d (N=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, stage->r2_val, result,
                   (SREG >> 1) & 1, SREG & 1);
            break;
        case 7: // BR
            branch = 1;
            branch_addr = (stage->r1_val << 8) | stage->r2_val;
            printf("  [EX] BR R%d=%d, R%d=%d, to %d\n",
                   stage->r1, stage->r1_val, stage->r2, stage->r2_val, branch_addr);
            break;
        case 8: // SAL
            result = stage->r1_val << (imm & 0x7); // Limit shift to 0-7
            registers[stage->r1] = result;
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            printf("  [EX] SAL R%d = %d << %d = %d (N=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, imm & 0x7, result,
                   (SREG >> 1) & 1, SREG & 1);
            break;
        case 9: // SAR
            result = (int8_t)stage->r1_val >> (imm & 0x7); // Limit shift to 0-7
            registers[stage->r1] = result;
            SREG = (result & 0x80) ? (SREG | (1 << 1)) : (SREG & ~(1 << 1));
            SREG = (result == 0) ? (SREG | 1) : (SREG & ~1);
            printf("  [EX] SAR R%d = %d >> %d = %d (N=%d, Z=%d)\n",
                   stage->r1, stage->r1_val, imm & 0x7, result,
                   (SREG >> 1) & 1, SREG & 1);
            break;
        case 10: // LDR
            result = data_mem[mem_addr];
            registers[stage->r1] = result;
            printf("  [EX] LDR R%d = mem[%d] = %d\n", stage->r1, mem_addr, result);
            break;
        case 11: // STR
            data_mem[mem_addr] = stage->r1_val;
            printf("  [EX] STR mem[%d] = R%d = %d\n", mem_addr, stage->r1, stage->r1_val);
            break;
    }

    // Handle branch
    if (branch) {
        if (branch_addr >= program_size) {
            printf("Branch address %d out of bounds\n", branch_addr);
            exit(1);
        }
        PC = branch_addr;
        flush = 1;
    }

    // Keep SREG bits 7-5 zero
    SREG &= 0x1F;
}

// Print pipeline stage content
void printPipeline(int cycle) {
    printf("Clock Cycle %d:\n", cycle);

    // IF Stage
    if (IF.valid) {
        printf("  [IF] PC=%d, Instruction=0x%04X\n", IF.addr, IF.instruction);
    } else {
        printf("  [IF] Idle\n");
    }

    // ID Stage
    if (ID.valid) {
        printf("  [ID] Instruction=0x%04X, opcode=%d, ", ID.instruction, ID.opcode);
        if (ID.opcode <= 2 || ID.opcode == 6 || ID.opcode == 7) {
            // R-Format
            printf("R%d=%d, R%d=%d\n", ID.r1, ID.r1_val, ID.r2, ID.r2_val);
        } else {
            // I-Format
            int8_t imm = (ID.imm & 0x20) ? (ID.imm | 0xC0) : ID.imm;
            printf("R%d=%d, imm=%d\n", ID.r1, ID.r1_val,
                   (ID.opcode == 10 || ID.opcode == 11) ? (ID.imm & 0x3F) : imm);
        }
    } else {
        printf("  [ID] Idle\n");
    }

    // EX Stage
    if (EX.valid) {
        execute(&EX); // Execute and print
    } else {
        printf("  [EX] Idle\n");
    }

    printf("\n");
}

// Print final state
void printFinal() {
    printf("Final State:\n");
    for (int i = 0; i < REG_COUNT; i++) {
        if (registers[i] != 0) printf("R%d: %d\n", i, registers[i]);
    }
    printf("SREG: 0x%02X (C=%d, V=%d, N=%d, S=%d, Z=%d)\n",
           SREG, (SREG >> 3) & 1, (SREG >> 2) & 1, (SREG >> 1) & 1, (SREG >> 4) & 1, SREG & 1);
    printf("PC: %d\n", PC);

    printf("Instruction Memory:\n");
    for (int i = 0; i < program_size; i++) {
        printf("inst[%d]: 0x%04X\n", i, instr_mem[i]);
    }
    printf("Data Memory:\n");
    for (int i = 0; i < DATA_MEM_SIZE; i++) {
        if (data_mem[i] != 0) printf("data[%d]: %d\n", i, data_mem[i]);
    }
}

int main() {
    loadProgram("program.txt");

    // Fetch first instruction before cycle 1
    if (program_size > 0) {
        IF.instruction = instr_mem[PC];
        IF.addr = PC;
        IF.valid = 1;
        PC++;
    }

    int done = 0;
    while (!done) {
        // Print pipeline state
        printPipeline(cycle);

        // Move pipeline
        EX = ID;
        ID = IF;

        // Decode
        decode(&ID);

        // Fetch
        if (!fetched_all && PC < program_size) {
            IF.instruction = instr_mem[PC];
            IF.addr = PC;
            IF.valid = 1;
            PC++;
        } else {
            IF.valid = 0;
            if (PC >= program_size) fetched_all = 1;
        }

        // Handle branch flush
        if (flush) {
            ID.valid = 0;
            IF.valid = 0;
            flush = 0;
        }

        cycle++;

        // Check if pipeline is empty and all instructions executed
        if (fetched_all && !IF.valid && !ID.valid && !EX.valid) {
            done = 1;
        }
    }

    printFinal();
    return 0;
} 