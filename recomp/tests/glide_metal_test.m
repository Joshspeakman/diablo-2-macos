/* Exercise the real Glide guest ABI and native GPU, with synthetic assets.
 * Independent pixel expectations cover palette changes, chroma, blend modes,
 * vertex layouts, clipping, LFB read/write and resolution reopening. */
#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdarg.h>
#include "../runtime/win32/w32.h"
#include "../../src/renderer/renderer.h"
#include "../../src/dsr/dsr_proto.h"
uint8_t *G_MEM;uint32_t w32_process_heap,dsr_d3d9_w,dsr_d3d9_h;
static DSRRenderer *renderer;static uint32_t command[4*1024*1024],op,bytes,nextid,nextmem=0x100000;
static unsigned uploads,glows;
uint32_t *cmd_begin(uint32_t o,uint32_t size){assert(size<=sizeof command);op=o;bytes=size;return command;}
void cmd_end(void){if(op==DSR_SURFACE_UPLOAD)uploads++;if(op==DSR_GLIDE_GLOW)glows++;dsr_renderer_exec(renderer,op,(const uint8_t *)command,bytes);}
uint32_t next_object_id(void){return ++nextid;}
void surface_id_free(uint32_t id){(void)id;}
uint32_t heap_alloc(uint32_t heap,uint32_t flags,uint32_t size){uint32_t at=nextmem;nextmem+=(size+15)&~15u;assert(nextmem<32*1024*1024);return at;}
int heap_free(uint32_t heap,uint32_t at){return 1;}
void dsr_present(Ctx *c,uint32_t id){uint32_t *p=cmd_begin(DSR_PRESENT,4);*p=id;cmd_end();}
int cmd_readback(uint32_t id,const int32_t *rect,uint8_t *out,uint32_t pitch,uint32_t bpp){
 assert(bpp==4&&pitch>=(uint32_t)(rect[2]-rect[0])*bpp);
 volatile uint32_t done=0;dsr_renderer_set_readback(renderer,out,pitch*(rect[3]-rect[1]),&done);
 uint32_t *p=cmd_begin(DSR_READBACK,24);p[0]=id;p[1]=rect[0];p[2]=rect[1];p[3]=rect[2]-rect[0];p[4]=rect[3]-rect[1];p[5]=7;cmd_end();return done==7;
}
static int test_width;
int d2wide_width(void){return test_width;}int d2wide_shift(float x){return 0;}int d2wide_menu_shift(void){return 0;}
int d2zoom_draw(float *s,float *x,float *y){return 0;}void d2wide_frame(void){}
#include "../runtime/win32/glide3x.c"
static uint32_t call(void (*fn)(Ctx *),int n,...){
 Ctx c={.esp=0x1000};va_list args;va_start(args,n);for(int i=0;i<n;i++)rt_w32(G_MEM,c.esp+4+i*4,va_arg(args,uint32_t));va_end(args);
 fn(&c);assert(c.esp==0x1004+4*n);return c.eax;
}
#define API(name,n,...) call(imp_glide3x_##name,n,##__VA_ARGS__)
static uint32_t read_pixel(unsigned x,unsigned y){
 uint32_t out;int32_t r[]={x,y,x+1,y+1};assert(cmd_readback(gl.rt,r,(uint8_t *)&out,4,4));return out;
}
static void expect(unsigned x,unsigned y,uint32_t rgb){uint32_t got=read_pixel(x,y)&0xffffff;if(got!=rgb){fprintf(stderr,"%u,%u: %06x != %06x\n",x,y,got,rgb);abort();}}
static void quad(void){
 // Original Glide layout: x/y, packed ARGB, q, s/t (24 bytes).
 struct V {float x,y;uint32_t color;float q,s,t;} v[]={
  {0,0,0xffffffff,1,0,0},{32,0,0xffffffff,1,256,0},{32,32,0xffffffff,1,256,256},{0,32,0xffffffff,1,0,256}};
 memcpy(G_MEM+0x2000,v,sizeof v);API(grDrawVertexArrayContiguous,4,5u,4u,0x2000u,24u);
}
int main(int argc,char **argv){
 @autoreleasepool {
 G_MEM=calloc(1,32*1024*1024);
 setenv("DSR_RENDER_SCALE","2",1);renderer=dsr_renderer_create(MTLCreateSystemDefaultDevice());assert(renderer);
 API(grGlideInit,0);assert(API(grGet,3,0xfu,4u,0x3000u)==4&&rt_r32(G_MEM,0x3000)==1);
 assert(API(grGet,3,0x13u,4u,0x3000u)==4&&rt_r32(G_MEM,0x3000)==1);
 assert(!API(grGet,3,0x13u,16u,0x3000u));assert(!API(grGet,3,0x999u,4u,0x3000u));
 assert(API(grSstWinOpen,7,0u,8u,0u,0u,0u,2u,0u)==1);
 API(grVertexLayout,3,1u,0u,1u);API(grVertexLayout,3,0x30u,8u,1u);API(grVertexLayout,3,0x50u,12u,1u);API(grVertexLayout,3,0x40u,16u,1u);
 uint32_t info[]={1,1,0,5,0x4000};memcpy(G_MEM+0x3100,info,sizeof info);
 // 2x2 palette texture: upper left red, upper right chroma, lower left green, lower right blue.
 G_MEM[0x4000]=1;G_MEM[0x4001]=0;G_MEM[0x4002]=2;G_MEM[0x4003]=3;
 rt_w32(G_MEM,0x5004,0xffff0000);rt_w32(G_MEM,0x5008,0xff00ff00);rt_w32(G_MEM,0x500c,0xff0000ff);
 API(grTexDownloadTable,2,2u,0x5000u);assert(API(grTexTextureMemRequired,2,3u,0x3100u)==8);
 API(grTexDownloadMipMap,4,0u,0u,3u,0x3100u);API(grTexSource,4,0u,0u,3u,0x3100u);
 API(grColorCombine,5,3u,1u,0u,1u,0u);API(grAlphaCombine,5,1u,0u,1u,2u,0u);
 API(grConstantColorValue,1,0xffffffffu);API(grAlphaBlendFunction,4,4u,0u,4u,0u);
 API(grChromakeyValue,1,0u);API(grChromakeyMode,1,1u);API(grBufferClear,3,0x112233u,255u,0u);quad();API(grFinish,0);
 expect(4,4,0xff0000);expect(28,4,0x112233);expect(4,28,0x00ff00);expect(28,28,0x0000ff);
 // Identical state and pixel downloads must reuse the already uploaded copy.
 unsigned before=uploads;
 API(grTexDownloadTable,2,2u,0x5000u);API(grChromakeyValue,1,0u);
 API(grTexDownloadMipMap,4,0u,0u,3u,0x3100u);quad();assert(uploads==before);
 // Actual pixel/chroma changes must still reach the GPU, including after a
 // repeated download. The palette test below also checks recoloring on reuse.
 G_MEM[0x4000]=2;API(grTexDownloadMipMap,4,0u,0u,3u,0x3100u);quad();expect(4,4,0x00ff00);assert(uploads==before+1);
 G_MEM[0x4000]=1;API(grTexDownloadMipMap,4,0u,0u,3u,0x3100u);
 API(grChromakeyValue,1,0xff0000u);API(grBufferClear,3,0x112233u,255u,0u);quad();expect(4,4,0x112233);
 API(grChromakeyValue,1,0u);quad();expect(4,4,0xff0000);
 // Palette changes must recolor an existing texture without a second download.
 rt_w32(G_MEM,0x5004,0xffffff00);API(grTexDownloadTable,2,2u,0x5000u);quad();expect(4,4,0xffff00);
 // Constant color and source-alpha blending, independent from texture chroma.
 API(grChromakeyMode,1,0u);API(grColorCombine,5,1u,0u,1u,2u,0u);API(grConstantColorValue,1,0x80ff0000u);
 API(grAlphaBlendFunction,4,1u,5u,4u,0u);API(grBufferClear,3,0x0000ffu,255u,0u);quad();expect(4,4,0x80007f);
 assert((read_pixel(4,4)>>24)==128); // alpha has independent ONE/ZERO blending
 // Clipping scales with the target; framebuffer clears ignore draw clipping.
 API(grAlphaBlendFunction,4,4u,0u,4u,0u); API(grConstantColorValue,1,0xffff0000u);
 API(grClipWindow,4,16u,16u,32u,32u);API(grBufferClear,3,0x0000ffu,255u,0u);quad();
 expect(4,4,0x0000ff);expect(28,28,0xff0000);
 API(grClipWindow,4,0u,0u,800u,600u);
 // Additive and multiplicative modes used by lights and shadows.
 API(grConstantColorValue,1,0xff102030u);API(grAlphaBlendFunction,4,4u,4u,4u,0u);API(grBufferClear,3,0x102030u,255u,0u);quad();expect(4,4,0x204060);
 API(grConstantColorValue,1,0xff808080u);API(grAlphaBlendFunction,4,0u,2u,4u,0u);API(grBufferClear,3,0x804020u,255u,0u);quad();expect(4,4,0x402010);
 // RGB565 movie upload, followed by a real GPU framebuffer read.
 assert(API(grLfbLock,6,1u,1u,0u,0u,0u,0x6000u)==1);uint32_t fb=rt_r32(G_MEM,0x6004);rt_w16(G_MEM,fb,0x07e0);
 assert(API(grLfbUnlock,2,1u,1u)==1);expect(0,0,0x00ff00);
 assert(API(grLfbLock,6,0u,1u,0u,0u,0u,0x6000u)==1);assert(rt_r16(G_MEM,rt_r32(G_MEM,0x6004))==0x07e0);API(grLfbUnlock,2,0u,1u);
 // Sparse addresses, collisions, replacement and misses must resolve to the
 // correct texture. Each download starts with the same pixels and dimensions.
 for(unsigned i=1;i<256;i++)API(grTexDownloadMipMap,4,0u,i*4096u,3u,0x3100u);
 for(unsigned i=0;i<256;i++){Texture *t=texture(i*4096);assert(t&&t->address==i*4096&&t->w==2&&t->h==2);}
 assert(!texture(123456));
 uint32_t collision=8;
 while((((collision>>3)*2654435761u)>>19)!=0)collision+=8;
 assert(collision<TEX_MEMORY-4&&!texture(collision));
 API(grTexDownloadMipMap,4,0u,collision,3u,0x3100u);
 assert(texture(collision)&&texture(collision)!=texture(0));
 G_MEM[0x4000]=3;API(grTexDownloadMipMap,4,0u,collision,3u,0x3100u);
 assert(texture(collision)->pixels[0]==3&&texture(0)->pixels[0]==1);
 API(grSstWinClose,1,1u);assert(ntextures==0&&!texture(0)&&!texture(4096));assert(API(grSstWinOpen,7,0u,7u,0u,0u,0u,2u,0u)==1);assert(gl.width==640&&gl.height==480);
 // Widescreen must leave the guest's 800px texture-pool setup intact until
 // its first clear, then publish the expanded size before the client reads it.
 API(grSstWinClose,1,1u);test_width=1066;rt_w32(G_MEM,0x7c9138,800);
 assert(API(grSstWinOpen,7,0u,8u,0u,0u,0u,2u,0u)==1);
 assert(gl.width==1066&&rt_r32(G_MEM,0x7c9138)==800);
 API(grBufferClear,3,0u,0u,0u);assert(rt_r32(G_MEM,0x7c9138)==1066);
 // Only a paired world pass emits glow, once, even with both end hooks.
 API(grSstWinClose,1,1u);setenv("D2_GLIDE_GLOW","subtle",1);test_width=0;
 API(grSstWinOpen,7,0u,8u,0u,0u,0u,2u,0u);
 rt_w32(G_MEM,0x7a6a70,0x7000);rt_w32(G_MEM,0x7a0640,0x8000);
 rt_w32(G_MEM,0x8004,0);rt_w32(G_MEM,0x8008,0);rt_w32(G_MEM,0x800c,800);rt_w32(G_MEM,0x8010,560);
 glide_world_end();assert(glows==0);glide_world_begin();glide_world_end();glide_world_end();assert(glows==1);
 API(grSstWinClose,1,1u);glide_world_begin();glide_world_end();assert(glows==1);
 unsetenv("D2_GLIDE_GLOW");API(grSstWinOpen,7,0u,8u,0u,0u,0u,2u,0u);
 glide_world_begin();glide_world_end();assert(glows==1);
 API(grGlideShutdown,0);dsr_renderer_flush(renderer,1);free(G_MEM);
 }
 puts("Glide ABI, palette/chroma, color and alpha blending, HD rasterization, LFB and reopen passed");return 0;
}
