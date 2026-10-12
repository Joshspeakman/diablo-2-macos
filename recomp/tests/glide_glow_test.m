/* Behavioral checks on the real GPU: localized colored glow, unchanged dark
 * regions/HUD, strength, render-scale consistency, gamma and renderer isolation. */
#import <Foundation/Foundation.h>
#include "../../src/renderer/renderer.h"
#include "../../src/dsr/dsr_proto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { W=64,H=48 };
static void exec(DSRRenderer *r,uint32_t op,const void *p,uint32_t n){dsr_renderer_exec(r,op,p,n);}
static void scene(DSRRenderer *r){
    uint32_t up[6+W*H]={1,0,0,W,H,W*4};
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){
        uint32_t c=0xff101010;
        if(x>=20&&x<26&&y>=12&&y<18)c=0xffff8000;
        if(y>=34)c=0xff204060; // an old HUD underneath the next world frame
        if(x>=46&&x<54&&y>=34)c=0xffffffff;
        up[6+y*W+x]=c;
    }
    exec(r,DSR_SURFACE_UPLOAD,up,sizeof up);
}
static uint32_t *capture(DSRRenderer *r,id<MTLDevice> dev){
    uint32_t pr=1;exec(r,DSR_PRESENT,&pr,4);
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:W height:H mipmapped:NO];
    d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;d.storageMode=MTLStorageModeShared;
    id<MTLTexture> t=[dev newTextureWithDescriptor:d];
    dsr_renderer_present_to(r,t);dsr_renderer_flush(r,1);
    uint32_t *pixels=malloc(W*H*4);[t getBytes:pixels bytesPerRow:W*4 fromRegion:MTLRegionMake2D(0,0,W,H) mipmapLevel:0];return pixels;
}
static int channel(uint32_t c,int k){return(c>>(8*k))&255;}
int main(void){@autoreleasepool{
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();assert(dev);
    int totals[2]={0};
    for(int scale=1;scale<=2;scale++){
        setenv("DSR_RENDER_SCALE",scale==1?"1":"2",1);setenv("DSR_FILTER","pixel",1);
        DSRRenderer *r=dsr_renderer_create(dev);
        uint32_t cr[]={1,W,H,1,1,0,0,1,0,0};exec(r,DSR_SURFACE_CREATE,cr,sizeof cr);
        uint32_t rt[]={1,0};exec(r,DSR_SET_RT,rt,sizeof rt);
        uint32_t glide[16]={1};exec(r,DSR_GLIDE_STATE,glide,sizeof glide);
        scene(r);uint32_t *base=capture(r,dev);
        uint32_t glow[]={1,0,0,0,W,32};exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);
        uint32_t *off=capture(r,dev);assert(!memcmp(base,off,W*H*4));free(off);
        glow[1]=1;exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);uint32_t *subtle=capture(r,dev);
        int added=0;
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){
            int i=y*W+x;
            if(y>=32||x>=44)assert(subtle[i]==base[i]); // no HUD glow or distant ambient lift
            for(int k=0;k<3;k++)assert(channel(subtle[i],k)>=channel(base[i],k));
            added+=channel(subtle[i],2)-channel(base[i],2);
        }
        assert(added>100);totals[scale-1]=added;
        uint32_t halo=subtle[15*W+18];assert(channel(halo,2)>channel(halo,1)&&channel(halo,1)>=channel(halo,0));
        scene(r);glow[1]=2;exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);uint32_t *strong=capture(r,dev);
        int stronger=0;for(int i=0;i<W*H;i++)stronger+=channel(strong[i],2)-channel(base[i],2);assert(stronger>added);
        // A post-world HUD draw is exact even after the glow pass changed GPU state.
        uint32_t fill[]={1,4,36,12,44,0,0,0,0,0,0,0xff00ff00};exec(r,DSR_BLT,fill,sizeof fill);
        uint32_t *hud=capture(r,dev);assert(hud[40*W+8]==0xff00ff00);free(hud);
        // Gamma is still applied exactly once, after the scene glow.
        scene(r);glow[1]=1;exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);
        uint16_t gamma[3][256];for(int c=0;c<3;c++)for(int i=0;i<256;i++)gamma[c][i]=(255-i)*257;
        exec(r,DSR_GAMMA,gamma,sizeof gamma);uint32_t *inverted=capture(r,dev);
        for(int i=0;i<W*H;i++)for(int k=0;k<3;k++)assert(abs(channel(inverted[i],k)-(255-channel(subtle[i],k)))<=1);
        free(inverted);for(int c=0;c<3;c++)for(int i=0;i<256;i++)gamma[c][i]=i*257;exec(r,DSR_GAMMA,gamma,sizeof gamma);
        scene(r);glide[0]=0;exec(r,DSR_GLIDE_STATE,glide,sizeof glide);exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);
        uint32_t *d3d=capture(r,dev);assert(!memcmp(base,d3d,W*H*4));free(d3d);
        // Resizing intermediates and clipping invalid/empty rectangles remain safe.
        glide[0]=1;exec(r,DSR_GLIDE_STATE,glide,sizeof glide);glow[4]=0;exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);
        uint32_t *empty=capture(r,dev);assert(!memcmp(base,empty,W*H*4));free(empty);
        cr[1]=W/2;cr[2]=H/2;exec(r,DSR_SURFACE_CREATE,cr,sizeof cr);glow[4]=W;exec(r,DSR_GLIDE_GLOW,glow,sizeof glow);dsr_renderer_flush(r,1);
        free(base);free(subtle);free(strong);
    }
    assert(abs(totals[0]-totals[1])<=totals[0]/10+10);
    printf("ok: Glide glow locality, color, HUD, strengths, gamma, renderer isolation and 1x/2x scaling (%d/%d)\n",totals[0],totals[1]);
}return 0;}
