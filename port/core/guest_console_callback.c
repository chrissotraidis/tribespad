#include "guest_console_callback.h"
#include "guest_console.h"
#include "guest_heap.h"
#include <signal.h>
#include <stdlib.h>
uint32_t tribes_console_dispatch(_cpu *cpu,uint32_t event){
    if(!cpu){tribes_set_last_error(87);return TRIBES_CONSOLE_ERROR;}
    TribesConsoleDispatch *d=NULL;if(!tribes_console_begin(cpu->_pointer_offset,event,&d))return TRIBES_CONSOLE_ERROR;
    if(tribes_console_ignored(d)){tribes_console_end(d);return TRIBES_CONSOLE_IGNORED;}
    uint32_t address,handled=0;
    while((address=tribes_console_next(d))){
        if(cpu->_esp<128){tribes_console_end(d);abort();}
        if(tribes_call_guest(cpu,address,1,&event,cpu->_esp-64)){handled=1;break;}
    }
    tribes_console_end(d);
    return (handled?TRIBES_CONSOLE_HANDLED:0)|((!handled||event==2||event==5||event==6)?TRIBES_CONSOLE_TERMINATE:0);
}
uint32_t tribes_console_deliver_posix(_cpu *cpu,int signal_number){
    uint32_t event;
    switch(signal_number){case SIGINT:event=0;break;case SIGQUIT:event=1;break;case SIGHUP:event=2;break;default:tribes_set_last_error(50);return TRIBES_CONSOLE_ERROR;}
    return tribes_console_dispatch(cpu,event);
}
