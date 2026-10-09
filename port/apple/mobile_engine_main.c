/* Full original-engine entry through SDL's UIKit lifecycle. Development
   Simulator builds currently use the explicitly prepared workspace assets. */
#include "sdl_main.h"
#include <stdio.h>
#include <unistd.h>
#include "mobile-engine-launch-config.h"
extern int tribes_mobile_engine_main(int,char **);
static int enter_engine(int argc,char **argv) {
    (void)argc;(void)argv;
    if(chdir(launch_workdir)){perror("TribesPad workspace");return 125;}
    fprintf(stderr,"TribesPad iPad: entering full checked original engine\n");
    fprintf(stderr,"TribesPad iPad: GPU elapsed-time profiling unavailable; original pre-sample scheduling estimate retained\n");
    return tribes_mobile_engine_main(8,launch_arguments);
}
int main(int argc,char **argv){return tribes_sdl_run_application(argc,argv,enter_engine);}
