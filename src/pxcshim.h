/* pxcshim.h - FRAME struct + PXC vtable ABI for the pxc2_nt.dll user-mode shim.
 * Layout is the authoritative SDK ABI: FRAME from frame.h/iframe.h (0x18 bytes),
 * PXC vtable order from PXC.H (TAGpxc). Do NOT reorder the vtable - OpenLibrary
 * copies these pointers into the caller's struct in this exact order.
 * All callbacks are __stdcall; the _Name@N decorated exports of the original
 * pxc2_nt.dll give the exact byte counts (see pxc2_nt.def comments).
 */
#ifndef PXCSHIM_H
#define PXCSHIM_H
#include <windows.h>

#pragma pack(push, 1)
typedef union { void *pointer; unsigned long data; } BUFFERDATA;

typedef struct tagFRAME {
    struct tagFRAME *next;              /* +0x00 */
    short dx, dy;                       /* +0x04 width, +0x06 height */
    short type;                         /* +0x08 PBITS_* pixel format */
    unsigned char user_type;            /* +0x0A PTYPE_UFLAT=1 */
    unsigned char dma_type;             /* +0x0B PTYPE_DVIRTUAL=3 */
    BUFFERDATA user;                    /* +0x0C .pointer -> pixel array (shim fills) */
    BUFFERDATA dma;                     /* +0x10 (= user for a shim) */
    void (__stdcall *close)(struct tagFRAME *); /* +0x14 */
} FRAME;                                /* sizeof == 0x18 */
#pragma pack(pop)

/* pixel formats (frame.h) + bytes/pixel */
#define PBITS_Y8     0x0008  /* 1 */
#define PBITS_RGB15  0x060F  /* 2 */
#define PBITS_RGB16  0x0210  /* 2 */
#define PBITS_RGB24  0x0218  /* 3  B,G,R */
#define PBITS_RGB32  0x0620  /* 4  B,G,R,A */
#define PBITS_YUV422 0x0810  /* 2 */

/* video detect / model (PXC.H) */
#define NTSC_FORMAT  0x01
#define PXC200_F     0x02
#define ERR_NONE     0x00000000L

/* The PXC vtable. OpenLibrary(iface,size) installs &g_pxc into *iface from
 * offset 8 (after pad1,pad2) for up to `size` bytes. Order == PXC.H TAGpxc. */
typedef struct TAGpxc {
    long pad1, pad2;
    long  (__stdcall *AllocateFG)(short n);
    void  (__stdcall *FreeFG)(long fgh);
    short (__stdcall *ReadRevision)(long fgh);
    void  (__stdcall *Reset)(long fgh);
    long  (__stdcall *CheckError)(long fgh);
    short (__stdcall *ReadProtection)(long fgh);
    long  (__stdcall *ReadSerial)(long fgh);
    short (__stdcall *GetIOType)(long fgh, short n);
    short (__stdcall *SetIOType)(long fgh, short n, short type);
    long  (__stdcall *ReadIO)(long fgh);
    short (__stdcall *WriteImmediateIO)(long fgh, long mask, long state);
    long  (__stdcall *Grab)(long fgh, FRAME *frh, short flags);
    long  (__stdcall *Wait)(long fgh, short flags);
    short (__stdcall *WaitVB)(long fgh);
    void  (__stdcall *WaitFinished)(long fgh, long qh);
    short (__stdcall *IsFinished)(long fgh, long qh);
    void  (__stdcall *KillQueue)(long fgh);
    FRAME*(__stdcall *AllocateBuffer)(short dx, short dy, short type);
    void  (__stdcall *FreeFrame)(FRAME *frh);
    short (__stdcall *SetFieldCount)(long fgh, long c);
    long  (__stdcall *GetFieldCount)(long fgh);
    long  (__stdcall *GrabContinuous)(long fgh, FRAME *frh, short state, short flags);
    short (__stdcall *VideoType)(long fgh);
    short (__stdcall *SetXResolution)(long fgh, short rez);
    short (__stdcall *SetYResolution)(long fgh, short rez);
    short (__stdcall *SetWidth)(long fgh, short w);
    short (__stdcall *SetHeight)(long fgh, short h);
    short (__stdcall *SetLeft)(long fgh, short l);
    short (__stdcall *SetTop)(long fgh, short t);
    short (__stdcall *GetXResolution)(long fgh);
    short (__stdcall *GetYResolution)(long fgh);
    short (__stdcall *GetWidth)(long fgh);
    short (__stdcall *GetHeight)(long fgh);
    short (__stdcall *GetLeft)(long fgh);
    short (__stdcall *GetTop)(long fgh);
    long  (__stdcall *WaitAnyEvent)(long fgh, long ioh, long mask, long state, short flags);
    long  (__stdcall *WaitAllEvents)(long fgh, long ioh, long mask, long state, short flags);
    long  (__stdcall *SwitchGrab)(long fgh, FRAME *f0, FRAME *f1, FRAME *f2, FRAME *f3, short flags);
    long  (__stdcall *SwitchCamera)(long fgh, short flags);
    short (__stdcall *GetSwitch)(long fgh);
    long  (__stdcall *SetCamera)(long fgh, short n, short flags);
    short (__stdcall *GetCamera)(long fgh);
    long  (__stdcall *SetContrast)(long fgh, float c, short flags);
    long  (__stdcall *SetBrightness)(long fgh, float b, short flags);
    float (__stdcall *GetContrast)(long fgh);
    float (__stdcall *GetBrightness)(long fgh);
    long  (__stdcall *SetHue)(long fgh, float h, short flags);
    long  (__stdcall *SetSaturation)(long fgh, float s, short flags);
    float (__stdcall *GetHue)(long fgh);
    float (__stdcall *GetSaturation)(long fgh);
    short (__stdcall *SetPixelFormat)(long fgh, short type);
    float (__stdcall *SetVideoLevel)(long fgh, float white);
    float (__stdcall *GetVideoLevel)(long fgh);
    short (__stdcall *SetChromaControl)(long fgh, short cf);
    short (__stdcall *GetChromaControl)(long fgh);
    short (__stdcall *SetLumaControl)(long fgh, short cf);
    short (__stdcall *GetLumaControl)(long fgh);
    short (__stdcall *SetVideoDetect)(long fgh, short type);
    short (__stdcall *GetVideoDetect)(long fgh);
    float (__stdcall *SetStrobePeriod)(long fgh, short n, float p);
    float (__stdcall *GetStrobePeriod)(long fgh, short n);
    short (__stdcall *SetHoldoffStart)(long fgh, short field, short start);
    short (__stdcall *SetHoldoffWidth)(long fgh, short field, short width);
    short (__stdcall *SetHoldoffMask)(long fgh, long mask);
    short (__stdcall *GetHoldoffStart)(long fgh, short field);
    short (__stdcall *GetHoldoffWidth)(long fgh, short field);
    long  (__stdcall *GetHoldoffMask)(long fgh);
    float (__stdcall *SetDoubleStrobe)(long fgh, short n, float p);
    float (__stdcall *GetDoubleStrobe)(long fgh, short n);
    short (__stdcall *TriggerStrobe)(long fgh, short n, long mask);
    short (__stdcall *SyncStrobe)(long fgh, short n, short field, short line);
    short (__stdcall *FireStrobe)(long fgh, long mask);
    short (__stdcall *SetDecisionPoint)(long fgh, short field, short line);
    short (__stdcall *GetDecisionPoint)(long fgh, short field);
    short (__stdcall *SetDebounce)(long fgh, short n, short db);
    short (__stdcall *GetDebounce)(long fgh, short n);
    short (__stdcall *GetModelNumber)(long fgh);
} PXC;

#endif /* PXCSHIM_H */
