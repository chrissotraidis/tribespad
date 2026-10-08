#include "../port/core/guest_curl.h"
#include "../port/core/guest_callback.h"
#include "../port/core/native_winsock.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
extern pthread_t owner;
extern size_t live,counts[5];
extern int fail_next;
extern _cpu *curl_runtime_test_setup(void);
extern void curl_runtime_test_finish(void);
extern void curl_wrong_init(void),curl_wrong_multi(void),curl_wrong_cleanup(void),curl_wrong_global_cleanup(void);
extern char *curl_version(void);
extern void curl_wrong_version(void),curl_wrong_reset(void);
extern uint32_t version_expected;
extern void curl_bad_callback(void *),curl_bad_pointer(void *);
extern void curl_timer_bad_callback(void *),curl_timer_bad_data(void *),curl_timer_bad_width(void),curl_timer_dynamic_option(uint32_t),curl_timer_unknown_option(void);
extern void curl_socket_bad_callback(void *),curl_close_bad_callback(void *),curl_easy_guest_string(void *),curl_easy_wrong_long(void),curl_wrong_info_selector(void),curl_wrong_action_width(void);
extern uint32_t timer_calls,timer_multi,timer_timeout,timer_data,timer_result,timer_alternate;
extern uint32_t timer_reenter,timer_nested_result;
extern uint32_t socket_multi,socket_calls,socket_active,socket_what,socket_last,socket_removes,socket_assigns,socket_result;
extern uint32_t header_calls,progress_calls,body_bytes,header_abort,progress_abort,callback_easy;
extern uint64_t progress_max[4];
static uint32_t call(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *arguments) {
    return tribes_call_guest_cdecl(cpu,target,count,arguments,cpu->_esp-128);
}
static uint32_t append(_cpu *cpu,uint32_t list,const char *text){
    strcpy((char *)(uintptr_t)(cpu->_pointer_offset+0x124000),text);
    uint32_t a[]={list,0x124000};return call(cpu,23,2,a);
}
static void list_exercises(_cpu *cpu,const char *fault){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    uint32_t init[]={1,1,2,3,4,5};assert(!call(cpu,6,6,init));size_t initial=live;
    if(fault&&!strcmp(fault,"list-unreadable")){
        strcpy((char *)memory+0x124000,"Accept: text/plain");assert(tribes_memory_permissions_set(0x124000,4096,0));uint32_t a[]={0,0x124000};call(cpu,23,2,a);assert(0);
    }
    uint32_t list=append(cpu,0,"Accept: text/plain");assert(list);
    uint32_t node[2];memcpy(node,memory+list,8);assert(!node[1]&&!strcmp((char *)memory+node[0],"Accept: text/plain"));
    memset(memory+0x124000,'X',18);assert(!strcmp((char *)memory+node[0],"Accept: text/plain"));
    if(fault&&!strcmp(fault,"list-mutated")){memset(memory+list+4,0xff,4);append(cpu,list,"X-Test: changed");assert(0);}
    fail_next=1;assert(!append(cpu,list,"X-Test: OOM"));uint32_t next;memcpy(&next,memory+list+4,4);assert(!next);
    assert(append(cpu,list,"X-Test: retained")==list);memcpy(&next,memory+list+4,4);assert(next&&next!=list);memcpy(node,memory+next,8);assert(!node[1]&&!strcmp((char *)memory+node[0],"X-Test: retained"));
    if(fault&&!strcmp(fault,"list-live-cleanup")){call(cpu,9,0,NULL);assert(0);}
    uint32_t e=call(cpu,11,0,NULL),set[]={e,10023,list};assert(e&&!call(cpu,21,3,set));
    if(fault&&!strcmp(fault,"list-active-free")){uint32_t m=call(cpu,7,0,NULL),pair[]={m,e};assert(!call(cpu,13,2,pair));call(cpu,24,1,&list);assert(0);}
    call(cpu,24,1,&list);
    if(fault&&!strcmp(fault,"list-stale")){append(cpu,list,"X-Test: stale");assert(0);}
    if(fault&&!strcmp(fault,"list-stale-transfer")){uint32_t m=call(cpu,7,0,NULL),pair[]={m,e};call(cpu,13,2,pair);assert(0);}
    set[2]=0;assert(!call(cpu,21,3,set));call(cpu,12,1,&e);uint32_t zero=0;call(cpu,24,1,&zero);assert(live==initial);call(cpu,9,0,NULL);
}
static void url_exercises(_cpu *cpu,const char *fault){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    uint32_t init[]={1,1,2,3,4,5};assert(!call(cpu,6,6,init));size_t initial=live;
    fail_next=1;assert(!call(cpu,25,0,NULL)&&!fail_next&&live==initial);
    uint32_t u=call(cpu,25,0,NULL);assert(u);
    uint32_t set[]={u,0,0x124000,0x84};strcpy((char *)memory+0x124000,"example.com/path");
    if(fault&&!strcmp(fault,"url-unreadable")){assert(tribes_memory_permissions_set(0x124000,4096,0));call(cpu,26,4,set);assert(0);}
    assert(!call(cpu,26,4,set));set[1]=99;assert(call(cpu,26,4,set)==9);
    set[1]=6;strcpy((char *)memory+0x124000,"invalid-port");assert(call(cpu,26,4,set)==4);
    set[0]=0;assert(call(cpu,26,4,set)==1);set[0]=u;
    if(fault&&!strcmp(fault,"url-live-cleanup")){call(cpu,9,0,NULL);assert(0);}
    uint32_t e=call(cpu,11,0,NULL),option[]={e,10282,u};assert(e&&!call(cpu,21,3,option));
    if(fault&&!strcmp(fault,"url-active-free")){uint32_t m=call(cpu,7,0,NULL),pair[]={m,e};assert(!call(cpu,13,2,pair));call(cpu,27,1,&u);assert(0);}
    call(cpu,27,1,&u);
    if(fault&&!strcmp(fault,"url-stale")){call(cpu,26,4,set);assert(0);}
    if(fault&&!strcmp(fault,"url-stale-transfer")){uint32_t m=call(cpu,7,0,NULL),pair[]={m,e};call(cpu,13,2,pair);assert(0);}
    option[2]=0;assert(!call(cpu,21,3,option));call(cpu,12,1,&e);uint32_t zero=0;call(cpu,27,1,&zero);
    assert(live==initial);call(cpu,9,0,NULL);
}
static uint32_t base_socket(_cpu *cpu,uint32_t socket,const char *fault){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    memset(memory+0x126000,0xa5,32);memset(memory+0x127000,0xa5,32);
    uint32_t a[]={socket,0x48000022,0,0,0x126001,4,0x127001,0,0};
    if(fault&&!strcmp(fault,"base-readonly"))assert(tribes_memory_permissions_set(0x126000,4096,1));
    if(fault&&!strcmp(fault,"base-count-readonly"))assert(tribes_memory_permissions_set(0x127000,4096,1));
    if(fault&&!strcmp(fault,"base-cross-page")){a[4]=0x126ffe;assert(tribes_memory_permissions_set(0x127000,4096,0));}
    if(fault&&!strcmp(fault,"base-async"))a[7]=0x128000;
    tribes_set_last_error(0x1234);assert(!tribes_call_guest(cpu,32,9,a,cpu->_esp-128));assert(tribes_get_last_error()==0x1234);
    uint32_t h,n;memcpy(&h,memory+0x126001,4);memcpy(&n,memory+0x127001,4);assert(h==socket&&n==4&&memory[0x126000]==0xa5&&memory[0x126005]==0xa5&&memory[0x127000]==0xa5&&memory[0x127005]==0xa5);
    a[5]=3;assert(tribes_call_guest(cpu,32,9,a,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10014);assert(!memcmp(memory+0x126001,&h,4));
    a[5]=4;a[4]=0;assert(tribes_call_guest(cpu,32,9,a,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10014);
    a[4]=0x126001;a[6]=0;assert(tribes_call_guest(cpu,32,9,a,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10014);
    a[6]=0x127001;a[0]=0;assert(tribes_call_guest(cpu,32,9,a,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10038);
    return h;
}
static void borrowed_exercises(_cpu *cpu) {
    uint64_t base=cpu->_pointer_offset;unsigned char *memory=(unsigned char *)(uintptr_t)base;int owner_token=0;
    assert(tribes_wsa_startup(base,0x101,0x120000)==0);
    for(unsigned ipv6=0;ipv6<2;ipv6++) {
        int fd=socket(ipv6?AF_INET6:AF_INET,SOCK_STREAM,0);assert(fd>=0);
        struct sockaddr_storage native={0};socklen_t n;
        if(ipv6){struct sockaddr_in6 *p=(struct sockaddr_in6 *)&native;p->sin6_len=sizeof *p;p->sin6_family=AF_INET6;p->sin6_addr=in6addr_loopback;n=sizeof *p;}
        else {struct sockaddr_in *p=(struct sockaddr_in *)&native;p->sin_len=sizeof *p;p->sin_family=AF_INET;p->sin_addr.s_addr=htonl(INADDR_LOOPBACK);n=sizeof *p;}
        assert(bind(fd,(struct sockaddr *)&native,n)==0&&getsockname(fd,(struct sockaddr *)&native,&n)==0);
        uint32_t h=tribes_socket_attach(base,fd,&owner_token);assert(h!=UINT32_MAX&&tribes_socket_attach(base,fd,&owner_token)==h);
        h=base_socket(cpu,h,NULL);
        int duplicate=tribes_socket_duplicate(base,h);assert(duplicate>=0&&duplicate!=fd&&close(duplicate)==0);
        uint32_t capacity=28;memcpy(memory+0x122000,&capacity,4);memset(memory+0x121000,0xa5,32);
        assert(tribes_getsockname(base,h,0x121001,0x122000)==0);
        uint16_t family;memcpy(&family,memory+0x121001,2);assert(family==(ipv6?23:2));
        memcpy(&capacity,memory+0x122000,4);assert(capacity==(ipv6?28:16)&&memory[0x121000]==0xa5&&memory[0x121001+capacity]==0xa5);
        assert(!memcmp(memory+0x121003,(unsigned char *)&native+2,ipv6?26:6));
        capacity=628;memcpy(memory+0x122000,&capacity,4);assert(tribes_getsockopt(base,h,0xffff,0x2005,0x121000,0x122000)==0);
        uint32_t f;memcpy(&f,memory+0x121000+76,4);assert(f==(ipv6?23:2));
        assert(tribes_socket_detach(base,h,&owner_token)==fd&&fcntl(fd,F_GETFD)>=0);
        assert(tribes_socket_duplicate(base,h)==-1&&tribes_get_last_error()==10038);
        uint32_t query[]={h,0x48000022,0,0,0x126001,4,0x127001,0,0};assert(tribes_call_guest(cpu,32,9,query,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10038);
        uint32_t next=tribes_socket_attach(base,fd,&owner_token);assert(next!=h&&next!=UINT32_MAX);
        assert(tribes_socket_detach(base,next,&owner_token)==fd&&close(fd)==0);
    }
    assert(tribes_wsa_cleanup(base)==0);
}
static uint32_t completion(_cpu *cpu,uint32_t multi,uint32_t easy,uint32_t status){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;memset(memory+0x125000,0xa5,16);
    uint32_t args[]={multi,0x125001},words[3],remaining=99;
    uint32_t message=call(cpu,34,2,args);assert(message);memcpy(words,memory+message,12);
    assert(words[0]==1&&words[1]==easy&&words[2]==status);memcpy(&remaining,memory+0x125001,4);
    assert(!remaining&&memory[0x125000]==0xa5&&memory[0x125005]==0xa5);
    assert(!call(cpu,34,2,args));assert(!memcmp(words,memory+message,12));return message;
}
static void completion_exercises(_cpu *cpu,const char *fault){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t init[]={1,1,2,3,4,5};assert(!call(cpu,6,6,init));
    uint32_t m=call(cpu,7,0,NULL),a[]={m,0x125001};assert(m);
    if(fault&&!strcmp(fault,"completion-readonly"))assert(tribes_memory_permissions_set(0x125000,4096,1));
    if(fault&&!strcmp(fault,"completion-cross-page")){a[1]=0x125ffe;assert(tribes_memory_permissions_set(0x126000,4096,0));}
    assert(!call(cpu,34,2,a));assert(!fault);
    uint32_t easies[2],pairs[2][2];strcpy((char *)memory+0x121000,"http://[");
    for(unsigned i=0;i<2;i++){
        easies[i]=call(cpu,11,0,NULL);assert(easies[i]);uint32_t option[]={easies[i],10002,0x121000};assert(!call(cpu,21,3,option));
        pairs[i][0]=m;pairs[i][1]=easies[i];assert(!call(cpu,13,2,pairs[i]));
    }
    uint32_t action[]={m,UINT32_MAX,0,0x123000};assert(!call(cpu,20,4,action));
    uint32_t running;memcpy(&running,memory+0x123000,4);assert(!running);
    uint32_t first=call(cpu,34,2,a),words[3],remaining;assert(first);memcpy(words,memory+first,12);memcpy(&remaining,memory+a[1],4);
    assert(words[0]==1&&words[2]==3&&remaining==1&&(words[1]==easies[0]||words[1]==easies[1]));
    uint32_t other=words[1]==easies[0]?easies[1]:easies[0];uint32_t second=completion(cpu,m,other,3);assert(second!=first&&!memcmp(words,memory+first,12));
    assert(!call(cpu,14,2,pairs[0]));assert(!tribes_global_size(cpu->_pointer_offset,first)&&!tribes_global_size(cpu->_pointer_offset,second));
    assert(!call(cpu,14,2,pairs[1]));for(unsigned i=0;i<2;i++)call(cpu,12,1,&easies[i]);assert(!call(cpu,8,1,&m));
    assert(!call(cpu,34,2,a));call(cpu,9,0,NULL);
}
static void socket_exercises(_cpu *cpu,const char *url) {
    assert(url);unsigned char *memory=(unsigned char *)(uintptr_t)cpu->_pointer_offset;
    uint32_t init[]={1,1,2,3,4,5};assert(call(cpu,6,6,init)==0);
    assert(tribes_wsa_startup(cpu->_pointer_offset,0x101,0x120000)==0);
    socket_multi=call(cpu,7,0,NULL);assert(socket_multi);
    uint32_t opt[]={socket_multi,10002,0xfedcab01};assert(call(cpu,10,3,opt)==0);
    opt[1]=20001;opt[2]=19;assert(call(cpu,10,3,opt)==0);
    opt[1]=20004;opt[2]=15;assert(call(cpu,10,3,opt)==0);
    uint32_t stable=0,u=call(cpu,25,0,NULL),pooled=call(cpu,11,0,NULL);assert(u&&pooled);
    for(unsigned transfer=0;transfer<2;transfer++) {
        uint32_t e=pooled;
        snprintf((char *)memory+0x121000,1024,"%s/%s",url,transfer?"two":"one");
        uint32_t parts[]={u,0,0,0};assert(!call(cpu,26,4,parts));
        strcpy((char *)memory+0x121000,url);parts[2]=0x121000;parts[3]=0x84;assert(!call(cpu,26,4,parts));
        strcpy((char *)memory+0x121000,transfer?"/two":"/one");parts[1]=7;parts[3]=0;assert(!call(cpu,26,4,parts));
        strcpy((char *)memory+0x121000,"check=one two");parts[1]=8;parts[3]=0x80;assert(!call(cpu,26,4,parts));
        memset(memory+0x121000,'X',64); /* native URL owns all copied parts */
        uint32_t set[]={e,10282,u};assert(call(cpu,21,3,set)==0);
        set[1]=113;set[2]=UINT32_MAX;assert(call(cpu,21,3,set)==43);set[2]=3;assert(call(cpu,21,3,set)==43);set[2]=0;assert(!call(cpu,21,3,set));set[2]=1;assert(!call(cpu,21,3,set));
        memory[0x122000]='*';memory[0x122001]=0;set[1]=10177;set[2]=0x122000;assert(call(cpu,21,3,set)==0);
        set[1]=10103;set[2]=0xcdef5678;assert(call(cpu,21,3,set)==0);
        set[1]=155;set[2]=5000;assert(call(cpu,21,3,set)==0);
        uint32_t headers=append(cpu,0,transfer?"Accept:":"Accept: text/plain");assert(headers);
        assert(append(cpu,headers,transfer?"X-TribesPad: two":"X-TribesPad: one")==headers);
        assert(append(cpu,headers,"X-Empty;")==headers);
        set[1]=10023;set[2]=headers;assert(!call(cpu,21,3,set));
        uint32_t pair[]={socket_multi,e};assert(call(cpu,13,2,pair)==0);
        uint32_t running=1;memcpy(memory+0x123000,&running,4);
        uint32_t unrelated=append(cpu,0,"X-Unrelated: idle");assert(unrelated);call(cpu,24,1,&unrelated);
        uint32_t action[]={socket_multi,UINT32_MAX,0,0x123000};
        assert(call(cpu,20,4,action)==0);memcpy(&running,memory+0x123000,4);
        for(unsigned i=0;running&&i<100;i++) {
            action[1]=UINT32_MAX;action[2]=0;
            if(socket_active) {
                int fd=tribes_socket_duplicate(cpu->_pointer_offset,socket_active);assert(fd>=0);
                struct pollfd p={fd,(short)((socket_what&1?POLLIN:0)|(socket_what&2?POLLOUT:0)),0};
                int ready=poll(&p,1,100);assert(ready>=0);assert(close(fd)==0);
                if(ready){action[1]=socket_active;action[2]=(p.revents&POLLIN?1:0)|(p.revents&POLLOUT?2:0)|(p.revents&(POLLERR|POLLHUP|POLLNVAL)?4:0);}
            }
            assert(call(cpu,20,4,action)==0);memcpy(&running,memory+0x123000,4);
        }
        assert(!running&&!socket_active&&socket_calls&&socket_removes==transfer+1);
        uint32_t info[]={e,0x200002,0x123004};assert(call(cpu,18,3,info)==0);
        uint32_t code;memcpy(&code,memory+0x123004,4);assert(code==204);info[1]=0x200019;assert(!call(cpu,18,3,info));memcpy(&code,memory+0x123004,4);assert(!code);uint32_t message=completion(cpu,socket_multi,e,0);
        if(!transfer)stable=socket_last;else assert(socket_last==stable);
        int fd=tribes_socket_duplicate(cpu->_pointer_offset,stable);assert(fd>=0&&close(fd)==0);
        uint32_t assign[]={socket_multi,stable,0};assert(call(cpu,17,3,assign)==5);
        assert(call(cpu,14,2,pair)==0);assert(!tribes_global_size(cpu->_pointer_offset,message));call(cpu,35,1,&e);
        uint32_t private_info[]={e,0x100015,0x123004};assert(!call(cpu,18,3,private_info));
        memcpy(&code,memory+0x123004,4);assert(!code);
        call(cpu,24,1,&headers);
    }
    call(cpu,12,1,&pooled);call(cpu,27,1,&u);
    assert(socket_assigns>=2&&call(cpu,8,1,&socket_multi)==0);
    assert(tribes_socket_duplicate(cpu->_pointer_offset,stable)==-1&&tribes_get_last_error()==10038);
    /* Reject the first real socket notification. Curl must propagate the
       error and close the unpublished poll without leaking its handle. */
    socket_multi=call(cpu,7,0,NULL);uint32_t e=call(cpu,11,0,NULL);assert(socket_multi&&e);
    opt[0]=socket_multi;opt[1]=10002;opt[2]=0xfedcab01;assert(call(cpu,10,3,opt)==0);
    opt[1]=20001;opt[2]=19;assert(call(cpu,10,3,opt)==0);
    snprintf((char *)memory+0x121000,1024,"%s/abort",url);
    uint32_t set[]={e,10002,0x121000};assert(call(cpu,21,3,set)==0);
    set[1]=10177;set[2]=0x122000;assert(call(cpu,21,3,set)==0);
    uint32_t pair[]={socket_multi,e};assert(call(cpu,13,2,pair)==0);
    socket_result=UINT32_MAX;
    uint32_t action[]={socket_multi,UINT32_MAX,0,0x123000};assert(call(cpu,20,4,action)==11);
    socket_result=0;assert(call(cpu,14,2,pair)==0);call(cpu,12,1,&e);
    assert(call(cpu,8,1,&socket_multi)==0);
    assert(tribes_socket_duplicate(cpu->_pointer_offset,socket_last)==-1);
    socket_active=0;
    assert(tribes_wsa_cleanup(cpu->_pointer_offset)==0);call(cpu,9,0,NULL);
}
static void ipv6_exercise(_cpu *cpu,const char *url){
    assert(url);unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    uint32_t init[]={1,1,2,3,4,5};assert(!call(cpu,6,6,init));uint32_t e=call(cpu,11,0,NULL);assert(e);
    strcpy((char *)memory+0x121000,url);uint32_t option[]={e,10002,0x121000};assert(!call(cpu,21,3,option));
    strcpy((char *)memory+0x122000,"*");option[1]=10177;option[2]=0x122000;assert(!call(cpu,21,3,option));
    option[1]=113;option[2]=2;assert(!call(cpu,21,3,option));assert(!call(cpu,28,1,&e));
    uint32_t info[]={e,0x200002,0x123004},code;assert(!call(cpu,18,3,info));memcpy(&code,memory+0x123004,4);assert(code==204);
    call(cpu,12,1,&e);call(cpu,9,0,NULL);
}
static void select_exercises(_cpu *cpu,const char *fault){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;uint64_t base=cpu->_pointer_offset;
    assert(!tribes_wsa_startup(base,0x101,0x120000));int owner_token=0;
    int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);struct sockaddr_in addr={0};addr.sin_len=sizeof addr;addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(listener,(struct sockaddr *)&addr,sizeof addr)&&!listen(listener,1));socklen_t len=sizeof addr;assert(!getsockname(listener,(struct sockaddr *)&addr,&len));
    int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0&&!connect(fd,(struct sockaddr *)&addr,len));int peer=accept(listener,NULL,NULL);assert(peer>=0);
    uint32_t h=tribes_socket_attach(base,fd,&owner_token);assert(h!=UINT32_MAX);
    uint32_t set[]={1,h},timer[]={0,20000},a[]={0,0x126001,0,0,0x128000};
    memset(memory+0x126000,0xa5,32);memcpy(memory+0x126001,set,8);memcpy(memory+0x128000,timer,8);
    if(fault&&!strcmp(fault,"select-readonly")){a[1]=0;a[2]=0x126001;assert(tribes_memory_permissions_set(0x126000,4096,1));}
    if(fault&&!strcmp(fault,"select-cross-page")){a[1]=0x126ffe;assert(tribes_memory_permissions_set(0x127000,4096,0));}
    uint32_t result=tribes_call_guest(cpu,33,5,a,cpu->_esp-128);assert(!fault&&result==0);uint32_t count;memcpy(&count,memory+0x126001,4);assert(!count&&memory[0x126000]==0xa5&&memory[0x126009]==0xa5&&!memcmp(memory+0x128000,timer,8));
    assert(send(peer,"z",1,0)==1);memcpy(memory+0x126001,set,8);timer[0]=1;timer[1]=0;memcpy(memory+0x128000,timer,8);assert(tribes_call_guest(cpu,33,5,a,cpu->_esp-128)==1);memcpy(memory+0x126001,set,8);memcpy(memory+0x127001,set,8);a[2]=0x127001;
    tribes_set_last_error(0xabc);assert(tribes_call_guest(cpu,33,5,a,cpu->_esp-128)==2&&tribes_get_last_error()==0xabc);
    assert(!memcmp(memory+0x126001,set,8)&&!memcmp(memory+0x127001,set,8));char byte;assert(recv(fd,&byte,1,0)==1&&byte=='z');
    close(peer);memcpy(memory+0x126001,set,8);a[2]=0;timer[1]=0;memcpy(memory+0x128000,timer,8);a[4]=0;assert(tribes_call_guest(cpu,33,5,a,cpu->_esp-128)==1&&recv(fd,&byte,1,0)==0);
    assert(tribes_socket_detach(base,h,&owner_token)==fd);close(fd);close(listener);
    a[4]=0x128000;assert(tribes_call_guest(cpu,33,5,a,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10038);
    a[1]=0;assert(tribes_call_guest(cpu,33,5,a,cpu->_esp-128)==UINT32_MAX&&tribes_get_last_error()==10022);
    /* Release a reserved loopback port to obtain a real refused connection.
       Poll must leave SO_ERROR for the actual socket owner. */
    listener=socket(AF_INET,SOCK_STREAM,0);addr.sin_port=0;assert(listener>=0&&!bind(listener,(struct sockaddr *)&addr,sizeof addr));len=sizeof addr;assert(!getsockname(listener,(struct sockaddr *)&addr,&len));close(listener);
    fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0&&!fcntl(fd,F_SETFL,O_NONBLOCK));assert(connect(fd,(struct sockaddr *)&addr,len)==-1&&errno==EINPROGRESS);
    h=tribes_socket_attach(base,fd,&owner_token);assert(h!=UINT32_MAX);set[1]=h;memcpy(memory+0x126001,set,8);memcpy(memory+0x127001,set,8);timer[0]=1;timer[1]=0;memcpy(memory+0x128000,timer,8);a[2]=0x126001;a[3]=0x127001;
    assert(tribes_call_guest(cpu,33,5,a,cpu->_esp-128)==1);memcpy(&count,memory+0x126001,4);assert(!count&&!memcmp(memory+0x127001,set,8));
    int error=0;len=sizeof error;assert(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)&&error==ECONNREFUSED);
    assert(tribes_socket_detach(base,h,&owner_token)==fd);close(fd);assert(!tribes_wsa_cleanup(base));
}
static void http_callback_exercises(_cpu *cpu,const char *url,const char *fault){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    uint32_t init[]={1,1,2,3,4,5};assert(!call(cpu,6,6,init));
    uint32_t e=call(cpu,11,0,NULL);assert(e);callback_easy=e;
    uint32_t option[]={e,10010,0x124001};memset(memory+0x124000,0xa5,258);memory[0x124001]=0;
    if(fault&&!strcmp(fault,"error-readonly"))assert(tribes_memory_permissions_set(0x124000,4096,1));
    if(fault&&!strcmp(fault,"error-cross-page")){option[2]=0x124f80;assert(tribes_memory_permissions_set(0x125000,4096,0));}
    assert(!call(cpu,21,3,option));
    if(fault&&!strcmp(fault,"error-overlap")){option[0]=call(cpu,11,0,NULL);option[2]+=16;call(cpu,21,3,option);assert(0);}
    if(fault&&!strcmp(fault,"error-revoked")){assert(tribes_memory_permissions_set(0x124000,4096,1));call(cpu,28,1,&e);assert(0);}
    assert(!fault);
    snprintf((char *)memory+0x121000,1024,"%s/redirect",url);
    option[1]=10002;option[2]=0x121000;assert(!call(cpu,21,3,option));
    memory[0x122000]='*';memory[0x122001]=0;option[1]=10177;option[2]=0x122000;assert(!call(cpu,21,3,option));
    uint32_t settings[][2]={{10103,0x1234abcd},{10029,0xabcd1234},{20079,29},{10057,0xfedcba98},{20219,30},{43,0},{52,1},{155,5000},{10001,0x98765432},{20011,31}};
    for(unsigned i=0;i<sizeof settings/sizeof *settings;i++){option[1]=settings[i][0];option[2]=settings[i][1];assert(!call(cpu,21,3,option));}
    assert(call(cpu,28,1,&e)==18); /* actual advertised length exceeds payload */
    assert(header_calls>=6&&body_bytes==3&&progress_calls&&progress_max[0]==UINT64_C(4294967301)&&progress_max[1]==3);
    assert(memory[0x124000]==0xa5&&memory[0x124101]==0xa5&&memory[0x124001]&&memchr(memory+0x124001,0,256));
    snprintf((char *)memory+0x121000,1024,"%s/cancel",url);option[1]=10002;option[2]=0x121000;assert(!call(cpu,21,3,option));
    header_abort=1;assert(call(cpu,28,1,&e)==23);header_abort=0;
    progress_abort=1;assert(call(cpu,28,1,&e)==42);progress_abort=0;
    assert(memory[0x124001]&&memchr(memory+0x124001,0,256));
    /* Reset releases the error buffer and callbacks, and clears the URL. */
    call(cpu,35,1,&e);assert(tribes_memory_permissions_set(0x124000,4096,0));
    assert(call(cpu,28,1,&e)==3);
    uint32_t old_headers=header_calls,old_progress=progress_calls,old_body=body_bytes;
    snprintf((char *)memory+0x121000,1024,"%s/reset",url);
    option[1]=10002;option[2]=0x121000;assert(!call(cpu,21,3,option));
    option[1]=10177;option[2]=0x122000;assert(!call(cpu,21,3,option));
    assert(!call(cpu,28,1,&e));
    assert(header_calls==old_headers&&progress_calls==old_progress&&body_bytes==old_body);
    call(cpu,12,1,&e);callback_easy=0;assert(tribes_memory_permissions_set(0x124000,4096,3));call(cpu,9,0,NULL);
}
int main(int argc,char **argv) {
    owner=pthread_self(); _cpu *cpu=curl_runtime_test_setup();
    if(argc>1) {
        if(!strncmp(argv[1],"completion-",11)){completion_exercises(cpu,argv[1]);abort();}
        if(!strncmp(argv[1],"select-",7)){select_exercises(cpu,argv[1]);abort();}
        if(!strncmp(argv[1],"base-",5)){assert(!tribes_wsa_startup(cpu->_pointer_offset,0x101,0x120000));int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);int owner_token=0;uint32_t h=tribes_socket_attach(cpu->_pointer_offset,fd,&owner_token);assert(h!=UINT32_MAX);base_socket(cpu,h,argv[1]);abort();}
        if(!strncmp(argv[1],"error-",6)){http_callback_exercises(cpu,NULL,argv[1]);abort();}
        if(!strncmp(argv[1],"url-",4)){url_exercises(cpu,argv[1]);abort();}
        if(!strncmp(argv[1],"list-",5)){list_exercises(cpu,argv[1]);abort();}
        if(!strcmp(argv[1],"wrong-reset"))curl_wrong_reset();
        else if(!strcmp(argv[1],"wrong-init")) curl_wrong_init();
        else if(!strcmp(argv[1],"wrong-version"))curl_wrong_version();
        else if(!strcmp(argv[1],"version-protected")) {
            uint32_t v=call(cpu,22,0,NULL);assert(v);
            assert(tribes_memory_permissions_set(v&~4095u,4096,1));
            call(cpu,22,0,NULL);
        }
        else if(!strcmp(argv[1],"wrong-multi")) curl_wrong_multi();
        else if(!strcmp(argv[1],"wrong-cleanup")) curl_wrong_cleanup();
        else if(!strcmp(argv[1],"wrong-global-cleanup")) curl_wrong_global_cleanup();
        else if(!strcmp(argv[1],"bad-callback")) curl_bad_callback((void *)&puts);
        else if(!strcmp(argv[1],"guest-callback")) curl_bad_callback((void *)(uintptr_t)(cpu->_pointer_offset+0x100000));
        else if(!strcmp(argv[1],"guest-multi")) curl_bad_pointer((void *)(uintptr_t)(cpu->_pointer_offset+0x100000));
        else if(!strcmp(argv[1],"before-init")) (void)call(cpu,7,0,NULL);
        else if(!strcmp(argv[1],"timer-bad-callback")) curl_timer_bad_callback((void *)&puts);
        else if(!strcmp(argv[1],"timer-guest-callback")) curl_timer_bad_callback((void *)(uintptr_t)(cpu->_pointer_offset+0x100000));
        else if(!strcmp(argv[1],"timer-guest-data")) curl_timer_bad_data((void *)(uintptr_t)(cpu->_pointer_offset+0x100000));
        else if(!strcmp(argv[1],"wrong-timer-width")) curl_timer_bad_width();
        else if(!strcmp(argv[1],"wrong-timer-dynamic")) curl_timer_dynamic_option(20004);
        else if(!strcmp(argv[1],"wrong-timer-option")) curl_timer_unknown_option();
        else if(!strcmp(argv[1],"socket-bad-callback"))curl_socket_bad_callback((void *)&puts);
        else if(!strcmp(argv[1],"close-bad-callback"))curl_close_bad_callback((void *)&puts);
        else if(!strcmp(argv[1],"easy-guest-string"))curl_easy_guest_string((void *)(uintptr_t)(cpu->_pointer_offset+0x100000));
        else if(!strcmp(argv[1],"wrong-easy-long"))curl_easy_wrong_long();
        else if(!strcmp(argv[1],"wrong-info-selector"))curl_wrong_info_selector();
        else if(!strcmp(argv[1],"wrong-action-width"))curl_wrong_action_width();
        else if(!strncmp(argv[1],"borrowed-",9)) {
            assert(tribes_wsa_startup(cpu->_pointer_offset,0x101,0x120000)==0);
            int fd=socket(AF_INET,SOCK_STREAM,0),owner_token=0;assert(fd>=0);
            uint32_t h=tribes_socket_attach(cpu->_pointer_offset,fd,&owner_token);assert(h!=UINT32_MAX);
            if(!strcmp(argv[1],"borrowed-close"))tribes_closesocket(cpu->_pointer_offset,h);
            else if(!strcmp(argv[1],"borrowed-cleanup"))tribes_wsa_cleanup(cpu->_pointer_offset);
            else if(!strcmp(argv[1],"borrowed-owner"))tribes_socket_detach(cpu->_pointer_offset,h,&fd);
        }
        else {
            uint32_t init[]={1,1,2,3,4,5}; assert(call(cpu,6,6,init)==0);
            uint32_t e=call(cpu,11,0,NULL); assert(e);
            if(!strcmp(argv[1],"reset-stale")){call(cpu,12,1,&e);call(cpu,35,1,&e);}
            else if(!strcmp(argv[1],"reset-attached")){uint32_t pair[]={call(cpu,7,0,NULL),e};assert(!call(cpu,13,2,pair));call(cpu,35,1,&e);}
            else if(!strcmp(argv[1],"stale-easy")) { call(cpu,12,1,&e); call(cpu,12,1,&e); }
            else if(!strcmp(argv[1],"live-easy-cleanup")) call(cpu,9,0,NULL);
            else if(!strcmp(argv[1],"unsupported-option")) { uint32_t a[]={call(cpu,7,0,NULL),20007,15}; call(cpu,10,3,a); }
        }
        return 99;
    }
    borrowed_exercises(cpu);
    list_exercises(cpu,NULL);
    version_expected=call(cpu,22,0,NULL);assert(version_expected&&!live);
    char *version=(char *)(uintptr_t)(cpu->_pointer_offset+version_expected);
    assert(!strcmp(version,curl_version())&&strstr(version,"libcurl/8.22.0 LibreSSL/4.3.2"));
    version[0]='!';assert(call(cpu,22,0,NULL)==version_expected&&!strcmp(version,curl_version()));
    uint32_t args[]={1,1,2,3,4,0};
    assert(call(cpu,6,6,args)==2 && !live);
    args[5]=5; assert(call(cpu,6,6,args)==0);
    /* A valid later init increments references without replacing callbacks. */
    uint32_t again[]={1,99,99,99,99,99}; assert(call(cpu,6,6,again)==0);
    uint32_t handle=call(cpu,7,0,NULL); assert(handle && live);
    (void)call(cpu,9,0,NULL); /* One global reference remains. */
    uint32_t bad=0; assert(call(cpu,8,1,&bad)==1);
    assert(call(cpu,8,1,&handle)==0 && !live);
    assert(call(cpu,8,1,&handle)==1);
    fail_next=1; assert(!call(cpu,7,0,NULL) && !live && !fail_next);
    (void)call(cpu,9,0,NULL);
    assert(call(cpu,6,6,args)==0); handle=call(cpu,7,0,NULL); assert(handle);
    assert(call(cpu,8,1,&handle)==0); (void)call(cpu,9,0,NULL);
    assert(call(cpu,6,6,args)==0);
    handle=call(cpu,7,0,NULL); uint32_t e=call(cpu,11,0,NULL); assert(handle && e && handle!=e);
    uint32_t pair[]={handle,e},option[]={handle,10005,0xf1234567};
    assert(call(cpu,10,3,option)==0); option[1]=20004; option[2]=15;
    assert(call(cpu,10,3,option)==0 && !timer_calls);
    timer_reenter=1;
    assert(call(cpu,13,2,pair)==0);
    assert(timer_calls==1 && timer_multi==handle && timer_timeout==0 && timer_data==0xf1234567 && timer_nested_result==8);
    assert(call(cpu,13,2,pair)==7); /* Already attached, native status. */
    assert(call(cpu,14,2,pair)==0);
    assert(timer_calls==2 && timer_timeout==UINT32_MAX && timer_nested_result==8);
    timer_reenter=0;
    option[1]=10005; option[2]=0; assert(call(cpu,10,3,option)==0);
    option[1]=20004; option[2]=16; assert(call(cpu,10,3,option)==0);
    assert(call(cpu,13,2,pair)==0 && timer_calls==3 && timer_alternate==1 && timer_data==0);
    assert(call(cpu,14,2,pair)==0 && timer_calls==4 && timer_alternate==2 && timer_timeout==UINT32_MAX);
    option[2]=0; assert(call(cpu,10,3,option)==0);
    assert(call(cpu,13,2,pair)==0 && call(cpu,14,2,pair)==0 && timer_calls==4);
    option[2]=15; assert(call(cpu,10,3,option)==0); timer_result=UINT32_MAX;
    assert(call(cpu,13,2,pair)==11 && timer_calls==5); /* Real native callback abort. */
    timer_result=0;
    call(cpu,12,1,&e); assert(call(cpu,8,1,&handle)==0);
    /* A live easy can outlive its multi; native multi cleanup detaches it. */
    handle=call(cpu,7,0,NULL); e=call(cpu,11,0,NULL); pair[0]=handle; pair[1]=e;
    assert(call(cpu,13,2,pair)==0 && call(cpu,8,1,&handle)==0);
    call(cpu,12,1,&e);
    /* Typed registries reject swapped handles and stale multi references. */
    handle=call(cpu,7,0,NULL); e=call(cpu,11,0,NULL);
    uint32_t swapped[]={e,handle}; assert(call(cpu,13,2,swapped)==1);
    swapped[0]=handle; assert(call(cpu,13,2,swapped)==2);
    call(cpu,12,1,&e); assert(call(cpu,8,1,&handle)==0);
    fail_next=1; assert(!call(cpu,11,0,NULL) && !fail_next);
    call(cpu,9,0,NULL);
    url_exercises(cpu,NULL);
    socket_exercises(cpu,getenv("TRIBES_CURL_SOCKET_TEST_URL"));
    select_exercises(cpu,NULL);
    completion_exercises(cpu,NULL);
    ipv6_exercise(cpu,getenv("TRIBES_CURL_IPV6_TEST_URL"));
    http_callback_exercises(cpu,getenv("TRIBES_CURL_SOCKET_TEST_URL"),NULL);
    assert(call(cpu,22,0,NULL)==version_expected&&!strcmp(version,curl_version())&&!live);
    curl_runtime_test_finish();
    assert(counts[4] && counts[1]);
    puts("PASS: emitted curl ABI, timer/socket callbacks, nested PRIVATE/assign calls, two HTTP requests reusing one native connection and guest socket identity, polling removal versus final close, cleanup/reinitialization, allocation failure, CPU/FP and zero live curl allocations; persistent version storage");
    return 0;
}
