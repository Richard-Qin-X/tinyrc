/* Source code for TinyRC.
   Copyright (C) 2026 Richard Qin

This file is part of TinyRC.

TinyRC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

TinyRC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with RCC; see the file LICENSE.  If not see
<http://www.gnu.org/licenses/>.  */

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Registers */

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

/* Condition Flags */

enum {
  FL_POS = 1 << 0, /* P */
  FL_ZRO = 1 << 1, /* Z */
  FL_NEG = 1 << 2, /* N */
};

/* Processor Status Register */

#define PSR_SUPERVISOR (1 << 15)
#define PSR_PRIORITY_MASK 0x0700
#define PSR_COND_MASK 0x0007

/* Opcodes */

enum {
  OP_BR = 0,
  OP_ADD,
  OP_LD,
  OP_ST,
  OP_JSR,
  OP_AND,
  OP_LDR,
  OP_STR,
  OP_RTI,
  OP_NOT,
  OP_LDI,
  OP_STI,
  OP_JMP,
  OP_RES,
  OP_LEA,
  OP_TRAP
};

/* Memory Mapped Registers */
enum {
  MR_KBSR = 0xFE00,
  MR_KBDR = 0xFE02,
  MR_DSR = 0xFE04,
  MR_DDR = 0xFE06,
  MR_TR = 0xFE08,
  MR_TMI = 0xFE0A,
  MR_MPR = 0xFE12,
  MR_MCR = 0xFFFE
};

/* TRAP Codes */

enum {
  TRAP_GETC = 0x20,
  TRAP_OUT = 0x21,
  TRAP_PUTS = 0x22,
  TRAP_IN = 0x23,
  TRAP_PUTSP = 0x24,
  TRAP_HALT = 0x25
};

/* Exception Vector */
enum { EXC_PROTECTION = 0x00, INT_TIMER = 0x01 };
#define EXCEPTION_VECTOR_BASE 0x0100

/* Machine Mode */
enum { MODE_SUPERVISOR = 0, MODE_USER = 1 };

/* Memory Storage */
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; /* 65536 locations */

/* Register Storage */
uint16_t reg[R_COUNT];

uint16_t psr;

int exception_raised = 0;

uint16_t ssp;
uint16_t usp;

uint16_t timer_ticks = 0;

#define SUPERVISOR_STACK_START 0xFDFF
#define USER_STACK_START 0xBFFF

/* Trap Return Stack */
#define TRAP_STACK_MAX 32

uint16_t trap_mode_stack[TRAP_STACK_MAX];
uint16_t trap_return_pc_stack[TRAP_STACK_MAX];
uint16_t trap_depth = 0;

/* Change Mode */

int
is_supervisor_mode(void) {
  return (psr & PSR_SUPERVISOR) != 0;
}

void
enter_supervisor_mode(void) {
  psr |= PSR_SUPERVISOR;
  // fprintf(stderr, "[VM] enter supervisor\n");
}

void
enter_user_mode(void) {
  psr &= ~PSR_SUPERVISOR;
  // fprintf(stderr, "[VM] enter user\n");
}

void
set_condition(uint16_t cond) {
  reg[R_COND] = cond;
  psr = (psr & ~PSR_COND_MASK) | cond;
}

void
push_trap_state(uint16_t return_pc) {
  if (trap_depth >= TRAP_STACK_MAX) {
    abort();
  }

  trap_mode_stack[trap_depth] = psr & PSR_SUPERVISOR;
  trap_return_pc_stack[trap_depth] = return_pc;
  ++trap_depth;
}

void
maybe_pop_trap_state(uint16_t target_pc) {
  if (trap_depth == 0) {
    return;
  }

  if (target_pc != trap_return_pc_stack[trap_depth - 1]) {
    return;
  }

  --trap_depth;

  psr = (psr & ~PSR_SUPERVISOR) | trap_mode_stack[trap_depth];
  // fprintf(stderr, "[VM] trap return, mode=%s\n",
  //        is_supervisor_mode() ? "supervisor" : "user");
}

/* Input Buffering */

// Unix like
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct termios original_tio;

void
disable_input_buffering() {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_lflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void
restore_input_buffering() {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t
check_key() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

// Windows
#elif defined(_WIN32) || defined(_WIN64)

#include <Windows.h>
#include <conio.h> // _kbhit

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void
disable_input_buffering() {
  hStdin = GetStdHandle(STD_INPUT_HANDLE);
  GetConsoleMode(hStdin, &fdwOldMode);     /* 保存旧模式 */
  fdwMode = fdwOldMode ^ ENABLE_ECHO_INPUT /* 不回显输入 */
            ^ ENABLE_LINE_INPUT;           /* 有一个或更多字符可用时返回 */
  SetConsoleMode(hStdin, fdwMode);         /* 设置新模式 */
  FlushConsoleInputBuffer(hStdin);         /* 清空缓冲区 */
}

void
restore_input_buffering() {
  SetConsoleMode(hStdin, fdwOldMode);
}

uint16_t
check_key() {
  return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

#endif

/* Handle Interrupt */
void
handle_interrupt(int signal) {
  restore_input_buffering();
  printf("\n");
  exit(-2);
}

/* Sign Extend */
uint16_t
sign_extend(uint16_t x, int bit_count) {
  if ((x >> (bit_count - 1)) & 1) {
    x |= (0xFFFF << bit_count);
  }
  return x;
}

/* Swap */
uint16_t
swap16(uint16_t x) {
  return (x << 8) | (x >> 8);
}

/* Update Flags */
void
update_flags(uint16_t r) {
  if (reg[r] == 0) {
    set_condition(FL_ZRO);
  } else if (reg[r] >> 15) {
    set_condition(FL_NEG);
  } else {
    set_condition(FL_POS);
  }
}

/* Read Image File */
void
read_image_file(FILE *file) {
  uint16_t origin;
  fread(&origin, sizeof(origin), 1, file);
  origin = swap16(origin);

  size_t max_read = MEMORY_MAX - origin;
  uint16_t *p = memory + origin;
  size_t read = fread(p, sizeof(uint16_t), max_read, file);

  while (read-- > 0) {
    *p = swap16(*p);
    ++p;
  }
}

int
read_image(const char *image_path) {
  FILE *file = fopen(image_path, "rb");
  if (!file) {
    return 0;
  };
  read_image_file(file);
  fclose(file);
  return 1;
}

/* Memory Access */
int
mpr_allows_user_access(uint16_t address) {
  uint16_t segment = address >> 12;
  uint16_t mask = 1 << segment;

  return (memory[MR_MPR] & mask) != 0;
}

void
push_exception_frame(uint16_t saved_pc, uint16_t saved_psr) {
  if ((saved_psr & PSR_SUPERVISOR) == 0) {
    usp = reg[R_R6];
    reg[R_R6] = ssp;
  }

  memory[--reg[R_R6]] = saved_psr;
  memory[--reg[R_R6]] = saved_pc;

  ssp = reg[R_R6];
}

void
raise_interrupt(uint16_t vector) {
  uint16_t saved_pc = reg[R_PC];
  uint16_t saved_psr = psr;

  push_exception_frame(saved_pc, saved_psr);

  enter_supervisor_mode();

  reg[R_PC] = memory[EXCEPTION_VECTOR_BASE + vector];
}

void
raise_exception(uint16_t vector) {
  uint16_t saved_pc = reg[R_PC];
  uint16_t saved_psr = psr;

  push_exception_frame(saved_pc, saved_psr);

  enter_supervisor_mode();

  exception_raised = 1;
  reg[R_PC] = memory[EXCEPTION_VECTOR_BASE + vector];
}

void
protection_fault(uint16_t address) {
  (void)address;
  raise_exception(EXC_PROTECTION);
}

int
is_device_register(uint16_t address) {
  return address >= 0xFE00;
}

void
check_memory_access(uint16_t address) {
  if (is_supervisor_mode()) {
    return;
  }

  if (!mpr_allows_user_access(address)) {
    protection_fault(address);
    return;
  }

  if (is_device_register(address)) {
    protection_fault(address);
    return;
  }
}

void
mem_write(uint16_t address, uint16_t val) {
  check_memory_access(address);

  if (exception_raised) {
    return;
  }

  if (address == MR_DDR) {
    putc((char)(val & 0xFF), stdout);
    fflush(stdout);
    memory[MR_DDR] = val;
    return;
  }

  if (address == MR_TMI) {
    memory[MR_TMI] = val;
    timer_ticks = 0;
    memory[MR_TR] = 0;
    return;
  }

  if (address == MR_TR) {
    memory[MR_TR] = val;
    return;
  }

  if (address == MR_MCR) {
    memory[MR_MCR] = val;
    return;
  }

  memory[address] = val;
}

uint16_t
mem_read(uint16_t address) {
  check_memory_access(address);

  if (exception_raised) {
    return 0;
  }

  if (address == MR_KBSR) {
    if (check_key()) {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }

  if (address == MR_DSR) {
    memory[MR_DSR] = (1 << 15);
  }

  return memory[address];
}

void
service_timer(void) {
  uint16_t interval = memory[MR_TMI];

  if (interval == 0) {
    return;
  }

  ++timer_ticks;
  memory[MR_TR] = timer_ticks;

  if (timer_ticks >= interval) {
    timer_ticks = 0;
    memory[MR_TR] = 0;

    raise_interrupt(INT_TIMER);
  }
}

int
main(int argc, char *argv[]) {
  /* Load Arguments */
  if (argc < 2) {
    printf("lc3 [image-file1] ...\n");
    exit(2);
  }

  for (int j = 1; j < argc; ++j) {
    if (!read_image(argv[j])) {
      printf("failed to load image: %s\n", argv[j]);
      exit(1);
    }
  }

  /* Setup */
  signal(SIGINT, handle_interrupt);
  disable_input_buffering();
  atexit(restore_input_buffering);

  /* since exactly one condition flag should be set at any given time, set the Z
   * flag */
  psr = PSR_SUPERVISOR | FL_ZRO;
  reg[R_COND] = FL_ZRO;
  ssp = SUPERVISOR_STACK_START;
  usp = USER_STACK_START;
  reg[R_R6] = ssp;

  /* set the PC to starting position */
  /* 0x3000 is the default */
  enum { PC_START_USER = 0x3000, PC_START_OS = 0x0200 };
  /* set the PC to OS entry point */
  reg[R_PC] = PC_START_OS;

  int running = 1;

  /* machine starts running */
  memory[MR_MCR] = 1 << 15;

  while (running && (memory[MR_MCR] & (1 << 15))) {
    exception_raised = 0;

    uint16_t psr_at_fetch = psr;

    /* FETCH */
    uint16_t instr = mem_read(reg[R_PC]++);
    if (exception_raised) {
      continue;
    }
    uint16_t op = instr >> 12;

    switch (op) {
    case OP_ADD: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      uint16_t imm_flag = (instr >> 5) & 0x1;

      if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[r0] = reg[r1] + imm5;
      } else {
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] + reg[r2];
      }

      update_flags(r0);
    } break;
    case OP_AND: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      uint16_t imm_flag = (instr >> 5) & 0x1;

      if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[r0] = reg[r1] & imm5;
      } else {
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] & reg[r2];
      }
      update_flags(r0);
    } break;
    case OP_NOT: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      reg[r0] = ~reg[r1];
      update_flags(r0);
    } break;
    case OP_BR: {
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      uint16_t cond_flag = (instr >> 9) & 0x7;
      uint16_t cond = psr & PSR_COND_MASK;

      if (cond_flag & cond) {
        reg[R_PC] += pc_offset;
      }
    } break;
    case OP_JMP: {
      uint16_t base_r = (instr >> 6) & 0x7;
      uint16_t low6 = instr & 0x3F;

      if (low6 == 0x01) {
        /* JMPT BaseR */
        ssp = reg[R_R6];
        reg[R_R6] = usp;

        reg[R_PC] = reg[base_r];
        enter_user_mode();
      } else if (low6 == 0x00) {
        /* JMP BaseR / RET */
        uint16_t target_pc = reg[base_r];

        reg[R_PC] = target_pc;

        if (base_r == R_R7) {
          maybe_pop_trap_state(target_pc);
        }
      } else {
        abort();
      }
    } break;
    case OP_JSR: {
      uint16_t long_flag = (instr >> 11) & 1;
      reg[R_R7] = reg[R_PC];
      if (long_flag) {
        uint16_t long_pc_offset = sign_extend(instr & 0x7FF, 11);
        reg[R_PC] += long_pc_offset; /* JSR */
      } else {
        uint16_t r1 = (instr >> 6) & 0x7;
        reg[R_PC] = reg[r1]; /* JSRR */
      }
    } break;
    case OP_LD: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

      uint16_t value = mem_read(reg[R_PC] + pc_offset);
      if (exception_raised) {
        break;
      }

      reg[r0] = value;
      update_flags(r0);
    } break;
    case OP_LDI: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

      uint16_t pointer = mem_read(reg[R_PC] + pc_offset);
      if (exception_raised) {
        break;
      }
      uint16_t value = mem_read(pointer);
      if (exception_raised) {
        break;
      }

      reg[r0] = value;
      update_flags(r0);
    } break;
    case OP_LDR: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      uint16_t offset = sign_extend(instr & 0x3F, 6);

      uint16_t value = mem_read(reg[r1] + offset);
      if (exception_raised) {
        break;
      }

      reg[r0] = value;
      update_flags(r0);
    } break;
    case OP_LEA: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      reg[r0] = reg[R_PC] + pc_offset;
      update_flags(r0);
    } break;
    case OP_ST: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      mem_write(reg[R_PC] + pc_offset, reg[r0]);
      if (exception_raised) {
        break;
      }
    } break;
    case OP_STI: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      uint16_t pointer = mem_read(reg[R_PC] + pc_offset);
      if (exception_raised) {
        break;
      }

      mem_write(pointer, reg[r0]);
      if (exception_raised) {
        break;
      }
    } break;
    case OP_STR: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      uint16_t offset = sign_extend(instr & 0x3F, 6);
      mem_write(reg[r1] + offset, reg[r0]);
      if (exception_raised) {
        break;
      }
    } break;
    case OP_TRAP: {
      uint16_t trapvect8 = instr & 0xFF;
      uint16_t return_pc = reg[R_PC];
      push_trap_state(return_pc);
      reg[R_R7] = return_pc;
      enter_supervisor_mode();
      reg[R_PC] = mem_read(trapvect8);
    } break;
    case OP_RES: {
      abort();
    } break;
    case OP_RTI: {
      if (!is_supervisor_mode()) {
        raise_exception(EXC_PROTECTION);
        break;
      }

      uint16_t saved_pc = memory[reg[R_R6]++];
      uint16_t saved_psr = memory[reg[R_R6]++];

      ssp = reg[R_R6];

      reg[R_PC] = saved_pc;
      psr = saved_psr;
      reg[R_COND] = psr & PSR_COND_MASK;

      if (!is_supervisor_mode()) {
        reg[R_R6] = usp;
      }
    } break;
    default:
      abort();
      break;
    }
    if (!exception_raised && ((psr_at_fetch & PSR_SUPERVISOR) == 0) &&
        !is_supervisor_mode()) {
      service_timer();
    }
  }

  restore_input_buffering();
  return 0;
}
