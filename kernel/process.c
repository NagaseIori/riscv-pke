/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// current points to the currently running user-mode application.
process* current = NULL;

// points to the first free page in our simple heap. added @lab2_2
uint64 g_ufree_page = USER_FREE_ADDRESS_START;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  // SSTATUS_SPP and SSTATUS_SPIE are defined in kernel/riscv.h
  // set S Previous Privilege mode (the SSTATUS_SPP bit in sstatus register) to User mode.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  // write x back to 'sstatus' register to enable interrupts, and sret destination mode.
  write_csr(sstatus, x);

  // set S Exception Program Counter (sepc register) to the elf entry pc.
  write_csr(sepc, proc->trapframe->epc);

  // make user page table. macro MAKE_SATP is defined in kernel/riscv.h. added @lab2_1
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // return_to_user() is defined in kernel/strap_vector.S. switch to user mode with sret.
  // note, return_to_user takes two parameters @ and after lab2_1.
  return_to_user(proc->trapframe, user_satp);
}

static void vm_area_split(pagetable_t pt, vm_area **head, uint64 begin, uint64 end) {
  vm_area *p = *head;
  while(p) {
    if(begin >= p->vm_start && end <= p->vm_end) {
      if(begin > p->vm_start) {
        vm_area *lvm = (vm_area *)user_va_to_pa(pt, (void *)p->vm_start);
        lvm->next = *head;
        *head = lvm;
        lvm->vm_start = p->vm_start;
        lvm->vm_end = begin;
      }
      if(end < p->vm_end) {
        // sprint("SPLIT Right vm_area 0x%lx:0x%lx\n", end, p->vm_end);
        vm_area *rvm = (vm_area *)user_va_to_pa(pt, (void *)end);
        // sprint("ACQUIRED PA:0x%lx\n", rvm);
        rvm->next = *head;
        *head = rvm;
        rvm->vm_start = end;
        rvm->vm_end = p->vm_end;
      }
      return;
    }
    p = p->next;
  }
  panic("vm_area_split");
}

static void vm_area_union(pagetable_t pt, vm_area **head, uint64 begin, uint64 end)
{
  vm_area *p = *head;
  while(p) {
    if(p->vm_end == begin || p->vm_start == end) {
      p->vm_start = begin<p->vm_start?begin:p->vm_start;
      p->vm_end = end>p->vm_end?end:p->vm_end;
      return;
    }
    p = p->next;
  }
  vm_area *nvm = (vm_area *)user_va_to_pa(pt, (void *)begin);
  nvm->next = *head;
  *head = nvm;
  nvm->vm_start = begin;
  nvm->vm_end = end;
  return;
}

void mmap_init(process *p) {
  process_mmap * mmap = &p->mmap;
  mmap->free = NULL;
  mmap->used = NULL;
  for(int i=0; i<PROCESS_HEAP_SIZE; i+=PGSIZE) {
    void* pa = alloc_page();
    user_vm_map(p->pagetable, g_ufree_page + i, PGSIZE, (uint64)pa, prot_to_type(PROT_WRITE | PROT_READ, 1));
  }
  // sprint("MMAP INIT START\n");
  vm_area *free = (vm_area *)user_va_to_pa(p->pagetable, (void*)g_ufree_page);
  free->next = NULL;
  free->vm_start = g_ufree_page;
  free->vm_end = g_ufree_page + PROCESS_HEAP_SIZE;
  g_ufree_page += PROCESS_HEAP_SIZE;
  mmap->free = free;
  // sprint("ACQUIRED PA:0x%lx\n", free);
  // sprint("MMAP INIT: VM_START:0x%lx VM_END:0x%lx\n", free->vm_start, free->vm_end);
}

uint64 mmap_map(process *p, int byte) {
  byte = ((byte>>3)+1)<<3;
  byte += 2*sizeof(vm_area);
  // sprint("REQUEST MEMORY SIZE BYTES:%d\n", byte);
  process_mmap *mmap = &p->mmap;
  vm_area *vmp = mmap->free;
  while(vmp) {
    if(vmp->vm_end - vmp->vm_start >= byte) {
      // sprint("ALLOCATE MEMORY:0x%lx:0x%lx\n", vmp->vm_start, vmp->vm_end);
      int start = vmp->vm_start, end = vmp->vm_end;
      vm_area_split(p->pagetable, &mmap->free, start, start+byte-sizeof(vm_area));
      vm_area_union(p->pagetable, &mmap->used, start, start+byte-sizeof(vm_area));
      // sprint("ALLOCATE FINISHED.\n");
      return start + sizeof(vm_area);
    }
    vmp = vmp->next;
  }
  if(!vmp)
    panic("mmap_map");
  return -1;
}

void mmap_unmap(process *p, int va) {
  process_mmap *mmap = &p->mmap;
  vm_area *vmp = mmap->used;
  while(vmp) {
    if(va >= vmp->vm_start && va < vmp->vm_end) {
      int start = vmp->vm_start, end = vmp->vm_end;
      vm_area_split(p->pagetable, &mmap->used, start, end);
      vm_area_union(p->pagetable, &mmap->free, start, end);
      return;
    }
    vmp = vmp->next;
  }
  if(!vmp)
    panic("mmap_unmap");
  return;
}