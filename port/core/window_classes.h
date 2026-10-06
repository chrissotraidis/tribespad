#ifndef TRIBESPAD_WINDOW_CLASSES_H
#define TRIBESPAD_WINDOW_CLASSES_H
#include <stdint.h>
uint32_t tribes_register_class(uint64_t base,uint32_t descriptor);
uint32_t tribes_unregister_class(uint64_t base,uint32_t name,uint32_t instance);
/* Host window creation retains the class while it owns a live window.
   fields[9] is the retained class atom, independent of source-string lifetime. */
uint32_t tribes_class_acquire(uint64_t base,uint32_t name,uint32_t instance,uint32_t fields[10]);
void tribes_class_release(uint64_t base,uint32_t atom);
#endif
