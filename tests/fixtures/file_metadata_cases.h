/* Shared emitted-ABI file-metadata cases; included by host and simulator drivers. */
/* Exercise metadata opens through both real stdcall wrappers, including their
   sharing lifetime and the distinction between a link object and its target. */
static uint32_t metadata_open(const char *name,uint32_t access,uint32_t share,uint32_t flags,unsigned wide){
    strcpy((char *)memory+0x11100,name);
    for(size_t i=0;i<=strlen(name);i++){uint16_t ch=(unsigned char)name[i];memcpy(memory+0x11000+2*i,&ch,2);}
    _cpu cpu;memset(&cpu,0x39,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1d003;
    uint32_t args[]={wide?0x11000:0x11100,access,share,0,3,flags,0};
    memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
    tribes_enter(&cpu,wide?2:1);expected._eax=cpu._eax;expected._esp+=sizeof args;
    assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0x1d003,args,sizeof args));
    return cpu._eax;
}
static void metadata_cases(const char *root){
    char linkpath[4096],dangling[4096],outside[4096],directory[4096],hardlink[4096],file[4096];
    snprintf(linkpath,sizeof linkpath,"%s/link",root);snprintf(dangling,sizeof dangling,"%s/dangling",root);
    snprintf(outside,sizeof outside,"%s/outside",root);snprintf(directory,sizeof directory,"%s/folder",root);
    snprintf(hardlink,sizeof hardlink,"%s/hard",root);snprintf(file,sizeof file,"%s/public.store",root);
    assert(!symlink("public.store",linkpath)&&!symlink("absent",dangling)&&!symlink("/",outside));
    assert(!mkdir(directory,0700)&&!link(file,hardlink));
    for(unsigned wide=0;wide<2;wide++){
        tribes_set_last_error(4321);
        uint32_t h=metadata_open("PuBlIc.StOrE",0x10180,7,0x02200000,wide);
        assert(h!=UINT32_MAX&&tribes_get_last_error()==4321&&tribes_get_file_type(h)==1);
        int fd=tribes_fd_borrow(h);struct stat st;assert(fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode));close(fd);
        assert(!tribes_read_file(base,h,0x14000,1,0x15000,0)&&tribes_get_last_error()==5);
        assert(!tribes_write_file(base,h,0x14000,1,0x15000,0)&&tribes_get_last_error()==5);
        assert(tribes_set_file_pointer(base,h,0,0,0)==UINT32_MAX&&tribes_get_last_error()==5);
        assert(metadata_open("hard",0x80000000,3,0,wide)==UINT32_MAX&&tribes_get_last_error()==32);
        assert(tribes_duplicate_handle(base,UINT32_MAX,h,UINT32_MAX,0x16000,0,0,2));
        uint32_t duplicate;memcpy(&duplicate,memory+0x16000,4);assert(tribes_close_handle(h));
        assert(metadata_open("hard",0,3,0,wide)==UINT32_MAX&&tribes_get_last_error()==32);
        assert(tribes_close_handle(duplicate));
        h=metadata_open("hard",0x80000000,3,0,wide);assert(h!=UINT32_MAX);
        assert(metadata_open("public.store",0x10180,7,0x02200000,wide)==UINT32_MAX&&tribes_get_last_error()==32);
        /* Attribute access alone does not demand FILE_SHARE_READ/WRITE. */
        uint32_t attrs=metadata_open("public.store",0x180,1,0x02200000,wide);assert(attrs!=UINT32_MAX);
        assert(tribes_close_handle(attrs)&&tribes_close_handle(h));
        h=metadata_open("public.store",0x180,0,0,wide);assert(h!=UINT32_MAX);
        attrs=metadata_open("hard",0x180,0,0,wide);assert(attrs!=UINT32_MAX);
        assert(tribes_close_handle(h)&&tribes_close_handle(attrs));
        assert(metadata_open("cacert.pem",0x10180,7,0x02200000,wide)==UINT32_MAX&&tribes_get_last_error()==2);
        assert(metadata_open("missing/child",0x10180,7,0x02200000,wide)==UINT32_MAX&&tribes_get_last_error()==3);
        assert(metadata_open("folder",0x180,7,0,wide)==UINT32_MAX&&tribes_get_last_error()==5);
        h=metadata_open("folder",0x10180,7,0x02200000,wide);assert(h!=UINT32_MAX);
        fd=tribes_fd_borrow(h);assert(fd>=0&&!fstat(fd,&st)&&S_ISDIR(st.st_mode));close(fd);assert(tribes_close_handle(h));
        const char *links[]={"link","dangling","outside"};
        for(unsigned i=0;i<3;i++){
            h=metadata_open(links[i],0x10180,7,0x02200000,wide);assert(h!=UINT32_MAX&&tribes_get_file_type(h)==1);
            fd=tribes_fd_borrow(h);assert(fd>=0&&!fstat(fd,&st)&&S_ISLNK(st.st_mode));close(fd);assert(tribes_close_handle(h));
        }
        /* Link-object reservations must not alias the target inode. */
        h=metadata_open("link",0x10180,0,0x02200000,wide);assert(h!=UINT32_MAX);
        uint32_t target=metadata_open("public.store",0x80000000,0,0x02200000,wide);assert(target!=UINT32_MAX);
        fd=tribes_fd_borrow_access(target,TRIBES_FILE_READ);char contents[6];
        assert(fd>=0&&pread(fd,contents,6,0)==6&&!memcmp(contents,"TRIBES",6));close(fd);
        assert(metadata_open("link",0x80000000,7,0,wide)==UINT32_MAX&&tribes_get_last_error()==32);
        assert(tribes_close_handle(target)&&tribes_close_handle(h));
        h=metadata_open("link",0x180,7,0,wide);assert(h!=UINT32_MAX);
        fd=tribes_fd_borrow(h);assert(fd>=0&&!fstat(fd,&st)&&st.st_ino==fixture.st_ino);close(fd);assert(tribes_close_handle(h));
        assert(metadata_open("dangling",0x180,7,0,wide)==UINT32_MAX&&tribes_get_last_error()==2);
        assert(metadata_open("outside",0x180,7,0,wide)==UINT32_MAX&&tribes_get_last_error()==5);
        assert(metadata_open("outside/tmp",0x180,7,0x02200000,wide)==UINT32_MAX&&tribes_get_last_error()==5);
    }
    assert(!unlink(linkpath)&&!unlink(dangling)&&!unlink(outside)&&!unlink(hardlink)&&!rmdir(directory));
    /* Restore names used by subsequent protected-pointer checks. */
    strcpy((char *)memory+0x11100,"public.store");
    for(size_t i=0;i<=strlen("public.store");i++){uint16_t ch=(unsigned char)"public.store"[i];memcpy(memory+0x11000+2*i,&ch,2);}
}
