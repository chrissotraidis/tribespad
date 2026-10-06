#include <assert.h>
#include <pthread.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int enabled,entered,released;
void tribes_connect_test_hold(void){pthread_mutex_lock(&lock);enabled=1;entered=released=0;pthread_mutex_unlock(&lock);}
void tribes_connect_test_entered(void){pthread_mutex_lock(&lock);while(!entered)pthread_cond_wait(&changed,&lock);pthread_mutex_unlock(&lock);}
void tribes_connect_test_release(void){pthread_mutex_lock(&lock);released=1;enabled=0;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&lock);}
void tribes_socket_connect_observation(void){pthread_mutex_lock(&lock);if(enabled){entered=1;pthread_cond_broadcast(&changed);while(!released)pthread_cond_wait(&changed,&lock);}pthread_mutex_unlock(&lock);}
