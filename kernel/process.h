#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"
typedef struct vm_area_t vm_area;
// Process virtual memory map
typedef struct vm_area_t
{
  uint64 vm_start;
  uint64 vm_end;
  vm_area *next;
} vm_area;

typedef struct process_mmap_t
{
  vm_area *used, *free;
} process_mmap;


typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs;

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp;
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap;
  // saved user process counter
  /* offset:264 */ uint64 epc;

  // kernel page table. added @lab2_1
  /* offset:272 */ uint64 kernel_satp;
}trapframe;

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;
  process_mmap mmap;
}process;

// switch to run user app
void switch_to(process*);

// current running process
extern process* current;

// address of the first free page in our simple heap. added @lab2_2
extern uint64 g_ufree_page;

void mmap_init(process *p);
uint64 mmap_map(process *p, int byte);
void mmap_unmap(process *p, int va);

#endif
