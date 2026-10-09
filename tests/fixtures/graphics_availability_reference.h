/* Frozen pre-optimization platform policy from 0665fd7.
   Independent reference for all generated exports, not a rendering backend. */
static int reference_available(const GraphicsExport *e){
#if TARGET_OS_IPHONE
#ifdef TB_GL4ES
    if(!strcmp(e->name,"glDrawBuffer"))return 1; /* bounded default-back selection */
    static const char *legacy[]={"glPrimitiveRestartIndex","glFenceSync","glIsSync","glClientWaitSync","glGetSynciv","glDeleteSync","glDrawArrays","glTexCoord2fv","glLoadMatrixf","glTexGeni","glTexGenfv","glPolygonMode","glAlphaFunc","glFogCoordPointerEXT","glVertex3fv","glFogi","glFogf","glFogfv","glLightfv","glLightf","glLightModelfv","glRotatef","glTexCoord2f","glTranslatef","glColor3ub","glVertex3f","glColor4f","glVertex2i","glPushMatrix","glPopMatrix","glScalef","glFrustum","glMultMatrixf","glMultMatrixd","glColor4fv","glMaterialfv","glTexEnvfv","glTexEnvf","glNormalPointer","glBegin","glVertex2f","glEnd","glTexEnvi","glColor4ub","glEnableClientState","glDisableClientState","glColorPointer","glVertexPointer","glTexCoordPointer","glDrawElements","glMatrixMode","glLoadIdentity","glLoadMatrixd","glClientActiveTextureARB","glMultiTexCoord2fARB","glMultiTexCoord2fvARB","glMultiTexCoord3fvARB"};
    for(unsigned i=0;i<sizeof legacy/sizeof *legacy;i++)if(!strcmp(e->name,legacy[i]))return 1;
#endif
    if(!strcmp(e->name,"glDrawArrays"))return 0;
    if(!strcmp(e->name,"glTexCoord2fv")||!strcmp(e->name,"glPrimitiveRestartIndex"))return 0;
    if(!strcmp(e->name,"glLoadMatrixf")||!strcmp(e->name,"glTexGeni")||!strcmp(e->name,"glTexGenfv")||!strcmp(e->name,"glPolygonMode")||!strcmp(e->name,"glAlphaFunc")||!strcmp(e->name,"glFogCoordPointerEXT")||!strcmp(e->name,"glVertex3fv"))return 0;
    if(!strcmp(e->name,"glFogi")||!strcmp(e->name,"glFogf")||!strcmp(e->name,"glFogfv"))return 0;
    if(!strcmp(e->name,"glLightfv")||!strcmp(e->name,"glLightf")||!strcmp(e->name,"glLightModelfv"))return 0;
    if(!strcmp(e->name,"glRotatef")||!strcmp(e->name,"glTexCoord2f")||!strcmp(e->name,"glTranslatef")||!strcmp(e->name,"glColor3ub")||!strcmp(e->name,"glVertex3f")||!strcmp(e->name,"glColor4f")||!strcmp(e->name,"glVertex2i"))return 0;
    if(!strcmp(e->name,"glPushMatrix")||!strcmp(e->name,"glPopMatrix")||!strcmp(e->name,"glScalef")||!strcmp(e->name,"glFrustum")||!strcmp(e->name,"glMultMatrixf")||!strcmp(e->name,"glMultMatrixd")||!strcmp(e->name,"glColor4fv")||!strcmp(e->name,"glMaterialfv")||!strcmp(e->name,"glTexEnvfv")||!strcmp(e->name,"glTexEnvf")||!strcmp(e->name,"glNormalPointer"))return 0;
    return strcmp(e->name,"glIsSync")!=0 && strcmp(e->name,"glGetSynciv")!=0 && strcmp(e->name,"glFenceSync")!=0 && strcmp(e->name,"glClientWaitSync")!=0 && strcmp(e->name,"glDeleteSync")!=0 && strcmp(e->name,"glBegin")!=0 && strcmp(e->name,"glVertex2f")!=0 && strcmp(e->name,"glEnd")!=0 && strcmp(e->name,"glTexImage3D")!=0 && strcmp(e->name,"glTexSubImage3D")!=0 && strcmp(e->name,"glTexEnvi")!=0 && strcmp(e->name,"glColor4ub")!=0 && strcmp(e->name,"glEnableClientState")!=0 && strcmp(e->name,"glDisableClientState")!=0 && strcmp(e->name,"glColorPointer")!=0 && strcmp(e->name,"glVertexPointer")!=0 && strcmp(e->name,"glTexCoordPointer")!=0 && strcmp(e->name,"glDrawElements")!=0 && strcmp(e->name,"glMatrixMode")!=0 && strcmp(e->name,"glLoadIdentity")!=0 && strcmp(e->name,"glLoadMatrixd")!=0 && strcmp(e->name,"glBeginQuery")!=0 && strcmp(e->name,"glEndQuery")!=0 && strcmp(e->name,"glGetQueryObjectui64v")!=0 && strcmp(e->name,"glGenQueries")!=0 && strcmp(e->name,"glDrawBuffer")!=0 && strcmp(e->name,"glClientActiveTextureARB")!=0 && strncmp(e->name,"glMultiTexCoord",14)!=0;
#else
    (void)e;return 1;
#endif
}
