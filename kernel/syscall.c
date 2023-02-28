/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"

#include "elf.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

//
// implement the SYS_user_print_backtrace
//
ssize_t sys_user_print_backtrace(uint64 floors)
{
  if (floors == 0)
    return 0;
  
  // print backtrace
  uint64 r_address = current->trapframe->regs.ra;
  uint64 *fp = (uint64 *)(current->trapframe->regs.s0);
  fp = (uint64 *)(*(fp - 1));
  elf_header ehdr = cur_elf_ctx.ehdr;

  // sprint("here\n");
  // find symtab & strtab
  Elf64_Shdr elf_symtab, elf_strtab;
  uint16 shnum = ehdr.shnum;
  uint64 shoff = ehdr.shoff;
  uint16 shentsize = ehdr.shentsize;
  uint16 shstrndx = ehdr.shstrndx;

  // sprint("here\n");
  for (uint16 i = 0; i < shnum; i++)
  {
    Elf64_Shdr cur;
    // the shoff is the offset to the beginning of the elf file which is not is the memory right now
    elf_fpread(&cur_elf_ctx, &cur, sizeof(cur), shoff + i * shentsize);
    // sprint("here\n");
    if (cur.sh_type == SHT_SYMTAB)
    {
      // sprint("here\n");
      elf_symtab = cur;
    }
    // shstrndx is not what we want
    if (cur.sh_type == SHT_STRTAB && i != shstrndx)
    {
      elf_strtab = cur;
    }
  }
  // sprint("here\n");
  char str_ctl[elf_strtab.sh_size];
  elf_fpread(&cur_elf_ctx, str_ctl, elf_strtab.sh_size, elf_strtab.sh_offset); // the strtab is arranged as char
  // for(int i=0;i<elf_strtab.sh_size;i++) sprint("%c", str_ctl[i]);

  // prepare the backtrace variable
  uint64 stnum = elf_symtab.sh_size / elf_symtab.sh_entsize;
  uint64 stbase = elf_symtab.sh_offset;
  uint64 stentsize = elf_symtab.sh_entsize;
  // sprint("here\n");

  // backtrace
  uint64 cur_layer = 0;
  char *fname = NULL;
  while (cur_layer < floors)
  {
    // sprint("%lld\n", fp);
    r_address = (*(fp - 1));
    // sprint("%x\n", r_address);
    // find the function address according to the return address

    for (uint64 i = 0; i < stnum; i++)
    {
      Elf64_Sym cur;
      elf_fpread(&cur_elf_ctx, &cur, sizeof(cur), stbase + i * stentsize);
      if ((cur.st_info & 0xf) == STT_FUNC && r_address >= cur.st_value && r_address < cur.st_value + cur.st_size)
      {
        // decode the symbol in strtab
        // sprint("%x\n", cur.st_value);
        fname = &str_ctl[cur.st_name];
        break;
      }
    }

    cur_layer++;
    if (fname != NULL)
    {
      sprint("%s\n", fname);
      if (strcmp(fname, "main") == 0)
        break;
    }
    else
    {
      sprint("function name don't exist!");
      return 1;
    }
    // sprint("here\n");
    // update the return address
    fp = (uint64 *)(*(fp - 2));
    // sprint("here\n");
  }

  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
