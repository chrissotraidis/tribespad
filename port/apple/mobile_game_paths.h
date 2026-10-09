#ifndef TB_MOBILE_GAME_PATHS_H
#define TB_MOBILE_GAME_PATHS_H
/* Resolve bundled read-only inputs and initialize the writable game profile.
   On success the caller owns eight strdup strings and a NULL terminator. */
int tb_mobile_prepare_paths(const char *resources, const char *storage, char *arguments[9]);
#endif
