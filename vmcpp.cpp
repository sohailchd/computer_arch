#include <cstdio>
#include <cassert>

#include "util.hpp"
#include "vm.hpp"
#include "vm_ops.hpp"

#define BUFF_LEN 256

const char * get_reg_string(int n)
{
    const char * reg_strings[] = {
        "R_R0", "R_R1", "R_R2", "R_R3", "R_R4", "R_R5", "R_R6", "R_R7", "R_PC", "R_COND", "R_COUNT"
    };
    return reg_strings[n];
}

void vm_init(struct vm_state * v, int use_debugger)
{
    assert(v != NULL);

    for (int i = 0; i < R_COUNT; i++) {
        v->reg[i] = 0;
    }

    for (int i = 0; i < VM_MEM_SIZE; i++) {
        v->mem[i] = 0;
    }

    v->ir = 0;
    v->running = 0;
    v->debugger_enabled = use_debugger;
    v->debugger_steps = -1;
    if (use_debugger) {
        vm_dbg_init(v);
    }
}

void vm_destroy(struct vm_state * v)
{
    assert(v != NULL);
    if (v->debugger_enabled) {
        vm_dbg_destroy(v);
    }
}

int vm_load(struct vm_state * v, const char * bin_loc)
{
    assert(v != NULL && bin_loc != NULL);

    int bytes;
    uint8_t * image = load_file(bin_loc, &bytes);
    if (!image) {
        return 1;
    }

    if (bytes % 2 == 1) {
        free(image);
        return 1;
    }

    uint32_t addr = (image[0] << 8) | image[1];
    for (int i = 2; i < bytes; i += 2) {
        uint16_t val = (image[i] << 8) | image[i + 1];
        assert(addr < VM_MEM_SIZE);
        v->mem[addr++] = val;
    }

    free(image);
    return 0;
}

int vm_execute(struct vm_state * v)
{
    assert(v != NULL);

    static int (* const vm_execute_instr[16])(struct vm_state *) = {
        //xx00    xx01      xx10      xx11
        vm_br,    vm_add,   vm_ld,    vm_st,      //00xx
        vm_jsr,   vm_and,   vm_ldr,   vm_str,     //01xx
        vm_rti,   vm_not,   vm_ldi,   vm_sti,     //10xx
        vm_jmp,   vm_res,   vm_lea,   vm_trap     //11xx
    };

    char buffer[BUFF_LEN];
    int status = 0;

    v->reg[R_PC] = PC_START;
    v->running = 1;

    if (v->debugger_enabled) {
        vm_dbg_run(v, "Use exit() or Ctrl-D (i.e. EOF) to exit");
    }

    while (v->running) {
        uint16_t pc = vm_reg_read(v, R_PC);
        v->ir = v->mem[pc];
        if (v->debugger_enabled && check_flag(&v->mem_conds, pc, VMDBG_MEM_EX)) {
            sprintf(buffer, "Debugger: Hit breakpoint at 0x%x\n", pc);
            vm_dbg_run(v, buffer);
        } else if (v->debugger_enabled && v->debugger_steps == 0) {
            vm_dbg_run(v, NULL);
            v->debugger_steps = -1;
        }

        vm_reg_write(v, R_PC, pc + 1, 0);

        uint16_t op = (v->ir) >> 12;
        int result = vm_execute_instr[op](v);

        switch (result) {
        case VM_NONE:
            break;

        case VM_HALT:
            v->running = 0;
            break;

        default:
            v->running = 1;
            status = 1;
            break;
        }

        if (v->debugger_enabled && v->debugger_steps != -1) {
            v->debugger_steps -= 1;
        }
    }

    return status;
}