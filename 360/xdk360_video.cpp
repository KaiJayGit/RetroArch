/*  SSNES - A Super Nintendo Entertainment System (SNES) Emulator frontend for libsnes.
 *  Copyright (C) 2010-2012 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2012 - Daniel De Matteis
 *
 *  Some code herein may be based on code found in BSNES.
 * 
 *  SSNES is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  SSNES is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with SSNES.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <xtl.h>

#include "../driver.h"

#include "xdk360_video.h"
#include "../general.h"
#include "../message.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static const char* g_strPixelShaderProgram =
    " sampler2D tex : register(s0);       "
    " struct PS_IN                        "
    " {                                   "
    "     float2 coord : TEXCOORD0;       "
    " };                                  "
    "                                     "
    " float4 main(PS_IN input) : COLOR    "
    " {                                   "
    "     return tex2D(tex, input.coord); "
    " }                                   ";

static const char* g_strVertexShaderProgram =
    " struct VS_IN                                  "
    "                                               "
    " {                                             "
    "     float2 pos : POSITION;                    "
    "     float2 coord : TEXCOORD0;                 "
    " };                                            "
    "                                               "
    " struct VS_OUT                                 "
    " {                                             "
    "     float4 pos : POSITION;                    "
    "     float2 coord : TEXCOORD0;                 "
    " };                                            "
    "                                               "
    " VS_OUT main(VS_IN input)                      "
    " {                                             "
    "     VS_OUT output;                            "
    "     output.pos = float4(input.pos, 0.0, 1.0); "
    "     output.coord = input.coord;               "
    "     return output;                            "
    " }                                             ";

typedef struct DrawVerticeFormats
{
   float x, y;
   float u, v;
} DrawVerticeFormats;

static bool g_quitting;
static bool g_first_msg;
unsigned g_frame_count;
void *g_d3d;
Console g_screen_console;

static void xdk360_gfx_free(void * data)
{
   if (g_d3d)
	   return;

   xdk360_video_t *vid = (xdk360_video_t*)data;

   if (!vid)
      return;

   vid->lpTexture->Release();
   vid->vertex_buf->Release();
   vid->pVertexDecl->Release();
   vid->pPixelShader->Release();
   vid->pVertexShader->Release();
   vid->xdk360_render_device->Release();
   vid->xdk360_device->Release();

   free(vid);
}

static void *xdk360_gfx_init(const video_info_t *video, const input_driver_t **input, void **input_data)
{
   if (g_d3d)
      return g_d3d;

   xdk360_video_t *vid = (xdk360_video_t*)calloc(1, sizeof(xdk360_video_t));
   if (!vid)
      return NULL;

   vid->xdk360_device = Direct3DCreate9(D3D_SDK_VERSION);
   if (!vid->xdk360_device)
   {
      free(vid);
      return NULL;
   }

   // Get video settings

   memset(&vid->video_mode, 0, sizeof(vid->video_mode));

   XGetVideoMode(&vid->video_mode);

   memset(&vid->d3dpp, 0, sizeof(vid->d3dpp));

   // no letterboxing in 4:3 mode (if widescreen is
   // unsupported
   if(!vid->video_mode.fIsWideScreen)
	   vid->d3dpp.Flags |= D3DPRESENTFLAG_NO_LETTERBOX;
   
   vid->d3dpp.BackBufferWidth		= vid->video_mode.fIsHiDef ? 1280 : 640;
   vid->d3dpp.BackBufferHeight      = vid->video_mode.fIsHiDef ? 720 : 480;
   vid->d3dpp.BackBufferFormat        = (D3DFORMAT)MAKESRGBFMT(D3DFMT_A8R8G8B8);
   vid->d3dpp.FrontBufferFormat       = (D3DFORMAT)MAKESRGBFMT(D3DFMT_LE_X8R8G8B8);
   vid->d3dpp.MultiSampleType         = D3DMULTISAMPLE_NONE;
   vid->d3dpp.MultiSampleQuality      = 0;
   vid->d3dpp.BackBufferCount         = 2;
   vid->d3dpp.EnableAutoDepthStencil  = TRUE;
   vid->d3dpp.AutoDepthStencilFormat  = D3DFMT_D24S8;
   vid->d3dpp.SwapEffect              = D3DSWAPEFFECT_DISCARD;
   vid->d3dpp.PresentationInterval    = video->vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

   vid->xdk360_device->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &vid->d3dpp, 
         &vid->xdk360_render_device);

   ID3DXBuffer* pShaderCodeV = NULL;
   ID3DXBuffer* pShaderCodeP = NULL;
   ID3DXBuffer* pErrorMsg = NULL;

   HRESULT hr = D3DXCompileShader(g_strVertexShaderProgram, (UINT)strlen(g_strVertexShaderProgram),
         NULL, NULL, "main", "vs_2_0", 0, &pShaderCodeV, &pErrorMsg, NULL);

   if (SUCCEEDED(hr))
   {
      hr = D3DXCompileShader(g_strPixelShaderProgram, (UINT)strlen(g_strPixelShaderProgram),
            NULL, NULL, "main", "ps_2_0", 0, &pShaderCodeP, &pErrorMsg, NULL);
   }

   if (FAILED(hr))
   {
      OutputDebugString(pErrorMsg ? (char*)pErrorMsg->GetBufferPointer() : "");
      vid->xdk360_render_device->Release();
      vid->xdk360_device->Release();
      free(vid);
      return NULL;
   }

   vid->xdk360_render_device->CreateVertexShader((const DWORD*)pShaderCodeV->GetBufferPointer(), &vid->pVertexShader);
   vid->xdk360_render_device->CreatePixelShader((const DWORD*)pShaderCodeP->GetBufferPointer(), &vid->pPixelShader);
   pShaderCodeV->Release();
   pShaderCodeP->Release();

   vid->xdk360_render_device->CreateTexture(512, 512, 1, 0, D3DFMT_LIN_X1R5G5B5,
               0, &vid->lpTexture, NULL);

   D3DLOCKED_RECT d3dlr;
   if (SUCCEEDED(vid->lpTexture->LockRect(0, &d3dlr, NULL, D3DLOCK_NOSYSLOCK)))
   {
      memset(d3dlr.pBits, 0, 512 * d3dlr.Pitch);
      vid->lpTexture->UnlockRect(0);
   }

   vid->last_width = 512;
   vid->last_height = 512;

   vid->xdk360_render_device->CreateVertexBuffer(4 * sizeof(DrawVerticeFormats), 0, 
               0, 0, &vid->vertex_buf, NULL);

   static const DrawVerticeFormats init_verts[] = {
      { -1.0f, -1.0f, 0.0f, 1.0f },
      {  1.0f, -1.0f, 1.0f, 1.0f },
      { -1.0f,  1.0f, 0.0f, 0.0f },
      {  1.0f,  1.0f, 1.0f, 0.0f },
   };

   void *verts_ptr;
   vid->vertex_buf->Lock(0, 0, &verts_ptr, 0);
   memcpy(verts_ptr, init_verts, sizeof(init_verts));
   vid->vertex_buf->Unlock();

   static const D3DVERTEXELEMENT9 VertexElements[] =
   {
      { 0, 0 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
      { 0, 2 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
      D3DDECL_END()
   };

   vid->xdk360_render_device->CreateVertexDeclaration(VertexElements, &vid->pVertexDecl);

   vid->xdk360_render_device->Clear(0, NULL, D3DCLEAR_TARGET,
         0xff000000, 1.0f, 0);

   vid->xdk360_render_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
   vid->xdk360_render_device->SetRenderState(D3DRS_ZENABLE, FALSE);

   D3DVIEWPORT9 vp = {0};
   vp.Width  = vid->video_mode.fIsHiDef ? 1280 : 640;
   vp.Height = vid->video_mode.fIsHiDef ? 720 : 480;
   vp.MinZ   = 0.0f;
   vp.MaxZ   = 1.0f;
   vid->xdk360_render_device->SetViewport(&vp);

   return vid;
}

static bool xdk360_gfx_frame(void *data, const void *frame,
      unsigned width, unsigned height, unsigned pitch, const char *msg)
{
   xdk360_video_t *vid = (xdk360_video_t*)data;
   g_frame_count++;

   vid->xdk360_render_device->Clear(0, NULL, D3DCLEAR_TARGET,
         0xff000000, 1.0f, 0);

   if (vid->last_width != width || vid->last_height != height)
   {
      D3DLOCKED_RECT d3dlr;
      if (SUCCEEDED(vid->lpTexture->LockRect(0, &d3dlr, NULL, D3DLOCK_NOSYSLOCK)))
      {
         memset(d3dlr.pBits, 0, 512 * d3dlr.Pitch);
         vid->lpTexture->UnlockRect(0);
      }

      float tex_w = width / 512.0f;
      float tex_h = height / 512.0f;
      const DrawVerticeFormats verts[] = {
         { -1.0f, -1.0f, 0.0f,  tex_h },
         {  1.0f, -1.0f, tex_w, tex_h },
         { -1.0f,  1.0f, 0.0f,  0.0f },
         {  1.0f,  1.0f, tex_w, 0.0f },
      };

      void *verts_ptr;
      vid->vertex_buf->Lock(0, 0, &verts_ptr, 0);
      memcpy(verts_ptr, verts, sizeof(verts));
      vid->vertex_buf->Unlock();

      vid->last_width = width;
      vid->last_height = height;
   }

   D3DLOCKED_RECT d3dlr;
   if (SUCCEEDED(vid->lpTexture->LockRect(0, &d3dlr, NULL, D3DLOCK_NOSYSLOCK)))
   {
      for (unsigned y = 0; y < height; y++)
      {
         const uint8_t *in = (const uint8_t*)frame + y * pitch;
         uint8_t *out = (uint8_t*)d3dlr.pBits + y * d3dlr.Pitch;
         memcpy(out, in, width * sizeof(uint16_t));
      }
      vid->lpTexture->UnlockRect(0);
   }

   vid->xdk360_render_device->SetTexture(0, vid->lpTexture);
   vid->xdk360_render_device->SetSamplerState(0, D3DSAMP_MINFILTER, g_settings.video.smooth ? D3DTEXF_LINEAR : D3DTEXF_POINT);
   vid->xdk360_render_device->SetSamplerState(0, D3DSAMP_MAGFILTER, g_settings.video.smooth ? D3DTEXF_LINEAR : D3DTEXF_POINT);
   vid->xdk360_render_device->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_BORDER);
   vid->xdk360_render_device->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_BORDER);

   vid->xdk360_render_device->SetVertexShader(vid->pVertexShader);
   vid->xdk360_render_device->SetPixelShader(vid->pPixelShader);

   vid->xdk360_render_device->SetVertexDeclaration(vid->pVertexDecl);
   vid->xdk360_render_device->SetStreamSource(0, vid->vertex_buf, 0, sizeof(DrawVerticeFormats));

   vid->xdk360_render_device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
   if (msg)
   {
	   if(IS_TIMER_EXPIRED() || g_first_msg)
	   {
		   g_screen_console.Format(msg);
		   g_first_msg = 0;
		   SET_TIMER_EXPIRATION(30);
	   }
	   
	   g_screen_console.Render();
   }

   if(!vid->block_swap)
	   vid->xdk360_render_device->Present(NULL, NULL, NULL, NULL);

   return true;
}

static void xdk360_set_swap_block_swap (void * data, bool toggle)
{
	(void)data;
	xdk360_video_t *vid = (xdk360_video_t*)g_d3d;
	vid->block_swap = toggle;

	if(toggle)
		SSNES_LOG("Swap is set to blocked.\n");
	else
		SSNES_LOG("Swap is set to non-blocked.\n");
}

static void xdk360_swap (void * data)
{
	(void)data;
	xdk360_video_t *vid = (xdk360_video_t*)g_d3d;
	vid->xdk360_render_device->Present(NULL, NULL, NULL, NULL);	
}

static void xdk360_gfx_set_nonblock_state(void *data, bool state)
{
   xdk360_video_t *vid = (xdk360_video_t*)data;
   SSNES_LOG("D3D Vsync => %s\n", state ? "off" : "on");
   if(state)
	   vid->xdk360_render_device->SetRenderState(D3DRS_PRESENTINTERVAL,
	   D3DPRESENT_INTERVAL_IMMEDIATE);
   else
	   vid->xdk360_render_device->SetRenderState(D3DRS_PRESENTINTERVAL,
	   D3DPRESENT_INTERVAL_ONE);
}

static bool xdk360_gfx_alive(void *data)
{
   (void)data;
   return !g_quitting;
}

static bool xdk360_gfx_focus(void *data)
{
   (void)data;
   return true;
}

void xdk360_video_set_vsync(bool vsync)
{
	xdk360_gfx_set_nonblock_state(g_d3d, vsync);
}

// 360 needs a working graphics stack before SSNESeven starts.
// To deal with this main.c,
// the top level module owns the instance, and is created beforehand.
// When SSNES gets around to init it, it is already allocated.
// When SSNES wants to free it, it is ignored.
void xdk360_video_init(void)
{
	video_info_t video_info = {0};
	// Might have to supply correct values here.
	video_info.vsync = g_settings.video.vsync;
	video_info.force_aspect = false;
	video_info.smooth = g_settings.video.smooth;
	video_info.input_scale = 2;

	g_d3d = xdk360_gfx_init(&video_info, NULL, NULL);

	g_first_msg = true;

	HRESULT hr = g_screen_console.Create("game:\\media\\Arial_12.xpr",
		0xff000000, 0xffffffff );
	if(FAILED(hr))
	{
		SSNES_ERR("Couldn't create debug console.\n");
	}
}

void xdk360_video_deinit(void)
{
	void *data = g_d3d;
	g_d3d = NULL;
	xdk360_gfx_free(data);
}

const video_driver_t video_xdk360 = {
   xdk360_gfx_init,
   xdk360_gfx_frame,
   xdk360_gfx_set_nonblock_state,
   xdk360_gfx_alive,
   xdk360_gfx_focus,
   NULL,
   xdk360_gfx_free,
   "xdk360",
   xdk360_set_swap_block_swap,
   xdk360_swap
};

