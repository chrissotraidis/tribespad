#ifndef TRIBES_GL4ES_CONTEXT_H
#define TRIBES_GL4ES_CONTEXT_H
#include <SDL3/SDL.h>
typedef struct TribesGL4ESContext TribesGL4ESContext;
/* Caller creates/makes current the native GLES2 context. Main-thread only. */
TribesGL4ESContext *tribes_gl4es_context_create(SDL_Window *, SDL_GLContext, TribesGL4ESContext *shared);
void tribes_gl4es_context_activate(TribesGL4ESContext *);
void tribes_gl4es_context_destroy(TribesGL4ESContext *);
bool tribes_gl4es_context_swap(TribesGL4ESContext *);
uint64_t tribes_gl4es_presented_frames(void);
uint64_t tribes_gl4es_take_longest_frame_ns(void);
void tribes_gl4es_resume_frame_stats(void);
void tribes_gl4es_note_gpu_wait(uint64_t ns);
#endif
