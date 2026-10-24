/* Starsiege: Tribes storage for the TribesPad apps (Mac picker and iOS). */
#ifndef TB_STARSIEGE_PATHS_H
#define TB_STARSIEGE_PATHS_H
/* gamedata: the bundle's GameData folder; storage: Application Support/TribesPad.
   Copies GameData/Starsiege/Game to storage/Starsiege/volume/TribesPad/Game when
   the bundled copy changed, keeping the player's config, temp and recordings.
   On success fills four strdup strings (image, volume, game, temp) and NULL. */
int tb_starsiege_prepare_paths(const char *gamedata,const char *storage,char *arguments[5]);
/* 1 when this app carries Starsiege's data. */
int tb_starsiege_bundled(const char *gamedata);
#endif
