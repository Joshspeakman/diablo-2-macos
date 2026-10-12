/* Native Glide 3 front end for Diablo II 1.14d's own 3dfx renderer.
 * The guest calls the original Glide ABI; textures, state and vertices are sent to
 * the native renderer's command ring. No Windows DLL or Voodoo driver is loaded.
 * API constants/semantics: 3dfx Glide 3 SDK (glide.h), see the Glide notes in README.md.
 * We advertise one TMU, 256px textures and no optional extensions: the original
 * game's single-TMU path is sufficient and avoids pretending to support hardware.
 */
#include "w32.h"
#include "dsr_producer.h"
#include "../../../src/dsr/dsr_proto.h"
#include <math.h>
#include <string.h>

#define TEX_MEMORY (16u << 20)
#define MAX_TEXTURES 4096
static struct {
    uint32_t rt, width, height, format, origin;
    uint32_t combine[16], constant, chroma, palette[256], generation;
    uint32_t layout[0x53]; unsigned char enabled[0x53];
    uint32_t source, filter, lfb, lfb_size, lfb_type, lfb_mode, lfb_origin;
    int open, dirty, glow, world;
    int32_t world_rect[4];
} gl;
typedef struct {
    uint32_t address, id, w, h, format, generation;
    unsigned char *pixels;
} Texture;
static Texture textures[MAX_TEXTURES]; static unsigned ntextures;
/* Guest texture addresses are sparse and frequently reused. Keep lookup cost
 * independent of how many textures a zoomed-out scene has loaded. Entries are
 * indices plus one; all textures and the index are cleared together on close. */
static uint16_t texture_index[MAX_TEXTURES * 2];
static unsigned texture_slot(uint32_t address) {
    unsigned slot=((address>>3)*2654435761u)>>19;
    while(texture_index[slot] && textures[texture_index[slot]-1].address!=address)
        slot=(slot+1)%(MAX_TEXTURES*2);
    return slot;
}

static void rs(uint32_t key, uint32_t value) { uint32_t *p=cmd_begin(DSR_RENDER_STATE,8);p[0]=key;p[1]=value;cmd_end(); }
static void tss(uint32_t key,uint32_t value) { uint32_t *p=cmd_begin(DSR_TSS,12);p[0]=0;p[1]=key;p[2]=value;cmd_end(); }
static uint32_t color(uint32_t v) {
    switch(gl.format) {
    case 1:return (v&0xff00ff00)|((v&255)<<16)|((v>>16)&255); /* ABGR */
    case 2:return (v>>8)|(v<<24);                         /* RGBA */
    case 3:return (v<<24)|((v>>8)&0xff00)|((v>>24)&255)|((v&0xff00)<<8); /* BGRA */
    default:return v;
    }
}
static void destroy(uint32_t id) {
    if(!id)return;uint32_t *p=cmd_begin(DSR_SURFACE_DESTROY,4);p[0]=id;cmd_end();surface_id_free(id);
}
static uint32_t surface(uint32_t w,uint32_t h,uint32_t kind) {
    uint32_t id=next_object_id(),*p=cmd_begin(DSR_SURFACE_CREATE,40);
    memset(p,0,40);p[0]=id;p[1]=w;p[2]=h;p[3]=kind;p[4]=1;p[7]=1;cmd_end();return id;
}
static void bind_rt(void) {
    uint32_t *p=cmd_begin(DSR_SET_RT,8);p[0]=gl.rt;p[1]=0;cmd_end();
    p=cmd_begin(DSR_VIEWPORT,24);p[0]=p[1]=0;p[2]=gl.width;p[3]=gl.height;p[4]=0;p[5]=0x3f800000;cmd_end();
}
static void close_glide(void) {
    destroy(gl.rt);gl.rt=0;gl.open=0;
    for(unsigned i=0;i<ntextures;i++){destroy(textures[i].id);free(textures[i].pixels);}
    ntextures=0;
    memset(texture_index,0,sizeof texture_index);
    if(gl.lfb){heap_free(w32_process_heap,gl.lfb);gl.lfb=0;gl.lfb_size=0;}
    gl.combine[0]=0;uint32_t *p=cmd_begin(DSR_GLIDE_STATE,64);memcpy(p,gl.combine,64);cmd_end();
}
/* Glide 3 LODs are log2 sizes; signed aspect ratios describe width/height. */
static int dimensions(uint32_t info,uint32_t *w,uint32_t *h,uint32_t *format) {
    int lod=(int)rt_r32(G_MEM,info+4),aspect=(int)rt_r32(G_MEM,info+8);
    if(lod<0||lod>8||aspect< -3||aspect>3)return 0;
    *w=*h=1u<<lod;if(aspect>0)*h>>=aspect;else *w>>=-aspect;
    if(!*w)*w=1;if(!*h)*h=1;*format=rt_r32(G_MEM,info+12);return 1;
}
static uint32_t pixel(const Texture *t,uint32_t n) {
    uint32_t v=t->format<8?t->pixels[n]:((uint16_t *)t->pixels)[n],r,g,b,a=255;
    switch(t->format) {
    case 5:return gl.palette[v]|0xff000000u;
    case 14:return (gl.palette[v&255]&0xffffff)|(v>>8<<24);
    case 2:return (v<<24)|0xffffff;
    case 3:return 0xff000000u|v*0x010101;
    case 4:return ((v>>4)*17<<24)|(v&15)*0x111111;
    case 10:r=((v>>11)&31)*255/31;g=((v>>5)&63)*255/63;b=(v&31)*255/31;break;
    case 11:a=(v&0x8000)?255:0;r=((v>>10)&31)*255/31;g=((v>>5)&31)*255/31;b=(v&31)*255/31;break;
    case 12:a=(v>>12)*17;r=((v>>8)&15)*17;g=((v>>4)&15)*17;b=(v&15)*17;break;
    case 13:return ((v>>8)<<24)|(v&255)*0x010101;
    case 8:a=v>>8; /* ARGB8332 */
    case 0:r=((v>>5)&7)*255/7;g=((v>>2)&7)*255/7;b=(v&3)*255/3;break;
    default:return 0xff000000u;
    }
    return (a<<24)|(r<<16)|(g<<8)|b;
}
static void upload(Texture *t) {
    uint32_t *p=cmd_begin(DSR_SURFACE_UPLOAD,24+4*t->w*t->h);
    p[0]=t->id;p[1]=p[2]=0;p[3]=t->w;p[4]=t->h;p[5]=t->w*4;
    for(uint32_t i=0;i<t->w*t->h;i++) {
        uint32_t v=pixel(t,i);
        if((v&0xffffff)==(gl.chroma&0xffffff))v&=0xffffff; // chroma coverage, tested separately from combined alpha
        p[6+i]=v;
    }
    cmd_end();t->generation=gl.generation;
}
static Texture *texture(uint32_t address) {
    unsigned index=texture_index[texture_slot(address)];return index?&textures[index-1]:NULL;
}
static void state(void) {
    if(gl.dirty) {uint32_t *p=cmd_begin(DSR_GLIDE_STATE,64);memcpy(p,gl.combine,64);cmd_end();gl.dirty=0;}
}
static float f32(uint32_t at) {float v;uint32_t x=rt_r32(G_MEM,at);memcpy(&v,&x,4);return v;}
typedef struct {float x,y,z,q;uint32_t argb;float s,t;} Vertex;
static void vertex(Vertex *out,uint32_t at,Texture *tex) {
    uint32_t xy=gl.layout[1];out->x=f32(at+xy)-.5f;out->y=f32(at+xy+4)-.5f;
    if(gl.origin==1)out->y=gl.height-1-out->y;
    float q=gl.enabled[0x50]?f32(at+gl.layout[0x50]):gl.enabled[4]?f32(at+gl.layout[4]):1;
    if(!isfinite(q)||fabsf(q)<1e-20f)q=1;
    out->q=q;out->z=0;out->argb=gl.enabled[0x30]?rt_r32(G_MEM,at+gl.layout[0x30]):0xffffffffu;
    out->s=out->t=0;
    if(tex&&gl.enabled[0x40]) {
        float largest=fmaxf(tex->w,tex->h);
        out->s=f32(at+gl.layout[0x40])/q/(256*tex->w/largest);
        out->t=f32(at+gl.layout[0x40]+4)/q/(256*tex->h/largest);
    }
}
static void draw_vertices(uint32_t kind,uint32_t count,uint32_t base,uint32_t stride,int indirect) {
    if(!gl.open||!count||count>65536)return;
    Texture *t=texture(gl.source);if(t&&t->generation!=gl.generation)upload(t);
    state();uint32_t *p=cmd_begin(DSR_TEXTURE,8);p[0]=0;p[1]=t?t->id:0;cmd_end();
    static const unsigned prim[]={1,3,2,6,5,6,4};if(kind>6)return;
    p=cmd_begin(DSR_DRAW,16+count*sizeof(Vertex));p[0]=prim[kind];p[1]=0x144;p[2]=count;p[3]=0;
    Vertex *v=(Vertex *)(p+4);float minx=1e9f;
    for(uint32_t i=0;i<count;i++) {vertex(v+i,indirect?rt_r32(G_MEM,base+i*4):base+i*stride,t);if(v[i].x<minx)minx=v[i].x;}
    int d2wide_shift(float),d2wide_menu_shift(void),d2zoom_draw(float *,float *,float *);
    int shift=d2wide_shift(minx)+d2wide_menu_shift();float scale,zx,zy;int zoom=d2zoom_draw(&scale,&zx,&zy);
    for(uint32_t i=0;i<count;i++){v[i].x+=shift;if(zoom){v[i].x=zx+(v[i].x-zx)*scale;v[i].y=zy+(v[i].y-zy)*scale;}}
    cmd_end();
}
IMPL(glide3x, grGlideInit) {if(gl.open||ntextures)close_glide();memset(&gl,0,sizeof gl);gl.generation=1;RET(0,0);}
IMPL(glide3x, grGlideShutdown) {close_glide();RET(0,0);}
/* The existing world-pass hooks bracket the original scene, before any HUD.
 * Capture the unzoomed view rectangle; zoom temporarily changes guest sizes. */
void glide_world_begin(void) {
    gl.world=0;
    if(!gl.open||!gl.glow)return;
    uint32_t view=rt_r32(G_MEM,0x7a0640);
    if(!view||!rt_r32(G_MEM,0x7a6a70))return;
    for(unsigned k=0;k<4;k++)gl.world_rect[k]=(int32_t)rt_r32(G_MEM,view+4+4*k);
    gl.world=1;
}
void glide_world_end(void) {
    if(!gl.world)return;
    gl.world=0;
    if(!gl.open||!gl.glow)return;
    state();
    uint32_t *p=cmd_begin(DSR_GLIDE_GLOW,24);p[0]=gl.rt;p[1]=gl.glow;
    memcpy(p+2,gl.world_rect,sizeof gl.world_rect);cmd_end();
    static int logged;
    if(!logged && getenv("W32_DDLOG")) {fprintf(stderr,"glide: world glow %d, rectangle %d,%d-%d,%d\n",gl.glow,gl.world_rect[0],gl.world_rect[1],gl.world_rect[2],gl.world_rect[3]);logged=1;}
}
IMPL(glide3x, grSstSelect) {RET(0,1);}
IMPL(glide3x, grSstWinOpen) {
    if(gl.open)close_glide();
    /* Resolution enum: 640x480=7, 800x600=8; these are the game's two modes. */
    if(ARG(1)!=7&&ARG(1)!=8)RET(0,7);
    gl.width=ARG(1)==8?800:640;gl.height=ARG(1)==8?600:480;gl.format=ARG(3);gl.origin=ARG(4);
    int d2wide_width(void);if(gl.width==800&&d2wide_width())gl.width=d2wide_width();
    dsr_d3d9_w=gl.width;dsr_d3d9_h=gl.height;
    gl.rt=surface(gl.width,gl.height,1);gl.open=1;gl.combine[0]=1;gl.dirty=1;
    const char *glow=getenv("D2_GLIDE_GLOW");
    gl.glow=glow&&!strcmp(glow,"subtle")?1:glow&&!strcmp(glow,"strong")?2:0;gl.world=0;
    uint32_t *p=cmd_begin(DSR_MODE,8);p[0]=gl.width;p[1]=gl.height;cmd_end();bind_rt();
    rs(7,0);rs(14,0);rs(22,1);rs(27,1);rs(19,2);rs(20,1);rs(168,15);rs(28,0);rs(174,0);rs(206,0);
    tss(16,1);tss(17,1);tss(18,0);tss(13,3);tss(14,3);
    fprintf(stderr,"glide: native Glide 3 renderer, %ux%u, one TMU\n",gl.width,gl.height);
    RET(1,7);
}
IMPL(glide3x, grSstWinClose) {close_glide();RET(1,1);}
IMPL(glide3x, grGet) {
    uint32_t key=ARG(0),len=ARG(1),out=ARG(2),v=0,bytes=4;
    switch(key) {
    case 1:v=16;break;case 2:bytes=16;break;case 4:v=64;break;case 5:v=256;break;
    case 0xa:v=256;break;case 0xb:v=3;break;case 0xc:v=16<<20;break;case 0xd:v=TEX_MEMORY;break;
    case 0xe:v=0;break;case 0xf:case 0x13:v=1;break;case 0x11:v=2;break;case 0x24:v=8;break;
    case 0x2a:v=8;break;case 0x26:bytes=16;break;case 0x27:case 0x28:bytes=8;break;
    case 8:case 0x14:v=0;break;default:RET(0,3);
    }
    if(!out||len!=bytes)RET(0,3);
    if(key==2){rt_w32(G_MEM,out,8);rt_w32(G_MEM,out+4,8);rt_w32(G_MEM,out+8,8);rt_w32(G_MEM,out+12,8);}
    else if(key==0x26){rt_w32(G_MEM,out,0);rt_w32(G_MEM,out+4,0);rt_w32(G_MEM,out+8,gl.width);rt_w32(G_MEM,out+12,gl.height);}
    else if(bytes==8){rt_w32(G_MEM,out,0);rt_w32(G_MEM,out+4,65535);}
    else rt_w32(G_MEM,out,v);RET(bytes,3);
}
IMPL(glide3x, grGetString) {
    static uint32_t strings[5];uint32_t key=ARG(0);
    static const char *s[]={"", "Native Glide", "Diablo II native Glide", "Native Metal", "3.0"};
    if(key<0xa0||key>0xa4)RET(0,1);key-=0xa0;
    if(!strings[key]){strings[key]=heap_alloc(w32_process_heap,8,strlen(s[key])+1);strcpy(GP(strings[key]),s[key]);}
    RET(strings[key],1);
}
IMPL(glide3x, grGetProcAddress) {RET(0,1);} /* no extensions advertised */
IMPL(glide3x, grVertexLayout) {if(ARG(0)<0x53&&ARG(1)<256){gl.layout[ARG(0)]=ARG(1);gl.enabled[ARG(0)]=ARG(2)!=0;}RET(0,3);}
IMPL(glide3x, grDrawVertexArray) {draw_vertices(ARG(0),ARG(1),ARG(2),0,1);RET(0,3);}
IMPL(glide3x, grDrawVertexArrayContiguous) {draw_vertices(ARG(0),ARG(1),ARG(2),ARG(3),0);RET(0,4);}
IMPL(glide3x, grDrawTriangle) {draw_vertices(6,3,c->esp+4,0,1);RET(0,3);}
IMPL(glide3x, grDrawLine) {draw_vertices(2,2,c->esp+4,0,1);RET(0,2);}
IMPL(glide3x, grDrawPoint) {draw_vertices(0,1,c->esp+4,0,1);RET(0,1);}
IMPL(glide3x, grTexMinAddress) {RET(0,1);}
IMPL(glide3x, grTexMaxAddress) {RET(TEX_MEMORY-1,1);}
IMPL(glide3x, grTexTextureMemRequired) {
    uint32_t w,h,f,info=ARG(1);if(!dimensions(info,&w,&h,&f))RET(0,2);
    uint32_t total=0,small=rt_r32(G_MEM,info),large=rt_r32(G_MEM,info+4);
    if(small>large)RET(0,2);
    for(uint32_t level=large;;level--){if(ARG(0)&(level&1?2:1))total+=w*h*(f<8?1:2);if(level==small)break;w=w>1?w/2:1;h=h>1?h/2:1;}
    RET((total+7)&~7u,2);
}
IMPL(glide3x, grTexDownloadMipMap) {
    uint32_t w,h,f,info=ARG(3),address=ARG(1);if(ARG(0)||!dimensions(info,&w,&h,&f))RET(0,4);
    uint32_t n=w*h*(f<8?1:2),data=rt_r32(G_MEM,info+16);if(!data||address>TEX_MEMORY-n)RET(0,4);
    Texture *t=texture(address);
    if(!t&&ntextures==MAX_TEXTURES)RET(0,4);
    /* Repeated downloads of unchanged pixels do not invalidate the GPU copy.
     * Palette/chroma changes still invalidate it through generation. */
    if(t&&t->w==w&&t->h==h&&t->format==f&&!memcmp(t->pixels,GP(data),n))RET(0,4);
    unsigned char *pixels=realloc(t?t->pixels:NULL,n);if(!pixels)RET(0,4);
    if(!t){t=&textures[ntextures++];memset(t,0,sizeof *t);t->address=address;texture_index[texture_slot(address)]=ntextures;}
    if(t->w!=w||t->h!=h){destroy(t->id);t->id=surface(w,h,3);}
    t->pixels=pixels;
    memcpy(pixels,GP(data),n);t->w=w;t->h=h;t->format=f;t->generation=0;RET(0,4);
}
IMPL(glide3x, grTexDownloadTable) {if(ARG(0)==2&&ARG(1)&&memcmp(gl.palette,GP(ARG(1)),1024)){memcpy(gl.palette,GP(ARG(1)),1024);gl.generation++;}RET(0,2);}
IMPL(glide3x, grTexSource) {if(ARG(0)==0)gl.source=ARG(1);RET(0,4);}
IMPL(glide3x, grTexFilterMode) {
    if(ARG(0)==0){const char *choice=getenv("D2_TEXTURES");
        tss(17,choice?(!strcmp(choice,"smooth")?2:1):(ARG(1)?2:1));tss(16,choice?(!strcmp(choice,"smooth")?2:1):(ARG(2)?2:1));}
    RET(0,3);
}
/* With one TMU, LOCAL selects that texture; the upstream texture is zero. D2
 * uses this configuration throughout its single-TMU renderer. */
IMPL(glide3x, grTexCombine) {RET(0,7);}
IMPL(glide3x, grTexMipMapMode) {RET(0,3);} /* D2 supplies one LOD per texture */
IMPL(glide3x, grColorCombine) {for(int i=0;i<5;i++)gl.combine[1+i]=ARG(i);gl.dirty=1;RET(0,5);}
IMPL(glide3x, grAlphaCombine) {for(int i=0;i<5;i++)gl.combine[6+i]=ARG(i);gl.dirty=1;RET(0,5);}
IMPL(glide3x, grConstantColorValue) {gl.constant=color(ARG(0));rs(60,gl.constant);RET(0,1);}
static uint32_t blend(uint32_t v,int dst) {
    static const uint32_t map[]={1,5,9,7,2,6,10,8};if(v==2)return dst?3:9;if(v==6)return dst?4:10;return v<8?map[v]:11;
}
IMPL(glide3x, grAlphaBlendFunction) {rs(19,blend(ARG(0),0));rs(20,blend(ARG(1),1));rs(206,1);rs(207,blend(ARG(2),0));rs(208,blend(ARG(3),1));rs(209,1);RET(0,4);}
IMPL(glide3x, grChromakeyMode) {gl.combine[11]=ARG(0)!=0;gl.dirty=1;RET(0,1);}
IMPL(glide3x, grChromakeyValue) {uint32_t value=color(ARG(0));if(gl.chroma!=value){gl.chroma=value;gl.generation++;}RET(0,1);}
IMPL(glide3x, grColorMask) {rs(168,(ARG(0)?7:0)|(ARG(1)?8:0));RET(0,2);}
IMPL(glide3x, grClipWindow) {uint32_t *p=cmd_begin(DSR_SCISSOR,16);for(int i=0;i<4;i++)p[i]=ARG(i);cmd_end();rs(174,1);RET(0,4);}
IMPL(glide3x, grDepthBufferMode) {RET(0,1);} /* D2 uses painter order, no auxiliary buffer */
IMPL(glide3x, grDepthBufferFunction) {RET(0,1);}
IMPL(glide3x, grDepthMask) {RET(0,1);}
IMPL(glide3x, grDitherMode) {RET(0,1);} /* native framebuffer retains full color precision */
IMPL(glide3x, grCoordinateSpace) {RET(0,1);} /* D2 uses window coordinates */
IMPL(glide3x, grEnable) {RET(0,1);}
IMPL(glide3x, grDisable) {RET(0,1);}
IMPL(glide3x, grFinish) {
    /* A synchronous read waits for all earlier commands and GPU draws. */
    if(gl.open){uint32_t pixel;int32_t rect[]={0,0,1,1};cmd_readback(gl.rt,rect,(uint8_t *)&pixel,4,4);}
    RET(0,0);
}
IMPL(glide3x, grBufferClear) {
    /* 1.14d's Glide texture-pool setup (0x509910) requires exactly 800x600.
     * Its first clear happens AFTER allocation (0x509a76), before the client
     * reads the screen size. Widen then; doing it in WinOpen leaves empty pools. */
    if(gl.width>800 && rt_r32(G_MEM,0x7c9138)==800)rt_w32(G_MEM,0x7c9138,gl.width);
    if(gl.open){bind_rt();uint32_t *p=cmd_begin(DSR_CLEAR,20);p[0]=1;p[1]=(color(ARG(0))&0xffffff)|(ARG(1)<<24);p[2]=0x3f800000;p[3]=p[4]=0;cmd_end();}RET(0,3);
}
IMPL(glide3x, grBufferSwap) {if(gl.open){dsr_present(c,gl.rt);void d2wide_frame(void);d2wide_frame();}RET(0,1);}
IMPL(glide3x, grLoadGammaTable) {
    uint32_t n=ARG(0);if(n!=256)RET(0,4);
    uint16_t *p=(uint16_t *)cmd_begin(DSR_GAMMA,1536);
    for(unsigned k=0;k<3;k++)for(unsigned i=0;i<256;i++){uint32_t v=rt_r32(G_MEM,ARG(k+1)+i*4);p[k*256+i]=(uint16_t)((v>255?255:v)*257);}cmd_end();RET(0,4);
}
/* Bink movies write RGB565 through Glide's linear framebuffer; screenshots read
 * it back. The 32-bit guest pointer never points at a host or OpenGL allocation. */
IMPL(glide3x, grLfbLock) {
    uint32_t mode=ARG(2)==255?0:ARG(2),info=ARG(5);if(!gl.open||!info||mode>5||mode==3)RET(0,6);
    uint32_t bpp=mode<3?2:4,bytes=gl.width*gl.height*bpp;
    if(gl.lfb_size<bytes){if(gl.lfb)heap_free(w32_process_heap,gl.lfb);gl.lfb=heap_alloc(w32_process_heap,8,bytes);gl.lfb_size=bytes;}
    gl.lfb_type=ARG(0)&1;gl.lfb_mode=mode;gl.lfb_origin=ARG(3);
    if(!gl.lfb_type){
        uint32_t *rgba=malloc(gl.width*gl.height*4);int32_t rect[]={0,0,gl.width,gl.height};
        if(!rgba)RET(0,6);int ok=cmd_readback(gl.rt,rect,(uint8_t *)rgba,gl.width*4,4);
        if(!ok){free(rgba);RET(0,6);}
        for(uint32_t y=0;y<gl.height;y++)for(uint32_t x=0;x<gl.width;x++){
            uint32_t v=rgba[y*gl.width+x],n=(gl.lfb_origin?gl.height-1-y:y)*gl.width+x;
            if(bpp==4)rt_w32(G_MEM,gl.lfb+n*4,v);
            else {uint16_t a=mode==0?((v>>8)&0xf800)|((v>>5)&0x7e0)|((v>>3)&31):((v>>9)&0x7c00)|((v>>6)&0x3e0)|((v>>3)&31)|(mode==2?0x8000:0);rt_w16(G_MEM,gl.lfb+n*2,a);}
        }
        free(rgba);
    }
    rt_w32(G_MEM,info+4,gl.lfb);rt_w32(G_MEM,info+8,gl.width*bpp);rt_w32(G_MEM,info+12,mode);rt_w32(G_MEM,info+16,gl.lfb_origin);RET(1,6);
}
IMPL(glide3x, grLfbUnlock) {
    if(gl.lfb&&gl.lfb_type){
        uint32_t *p=cmd_begin(DSR_SURFACE_UPLOAD,24+gl.width*gl.height*4);p[0]=gl.rt;p[1]=p[2]=0;p[3]=gl.width;p[4]=gl.height;p[5]=gl.width*4;
        for(uint32_t y=0;y<gl.height;y++)for(uint32_t x=0;x<gl.width;x++){
            uint32_t n=(gl.lfb_origin?gl.height-1-y:y)*gl.width+x,v;
            if(gl.lfb_mode>=4)v=rt_r32(G_MEM,gl.lfb+n*4)|0xff000000u;
            else {uint32_t a=rt_r16(G_MEM,gl.lfb+n*2),r,g,b=(a&31)*255/31;
                if(gl.lfb_mode==0){r=(a>>11)*255/31;g=((a>>5)&63)*255/63;}
                else {r=((a>>10)&31)*255/31;g=((a>>5)&31)*255/31;}v=0xff000000u|(r<<16)|(g<<8)|b;}
            p[6+y*gl.width+x]=v;
        }
        cmd_end();
        if(ARG(1)==0)dsr_present(c,gl.rt); /* writes to the visible front buffer (movies) */
    }RET(1,2);
}
IMPL(glide3x, guGammaCorrectionRGB) {
    uint16_t *p=(uint16_t *)cmd_begin(DSR_GAMMA,1536);
    for(unsigned k=0;k<3;k++){
        float gamma=f32(c->esp+4+k*4);if(!isfinite(gamma)||gamma<=0)gamma=1;
        for(unsigned i=0;i<256;i++)p[k*256+i]=(uint16_t)lroundf(powf(i/255.f,1/gamma)*65535);
    }cmd_end();RET(0,3);
}
