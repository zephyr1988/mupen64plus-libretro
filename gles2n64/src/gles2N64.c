
#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <string.h>

#include "m64p_types.h"
#include "m64p_plugin.h"

#include "gles2N64.h"
#include "Debug.h"
#include "OpenGL.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
#include "Config.h"
#include "Textures.h"
#include "ShaderCombiner.h"
#include "3DMath.h"
#include "../../libretro/SDL.h"

#ifdef __LIBRETRO__ // Prefix API
#define VIDEO_TAG(X) gln64##X

#define ReadScreen2 VIDEO_TAG(ReadScreen2)
#define PluginStartup VIDEO_TAG(PluginStartup)
#define PluginShutdown VIDEO_TAG(PluginShutdown)
#define PluginGetVersion VIDEO_TAG(PluginGetVersion)
#define CaptureScreen VIDEO_TAG(CaptureScreen)
#define ChangeWindow VIDEO_TAG(ChangeWindow)
#define CloseDLL VIDEO_TAG(CloseDLL)
#define DllTest VIDEO_TAG(DllTest)
#define DrawScreen VIDEO_TAG(DrawScreen)
#define GetDllInfo VIDEO_TAG(GetDllInfo)
#define InitiateGFX VIDEO_TAG(InitiateGFX)
#define MoveScreen VIDEO_TAG(MoveScreen)
#define RomClosed VIDEO_TAG(RomClosed)
#define RomOpen VIDEO_TAG(RomOpen)
#define ShowCFB VIDEO_TAG(ShowCFB)
#define SetRenderingCallback VIDEO_TAG(SetRenderingCallback)
#define UpdateScreen VIDEO_TAG(UpdateScreen)
#define ViStatusChanged VIDEO_TAG(ViStatusChanged)
#define ViWidthChanged VIDEO_TAG(ViWidthChanged)
#define ReadScreen VIDEO_TAG(ReadScreen)
#define FBGetFrameBufferInfo VIDEO_TAG(FBGetFrameBufferInfo)
#define FBRead VIDEO_TAG(FBRead)
#define FBWrite VIDEO_TAG(FBWrite)
#define ProcessDList VIDEO_TAG(ProcessDList)
#define ProcessRDPList VIDEO_TAG(ProcessRDPList)
#define ResizeVideoOutput VIDEO_TAG(ResizeVideoOutput)
#endif

u32         last_good_ucode = (u32) -1;
void        (*renderCallback)() = NULL;

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle,
        void *Context, void (*DebugCallback)(void *, int, const char *))
{
#ifdef __ARM_NEON__
   unsigned cpu = 0;

   if (perf_get_cpu_features_cb)
      cpu = perf_get_cpu_features_cb();

   if (cpu & RETRO_SIMD_NEON)
   {
      MathInitNeon();
      gSPInitNeon();
   }
#endif
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
   OGL_Stop();  // paulscode, OGL_Stop missing from Yongzh's code
   return M64ERR_SUCCESS; // __LIBRETRO__: Fix warning
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType,
        int *PluginVersion, int *APIVersion, const char **PluginNamePtr,
        int *Capabilities)
{
   /* set version info */
   if (PluginType != NULL)
      *PluginType = M64PLUGIN_GFX;

   if (PluginVersion != NULL)
      *PluginVersion = PLUGIN_VERSION;

   if (APIVersion != NULL)
      *APIVersion = PLUGIN_API_VERSION;

   if (PluginNamePtr != NULL)
      *PluginNamePtr = PLUGIN_NAME;

   if (Capabilities != NULL)
   {
      *Capabilities = 0;
   }

   return M64ERR_SUCCESS;
}

EXPORT void CALL ChangeWindow (void)
{
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
}

EXPORT int CALL InitiateGFX (GFX_INFO Gfx_Info)
{
    Config_gln64_LoadConfig();
    Config_gln64_LoadRomConfig(Gfx_Info.HEADER);

    OGL_Start();

    return 1;
}

EXPORT void CALL ProcessDList(void)
{
    OGL.frame_dl++;

    RSP_ProcessDList();
    OGL.mustRenderDlist = true;
}

EXPORT void CALL ProcessRDPList(void)
{
}

EXPORT void CALL ResizeVideoOutput(int Width, int Height)
{
}

EXPORT void CALL RomClosed (void)
{
}

EXPORT int CALL RomOpen (void)
{
    RSP_Init();
    OGL.frame_dl = 0;
    OGL.frame_prevdl = -1;
    OGL.mustRenderDlist = false;

    return 1;
}

EXPORT void CALL RomResumed(void)
{
}

EXPORT void CALL ShowCFB (void)
{
}

EXPORT void CALL UpdateScreen (void)
{
   //has there been any display lists since last update
   if (OGL.frame_prevdl == OGL.frame_dl)
      return;

   OGL.frame_prevdl = OGL.frame_dl;

   if (OGL.mustRenderDlist)
   {
      OGL.screenUpdate=true;
      VI_UpdateScreen();
      OGL.mustRenderDlist = false;
   }
}

EXPORT void CALL ViStatusChanged (void)
{
}

EXPORT void CALL ViWidthChanged (void)
{
}

/******************************************************************
  Function: FrameBufferRead
  Purpose:  This function is called to notify the dll that the
            frame buffer memory is beening read at the given address.
            DLL should copy content from its render buffer to the frame buffer
            in N64 RDRAM
            DLL is responsible to maintain its own frame buffer memory addr list
            DLL should copy 4KB block content back to RDRAM frame buffer.
            Emulator should not call this function again if other memory
            is read within the same 4KB range

            Since depth buffer is also being watched, the reported addr
            may belong to depth buffer
  input:    addr        rdram address
            val         val
            size        1 = uint8, 2 = uint16, 4 = uint32
  output:   none
*******************************************************************/ 

EXPORT void CALL FBRead(u32 addr)
{
}

/******************************************************************
  Function: FrameBufferWrite
  Purpose:  This function is called to notify the dll that the
            frame buffer has been modified by CPU at the given address.

            Since depth buffer is also being watched, the reported addr
            may belong to depth buffer

  input:    addr        rdram address
            val         val
            size        1 = uint8, 2 = uint16, 4 = uint32
  output:   none
*******************************************************************/ 

EXPORT void CALL FBWrite(u32 addr, u32 size)
{
}

/************************************************************************
Function: FBGetFrameBufferInfo
Purpose:  This function is called by the emulator core to retrieve frame
          buffer information from the video plugin in order to be able
          to notify the video plugin about CPU frame buffer read/write
          operations

          size:
            = 1     byte
            = 2     word (16 bit) <-- this is N64 default depth buffer format
            = 4     dword (32 bit)

          when frame buffer information is not available yet, set all values
          in the FrameBufferInfo structure to 0

input:    FrameBufferInfo pinfo[6]
          pinfo is pointed to a FrameBufferInfo structure which to be
          filled in by this function
output:   Values are return in the FrameBufferInfo structure
          Plugin can return up to 6 frame buffer info
 ************************************************************************/

EXPORT void CALL FBGetFrameBufferInfo(void *p)
{
}

// paulscode, API changed this to "ReadScreen2" in Mupen64Plus 1.99.4
EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
   /* TODO: 'int front' was added in 1.99.4.  What to do with this here? */
   OGL_ReadScreen(dest, width, height);
}

EXPORT void CALL SetRenderingCallback(void (*callback)())
{
   renderCallback = callback;
}

EXPORT void CALL StartGL(void)
{
   OGL_Start();
}

EXPORT void CALL StopGL(void)
{
   OGL_Stop();
}

#ifdef __LIBRETRO__
void gles2n64_reset(void)
{
   // HACK: Check for leaks!
   OGL_Stop();
   OGL_Start();
   RSP_Init();
}
#endif

