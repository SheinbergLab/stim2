#include "radtypes.h"

#include <windows.h>
#include <GL/gl.h>

static HDC gl_dc;
static HGLRC gl_rc;


typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int *attribList);

#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_DEBUG_BIT_ARB         0x00000001
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

#if defined( BINKGPUAPITYPE )
  #define open_graphics open_graphicsGL
  #define close_graphics close_graphicsGL
  #define start_graphics_frame start_graphics_frameGL
  #define end_graphics_frame end_graphics_frameGL
  #define check_for_graphics_device_reset check_for_graphics_device_resetGL
#endif

static U32 Width;
static U32 Height;

extern "C" void * open_graphics( HWND window, U32 width, U32 height )
{
  static const PIXELFORMATDESCRIPTOR pfd = {
    sizeof( pfd ),
    1,
    PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
    PFD_TYPE_RGBA,
    24, 0,0, 0,0, 0,0, 8,0, // 24 bits color, 8 bits alpha
    0,0,0,0,0, // no accumulation buffer
    24,8, // at least 24 bits Z, at least 8 bits stencil
    0, // no aux
    PFD_MAIN_PLANE,
    0,0,0,0,
  };

  static int ctxAttribs_43core[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
    WGL_CONTEXT_MINOR_VERSION_ARB, 3,
#ifdef _DEBUG
    WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
#endif
    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
    0
  };

  static int ctxAttribs_31core[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 1,
    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
    0
  };

  static int ctxAttribs_21[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
    WGL_CONTEXT_MINOR_VERSION_ARB, 1,
    0
  };

  Width = width;
  Height = height;

  // Set the pixel format
  gl_dc = GetDC( window );
  if ( !SetPixelFormat( gl_dc, ChoosePixelFormat( gl_dc, &pfd ), &pfd ) )
    return 0;

  // Create a temp rendering context - we need this so we can call wglGetProcAddress.
  HGLRC tempglrc = wglCreateContext( gl_dc );
  wglMakeCurrent( gl_dc, tempglrc );

  PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC) wglGetProcAddress( "wglCreateContextAttribsARB" );

  wglMakeCurrent( gl_dc, NULL );
  wglDeleteContext( tempglrc );

  // Create our real context.
  if ( wglCreateContextAttribsARB )
  {
    // try for 4.3 first
    gl_rc = wglCreateContextAttribsARB( gl_dc, NULL, ctxAttribs_43core );

    // if that didn't work, try 3.1
    if ( !gl_rc )
      gl_rc = wglCreateContextAttribsARB( gl_dc, NULL, ctxAttribs_31core );

    // if that still didn't work, try 2.1
    if ( !gl_rc )
      gl_rc = wglCreateContextAttribsARB( gl_dc, NULL, ctxAttribs_21 );
  }

  // still don't have one, okay - just hit me with whatever you have
  if ( !gl_rc )
  {
    gl_rc = wglCreateContext( gl_dc );
    if ( !gl_rc )
      return 0;  // No GL context at all...
  }

  wglMakeCurrent( gl_dc, gl_rc );
  glViewport( 0, 0, width, height );

  // And return success.
  return &gl_rc;
}


extern "C" void close_graphics( void )
{
  wglMakeCurrent( gl_dc, NULL );
  wglDeleteContext( gl_rc );
  ReleaseDC( WindowFromDC( gl_dc ), gl_dc );
}


extern "C" void * start_graphics_frame( void )
{
  glViewport(0,0,Width,Height);
  
  // Clear the screen.
  glClearColor( 0.0f, 0.125f, 0.3f, 1.0f );
  glClear( GL_COLOR_BUFFER_BIT );
  
  return 0;
}


extern "C" void end_graphics_frame( void )
{
  // End the rendering.
   SwapBuffers( gl_dc );
}


extern "C" void check_for_graphics_device_reset( void * texture_set )
{
  // this is only necessary on DX9, not GL
}


// @cdep pre $addtocswitches(-I$clipfilename($file)\..\..\tools)
// @cdep pre $requiresbinary(opengl32.lib)
