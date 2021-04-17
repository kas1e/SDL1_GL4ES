/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/*
 * GL support for AmigaOS 4 using GL4ES->OGLES2
 */

#if defined (SDL_VIDEO_OPENGL) && defined (SDL_GL4ES) 

#include "SDL_os4video.h"
#include "SDL_os4utils.h"
#include "SDL_os4blit.h"

#include <proto/exec.h>
#include <proto/intuition.h>

#include "agl.h"

//#define DEBUG
#include "../../main/amigaos4/SDL_os4debug.h"


static int os4video_GetPixelDepth(_THIS)
{
	int depth = _this->gl_config.buffer_size;

	if (depth < 16) {
		depth = 16;
	}

	return depth;
} 


/*
 * initialize OGLES2 context
 */
int os4video_GL_Init(_THIS)
{

	ULONG errCode = 0; 
	int depth;

	struct SDL_PrivateVideoData *hidden = _this->hidden;
	
	dprintf("Initializing GL4ES->OGLES2.. \n");

	if (hidden->IGL)
	{
		// Happens when toggling fullscreen mode
		dprintf("Old OpenGLES context exists\n");
		return 0;
	}
		
	depth = os4video_GetPixelDepth(_this); 
		
	struct TagItem create_context_taglist[] =
	{
		{OGLES2_CCT_WINDOW, (ULONG)hidden->win},
		{OGLES2_CCT_VSYNC, 0},
		{OGLES2_CCT_DEPTH, depth},
		{OGLES2_CCT_STENCIL, TRUE},
		{OGLES2_CCT_RESIZE_VIEWPORT,TRUE},
		{TAG_DONE,	0}
	};
				
			
	hidden->IGL = aglCreateContext2(&errCode,create_context_taglist);
 
 
	if (hidden->IGL)
	{
		dprintf("in hidden->IGL\n");
		_this->gl_config.driver_loaded = 1;
	
		aglMakeCurrent(hidden->IGL);		
		
		hidden->OpenGL = TRUE;

		return 0;
	}
	else
	{
		_this->gl_config.driver_loaded = 0;
		SDL_SetError("Failed to create OGLES2 context");
	}

	return -1;

}



void os4video_GL_Term(_THIS)
{

	struct SDL_PrivateVideoData *hidden = _this->hidden;

	dprintf("Here\n");
	
	if (hidden->OpenGL)
	{

		aglDestroyContext(hidden->IGL);
		hidden->IGL = NULL;

		_this->gl_config.driver_loaded = 0;

		hidden->OpenGL = FALSE;
	}
	
	dprintf("Closing of ogles2 libraries done inside of gl4es\n");

}

int	os4video_GL_GetAttribute(_THIS, SDL_GLattr attrib, int* value)
{
   return 0;
}

int	os4video_GL_MakeCurrent(_THIS)
{

	struct SDL_PrivateVideoData *hidden = _this->hidden;
	
	aglMakeCurrent(hidden->IGL);
	
	return 0;
}

void os4video_GL_SwapBuffers(_THIS)
{		
    // Swap the Buffers!
      aglSwapBuffers();	
}


void *os4video_GL_GetProcAddress(_THIS, const char *proc) {
	void *func = NULL;

	if ( !_this->gl_config.driver_loaded )
	{
		if (os4video_GL_Init(_this) != 0)
		{
			return NULL;
		}
	}

	func = (void *)aglGetProcAddress(proc);
	return func;
}

int os4video_GL_LoadLibrary(_THIS, const char *path) {
	/* Library is always open */
	_this->gl_config.driver_loaded = 1;

	return 0;
}



#endif
