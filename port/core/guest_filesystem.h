#ifndef TRIBESPAD_GUEST_FILESYSTEM_H
#define TRIBESPAD_GUEST_FILESYSTEM_H
#include <stddef.h>
#include <stdint.h>
/* Immutable, explicit mounts: C:\ root, C:\TribesPad\Game and Temp.
   Initial process current directory is C:\TribesPad\Game. Set/Get share process state. Existing ASCII paths only for now. */
int tribes_filesystem_configure(uint64_t base,const char *volume,const char *game,const char *temp);
int tribes_filesystem_open_create(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int mode,int *created);
int tribes_filesystem_create_directory(uint64_t base,const char *guest);
int tribes_filesystem_resolve(uint64_t base,const char *guest,char *host,size_t capacity);
/* Resolve the final link itself; intermediate links retain mount confinement. */
int tribes_filesystem_resolve_link(uint64_t base,const char *guest,char *host,size_t capacity);
/* One CWD snapshot for both the host path and canonical C: name. */
int tribes_filesystem_resolve_open(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int final_link);
uint32_t tribes_get_logical_drive_strings(uint64_t base,uint32_t capacity,uint32_t output);
uint32_t tribes_get_drive_type(uint64_t base,uint32_t root);
uint32_t tribes_disk_free_space(uint64_t base,uint32_t root,uint32_t sectors,uint32_t bytes,uint32_t free_clusters,uint32_t total_clusters);
uint32_t tribes_set_current_directory(uint64_t base,uint32_t name);
uint32_t tribes_get_current_directory(uint64_t base,uint32_t capacity,uint32_t output);
#endif
