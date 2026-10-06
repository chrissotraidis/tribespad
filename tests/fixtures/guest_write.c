#include <unistd.h>
ssize_t unsafe_write(int fd,const void *buffer,size_t size){return write(fd,buffer,size);}
