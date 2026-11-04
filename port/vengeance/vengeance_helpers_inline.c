/* Small helpers the translated code calls at almost every step, as bitcode that
   scripts/vengeance/build_engine.py links into each translated module (internal
   there), so the compiler inlines them where the game's code calls them. With
   their constant arguments (operation, register, width, count) a shift folds to
   a few instructions instead of a call through two functions. The executable
   still links the originals (port/core/shift.c, tribes_shift.c, guest_dispatch.c,
   guest_teb.c) for everything else. */
#include "../core/shift.c"
#undef CF
#undef PF
#undef ZF
#undef SF
#undef OF
#include "../llasm-support/tribes_shift.c"

/* guest_dispatch.c's, with its rare report out of line. */
extern void tribes_dispatch_null_call(_cpu *cpu);
void tribes_set_dispatch_target(_cpu *cpu,uint32_t target) {
    if(__builtin_expect(!target,0))tribes_dispatch_null_call(cpu);
    cpu->_dispatch_target=target;
}

/* guest_teb.c's fs: access. Every game function with a C++ destructor or a
   __try links itself into the exception chain at fs:[0] on entry and out on
   return: a tenth of the game thread in a menu that lists maps. That word is a
   load or store here, once the thread has its address (vengeance_seh.c). */
extern _Thread_local uintptr_t vengeance_fs_chain;
extern uintptr_t vengeance_fs_chain_find(void);
extern uint32_t vengeance_fs_read(uint32_t offset);
extern void vengeance_fs_write(uint32_t offset,uint32_t value);
uint32_t x86_read_fs_dword(uint32_t offset) {
    if(__builtin_expect(offset!=0,0))return vengeance_fs_read(offset);
    uintptr_t chain=vengeance_fs_chain;
    if(__builtin_expect(!chain,0))chain=vengeance_fs_chain_find();
    uint32_t value;__builtin_memcpy(&value,(const void *)chain,4);return value;
}
void x86_write_fs_dword(uint32_t offset,uint32_t value) {
    if(__builtin_expect(offset!=0,0)){vengeance_fs_write(offset,value);return;}
    uintptr_t chain=vengeance_fs_chain;
    if(__builtin_expect(!chain,0))chain=vengeance_fs_chain_find();
    __builtin_memcpy((void *)chain,&value,4);
}
