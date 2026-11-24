/* Generate deterministic QOI + RGBA reference fixtures for the DenOS renderer.
 * Build: cc -O3 -std=c11 tools/generate_gfx_fixtures.c -lm -o /tmp/gfx-fixtures
 * Run:   /tmp/gfx-fixtures resources/graphics-lab/fixtures
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define WIDTH 1600u
#define HEIGHT 900u
#define PIXELS ((size_t)WIDTH * (size_t)HEIGHT)

typedef struct { uint8_t r,g,b,a; } Pixel;
typedef struct { FILE *f; uint8_t failed; } Writer;

static void put8(Writer *w, uint8_t v) { if (fputc(v, w->f) == EOF) w->failed = 1; }
static void put_be32(Writer *w, uint32_t v) {
    put8(w,(uint8_t)(v>>24)); put8(w,(uint8_t)(v>>16));
    put8(w,(uint8_t)(v>>8)); put8(w,(uint8_t)v);
}
static int equal(Pixel a, Pixel b) { return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a; }
static unsigned hash(Pixel p) { return (p.r*3u+p.g*5u+p.b*7u+p.a*11u)&63u; }
static uint32_t noise(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t v = x * 0x9E3779B1u + y * 0x85EBCA77u + seed * 0xC2B2AE3Du;
    v ^= v >> 16; v *= 0x7FEB352Du; v ^= v >> 15;
    v *= 0x846CA68Bu; v ^= v >> 16; return v;
}
static int clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static Pixel scene(unsigned id, unsigned x, unsigned y) {
    Pixel p = {0,0,0,255};
    unsigned checker = ((x/16u) ^ (y/16u)) & 1u;
    uint32_t n = noise(x,y,id+1u);
    switch (id) {
    case 0: /* RGB/gray calibration bars */
        if (x < WIDTH/5u) { p.r=(uint8_t)(x*255u/(WIDTH/5u)); p.g=(uint8_t)(y*255u/HEIGHT); p.b=(uint8_t)((x+y)*255u/(WIDTH+HEIGHT)); }
        else {
            unsigned bar=(x-WIDTH/5u)/(WIDTH*4u/25u);
            static const uint8_t colors[8][3]={{255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0}};
            p.r=colors[bar&7u][0]; p.g=colors[bar&7u][1]; p.b=colors[bar&7u][2];
        } break;
    case 1: /* pixel-grid and high-frequency checkerboard */
        p.r=(uint8_t)(checker?232:18); p.g=(uint8_t)(checker?88:112); p.b=(uint8_t)(checker?42:210);
        if ((x%16u)==0 || (y%16u)==0) p.r=p.g=p.b=255;
        break;
    case 2: /* smooth full-canvas RGB gradient */
        p.r=(uint8_t)(x*255u/(WIDTH-1u)); p.g=(uint8_t)(y*255u/(HEIGHT-1u));
        p.b=(uint8_t)((x+y)*255u/(WIDTH+HEIGHT-2u)); break;
    case 3: /* alpha ramps and compositing checker */
        p.r=(uint8_t)(x*255u/(WIDTH-1u)); p.g=(uint8_t)(255u-y*255u/(HEIGHT-1u));
        p.b=(uint8_t)(80u+(x/32u)%176u); p.a=(uint8_t)((x/4u)&255u);
        if (checker) { p.r=(uint8_t)(p.r/2u); p.g=(uint8_t)(p.g/2u); }
        break;
    case 4: /* line, circle and clipping boundary calibration */
        p.r=24; p.g=30; p.b=42;
        if (x%100u<2u || y%100u<2u) p.r=p.g=p.b=180;
        if (abs((int)(x%400u)-200)<2 || abs((int)(y%300u)-150)<2) p.r=255,p.g=180,p.b=30;
        { int dx=(int)(x%400u)-200, dy=(int)(y%300u)-150; int d=dx*dx+dy*dy;
          if (abs(d-10000)<500) p.r=30,p.g=230,p.b=180; }
        break;
    case 5: /* worst-case high-entropy decoder stress image */
        p.r=(uint8_t)n; p.g=(uint8_t)(n>>8); p.b=(uint8_t)(n>>16); p.a=(uint8_t)(n>>24); break;
    case 6: /* textured gradient with repeatable micro-noise */
        p.r=(uint8_t)clamp((int)(x*210u/WIDTH)+(int)(n&31u)-15);
        p.g=(uint8_t)clamp((int)(y*220u/HEIGHT)+(int)((n>>8)&31u)-15);
        p.b=(uint8_t)clamp(110+(int)((x+y)*90u/(WIDTH+HEIGHT))+(int)((n>>16)&47u)-23);
        break;
    case 7: /* palette / tile atlas */
        p.r=(uint8_t)((x/100u*37u+y/100u*11u)&255u);
        p.g=(uint8_t)((x/100u*13u+y/100u*47u)&255u);
        p.b=(uint8_t)((x/100u*53u+y/100u*19u)&255u);
        if (x%100u<3u || y%100u<3u) p.r=p.g=p.b=255;
        break;
    case 8: /* ordered dither / quantization reference */
        { static const uint8_t bayer[8][8]={{0,48,12,60,3,51,15,63},{32,16,44,28,35,19,47,31},{8,56,4,52,11,59,7,55},{40,24,36,20,43,27,39,23},{2,50,14,62,1,49,13,61},{34,18,46,30,33,17,45,29},{10,58,6,54,9,57,5,53},{42,26,38,22,41,25,37,21}};
          int t=bayer[y&7u][x&7u]*4;
          p.r=(uint8_t)clamp((int)(x*255u/WIDTH)+t-126);
          p.g=(uint8_t)clamp((int)(y*255u/HEIGHT)+t-126);
          p.b=(uint8_t)clamp((int)((x+y)*255u/(WIDTH+HEIGHT))+t-126); }
        break;
    default: /* diagonal edge / scanline antialias reference */
        { int band=((int)x-(int)y*WIDTH/HEIGHT+WIDTH)%192;
          if (band<4) p.r=255,p.g=255,p.b=255;
          else if (band<96) p.r=(uint8_t)(40u+(n&15u)),p.g=120,p.b=220;
          else p.r=35,p.g=42,p.b=58; }
        if ((y&1u)==0) { p.r=(uint8_t)(p.r*9u/10u); p.g=(uint8_t)(p.g*9u/10u); p.b=(uint8_t)(p.b*9u/10u); }
        break;
    }
    return p;
}
static int encode_qoi(FILE *f, const char *raw_path, unsigned id) {
    Writer w={f,0}; Pixel index[64]={{0}}; Pixel previous={0,0,0,255};
    FILE *raw=fopen(raw_path,"wb"); if(!raw) return -1;
    put8(&w,'q');put8(&w,'o');put8(&w,'i');put8(&w,'f');put_be32(&w,WIDTH);put_be32(&w,HEIGHT);put8(&w,4);put8(&w,0);
    unsigned run=0;
    for(unsigned y=0;y<HEIGHT;y++) for(unsigned x=0;x<WIDTH;x++) {
        Pixel p=scene(id,x,y);
        if(fwrite(&p,1,4,raw)!=4) { fclose(raw); return -1; }
        if(equal(p,previous)) { run++; if(run==62u) { put8(&w,(uint8_t)(0xC0u|(run-1u)));run=0; } continue; }
        if(run) { put8(&w,(uint8_t)(0xC0u|(run-1u)));run=0; }
        unsigned h=hash(p);
        if(equal(index[h],p)) put8(&w,(uint8_t)h);
        else {
            index[h]=p;
            if(p.a==previous.a) {
                int dr=(int)p.r-previous.r,dg=(int)p.g-previous.g,db=(int)p.b-previous.b;
                int drdg=dr-dg,dbdg=db-dg;
                if(dr>=-2&&dr<=1&&dg>=-2&&dg<=1&&db>=-2&&db<=1)
                    put8(&w,(uint8_t)(0x40u|((unsigned)(dr+2)<<4)|((unsigned)(dg+2)<<2)|(unsigned)(db+2)));
                else if(dg>=-32&&dg<=31&&drdg>=-8&&drdg<=7&&dbdg>=-8&&dbdg<=7) {
                    put8(&w,(uint8_t)(0x80u|(unsigned)(dg+32)));
                    put8(&w,(uint8_t)(((unsigned)(drdg+8)<<4)|(unsigned)(dbdg+8)));
                } else { put8(&w,0xFE);put8(&w,p.r);put8(&w,p.g);put8(&w,p.b); }
            } else { put8(&w,0xFF);put8(&w,p.r);put8(&w,p.g);put8(&w,p.b);put8(&w,p.a); }
        }
        previous=p;
    }
    if(run) put8(&w,(uint8_t)(0xC0u|(run-1u)));
    for(int i=0;i<7;i++) put8(&w,0);put8(&w,1);
    fclose(raw); return w.failed?-1:0;
}
int main(int argc,char **argv) {
    static const char *names[10]={"01-color-calibration","02-pixel-grid","03-rgb-gradients","04-alpha-ramps","05-geometry-clipping","06-high-entropy-stress","07-texture-noise","08-palette-atlas","09-ordered-dither","10-scanline-edges"};
    char qoi_path[512],raw_path[512];
    if(argc!=2){fprintf(stderr,"usage: %s OUTPUT_DIRECTORY\n",argv[0]);return 2;}
    if(mkdir(argv[1],0755)<0){/* Existing directory is accepted. */}
    for(unsigned i=0;i<10;i++){
        snprintf(qoi_path,sizeof(qoi_path),"%s/%s.qoi",argv[1],names[i]);
        snprintf(raw_path,sizeof(raw_path),"%s/%s.rgba",argv[1],names[i]);
        FILE *q=fopen(qoi_path,"wb"); if(!q){perror(qoi_path);return 1;}
        int result=encode_qoi(q,raw_path,i); fclose(q);
        if(result<0){fprintf(stderr,"failed generating fixture %s\n",names[i]);return 1;}
        printf("generated %s (%ux%u)\n",names[i],WIDTH,HEIGHT);
    }
    return 0;
}
