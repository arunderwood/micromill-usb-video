/* shimtest.cpp - validates the pxc2_nt.dll shim END TO END against the real
 * capture device, driving it EXACTLY as PXC200_Support.dll does (LoadLibrary +
 * GetProcAddress("OpenLibrary") + the PXC vtable). Does NOT touch MicroMill, so
 * it carries zero mill-hardware risk. Saves captured frames to BMP.
 *
 * Usage: shimtest.exe <path-to-pxc2_nt.dll> [outdir] [type]
 *   type: 32 (RGB32,default) | 24 | 16 | 15 | 8   (the FRAME pixel format)
 * Build 32-bit (build-test.cmd) to match the 32-bit shim DLL.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/pxcshim.h"

typedef short (__stdcall *OpenLibraryFn)(PXC *, short);
typedef void  (__stdcall *CloseLibraryFn)(PXC *);

static int Bpp(int t){ switch(t){case PBITS_Y8:return 1;case PBITS_RGB15:case PBITS_RGB16:case PBITS_YUV422:return 2;case PBITS_RGB24:return 3;default:return 4;} }

/* Save a FRAME as a 24-bit BMP (converts from the frame's type for viewing). */
static void SaveBmp(const char *path, FRAME *f) {
    int w = f->dx, h = f->dy, t = f->type, bpp = Bpp(t);
    const BYTE *src = (const BYTE *)f->user.pointer;
    if (!src) return;
    int rowpad = (4 - (w * 3) % 4) % 4;
    DWORD imgBytes = (DWORD)(w * 3 + rowpad) * h;
    BITMAPFILEHEADER fh; BITMAPINFOHEADER ih;
    ZeroMemory(&fh,sizeof(fh)); ZeroMemory(&ih,sizeof(ih));
    fh.bfType=0x4D42; fh.bfOffBits=sizeof(fh)+sizeof(ih); fh.bfSize=fh.bfOffBits+imgBytes;
    ih.biSize=sizeof(ih); ih.biWidth=w; ih.biHeight=-h; ih.biPlanes=1; ih.biBitCount=24; ih.biCompression=BI_RGB;
    FILE *fp=NULL; if (fopen_s(&fp,path,"wb")||!fp) return;
    fwrite(&fh,sizeof(fh),1,fp); fwrite(&ih,sizeof(ih),1,fp);
    BYTE *line=(BYTE*)malloc((size_t)w*3+rowpad); if(!line){fclose(fp);return;}
    for (int y=0;y<h;++y){
        const BYTE *s=src+(size_t)y*w*bpp;
        for (int x=0;x<w;++x){
            BYTE b,g,r;
            const BYTE *p=s+(size_t)x*bpp;
            switch(t){
                case PBITS_Y8: b=g=r=p[0]; break;
                case PBITS_RGB24: b=p[0];g=p[1];r=p[2]; break;
                case PBITS_RGB32: b=p[0];g=p[1];r=p[2]; break;
                case PBITS_RGB16:{unsigned short v=p[0]|(p[1]<<8); b=(BYTE)((v&0x1F)<<3); g=(BYTE)(((v>>5)&0x3F)<<2); r=(BYTE)(((v>>11)&0x1F)<<3);}break;
                case PBITS_RGB15:{unsigned short v=p[0]|(p[1]<<8); b=(BYTE)((v&0x1F)<<3); g=(BYTE)(((v>>5)&0x1F)<<3); r=(BYTE)(((v>>10)&0x1F)<<3);}break;
                case PBITS_YUV422:{int yv=p[(x&1)?2:0]; b=g=r=(BYTE)yv;}break; /* luma only for preview */
                default: b=p[0];g=p[1];r=p[2]; break;
            }
            line[x*3+0]=b; line[x*3+1]=g; line[x*3+2]=r;
        }
        for(int k=0;k<rowpad;++k) line[w*3+k]=0;
        fwrite(line,(size_t)w*3+rowpad,1,fp);
    }
    free(line); fclose(fp);
}

static void Stats(FRAME *f){
    int w=f->dx,h=f->dy,t=f->type,bpp=Bpp(t); const BYTE*s=(const BYTE*)f->user.pointer;
    unsigned mn=255,mx=0; unsigned long long sum=0; size_t n=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const BYTE*p=s+((size_t)y*w+x)*bpp; unsigned l; if(t==PBITS_Y8)l=p[0]; else l=(unsigned)((p[2]*299+p[1]*587+p[0]*114)/1000); sum+=l; if(l<mn)mn=l; if(l>mx)mx=l; ++n;}
    printf("   luminance min=%u mean=%.1f max=%u\n", mn, n?(double)sum/n:0, mx);
}

int main(int argc, char **argv){
    const char *dll = (argc>1)?argv[1]:"..\\..\\src\\build\\pxc2_nt.dll";
    const char *outdir = (argc>2)?argv[2]:".";
    int type = PBITS_RGB32;
    if (argc>3){ int k=atoi(argv[3]); type = (k==24)?PBITS_RGB24:(k==16)?PBITS_RGB16:(k==15)?PBITS_RGB15:(k==8)?PBITS_Y8:PBITS_RGB32; }

    HMODULE h = LoadLibraryA(dll);
    if (!h){ printf("LoadLibrary(%s) failed %lu\n", dll, GetLastError()); return 1; }
    OpenLibraryFn  OpenLib  = (OpenLibraryFn)GetProcAddress(h, "OpenLibrary");
    CloseLibraryFn CloseLib = (CloseLibraryFn)GetProcAddress(h, "CloseLibrary");
    if (!OpenLib){ printf("no OpenLibrary export\n"); return 1; }

    PXC pxc; ZeroMemory(&pxc, sizeof(pxc));
    short r = OpenLib(&pxc, (short)sizeof(pxc));
    printf("OpenLibrary -> %d\n", r);
    if (!pxc.AllocateFG || !pxc.AllocateBuffer || !pxc.GrabContinuous){ printf("vtable not populated\n"); return 1; }

    long fgh = pxc.AllocateFG(0);
    printf("AllocateFG -> 0x%lx   model=%d  videoType=%d\n", fgh, pxc.GetModelNumber?pxc.GetModelNumber(fgh):-1, pxc.VideoType?pxc.VideoType(fgh):-1);

    FRAME *frame = pxc.AllocateBuffer(640, 480, (short)type);
    printf("AllocateBuffer(640,480,0x%03x) -> %p  user=%p\n", type, (void*)frame, frame?frame->user.pointer:NULL);
    if (!frame){ printf("alloc failed\n"); return 1; }

    /* wait for the capture engine to go live (device warm-up) */
    int live=0;
    for (int i=0;i<40 && !live;++i){ Sleep(100); live = pxc.GetVideoDetect?pxc.GetVideoDetect(fgh):1; }
    printf("GetVideoDetect(live) -> %d after warm-up\n", live);

    /* continuous grab, save several frames spaced over ~1.5s */
    pxc.GrabContinuous(fgh, frame, 1, 0);
    for (int i=0;i<5;++i){
        Sleep(300);
        char path[512]; sprintf_s(path, "%s\\shimtest_%d.bmp", outdir, i);
        SaveBmp(path, frame);
        printf(" frame %d saved %s\n", i, path); Stats(frame);
    }
    pxc.GrabContinuous(fgh, frame, 0, 0);

    /* single Grab path too */
    pxc.Grab(fgh, frame, 0);
    { char path[512]; sprintf_s(path, "%s\\shimtest_single.bmp", outdir); SaveBmp(path, frame); printf(" single Grab saved %s\n", path); Stats(frame); }

    pxc.FreeFrame(frame);
    if (CloseLib) CloseLib(&pxc);
    FreeLibrary(h);
    printf("done\n");
    return 0;
}
