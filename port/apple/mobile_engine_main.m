/* Full original-engine entry through SDL's UIKit lifecycle. */
#import <Foundation/Foundation.h>
#include "mobile_game_paths.h"
#include "sdl_main.h"
#include <stdio.h>
#include <unistd.h>
#include "mobile-engine-launch-config.h"
extern int tribes_mobile_engine_main(int,char **);
extern void tb_mobile_controls_install(void);
static int enter_engine(int argc,char **argv) {
    (void)argc;(void)argv;
#if TB_BUNDLED_GAME_DATA
    char *launch_arguments[9];
    NSURL *support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    if (!support) { fprintf(stderr,"TribesPad: no app support directory\n"); return 125; }
    NSString *storage = [support.path stringByAppendingPathComponent:@"TribesPad"];
    NSString *resources = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"GameData"];
    if (!tb_mobile_prepare_paths(resources.fileSystemRepresentation, storage.fileSystemRepresentation, launch_arguments)) return 125;
    const char *launch_workdir = storage.fileSystemRepresentation;
#endif
    if(chdir(launch_workdir)){perror("TribesPad working directory");return 125;}
    fprintf(stderr,"TribesPad iPad: entering full checked original engine\n");
    fprintf(stderr,"TribesPad iPad: GPU elapsed-time profiling unavailable; original pre-sample scheduling estimate retained\n");
    tb_mobile_controls_install();
    return tribes_mobile_engine_main(8,launch_arguments);
}
int main(int argc,char **argv){return tribes_sdl_run_application(argc,argv,enter_engine);}
