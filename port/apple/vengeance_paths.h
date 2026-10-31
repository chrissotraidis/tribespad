/* Tribes: Vengeance storage on iOS (vengeance_paths.m). gamedata is the
   bundle's GameData folder, storage Application Support/TribesPad. */
#ifndef TB_VENGEANCE_PATHS_H
#define TB_VENGEANCE_PATHS_H
/* 1 when the app carries Vengeance: the engine's module images in
   GameData/Vengeance and a game folder (bundled, or copied in for development). */
int tb_vengeance_bundled(const char *gamedata,const char *storage);
/* Prepares Application Support/TribesPad/Vengeance and fills the engine's four
   arguments (images, volume, game folder, temp), then the source Content
   folder, which the engine reads in place (VENGEANCE_CONTENT). 0 on failure. */
int tb_vengeance_prepare_paths(const char *gamedata,const char *storage,char *arguments[6]);
#endif
