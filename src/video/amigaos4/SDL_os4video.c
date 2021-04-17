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

#include "SDL_thread.h"
#include "../../events/SDL_sysevents.h"
#include "../../events/SDL_events_c.h"
#include "SDL_os4video.h"
#include "SDL_os4utils.h"
#include "SDL_os4blit.h"
#include "../SDL_pixels_c.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/icon.h>
#include <proto/dos.h>

#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>

#ifdef SDL_GL4ES
#include "agl.h"
#endif

//#define DEBUG
#include "../../main/amigaos4/SDL_os4debug.h"

typedef enum
{
	EKeepContext,
	EDestroyContext
} EOpenGlContextPolicy;

extern void SDL_Quit(void);

static int os4video_Available(void);
static SDL_VideoDevice *os4video_CreateDevice(int);

static int		os4video_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **	os4video_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *os4video_SetVideoMode(_THIS, SDL_Surface *current,
						int width, int height, int bpp, Uint32 flags);
static int		os4video_ToggleFullScreen(_THIS, int on);
void			os4video_UpdateMouse(_THIS);
SDL_Overlay *	os4video_CreateYUVOverlay(_THIS, int width, int height,
					Uint32 format, SDL_Surface *display);
static int		os4video_SetColors(_THIS, int firstcolor, int ncolors,
					SDL_Color *colors);
void 			os4video_UpdateRectsFullscreenDB(_THIS, int numrects, SDL_Rect *rects);
void 			os4video_UpdateRectsOffscreen(_THIS, int numrects, SDL_Rect *rects);
void 			os4video_UpdateRectsOffscreen_8bit(_THIS, int numrects, SDL_Rect *rects);
void 			os4video_UpdateRectsNone(_THIS, int numrects, SDL_Rect *rects);
static void 	os4video_VideoQuit(_THIS);
int 			os4video_AllocHWSurface(_THIS, SDL_Surface *surface);
int 			os4video_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst);
int 			os4video_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *rect, Uint32 color);
int 			os4video_SetHWColorKey(_THIS, SDL_Surface *surface, Uint32 key);
int 			os4video_SetHWAlpha(_THIS, SDL_Surface *surface, Uint8 value);
int 			os4video_LockHWSurface(_THIS, SDL_Surface *surface);
void 			os4video_UnlockHWSurface(_THIS, SDL_Surface *surface);
int 			os4video_FlipHWSurface(_THIS, SDL_Surface *surface);
void 			os4video_FreeHWSurface(_THIS, SDL_Surface *surface);
int 			os4video_GL_GetAttribute(_THIS, SDL_GLattr attrib, int* value);
int 			os4video_GL_MakeCurrent(_THIS);
void 			os4video_GL_SwapBuffers(_THIS);
void *			os4video_GL_GetProcAddress(_THIS, const char *proc);
int 			os4video_GL_LoadLibrary(_THIS, const char *path);
void 			os4video_SetCaption(_THIS, const char *title, const char *icon);
void 			os4video_SetIcon(_THIS, SDL_Surface *icon, Uint8 *mask);
int 			os4video_IconifyWindow(_THIS);
SDL_GrabMode 	os4video_GrabInput(_THIS, SDL_GrabMode mode);
int 			os4video_GetWMInfo(_THIS, SDL_SysWMinfo *info);
void 			os4video_FreeWMCursor(_THIS, WMcursor *cursor);
WMcursor *		os4video_CreateWMCursor(_THIS,
					Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y);
int 			os4video_ShowWMCursor(_THIS, WMcursor *cursor);
void 			os4video_WarpWMCursor(_THIS, Uint16 x, Uint16 y);
void 			os4video_CheckMouseMode(_THIS);
void 			os4video_InitOSKeymap(_THIS);
void 			os4video_PumpEvents(_THIS);

int 			os4video_GL_Init(_THIS);
void 			os4video_GL_Term(_THIS);

extern BOOL os4video_PixelFormatFromModeID(SDL_PixelFormat *vformat, uint32 displayID);
static void os4video_DeleteCurrentDisplay(_THIS, SDL_Surface *current, BOOL keepOffScreenBuffer, EOpenGlContextPolicy);
extern void ResetMouseColors(_THIS);
extern void ResetMouseState(_THIS);
extern void os4video_ResetCursor(struct SDL_PrivateVideoData *hidden);
extern void DeleteAppIcon(_THIS);

#ifndef SDL_GL4ES
extern SDL_bool os4video_AllocateOpenGLBuffers(_THIS, int width, int height);
#endif

static struct Library	*gfxbase;
static struct Library	*layersbase;
static struct Library	*intuitionbase;
static struct Library	*iconbase;
static struct Library	*workbenchbase;
static struct Library	*keymapbase;
static struct Library   *dosbase;

struct GraphicsIFace	*SDL_IGraphics;
struct LayersIFace		*SDL_ILayers;
struct IntuitionIFace	*SDL_IIntuition;
struct IconIFace		*SDL_IIcon;
struct WorkbenchIFace	*SDL_IWorkbench;
struct KeymapIFace		*SDL_IKeymap;
struct DOSIFace         *SDL_IDos;

#define MIN_LIB_VERSION	51

static BOOL
os4video_OpenLibraries(void)
{
	gfxbase       = IExec->OpenLibrary("graphics.library", 54);
	layersbase    = IExec->OpenLibrary("layers.library", MIN_LIB_VERSION);
	intuitionbase = IExec->OpenLibrary("intuition.library", MIN_LIB_VERSION);
	iconbase      = IExec->OpenLibrary("icon.library", MIN_LIB_VERSION);
	workbenchbase = IExec->OpenLibrary("workbench.library", MIN_LIB_VERSION);
	keymapbase    = IExec->OpenLibrary("keymap.library", MIN_LIB_VERSION);
	dosbase       = IExec->OpenLibrary("dos.library", MIN_LIB_VERSION);

	if (!gfxbase || !layersbase || !intuitionbase || !iconbase || !workbenchbase || !keymapbase || !dosbase)
		return FALSE;

	SDL_IGraphics  = (struct GraphicsIFace *)  IExec->GetInterface(gfxbase, "main", 1, NULL);
	SDL_ILayers    = (struct LayersIFace *)    IExec->GetInterface(layersbase, "main", 1, NULL);
	SDL_IIntuition = (struct IntuitionIFace *) IExec->GetInterface(intuitionbase, "main", 1, NULL);
	SDL_IIcon      = (struct IconIFace *)      IExec->GetInterface(iconbase, "main", 1, NULL);
	SDL_IWorkbench = (struct WorkbenchIFace *) IExec->GetInterface(workbenchbase, "main", 1, NULL);
	SDL_IKeymap    = (struct KeymapIFace *)    IExec->GetInterface(keymapbase, "main", 1, NULL);
	SDL_IDos       = (struct DOSIFace *)       IExec->GetInterface(dosbase, "main", 1, NULL);

	if (!SDL_IGraphics || !SDL_ILayers || !SDL_IIntuition || !SDL_IIcon || !SDL_IWorkbench || !SDL_IKeymap || !SDL_IDos)
		return FALSE;

	return TRUE;
}

static void
os4video_CloseLibraries(void)
{
	if (SDL_IDos) {
		IExec->DropInterface((struct Interface *) SDL_IDos);
		SDL_IDos = NULL;
	}
	if (SDL_IKeymap) {
		IExec->DropInterface((struct Interface *) SDL_IKeymap);
		SDL_IKeymap = NULL;
	}
	if (SDL_IWorkbench) {
		IExec->DropInterface((struct Interface *) SDL_IWorkbench);
		SDL_IWorkbench = NULL;
	}
	if (SDL_IIcon) {
		IExec->DropInterface((struct Interface *) SDL_IIcon);
		SDL_IIcon = NULL;
	}
	if (SDL_IIntuition) {
		IExec->DropInterface((struct Interface *) SDL_IIntuition);
		SDL_IIntuition = NULL;
	}
	if (SDL_ILayers) {
		IExec->DropInterface((struct Interface *) SDL_ILayers);
		SDL_ILayers = NULL;
	}
	if (SDL_IGraphics) {
		IExec->DropInterface((struct Interface *) SDL_IGraphics);
		SDL_IGraphics = NULL;
	}

	if (dosbase) {
		IExec->CloseLibrary(dosbase);
		dosbase = NULL;
	}
	if (keymapbase) {
		IExec->CloseLibrary(keymapbase);
		keymapbase = NULL;
	}
	if (workbenchbase) {
		IExec->CloseLibrary(workbenchbase);
		workbenchbase = NULL;
	}
	if (iconbase) {
		IExec->CloseLibrary(iconbase);
		iconbase = NULL;
	}
	if (intuitionbase) {
		IExec->CloseLibrary(intuitionbase);
		intuitionbase = NULL;
	}
	if (layersbase) {
		IExec->CloseLibrary(layersbase);
		layersbase = NULL;
	}
	if (gfxbase) {
		IExec->CloseLibrary(gfxbase);
		gfxbase = NULL;
	}
}

VideoBootStrap os4video_bootstrap =
{
	"OS4",
	"AmigaOS 4 Video",
	os4video_Available,
	os4video_CreateDevice
};

static inline uint16
os4video_SwapShort(uint16 x)
{
	return ((x&0xff)<<8) | ((x&0xff00)>>8);
}

static int
os4video_Available(void)
{
	return 1;
}

static void
os4video_DeleteDevice(_THIS)
{
	if (_this)
	{
		while (!IsMsgPortEmpty(_this->hidden->userPort))
		{
			struct Message *msg = IExec->GetMsg(_this->hidden->userPort);
			IExec->ReplyMsg(msg);
		}

		if (_this->hidden->mouse)
			IExec->FreeVec(_this->hidden->mouse);

		IExec->ObtainSemaphore(_this->hidden->poolSemaphore);
		IExec->FreeSysObject(ASOT_MEMPOOL,_this->hidden->pool);
		IExec->ReleaseSemaphore(_this->hidden->poolSemaphore);
		IExec->FreeSysObject(ASOT_SEMAPHORE, _this->hidden->poolSemaphore);
		IExec->FreeSysObject(ASOT_PORT, _this->hidden->userPort);
		IExec->FreeSysObject(ASOT_PORT, _this->hidden->appPort);

		if (_this->hidden->currentIcon)
		{
			SDL_IIcon->FreeDiskObject(_this->hidden->currentIcon);
		}

		if (_this->hidden->appName)
		{
			SDL_free(_this->hidden->appName);
		}

		os4video_CloseLibraries();

		IExec->FreeVec(_this->hidden);
		IExec->FreeVec(_this);
	}
}

static void
os4video_FindApplicationName(_THIS)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;
	size_t size;

	const size_t maxPathLength = 255;
	char nameBuffer[maxPathLength];

	if (SDL_IDos->GetCliProgramName(nameBuffer, maxPathLength - 1)) {
		dprintf("Current program name '%s'\n", nameBuffer);
	} else {
		dprintf("Failed to get CLI program name, checking task node\n");

		struct Task* me = IExec->FindTask(NULL);
		SDL_snprintf(nameBuffer, maxPathLength, "%s", ((struct Node *)me)->ln_Name);
	}

	size = SDL_strlen(nameBuffer) + 1;

	hidden->appName = SDL_malloc(size);

	if (hidden->appName) {
		SDL_snprintf(hidden->appName, size, nameBuffer);
	}

	dprintf("Application name: '%s'\n", hidden->appName);
}

static SDL_VideoDevice *
os4video_CreateDevice(int devnum)
{
	SDL_VideoDevice *os4video_device;
	dprintf("Creating OS4 video device\n");

	os4video_device = (SDL_VideoDevice *)IExec->AllocVecTags(sizeof(SDL_VideoDevice), AVT_ClearWithValue, 0, AVT_Type, MEMF_SHARED, TAG_DONE );
	if (!os4video_device)
	{
		dprintf("No memory for device\n");
		SDL_OutOfMemory();
		return 0;
	}

	os4video_device->hidden = (struct SDL_PrivateVideoData *)IExec->AllocVecTags(sizeof(struct SDL_PrivateVideoData), AVT_ClearWithValue, 0, AVT_Type, MEMF_SHARED, TAG_DONE );
	if (!os4video_device->hidden)
	{
		dprintf("No memory for private data\n");
		SDL_OutOfMemory();

		IExec->FreeVec(os4video_device);
		return 0;
	}

	os4video_device->hidden->poolSemaphore = IExec->AllocSysObjectTags(ASOT_SEMAPHORE, TAG_DONE);
	if (!os4video_device->hidden->poolSemaphore)
		goto fail;

	/* Create the pool we'll be using (Shared, might be used from threads) */
	os4video_device->hidden->pool = IExec->AllocSysObjectTags(ASOT_MEMPOOL,
		ASOPOOL_MFlags,    MEMF_SHARED,
		ASOPOOL_Threshold, 16384,
		ASOPOOL_Puddle,    16384,
		TAG_DONE);

	if (!os4video_device->hidden->pool)
		goto fail;

	os4video_device->hidden->mouse = IExec->AllocVecTags( 8, AVT_ClearWithValue, 0, AVT_Type, MEMF_SHARED, TAG_DONE );
	os4video_device->hidden->userPort = IExec->AllocSysObject(ASOT_PORT, NULL);

	if (!os4video_device->hidden->userPort)
		goto fail;

	os4video_device->hidden->appPort = IExec->AllocSysObject(ASOT_PORT, NULL);
	if (!os4video_device->hidden->appPort)
		goto fail;

	if (!os4video_OpenLibraries())
	{
		dprintf("Failed to open libraries\n");
		goto fail;
	}

	SDL_strlcpy(os4video_device->hidden->currentCaption, "SDL_Window", 128);
	SDL_strlcpy(os4video_device->hidden->currentIconCaption, "SDL Application", 128);

	os4video_FindApplicationName(os4video_device);

	os4video_device->VideoInit = os4video_VideoInit;
	os4video_device->ListModes = os4video_ListModes;
	os4video_device->SetVideoMode = os4video_SetVideoMode;

	os4video_device->VideoQuit = os4video_VideoQuit;
	os4video_device->SetColors = os4video_SetColors;
	os4video_device->UpdateRects = os4video_UpdateRectsNone;
	os4video_device->AllocHWSurface = os4video_AllocHWSurface;
	os4video_device->CheckHWBlit = os4video_CheckHWBlit;
	os4video_device->FillHWRect = os4video_FillHWRect;
	os4video_device->LockHWSurface = os4video_LockHWSurface;
	os4video_device->UnlockHWSurface = os4video_UnlockHWSurface;
	os4video_device->FlipHWSurface = os4video_FlipHWSurface;
	os4video_device->FreeHWSurface = os4video_FreeHWSurface;
	os4video_device->SetCaption = os4video_SetCaption;
	os4video_device->SetIcon = os4video_SetIcon;
	os4video_device->IconifyWindow = os4video_IconifyWindow;
	os4video_device->GrabInput = os4video_GrabInput;
	os4video_device->InitOSKeymap = os4video_InitOSKeymap;
	os4video_device->PumpEvents = os4video_PumpEvents;
	os4video_device->CreateWMCursor = os4video_CreateWMCursor;
	os4video_device->ShowWMCursor = os4video_ShowWMCursor;

	os4video_device->GetWMInfo = os4video_GetWMInfo;

	os4video_device->FreeWMCursor = os4video_FreeWMCursor;
	os4video_device->WarpWMCursor = os4video_WarpWMCursor;
	os4video_device->UpdateMouse = os4video_UpdateMouse;
	os4video_device->CheckMouseMode = os4video_CheckMouseMode;
	os4video_device->ToggleFullScreen = os4video_ToggleFullScreen;
	os4video_device->SetHWAlpha = os4video_SetHWAlpha;
	os4video_device->SetHWColorKey = os4video_SetHWColorKey;

#if SDL_VIDEO_OPENGL
	os4video_device->GL_LoadLibrary = os4video_GL_LoadLibrary;
	os4video_device->GL_GetProcAddress = os4video_GL_GetProcAddress;
	os4video_device->GL_GetAttribute = os4video_GL_GetAttribute;
	os4video_device->GL_MakeCurrent = os4video_GL_MakeCurrent;
	os4video_device->GL_SwapBuffers = os4video_GL_SwapBuffers;
#endif

	os4video_device->free = os4video_DeleteDevice;

	dprintf("Device created\n");

	return os4video_device;

fail:
	SDL_OutOfMemory();

	os4video_CloseLibraries();

	if (os4video_device->hidden->userPort)
		IExec->FreeSysObject(ASOT_PORT, os4video_device->hidden->userPort);

	if (os4video_device->hidden->appPort)
		IExec->FreeSysObject(ASOT_PORT, os4video_device->hidden->appPort);

	if (os4video_device->hidden->poolSemaphore)
		IExec->FreeSysObject(ASOT_SEMAPHORE, os4video_device->hidden->poolSemaphore);

	if (os4video_device->hidden->pool)
		IExec->FreeSysObject(ASOT_MEMPOOL,os4video_device->hidden->pool);

	IExec->FreeVec(os4video_device->hidden);
	IExec->FreeVec(os4video_device);

	return 0;
}

static int
os4video_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;
	uint32 displayID;
	uint64 freeMem = 0;

	/* Get the default public screen. For the time being
	 * we don't care about its screen mode. Assume it's RTG.
	 */
	hidden->publicScreen = SDL_IIntuition->LockPubScreen(NULL);
	if (!hidden->publicScreen)
	{
		dprintf("Cannot lock default public screen\n");
		SDL_SetError("Cannot lock default PubScreen");
		return -1;
	}

	if (!SDL_IIntuition->GetScreenAttr(hidden->publicScreen, SA_DisplayID, &displayID, sizeof(uint32)))
	{
		dprintf("Cannot get screen attributes\n");
		SDL_SetError("Cannot get screen attributes");
		return -1;
	}

	/* Set up the integral SDL_VideoInfo */
	SDL_memset(&_this->info, 0, sizeof(_this->info));
	_this->info.hw_available = 1;
	_this->info.wm_available = 1;
	_this->info.blit_hw      = 1;
	_this->info.blit_fill    = 1;
	_this->info.blit_hw_A    = 1;
	_this->info.blit_hw_CC   = 1;
	_this->info.current_w    = hidden->publicScreen->Width;
	_this->info.current_h    = hidden->publicScreen->Height;

	if (SDL_IGraphics->GetBoardDataTags(0, GBD_FreeMemory, &freeMem, TAG_DONE) == 1)
	{
		dprintf("Free video memory %u\n", (uint32)freeMem);
	}
	else
	{
		dprintf("Failed to query free video memory\n");
	}

	_this->info.video_mem = freeMem;

	if (FALSE == os4video_PixelFormatFromModeID(vformat, displayID))
	{
		dprintf("Cannot get pixel format\n");
		SDL_SetError("Cannot get pixel format from screenmode ID");
		return -1;
	}

	return 0;
}

static void
os4video_VideoQuit(_THIS)
{
	int i;
	struct SDL_PrivateVideoData *hidden;

	dprintf("In VideoQuit, this = %p\n", _this);

	hidden = _this->hidden;

	dprintf("DeleteCurrentDisplay\n");
	os4video_DeleteCurrentDisplay(_this, 0, FALSE, EDestroyContext);

	dprintf("Checking pubscreen\n");
	if (hidden->publicScreen)
	{
		SDL_IIntuition->UnlockPubScreen(NULL, hidden->publicScreen);
		hidden->publicScreen = NULL;
		for (i = 0; i < MAX_PIXEL_FORMATS; i++)
		{
			if (hidden->Modes[i]) IExec->FreeVec(hidden->Modes[i]);
		}
	}
}

static SDL_Rect **
os4video_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;

	if ((flags & SDL_FULLSCREEN) != SDL_FULLSCREEN)
	{
		/* Unless fullscreen is requested, claim we support any size */
		return (SDL_Rect **)-1;
	}

	if (format->Rmask == 0)
	{
		/* Handle the case when we've been supplied a desired
		 * bits-per-pixel, but no colour masks.
		 *
		 * We search for screens modes in appropriate pixel formats
		 * for this depth (in order of preference) until we find a pixel
		 * format that has valid modes.
		 */
		const PIX_FMT *pixelFormat;
		SDL_PixelFormat sdlformat;

		dprintf("Listing %d-bit screenmodes\n", format->BitsPerPixel);

		pixelFormat = os4video_GetPixelFormatsForBpp(format->BitsPerPixel);

		while (*pixelFormat != PIXF_NONE && *pixelFormat < MAX_PIXEL_FORMATS)
		{
			dprintf("Looking for modes with format=%d\n", *pixelFormat);

			if (hidden->Modes[*pixelFormat] == NULL)
			{
				/* If we haven't already got a list of modes for this
				 * format, try to build one now
				 */
				os4video_PIXFtoPF(&sdlformat, *pixelFormat);
				hidden->Modes[*pixelFormat] = os4video_MakeResArray(&sdlformat);
			}

			/* Stop searching, if we have modes in this format */
			if (hidden->Modes[*pixelFormat] != NULL)
			{
				dprintf("Found some\n");
				break;
			}

			/* Otherwise, no modes found for this format. Try next one */
			pixelFormat++;
		}

		if (*pixelFormat != PIXF_NONE && *pixelFormat < MAX_PIXEL_FORMATS)
		{
			/* We found some modes with this format. Return the list */
			return hidden->Modes[*pixelFormat];
		}
		else
		{
			dprintf("Found no %d-bit modes in any pixel format\n", format->BitsPerPixel);

			/* We found no modes in any format at this bpp */
			return (SDL_Rect **)NULL;
		}
	}
	else
	{
		/* We have been supplied colour masks. Look for modes
		 * which match precisely
		 */
		PIX_FMT pixelFormat = os4video_PFtoPIXF(format);

		dprintf("Listing %d-bit modes with format=%d\n", format->BitsPerPixel, pixelFormat);

		if (pixelFormat >= MAX_PIXEL_FORMATS)
		{
			dprintf("Invalid pixel format\n");
			return (SDL_Rect **)NULL;
		}

		if (pixelFormat == PIXF_NONE)
		{
			dprintf("No modes\n");
			return (SDL_Rect **)NULL;
		}

		if (hidden->Modes[pixelFormat])
		{
			dprintf("Returning prebuilt array\n");
			return hidden->Modes[pixelFormat];
		}

		hidden->Modes[pixelFormat] = os4video_MakeResArray(format);

		return hidden->Modes[pixelFormat];
	}
}

static void
os4video_ClearScreen(struct Screen *screen, int width, int height)
{
	struct RastPort tmpRP;

	SDL_IGraphics->InitRastPort(&tmpRP);
	tmpRP.BitMap = screen->RastPort.BitMap;

	SDL_IGraphics->RectFillColor(&tmpRP, 0, 0, width, height, 0);
}

static struct Screen *
os4video_OpenScreen(int width, int height, uint32 modeId)
{
	struct Screen *screen;

	uint32         openError = 0;
	uint32         screenWidth;
	uint32         screenLeftEdge = 0;

	screenWidth  = os4video_GetWidthFromMode(modeId);

	/* If requested width is smaller than this mode's width, then centre
	 * the screen horizontally.
	 *
	 * A similar tactic won't work for centring vertically, because P96
	 * screens don't support the SA_Top propery. We'll tackle that
	 * another way shortly...
	 */
	if (width < (int)screenWidth)
	{
		screenLeftEdge = (screenWidth - width) / 2;
	}

	screen = SDL_IIntuition->OpenScreenTags(NULL,
									 SA_Left,		screenLeftEdge,
									 SA_Width, 		width,
									 SA_Height,		height,
									 SA_Depth,		8,
									 SA_DisplayID,	modeId,
									 SA_Quiet,		TRUE,
									 SA_ShowTitle,	FALSE,
									 SA_ErrorCode,	&openError,
									 TAG_DONE);

	if (!screen)
	{
		dprintf("Screen didn't open (err:%d)\n", openError);

		switch (openError)
		{
			case OSERR_NOMONITOR:
				SDL_SetError("Monitor for display mode not available");
				break;
			case OSERR_NOCHIPS:
				SDL_SetError("Newer custom chips required");
				break;
			case OSERR_NOMEM:
			case OSERR_NOCHIPMEM:
				SDL_OutOfMemory();
				break;
			case OSERR_PUBNOTUNIQUE:
				SDL_SetError("Public screen name not unique");
				break;
			case OSERR_UNKNOWNMODE:
			case OSERR_TOODEEP:
				SDL_SetError("Unknown display mode");
				break;
			case OSERR_ATTACHFAIL:
				SDL_SetError("Attachment failed");
				break;
			default:
				SDL_SetError("OpenScreen failed");
		}
		return NULL;
	}

	os4video_ClearScreen(screen, width, height);

	dprintf("Screen %p opened\n", screen);

	return screen;
}

/*
 * Get the origin and dimensions of the visble area of the specified screen
 * as an IBox struct. This may be different than the Intuition screen as a whole
 * for "auto-scroll" screens.
 */
static BOOL
os4video_GetVisibleScreenBox(struct Screen *screen, struct IBox *screenBox)
{
	struct Rectangle dclip; /* The display clip - the bounds of the display mode
							 * relative to top/left corner of the overscan area */

	/* Get the screen's display clip */
	if (!SDL_IIntuition->GetScreenAttr(screen, SA_DClip, &dclip, sizeof dclip))
		return FALSE;

	/* Work out the geometry of the visible area
	 * Not sure this is 100% correct */
	screenBox->Left   = dclip.MinX - screen->ViewPort.DxOffset;
	screenBox->Top    = dclip.MinY - screen->ViewPort.DyOffset;
	screenBox->Width  = dclip.MaxX - dclip.MinX + 1;
	screenBox->Height = dclip.MaxY - dclip.MinY + 1;

	return TRUE;
}

/*
 * Calculate an appropriate position to place the top-left corner of a window of
 * size <width> x <height> on the specified screen.
 */
static void
os4video_GetBestWindowPosition (struct Screen *screen, int width, int height, uint32 *left, uint32 *top, BOOL addBorders)
{
	struct IBox screenBox =  {
		0, 0, screen->Width - 1, screen->Height -1
	};

	os4video_GetVisibleScreenBox(screen, &screenBox);

	dprintf("Visible screen: (%d,%d)/(%dx%d)\n", screenBox.Left, screenBox.Top, screenBox.Width, screenBox.Height);

	if (addBorders)
	{
		/*  The supplied window dimensions don't include the window frame,
		 * so add on the size of the window borders
		 */
		height += screen->WBorTop + screen->Font->ta_YSize + 1 + screen->WBorBottom;
		width  += screen->WBorLeft + screen->WBorRight;
	}

	/* If window fits within the visible screen area, then centre,
	 * the window on that area; otherwise position the
	 * window at the top-left of the visible screen (which will do
	 * for just now).
	 */
	if (width >= screenBox.Width)
		*left = screenBox.Left;
	else
		*left = screenBox.Left + (screenBox.Width - width) / 2;

	if (height >= screenBox.Height)
		*top = screenBox.Top;
	else
		*top = screenBox.Top + (screenBox.Height - height) / 2;
}

/*
 * Layer backfill hook for the SDL window on high/true-colour screens
 */
static void
os4video_BlackBackFill(const struct Hook *hook, struct RastPort *rp, const int *message)
{
	struct Rectangle *rect = (struct Rectangle *)(message + 1);  // The area to back-fill
	struct RastPort backfillRP;

	/* Create our own temporary rastport so that we can render safely */
	SDL_IGraphics->InitRastPort(&backfillRP);
	backfillRP.BitMap = rp->BitMap;

	SDL_IGraphics->RectFillColor(&backfillRP, rect->MinX, rect->MinY, rect->MaxX, rect->MaxY, 0);
}

static const struct Hook blackBackFillHook = {
	{0, 0},							/* h_MinNode */
	(ULONG(*)())os4video_BlackBackFill,	  /* h_Entry */
	0,								/* h_SubEntry */
	0		 						/* h_Data */
};

static void
os4video_CreateIconifyGadget(_THIS, struct Window *window) {
    struct SDL_PrivateVideoData *hidden = _this->hidden;

    dprintf("Called\n");

    struct DrawInfo *di = SDL_IIntuition->GetScreenDrawInfo(hidden->publicScreen);

    if (di) {
		hidden->iconifyImage = (struct Image *)SDL_IIntuition->NewObject(NULL, SYSICLASS,
			SYSIA_Which, ICONIFYIMAGE,
			SYSIA_DrawInfo, di,
			TAG_DONE);

		if (hidden->iconifyImage) {
			hidden->iconifyGadget = (struct Gadget *)SDL_IIntuition->NewObject(NULL, BUTTONGCLASS,
				GA_Image, hidden->iconifyImage,
				GA_ID, GID_ICONIFY,
				GA_TopBorder, TRUE,
				GA_RelRight, TRUE,
				GA_Titlebar, TRUE,
				GA_RelVerify, TRUE,
				TAG_DONE);

			if (hidden->iconifyGadget) {
				SDL_IIntuition->AddGadget(window, hidden->iconifyGadget, -1);
			} else {
				dprintf("Failed to create button class\n");
			}
		} else {
			dprintf("Failed to create image class\n");
		}

		SDL_IIntuition->FreeScreenDrawInfo(hidden->publicScreen, di);

	} else {
		dprintf("Failed to get screen draw info\n");
	}
}

static struct Window *
os4video_OpenWindow(_THIS, int width, int height, struct Screen *screen, Uint32 flags)
{
	struct Window *window;
	uint32 windowFlags;

	struct SDL_PrivateVideoData *hidden = _this->hidden;

	uint32 IDCMPFlags = IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
						IDCMP_DELTAMOVE | IDCMP_RAWKEY | IDCMP_ACTIVEWINDOW |
						IDCMP_INACTIVEWINDOW | IDCMP_INTUITICKS |
						IDCMP_EXTENDEDMOUSE;
	uint32 wX;
	uint32 wY;
	const struct Hook *backfillHook = &blackBackFillHook;

	if (flags & SDL_FULLSCREEN)
	{
		windowFlags = WFLG_BORDERLESS | WFLG_SIMPLE_REFRESH | WFLG_BACKDROP;
		wX = wY = 0;

		backfillHook = LAYERS_NOBACKFILL;
	}
	else
	{
		if (flags & SDL_NOFRAME)
			windowFlags = WFLG_SMART_REFRESH | WFLG_NOCAREREFRESH | WFLG_NEWLOOKMENUS | WFLG_BORDERLESS;
		else
		{
			windowFlags = WFLG_SMART_REFRESH | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET
						| WFLG_NOCAREREFRESH | WFLG_NEWLOOKMENUS;
			IDCMPFlags |= IDCMP_CLOSEWINDOW | IDCMP_GADGETUP;
		}

		if (flags & SDL_RESIZABLE)
		{
			windowFlags |= WFLG_SIZEGADGET | WFLG_SIZEBBOTTOM;
			IDCMPFlags  |= IDCMP_SIZEVERIFY;
		}

		os4video_GetBestWindowPosition (screen, width, height, &wX, &wY, !(flags & SDL_NOFRAME));

		/* In windowed mode, use our custom backfill to clear the layer to black for
		 * high/true-colour screens; otherwise, use the default clear-to-pen-0
		 * backfill */
		if (SDL_IGraphics->GetBitMapAttr(screen->RastPort.BitMap, BMA_BITSPERPIXEL) > 8)
			backfillHook = &blackBackFillHook;
		else
			backfillHook = LAYERS_BACKFILL;
	}

	windowFlags |= WFLG_REPORTMOUSE|WFLG_RMBTRAP;

	dprintf("Trying to open window at (%d,%d) of size (%dx%d)\n", wX, wY, width, height);

	window = SDL_IIntuition->OpenWindowTags (NULL,
									WA_Left,			wX,
									WA_Top,				wY,
									WA_InnerWidth,		width,
									WA_InnerHeight,		height,
									WA_Flags,			windowFlags,
									WA_PubScreen,		screen,
									WA_UserPort,		hidden->userPort,
									WA_IDCMP,			IDCMPFlags,
									WA_AutoAdjust,		FALSE,
									WA_BackFill,		backfillHook,
									TAG_DONE);

	if (window)
	{
		if (flags & SDL_RESIZABLE)
		{
			/* If this window is resizable, reset window size limits
			 * so that the user can actually resize it.
			 *
			 * What's a useful minimum size, anyway?
			 */
			SDL_IIntuition->WindowLimits(window,
									 window->BorderLeft + window->BorderRight  + 100,
									 window->BorderTop  + window->BorderBottom + 100,
									 -1,
									 -1);
		}

		SDL_IIntuition->SetWindowTitles(window, hidden->currentCaption, hidden->currentCaption);

		if (flags & SDL_FULLSCREEN)
		{
			SDL_IIntuition->ScreenToFront(screen);
		} else {
			if (!(flags & SDL_NOFRAME)) {
				if (width > 99 && height > 99) {
					os4video_CreateIconifyGadget(_this, window);
				} else {
					dprintf("Don't add gadget for too small window %d*%d\n", width, height);
				}
			}
		}

		SDL_IIntuition->ActivateWindow(window);
	}

	return window;
}

static BOOL
os4video_InitOffScreenBuffer(struct OffScreenBuffer *offBuffer, uint32 width, uint32 height, SDL_PixelFormat *format, BOOL hwSurface)
{
	BOOL     success     = FALSE;
	PIX_FMT  pixelFormat = os4video_PFtoPIXF(format);
	uint32   bpp         = os4video_PIXF2Bits(pixelFormat);

	/* Round up width/height to nearest 4 pixels */
	width  = (width + 3) & (~3);
	height = (height + 3) & (~3);

	dprintf("Allocating a %dx%dx%d off-screen buffer with pixel format %d, %sWSURFACE\n",
		width, height, bpp, pixelFormat, hwSurface ? "H" : "S");

	offBuffer->bitmap = SDL_IGraphics->AllocBitMapTags(
		width,
		height,
		bpp,
		BMATags_Clear, TRUE,
		BMATags_Displayable, hwSurface,
		BMATags_UserPrivate, !hwSurface,
		BMATags_PixelFormat, pixelFormat,
		TAG_DONE);

	if (offBuffer->bitmap)
	{
		offBuffer->width  = width;
		offBuffer->height = height;
		offBuffer->format = *format;
		offBuffer->pixels = NULL;
		offBuffer->pitch = 0;

		if (!hwSurface)
		{
			APTR baseAddress;
			uint32 bytesPerRow;

			APTR lock = SDL_IGraphics->LockBitMapTags(
				offBuffer->bitmap,
				LBM_BaseAddress, &baseAddress,
				LBM_BytesPerRow, &bytesPerRow,
				TAG_DONE);

			if (lock)
			{
				offBuffer->pixels = baseAddress;
				offBuffer->pitch = bytesPerRow;

				SDL_IGraphics->UnlockBitMap(lock);
			}
			else
			{
				dprintf("Failed to lock bitmap\n");
			}
		}

		dprintf("Pixels %p, pitch %d\n", offBuffer->pixels, offBuffer->pitch);

		success = TRUE;
	}
	else
	{
		dprintf("Failed to allocate off-screen buffer\n");
	}

	return success;
}

static void
os4video_FreeOffScreenBuffer(struct OffScreenBuffer *offBuffer)
{
	if (offBuffer->bitmap)
	{
		dprintf("Freeing off-screen buffer\n");

		SDL_IGraphics->FreeBitMap(offBuffer->bitmap);

		offBuffer->bitmap = NULL;
	}
}

static BOOL
os4video_ResizeOffScreenBuffer(struct OffScreenBuffer *offBuffer, uint32 width, uint32 height, BOOL hwSurface)
{
	BOOL success = TRUE;

	if (width > offBuffer->width || height > offBuffer->height ||
		(offBuffer->width - 4) > width || (offBuffer->height - 4) > height)
	{
		// If current surface is too small or too large, free it and
		// create a new one
		SDL_PixelFormat format = offBuffer->format; // Remember the pixel format
		os4video_FreeOffScreenBuffer(offBuffer);
		success = os4video_InitOffScreenBuffer(offBuffer, width, height, &format, hwSurface);
	}

	return success;
}


/*
 * Allocate and intialize resources required for supporting
 * full-screen double-buffering.
 */
static BOOL
os4video_InitDoubleBuffering(struct DoubleBufferData *dbData, struct Screen *screen)
{
	dprintf("Allocating resources for double-buffering\n");

	dbData->sb[0] = SDL_IIntuition->AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
	dbData->sb[1] = SDL_IIntuition->AllocScreenBuffer(screen, NULL, 0);

	dbData->SafeToFlip_MsgPort  = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
	dbData->SafeToWrite_MsgPort = IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);

	if (!dbData->sb[0] || !dbData->sb[1] || !dbData->SafeToFlip_MsgPort || !dbData->SafeToWrite_MsgPort)
	{
		dprintf("Failed\n");

		if (dbData->sb[0])
			SDL_IIntuition->FreeScreenBuffer(screen, dbData->sb[0]);

		if (dbData->sb[1])
			SDL_IIntuition->FreeScreenBuffer(screen, dbData->sb[1]);

		if (dbData->SafeToFlip_MsgPort)
			IExec->FreeSysObject(ASOT_PORT,dbData->SafeToFlip_MsgPort);

		if (dbData->SafeToWrite_MsgPort)
			IExec->FreeSysObject(ASOT_PORT,dbData->SafeToWrite_MsgPort);

		dbData->sb[0] = NULL;
		dbData->sb[1] = NULL;

		return FALSE;
	}

	/* Set up message ports for receiving SafeToWrite and SafeToFlip msgs
	 * from gfx.lib */
	dbData->sb[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = dbData->SafeToWrite_MsgPort;
	dbData->sb[0]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = dbData->SafeToFlip_MsgPort;
	
	dbData->sb[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = dbData->SafeToWrite_MsgPort;
	dbData->sb[1]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = dbData->SafeToFlip_MsgPort;

	dbData->SafeToFlip  = TRUE;
	dbData->SafeToWrite = TRUE;
	dbData->currentSB   = 1;

	return TRUE;
}

static void
os4video_FreeDoubleBuffering(struct DoubleBufferData *dbData, struct Screen *screen)
{
	dprintf("Freeing double-buffering resources\n");

	if (dbData->SafeToFlip_MsgPort) {
		while (IExec->GetMsg(dbData->SafeToFlip_MsgPort) != NULL)
		{}

		IExec->FreeSysObject(ASOT_PORT,dbData->SafeToFlip_MsgPort);
	}

	if (dbData->SafeToWrite_MsgPort) {
		while (IExec->GetMsg(dbData->SafeToWrite_MsgPort) != NULL)
		{}

		IExec->FreeSysObject(ASOT_PORT,dbData->SafeToWrite_MsgPort);
	}

	dbData->SafeToFlip_MsgPort  = NULL;
	dbData->SafeToWrite_MsgPort = NULL;

	if (dbData->sb[0])
		SDL_IIntuition->FreeScreenBuffer(screen, dbData->sb[0]);

	if (dbData->sb[1])
		SDL_IIntuition->FreeScreenBuffer(screen, dbData->sb[1]);

	dbData->sb[0] = NULL;
	dbData->sb[1] = NULL;
}


static void
os4video_DeleteCurrentDisplay(_THIS, SDL_Surface *current, BOOL keepOffScreenBuffer, EOpenGlContextPolicy policy)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;

	DeleteAppIcon(_this);

	_this->UpdateRects = os4video_UpdateRectsNone;

	ResetMouseColors(_this);

#if SDL_VIDEO_OPENGL
	if (hidden->OpenGL && (policy == EDestroyContext))
		os4video_GL_Term(_this);
#endif

	if (hidden->scr) {
		SDL_IIntuition->ScreenToBack(hidden->scr);

		/* Wait for next frame to make sure screen has
		 * gone to back */
		SDL_IGraphics->WaitTOF();
	}

	if (hidden->win)
	{
		if (hidden->iconifyGadget) {
			dprintf("Removing gadget\n");
			SDL_IIntuition->RemoveGadget(hidden->win, hidden->iconifyGadget);
			SDL_IIntuition->DisposeObject((Object *)hidden->iconifyGadget);
			hidden->iconifyGadget = NULL;
		}

		if (hidden->iconifyImage) {
			dprintf("Disposing image\n");
			SDL_IIntuition->DisposeObject((Object *)hidden->iconifyImage);
			hidden->iconifyImage = NULL;
		}

		dprintf("Closing window %p\n", hidden->win);
		SDL_IIntuition->CloseWindow(hidden->win);
		hidden->win = NULL;
	}

	if (hidden->scr)
	{
		os4video_FreeDoubleBuffering(&hidden->dbData, hidden->scr);

		dprintf("Closing screen %p\n", hidden->scr);

		SDL_IIntuition->CloseScreen(hidden->scr);
		hidden->scr = NULL;
	}

	if (!keepOffScreenBuffer)
		os4video_FreeOffScreenBuffer(&hidden->offScreenBuffer);

	if (current)
	{
		current->flags &= ~SDL_HWSURFACE;
		current->hwdata = NULL;
		current->w = current->h = 0;
	}

	/* Clear hardware record for the display surface - just in case */
	hidden->screenHWData.type = hwdata_other;
	hidden->screenHWData.lock = NULL;
	hidden->screenHWData.bm   = NULL;
}

static Uint32
os4video_ForceFullscreenFlags(Uint32 flags)
{
	dprintf("Forcing full-screen mode\n");

	flags |= SDL_FULLSCREEN;

	if (flags & SDL_OPENGL)
	{
		flags &= ~SDL_RESIZABLE;
	} else {
		flags &= ~(SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE);
	}

	return flags;
}

static Uint32
os4video_ApplyWindowModeFlags(SDL_Surface *current, Uint32 flags, int screenDepth)
{
	if (screenDepth > 8)
	{
		// Mark the surface as windowed
		if ((flags & SDL_OPENGL) == 0)
		{
			flags &= ~SDL_DOUBLEBUF;
		}

		current->flags = flags;
	}
	else
	{
		// Don't allow palette-mapped windowed surfaces -
		// We can't get exclusive access to the palette and
		// the results are ugly. Force a screen instead.
		flags = os4video_ForceFullscreenFlags(flags);

		flags |= SDL_FULLSCREEN;

		if (flags & SDL_OPENGL)
		{
			flags &= ~SDL_RESIZABLE;
		} else {
			flags &= ~(SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE);
		}
	}

	return flags;
}

static BOOL
os4video_IsModeUnsupported(uint32 modeId)
{
	// Check if this screen uses a little-endian pixel format (e.g.,
	// the early Radeon drivers only supported little-endian modes) which
	// we cannot express to SDL with a simple shift and mask alone.
	PIX_FMT fmt = os4video_GetPixelFormatFromMode(modeId);

	switch (fmt)
	{
		case PIXF_R5G6B5PC:
		case PIXF_R5G5B5PC:
		case PIXF_B5G6R5PC:
		case PIXF_B5G5R5PC:
			return TRUE;

		default:
			return FALSE;
	}
}

static void
os4video_SetupSurfacePixelFormat(_THIS, int bpp, int screenDepth, SDL_Surface *current)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;

	os4video_PIXFtoPF(&hidden->screenSdlFormat, hidden->screenNativeFormat);

	if (bpp == screenDepth)
	{
		// Set pixel format of surface to match the screen's format
		SDL_ReallocFormat (current, bpp,
						   hidden->screenSdlFormat.Rmask,
						   hidden->screenSdlFormat.Gmask,
						   hidden->screenSdlFormat.Bmask,
						   0 /* hidden->screenSdlFormat.Amask */);
	}
	else
	{
		// Set pixel format of surface to default for this depth
		SDL_ReallocFormat (current, bpp, 0, 0, 0, 0);
	}
}

static BOOL
os4video_SetupDoubleBuffering(_THIS, SDL_Surface *current)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;

	if (os4video_InitDoubleBuffering(&hidden->dbData, hidden->scr))
	{
		// Set surface to render to off-screen buffer
		current->hwdata->bm = hidden->dbData.sb[hidden->dbData.currentSB]->sb_BitMap;

		// Double-buffering requires an update
		_this->UpdateRects = os4video_UpdateRectsFullscreenDB;

		current->flags |= SDL_DOUBLEBUF;

		return TRUE;
	}

	SDL_IIntuition->CloseScreen(hidden->scr);
	hidden->scr = NULL;
	SDL_OutOfMemory();

	return FALSE;
}

static void
os4video_SetSurfaceParameters(_THIS, SDL_Surface *current, int width, int height, int bpp)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;

	current->w = width;
	current->h = height;

	if (current->flags & SDL_HWSURFACE)
	{
		current->pixels = (uint8*)0xdeadbeef;
		current->pitch  = 0;
		current->hwdata->type = hwdata_display_hw;
	}
	else
	{
		current->pixels = hidden->offScreenBuffer.pixels;
		current->pitch  = hidden->offScreenBuffer.pitch;
		current->hwdata->type = hwdata_display_sw;
	}

	current->offset = 0;

	if (bpp <= 8)
	{
		current->flags |= SDL_HWPALETTE;
	}

	current->flags &= ~SDL_HWACCEL; //FIXME

	current->hwdata->lock = 0;
}

#ifdef SDL_VIDEO_OPENGL
static BOOL
os4video_SetupOpenGl(_THIS, SDL_Surface *current, BOOL newOffScreenSurface)
{
	if (os4video_GL_Init(_this) != 0)
	{
		os4video_DeleteCurrentDisplay(_this, current, !newOffScreenSurface, EDestroyContext);
		return FALSE;
	}

	current->flags |= SDL_OPENGL;

	_this->UpdateRects = os4video_UpdateRectsNone;

	return TRUE;
}
#endif

static BOOL
os4video_NeedOffScreenSurface(Uint32 flags)
{
	BOOL swSurface = !(flags & SDL_HWSURFACE);
	BOOL window = !(flags & SDL_FULLSCREEN);
	BOOL noGl = !(flags & SDL_OPENGL);

	return ((swSurface || window) && noGl);
}

static BOOL
os4video_CreateDisplay(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags, BOOL newOffScreenSurface)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;
	struct Screen *screen;
	int screenDepth;

	dprintf("Creating a %dx%dx%d %s display\n", width, height, bpp,
		(flags & SDL_FULLSCREEN) ? "fullscreen" : "windowed");

	// ALWAYS set prealloc
	current->flags |= SDL_PREALLOC;
	flags |= SDL_PREALLOC;

	if (flags & SDL_OPENGL)
	{
		flags &= ~SDL_DOUBLEBUF; // Handled separately during GL swap
		flags &= ~SDL_HWSURFACE;
	}

	// Set up hardware record for this display surface
	current->hwdata = &hidden->screenHWData;
	SDL_memset(current->hwdata, 0, sizeof(struct private_hwdata));

	if (!(flags & SDL_FULLSCREEN))
	{
		screen = hidden->publicScreen;
		
		hidden->scr = NULL;
		hidden->screenNativeFormat = SDL_IGraphics->GetBitMapAttr(screen->RastPort.BitMap, BMA_PIXELFORMAT);
		
		screenDepth = os4video_PIXF2Bits(hidden->screenNativeFormat);

		dprintf("Screen depth: %d pixel format: %d\n", screenDepth, hidden->screenNativeFormat);

		flags = os4video_ApplyWindowModeFlags(current, flags, screenDepth);
	}

	if (flags & SDL_FULLSCREEN)
	{
		uint32 modeId = os4video_FindMode(width, height, bpp, flags | SDL_ANYFORMAT);

		if (modeId == INVALID_ID)
		{
			dprintf("No mode for this resolution\n");
			return FALSE;
		}

		screen = hidden->scr = os4video_OpenScreen(width, height, modeId);

		if (!screen)
		{
			return FALSE;
		}

		current->flags |= SDL_FULLSCREEN;

		if (os4video_IsModeUnsupported(modeId))
		{
			dprintf("Unsupported mode, switching to off-screen rendering\n");
			flags &= ~(SDL_HWSURFACE | SDL_DOUBLEBUF);
		}

		if (flags & SDL_HWSURFACE)
		{
			// We render to the screen's BitMap
			current->hwdata->bm = screen->RastPort.BitMap;
			current->flags |= SDL_HWSURFACE;

			_this->UpdateRects = os4video_UpdateRectsNone;
		}

		hidden->screenNativeFormat = SDL_IGraphics->GetBitMapAttr(screen->RastPort.BitMap, BMA_PIXELFORMAT);
		screenDepth = os4video_PIXF2Bits(hidden->screenNativeFormat);

		dprintf("Screen depth: %d pixel format: %d\n", screenDepth, hidden->screenNativeFormat);
	}

	os4video_SetupSurfacePixelFormat(_this, bpp, screenDepth, current);

	BOOL hwSurface = (flags & SDL_HWSURFACE) == SDL_HWSURFACE;

	if (os4video_NeedOffScreenSurface(flags))
	{
		if (newOffScreenSurface && !os4video_InitOffScreenBuffer(&hidden->offScreenBuffer, width, height, current->format, hwSurface))
		{
			dprintf("Failed to allocate off-screen buffer\n");
			SDL_OutOfMemory();
			return FALSE;
		}

		current->hwdata->bm = hidden->offScreenBuffer.bitmap;

		if (bpp > 8 || screenDepth == 8 || (flags & SDL_FULLSCREEN))
		{
			_this->UpdateRects = os4video_UpdateRectsOffscreen;
		}
		else
		{
			_this->UpdateRects = os4video_UpdateRectsOffscreen_8bit;
		}
	}

	if ((flags & SDL_DOUBLEBUF) && hwSurface)
	{
		if (!os4video_SetupDoubleBuffering(_this, current))
		{
			return FALSE;
		}
	}
	else
	{
		current->flags &= ~SDL_DOUBLEBUF;
	}

	os4video_SetSurfaceParameters(_this, current, width, height, bpp);

	hidden->pointerState = pointer_dont_know;
	hidden->win = os4video_OpenWindow(_this, width, height, screen, flags);

	if (!hidden->win)
	{
		dprintf("Failed to open window\n");
		os4video_DeleteCurrentDisplay(_this, current, !newOffScreenSurface, EDestroyContext);
		return FALSE;
	}

#if SDL_VIDEO_OPENGL
	if (flags & SDL_OPENGL)
	{
		if (!os4video_SetupOpenGl(_this, current, newOffScreenSurface))
		{
			return FALSE;
		}
#ifdef SDL_GL4ES
		// fix for ogles2 to not loosing window pointer when swith to/from fullscreen
		struct TagItem setparams[] = 
			{ 
				{OGLES2_CCT_WINDOW, (ULONG)hidden->win}, 
				{TAG_DONE, 0} 
			}; 
		aglSetParams2(setparams); 
#endif
	}
#endif

	return TRUE;
}

#ifdef DEBUG
static char *
os4video_GetFlagString(Uint32 flags)
{
    static char buffer[256];

    buffer[0] = '\0';

	if (flags & SDL_ANYFORMAT)                      SDL_strlcat(buffer, "ANYFORMAT ", sizeof(buffer));
	if (flags & SDL_HWSURFACE)                      SDL_strlcat(buffer, "HWSURFACE ", sizeof(buffer));
	if (flags & SDL_HWPALETTE)                      SDL_strlcat(buffer, "HWPALETTE ", sizeof(buffer));
	if (flags & SDL_DOUBLEBUF)                      SDL_strlcat(buffer, "DOUBLEBUF ", sizeof(buffer));
	if (flags & SDL_FULLSCREEN)                     SDL_strlcat(buffer, "FULLSCREEN ", sizeof(buffer));
	if (flags & SDL_OPENGL)                         SDL_strlcat(buffer, "OPENGL ", sizeof(buffer));
	if ((flags & SDL_OPENGLBLIT) == SDL_OPENGLBLIT) SDL_strlcat(buffer, "OPENGLBLIT ", sizeof(buffer));
	if (flags & SDL_RESIZABLE)                      SDL_strlcat(buffer, "RESIZEABLE ", sizeof(buffer));
	if (flags & SDL_NOFRAME)                        SDL_strlcat(buffer, "NOFRAME ", sizeof(buffer));
	if (flags & SDL_PREALLOC)                       SDL_strlcat(buffer, "PREALLOC", sizeof(buffer));

	return buffer;
}
#endif

static SDL_Surface *
os4video_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;
	BOOL needNew    = FALSE;
	BOOL needResize = FALSE;
	int success = TRUE;

	const Uint32 flagMask = SDL_HWPALETTE | SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN |
							SDL_OPENGL | SDL_OPENGLBLIT | SDL_RESIZABLE | SDL_NOFRAME | SDL_ANYFORMAT;

	dprintf("Requesting new video mode %dx%dx%d\n", width, height, bpp);
	dprintf("Requested flags: %s\n", os4video_GetFlagString(flags));
	dprintf("Current mode %dx%dx%d\n", current->w, current->h, current->format->BitsPerPixel);
	dprintf("Current mode flags %s\n", os4video_GetFlagString(current->flags));
	dprintf("Current hwdata %p\n", current->hwdata);

	/* If there's an existing primary surface open, check whether it fits the request */
	if (hidden->win)
	{
		if (current->w != width || current->h != height)
		{
			if (!(current->flags & SDL_FULLSCREEN))
			{
				dprintf("Requesting window resize\n");
				needResize = TRUE;
			}
			else
			{
				dprintf("Requesting screen with new size\n");
				needNew = TRUE;
			}
		}

		if (current->format->BitsPerPixel != bpp)
		{
			dprintf("Bits per pixel differ\n");
			needNew = TRUE;
		}

		if ((current->flags & flagMask) ^ (flags & flagMask))
		{
			dprintf("Surface flags differ\n");
			needNew = TRUE;
		}
	}
	else
		needNew = TRUE;

	/* Has just the surface size changed? */
	if (needResize && !needNew)
	{
		struct Window *w = hidden->win;

		/* If in windowed mode and only a change of size is requested.
		 * simply re-size the window. Don't bother opening a new one for this
		 * request */
		SDL_Lock_EventThread();

		dprintf("Resizing window: %dx%d\n", width, height);

		SDL_IIntuition->ChangeWindowBox(w,
									w->LeftEdge,
									w->TopEdge,
									w->BorderLeft + w->BorderRight  + width,
									w->BorderTop  + w->BorderBottom + height);

		BOOL hwSurface = (flags & SDL_HWSURFACE) == SDL_HWSURFACE;

		success = os4video_ResizeOffScreenBuffer(&hidden->offScreenBuffer, width, height, hwSurface);

		current->w = width;
		current->h = height;
		current->pixels = hidden->offScreenBuffer.pixels;
		current->pitch  = hidden->offScreenBuffer.pitch;

#if defined (SDL_VIDEO_OPENGL) && !defined(SDL_GL4ES)
		if ((current->flags & SDL_OPENGL) == SDL_OPENGL)
		{
			if (!os4video_AllocateOpenGLBuffers(_this, width, height))
			{
				success = FALSE;
			}
		}
#endif

		SDL_Unlock_EventThread();

		if (!success)
		{
			dprintf("Failed to resize window\n");
			os4video_DeleteCurrentDisplay(_this, current, TRUE, EDestroyContext);
		}
	}

	if (needNew)
	{
		uint32 ow, oh, obpp, oflags;

		dprintf("Creating new display\n");

		SDL_Lock_EventThread();

		ow = current->w;
		oh = current->h;
		obpp = current->format->BitsPerPixel;
		oflags = current->flags & flagMask;

		/* Remove the old display (might want to resize window if not fullscreen) */
		dprintf("Deleting old display\n");
		os4video_DeleteCurrentDisplay(_this, current, FALSE, EDestroyContext);

		dprintf("Opening new display\n");
		if (os4video_CreateDisplay(_this, current, width, height, bpp, flags, TRUE))
		{
			dprintf("New display created\n");
			dprintf("Obtained flags: %s\n", os4video_GetFlagString(current->flags));
		}
		else
		{
			success = FALSE;

			if (hidden->win)
			{
				dprintf("Retrying old mode\n");
				os4video_CreateDisplay(_this, current, ow, oh, obpp, oflags, TRUE);
			}
		}

		SDL_Unlock_EventThread();
	}

	return success ? current : NULL;
}


static int
os4video_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;
	int i;

	dprintf("Loading colors %d to %d\n", firstcolor, firstcolor + ncolors - 1);

	if (!hidden->scr)
	{
		/* Windowed mode, or palette on direct color screen */
		dprintf("Windowed\n");
		dprintf("Using color format %d bpp, masks = %p, %p, %p\n",
			hidden->screenSdlFormat.BitsPerPixel,
			hidden->screenSdlFormat.Rmask,
			hidden->screenSdlFormat.Gmask,
			hidden->screenSdlFormat.Bmask);

		switch (hidden->screenNativeFormat)
		{
			case PIXF_R5G6B5PC:
			case PIXF_R5G5B5PC:
			case PIXF_B5G6R5PC:
			case PIXF_B5G5R5PC:
				dprintf("Little endian screen format\n");
				for (i = firstcolor; i < firstcolor + ncolors; i++)
				{
					hidden->offScreenBuffer.palette[i] =
						os4video_SwapShort(SDL_MapRGB(&hidden->screenSdlFormat,
							colors[i - firstcolor].r,
							colors[i - firstcolor].g,
							colors[i - firstcolor].b));

					hidden->currentPalette[i] = colors[i - firstcolor];
				}
				break;
			default:
				dprintf("Big endian screen format\n");
				for (i = firstcolor; i < firstcolor + ncolors; i++)
				{
					hidden->offScreenBuffer.palette[i] =
						SDL_MapRGB(&hidden->screenSdlFormat,
							colors[i - firstcolor].r,
							colors[i - firstcolor].g,
							colors[i - firstcolor].b);

					hidden->currentPalette[i] = colors[i - firstcolor];
				}
				break;
		}
		/* Redraw screen so that palette changes take effect */
		SDL_UpdateRect(SDL_VideoSurface, 0, 0, 0, 0);
	}
	else
	{
		/* Fullscreen mode. First, convert to LoadRGB32 format */
		uint32 colTable[ncolors * 3 + 3];
	    uint32 *current;

		dprintf("Fullscreen\n");

		colTable[0] = (ncolors << 16) | firstcolor;

		current = &colTable[1];

		for (i = 0; i < ncolors; i++)
		{
			//dprintf("Setting %d to %d,%d,%d\n", i, colors[i].r, colors[i].g, colors[i].b);
			*current++ = ((uint32)colors[i/*+firstcolor*/].r << 24) | 0xffffff;
			*current++ = ((uint32)colors[i/*+firstcolor*/].g << 24) | 0xffffff;
			*current++ = ((uint32)colors[i/*+firstcolor*/].b << 24) | 0xffffff;
			hidden->currentPalette[i] = colors[i-firstcolor];
		}
		*current = 0;

		dprintf("Loading RGB\n");
		SDL_IGraphics->LoadRGB32(&hidden->scr->ViewPort, colTable);
	}

	dprintf("Done\n");
	return 1;
}

static void
os4video_LockEventThread(void)
{
	Uint32 eventThread = SDL_EventThreadID();

	if (eventThread && (SDL_ThreadID() == eventThread)) {
		eventThread = 0;
	}

	if (eventThread) {
		SDL_Lock_EventThread();
	}
}

static void
os4video_UnlockEventThread(void)
{
	Uint32 eventThread = SDL_EventThreadID();

	if (eventThread && (SDL_ThreadID() == eventThread)) {
		eventThread = 0;
	}

	if (eventThread) {
		SDL_Unlock_EventThread();
	}
}

static int
os4video_ToggleFullScreen(_THIS, int on)
{
	struct SDL_PrivateVideoData *hidden = _this->hidden;

	SDL_Surface *current = SDL_VideoSurface;
	SDL_Rect screenRect;

	Uint32 oldFlags = current->flags;
	Uint32 newFlags = oldFlags;
	Uint32 w = current->w;
	Uint32 h = current->h;
	Uint32 bpp = current->format->BitsPerPixel;

	// Don't switch if we don't own the window
	if (!hidden->windowActive)
		return 0;

	dprintf("Trying to toggle fullscreen (%d)\n", on);
	dprintf("Current flags: %s\n", os4video_GetFlagString(current->flags));

	if (on)
	{
		newFlags |= SDL_FULLSCREEN;
	}
	else
	{
		newFlags &= ~SDL_FULLSCREEN;
	}

	screenRect.x = 0;
	screenRect.y = 0;
	screenRect.w = current->w;
	screenRect.h = current->h;

	/* FIXME: Save and transfer palette */
	/* Make sure we're the only one */
	os4video_LockEventThread();

	os4video_DeleteCurrentDisplay(_this, current, TRUE, EKeepContext);

	if (os4video_CreateDisplay(_this, current, w, h, bpp, newFlags, FALSE))
	{
#if defined (SDL_VIDEO_OPENGL) && !defined(SDL_GL4ES)
		if (oldFlags & SDL_OPENGL)
		{
			if (!os4video_AllocateOpenGLBuffers(_this, w, h))
			{
				os4video_UnlockEventThread();
				return -1;
			}
		}
#endif
		os4video_UnlockEventThread();

		_this->UpdateRects(_this, 1, &screenRect);

		if (on && bpp == 8)
			_this->SetColors(_this, 0, 256, hidden->currentPalette);

		SDL_ResetKeyboard();
		ResetMouseState(_this);

		dprintf("Success. Obtained flags: %s\n", os4video_GetFlagString(current->flags));

		return 1;
	}

	dprintf("Switch failed, re-setting old display\n");

	if (os4video_CreateDisplay(_this, current, w, h, bpp, oldFlags, FALSE))
	{
#if defined (SDL_VIDEO_OPENGL) && !defined(SDL_GL4ES)
		if (oldFlags & SDL_OPENGL)
		{
			if (!os4video_AllocateOpenGLBuffers(_this, w, h))
			{
        		os4video_UnlockEventThread();
				return -1;
			}
		}
#endif

		os4video_UnlockEventThread();

		ResetMouseState(_this);

		_this->UpdateRects(_this, 1, &screenRect);

		if (!on && bpp == 8)
			_this->SetColors(_this, 0, 256, hidden->currentPalette);

		dprintf("Failure\n");
		return 0;
	}

#if SDL_VIDEO_OPENGL
	if (hidden->OpenGL)
		os4video_GL_Term(_this);
#endif

	dprintf("Fatal error: Can't restart video\n");

	SDL_Quit();

	return -1;
}

#if defined (SDL_VIDEO_OPENGL) && !defined(SDL_GL4ES)
/* These symbols are weak so they replace the MiniGL symbols in case libmgl.a is
 * not linked
 */


// Rich - These two declarations conflict with MiniGL 1.1 headers. Does this still
// even build with static libmgl? We can't do that for license reasons anyway...
//
//typedef struct {} GLcontext;

//GLcontext mini_CurrentContext __attribute__((weak));

#define WEAK(X)										\
void X(void) __attribute__((weak));					\
													\
void X(void) {};

WEAK(MGLCreateContextFromBitmap)
WEAK(MGLSetBitmap)
//WEAK(MGLGetProcAddress)
WEAK(MGLInit)
WEAK(MGLLockMode)
WEAK(MGLDeleteContext)
WEAK(MGLTerm)
WEAK(MGLUnlockDisplay)
WEAK(GLBegin)
WEAK(GLBindTexture)
WEAK(GLBlendFunc)
WEAK(GLColor4f)
WEAK(MGLSetState)
WEAK(GLEnd)
WEAK(GLFlush)
WEAK(GLGenTextures)
WEAK(GLGetString)
WEAK(GLLoadIdentity)
WEAK(GLMatrixMode)
WEAK(GLOrtho)
WEAK(GLPixelStorei)
WEAK(GLPopMatrix)
WEAK(GLPushMatrix)
WEAK(GLTexCoord2f)
WEAK(GLTexEnvi)
WEAK(GLTexImage2D)
WEAK(GLTexParameteri)
WEAK(GLTexSubImage2D)
WEAK(GLVertex4f)
WEAK(GLViewport)
WEAK(GLPushAttrib)
WEAK(GLPopAttrib)
WEAK(GLGetIntegerv)

#endif
