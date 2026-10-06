/* Development app entry for the complete checked original engine.
   Runtime files stay in the workspace; no guest execution is replaced here. */
#include <mach-o/dyld.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "macos-engine-launch-config.h"
int main(void) {
    char executable[PATH_MAX],engine[PATH_MAX];uint32_t size=sizeof executable;
    if(_NSGetExecutablePath(executable,&size)||!realpath(executable,engine))return 125;
    char *leaf=strrchr(engine,'/');if(!leaf)return 125;
    if((size_t)(leaf-engine)+sizeof "/TribesPadEngine">sizeof engine)return 125;
    strcpy(leaf,"/TribesPadEngine");
    if(!freopen(launch_stdout,"w",stdout)||!freopen(launch_stderr,"w",stderr))return 125;
    FILE *pid=fopen(launch_pid,"w");if(!pid)return 125;
    fprintf(pid,"%ld\n",(long)getpid());fclose(pid);
    fprintf(stderr,"TribesPad application launcher: entering checked engine, pid=%ld\n",(long)getpid());fflush(stderr);
    if(chdir(launch_workdir)){fprintf(stderr,"TribesPad working directory unavailable: %s\n",strerror(errno));return 125;}
    launch_arguments[0]=engine;
    execv(engine,launch_arguments);
    fprintf(stderr,"TribesPad engine exec failed: %s\n",strerror(errno));return 126;
}
