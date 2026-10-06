/* CoreText supplies actual font mapping, advances, bounds and grayscale pixels.
 * Private font references and raster buffers never enter guest memory.
 * Raster-font fallback is distinct from the game's bundled SDF rendering.
 */
#include "font.h"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t ceiling(CGFloat v){int32_t n=(int32_t)v;return n+(v>n);}
static int32_t flooring(CGFloat v){int32_t n=(int32_t)v;return n-(v<n);}
static int32_t nearest(CGFloat v){return v>=0?(int32_t)(v+.5):(int32_t)(v-.5);}
static int32_t cell(CTFontRef f){return ceiling(CTFontGetAscent(f))+ceiling(CTFontGetDescent(f));}
void *tb_apple_font_create(const char *face,int32_t height){
    if(!face||height==INT32_MIN||height>1024||height< -1024)return NULL;
    CFStringRef name=CFStringCreateWithCString(NULL,face,kCFStringEncodingWindowsLatin1);
    if(!name)return NULL;
    CGFloat size=height<0?-(CGFloat)height:height?height:12;
    CTFontRef font=CTFontCreateWithName(name,size,NULL);
    if(font&&height>0){
        /* Positive GDI height is a cell height, negative height is em height.
           Match the largest actual font whose rounded ascent+descent fits. */
        CGFloat low=0,high=size;
        for(unsigned i=0;i<18;i++){
            CGFloat middle=(low+high)/2;CTFontRef candidate=CTFontCreateWithName(name,middle,NULL);
            if(!candidate){CFRelease(font);font=NULL;break;}
            if(cell(candidate)<=height){low=middle;CFRelease(font);font=candidate;}
            else {high=middle;CFRelease(candidate);}
        }
        if(font&&cell(font)>height){CFRelease(font);font=NULL;}
    }
    CFRelease(name);
    if(font){
        CFStringRef actual=CTFontCopyPostScriptName(font);char mapped[256]={0};
        if(actual){(void)CFStringGetCString(actual,mapped,sizeof mapped,kCFStringEncodingUTF8);CFRelease(actual);}
        fprintf(stderr,"TribesPad native raster font requested=%s selected=%s height=%d cell=%d\n",face,mapped,height,cell(font));
    }
    return (void *)font;
}
void tb_apple_font_release(void *font){if(font)CFRelease(font);}
static UniChar character(uint32_t c){
    unsigned char byte=(unsigned char)c;UniChar result=byte;
    if(c>=128&&c<160){
        CFStringRef string=CFStringCreateWithBytes(NULL,&byte,1,kCFStringEncodingWindowsLatin1,false);
        if(string){result=CFStringGetCharacterAtIndex(string,0);CFRelease(string);}
    }
    return result;
}
static CGGlyph glyph(CTFontRef f,uint32_t c){
    UniChar ch=character(c);CGGlyph g=0;
    (void)CTFontGetGlyphsForCharacters(f,&ch,&g,1);return g;
}
int tb_apple_font_metrics(void *font,TBFontMetrics *output){
    if(!font||!output)return 0;CTFontRef f=font;TBFontMetrics m={0};
    m.ascent=ceiling(CTFontGetAscent(f));m.descent=ceiling(CTFontGetDescent(f));m.height=m.ascent+m.descent;
    m.internal_leading=m.height-nearest(CTFontGetSize(f));if(m.internal_leading<0)m.internal_leading=0;
    m.external_leading=ceiling(CTFontGetLeading(f));m.weight=400;m.aspect_x=m.aspect_y=96;
    int32_t total=0;unsigned count=0;
    for(uint32_t c=32;c<256;c++){
        CGGlyph g=glyph(f,c);CGSize advance={0};
        CTFontGetAdvancesForGlyphs(f,kCTFontOrientationHorizontal,&g,&advance,1);
        int32_t width=nearest(advance.width);if(width>m.maximum_width)m.maximum_width=width;
        if(c>=65&&c<=90){total+=width;count++;}
    }
    m.average_width=count?(total+(int32_t)count/2)/(int32_t)count:0;
    m.first=32;m.last=255;m.default_char='?';m.break_char=' ';m.pitch_family=0x26;m.charset=0;
    *output=m;return 1;
}
int tb_apple_font_glyph(void *font,uint32_t c,TBGlyphMetrics *metrics,unsigned char **pixels,uint32_t *bytes){
    if(!font||c>255||!metrics||!pixels||!bytes)return 0;CTFontRef f=font;CGGlyph g=glyph(f,c);CGSize advance={0};
    CTFontGetAdvancesForGlyphs(f,kCTFontOrientationHorizontal,&g,&advance,1);
    CGRect rect=CTFontGetBoundingRectsForGlyphs(f,kCTFontOrientationHorizontal,&g,NULL,1);
    TBGlyphMetrics m={0};int32_t ax=nearest(advance.width);if(ax<INT16_MIN||ax>INT16_MAX)return 0;m.advance_x=(int16_t)ax;
    unsigned char *data=NULL;uint32_t length=0;
    if(rect.size.width>0&&rect.size.height>0){
        int32_t left=flooring(rect.origin.x),bottom=flooring(rect.origin.y);
        int32_t right=ceiling(rect.origin.x+rect.size.width),top=ceiling(rect.origin.y+rect.size.height);
        if(right<=left||top<=bottom||right-left>4096||top-bottom>4096)return 0;
        m.width=(uint32_t)(right-left);m.height=(uint32_t)(top-bottom);m.x=left;m.y=top;
        uint32_t stride=(m.width+3)&~3u;if((uint64_t)stride*m.height>16777216)return 0;length=stride*m.height;
        data=calloc(1,length);if(!data)return 0;
        CGColorSpaceRef space=CGColorSpaceCreateDeviceGray();
        CGContextRef context=space?CGBitmapContextCreate(data,m.width,m.height,8,stride,space,kCGImageAlphaNone):NULL;
        if(space)CGColorSpaceRelease(space);if(!context){free(data);return 0;}
        CGContextSetShouldAntialias(context,true);CGContextSetShouldSmoothFonts(context,false);
        CGContextSetShouldSubpixelPositionFonts(context,false);CGContextSetShouldSubpixelQuantizeFonts(context,true);
        CGContextSetGrayFillColor(context,1,1);
        CGPoint position={-left,-bottom};CTFontDrawGlyphs(f,&g,&position,1,context);CGContextRelease(context);
        /* Quartz rows are bottom-up in this untransformed bitmap context. */
        for(uint32_t y=0;y<(m.height+1)/2;y++)for(uint32_t x=0;x<stride;x++){
            uint32_t a=y*stride+x,b=(m.height-y-1)*stride+x;
            unsigned char v=data[a],w=data[b];data[a]=(unsigned char)((w*64u+127u)/255u);data[b]=(unsigned char)((v*64u+127u)/255u);
        }
    }
    *metrics=m;*pixels=data;*bytes=length;return 1;
}
int tb_apple_font_extent(void *font,const unsigned char *text,uint32_t count,int32_t output[2]){
    if(!font||(!text&&count)||!output||count>65536)return 0;CTFontRef f=font;CGFloat width=0;
    for(uint32_t i=0;i<count;i++){
        CGGlyph g=glyph(f,text[i]);CGSize advance={0};CTFontGetAdvancesForGlyphs(f,kCTFontOrientationHorizontal,&g,&advance,1);width+=advance.width;
    }
    if(width>INT32_MAX||width<INT32_MIN)return 0;output[0]=nearest(width);output[1]=cell(f);return 1;
}
