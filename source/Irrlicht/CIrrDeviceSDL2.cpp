// Based on SDL1 device
// See https://wiki.libsdl.org/MigrationGuide for API changes on SDL2

// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_SDL2_DEVICE_

#include "CIrrDeviceSDL2.h"
#include "IEventReceiver.h"
#include "irrList.h"
#include "os.h"
#include "CTimer.h"
#include "irrString.h"
#include "Keycodes.h"
#include "COSOperator.h"
#include <stdio.h>
#include <stdlib.h>
#include "SIrrCreationParameters.h"

#include "CEGLManager.h"

#include "SDL_syswm.h"
#include "SDL_video.h"

// FIMXE: Split ContextManager into separate files
#include "SIrrCreationParameters.h"
#include "SExposedVideoData.h"
#include "IContextManager.h"

namespace irr
{
	namespace video
	{
		class CSDLContextManager : public IContextManager
		{
		public:
			CSDLContextManager(SDL_Window* me) : Window(me) {}
			~CSDLContextManager() {}
			bool initialize(const SIrrlichtCreationParameters& params,
				const SExposedVideoData& data) {
				Data = data;
				return true;
			}
			void terminate() {}
			bool generateSurface() {
				return true;
			}
			void destroySurface() {
			}
			bool generateContext() {
				Context = SDL_GL_CreateContext(Window);
				if (Context) {
					SDL_GL_MakeCurrent(Window, Context);
					SDL_GL_SetSwapInterval(0);
				}
				return (Context) ? true : false;
			}
			void destroyContext() {
				if (Context) {
					SDL_GL_DeleteContext(Context);
					Context = NULL;
				}
			}
			const SExposedVideoData& getContext() const {
				return Data;
			}
			bool activateContext(const SExposedVideoData& videoData) {
				return (SDL_GL_MakeCurrent(Window, Context) == 0) ? true : false;
			}
			bool swapBuffers() {
				SDL_GL_SwapWindow(Window);
				return true;
			}
		private:
			SExposedVideoData Data;
			SDL_GLContext Context;
			SDL_Window* Window;
		};
	}
}


static int SDLDeviceInstances = 0;

namespace irr
{
	namespace video
	{
		#if defined(_IRR_COMPILE_WITH_OGLES1_)
		IVideoDriver* createOGLES1Driver(const irr::SIrrlichtCreationParameters& params, io::IFileSystem* io, IContextManager* contextManager);
		#endif

		#if defined(_IRR_COMPILE_WITH_OGLES2_)
		IVideoDriver* createOGLES2Driver(const irr::SIrrlichtCreationParameters& params, io::IFileSystem* io, IContextManager* contextManager);
		#endif

		#if defined(_IRR_COMPILE_WITH_WEBGL1_)
		IVideoDriver* createWebGL1Driver(const irr::SIrrlichtCreationParameters& params, io::IFileSystem* io, IContextManager* contextManager);
		#endif
	} // end namespace video

} // end namespace irr


namespace irr
{
//! constructor
CIrrDeviceSDL2::CIrrDeviceSDL2(const SIrrlichtCreationParameters& param)
	: CIrrDeviceStub(param),
	ScreenSurface(0), ScreenWindow(0), ScreenTexture(0),
	MouseX(0), MouseY(0), MouseXRel(0), MouseYRel(0), MouseButtonStates(0),
	Width(param.WindowSize.Width), Height(param.WindowSize.Height),
	Resizable(false), WindowHasFocus(false), WindowMinimized(false)
{

	#ifdef _DEBUG
	setDebugName("CIrrDeviceSDL2");
	#endif

	if ( ++SDLDeviceInstances == 1 )
	{
		// Initialize SDL... Timer for sleep, video for the obvious, and
		// noparachute prevents SDL from catching fatal errors.
		if (SDL_Init( SDL_INIT_TIMER|SDL_INIT_VIDEO|
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
					SDL_INIT_JOYSTICK|
#endif
					SDL_INIT_NOPARACHUTE ) < 0)
		{
			os::Printer::log( "Unable to initialize SDL!", SDL_GetError());
			Close = true;
		}
		else
		{
			os::Printer::log("SDL initialized", ELL_INFORMATION);
		}
	}

	SDL_VERSION(&Info.version);

	core::stringc sdlversion = "SDL Version ";
	sdlversion += Info.version.major;
	sdlversion += ".";
	sdlversion += Info.version.minor;
	sdlversion += ".";
	sdlversion += Info.version.patch;

	Operator = new COSOperator(sdlversion);
	if ( SDLDeviceInstances == 1 )
	{
		os::Printer::log(sdlversion.c_str(), ELL_INFORMATION);
	}

	// create keymap
	createKeyMap();
	
	// FIXME: SDL2 does not have Unicode translation
	// FIXME: Ignore CreationParams.Doublebuffer -- no gain on modern platforms

	if (CreationParams.Fullscreen)
		Fullscreen = true;
#if 0 // FIXME: Add OpenGL context to the device
	if (CreationParams.DriverType == video::EDT_OPENGL)
		window_flags |= SDL_WINDOW_OPENGL;
#endif
	// create window
	if (CreationParams.DriverType != video::EDT_NULL)
	{
		// create the window, only if we do not use the null device
		createWindow();
	}

	// create cursor control
	CursorControl = new CCursorControl(this);

	// create driver
	createDriver();

	if (VideoDriver)
		createGUIAndScene();
}


//! destructor
CIrrDeviceSDL2::~CIrrDeviceSDL2()
{
	if ( --SDLDeviceInstances == 0 )
	{
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
		const u32 numJoysticks = Joysticks.size();
		for (u32 i=0; i<numJoysticks; ++i)
			SDL_JoystickClose(Joysticks[i]);
#endif
		SDL_Quit();

		os::Printer::log("Quit SDL", ELL_INFORMATION);
	}
}

void CIrrDeviceSDL2::logAttributes()
{
	core::stringc sdl_attr("SDL attribs:");
	int value = 0;
	if ( SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &value ) == 0 )
		sdl_attr += core::stringc(" r:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &value ) == 0 )
		sdl_attr += core::stringc(" g:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &value ) == 0 )
		sdl_attr += core::stringc(" b:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_ALPHA_SIZE, &value ) == 0 )
		sdl_attr += core::stringc(" a:") + core::stringc(value);

	if ( SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &value) == 0 )
		sdl_attr += core::stringc(" depth:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &value ) == 0 )
		sdl_attr += core::stringc(" stencil:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_DOUBLEBUFFER, &value ) == 0 )
		sdl_attr += core::stringc(" doublebuf:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_MULTISAMPLEBUFFERS, &value ) == 0 )
		sdl_attr += core::stringc(" aa:") + core::stringc(value);
	if ( SDL_GL_GetAttribute( SDL_GL_MULTISAMPLESAMPLES, &value ) == 0 )
		sdl_attr += core::stringc(" aa-samples:") + core::stringc(value);

	os::Printer::log(sdl_attr.c_str());
}

bool CIrrDeviceSDL2::createWindow()
{
	if ( Close )
		return false;



	if (CreationParams.DriverType == video::EDT_BURNINGSVIDEO || CreationParams.DriverType == video::EDT_SOFTWARE) {
		SoftwareRendered = true;
	}
	else {
		// Assume OpenGLES
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		// FIXME: Assume GLES2
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	}

	int flags = SoftwareRendered ? 0 : SDL_WINDOW_OPENGL;

	ScreenWindow = SDL_CreateWindow("Untitled", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		Width, Height, flags);


	if (SoftwareRendered)
	{
		ScreenRenderer = SDL_CreateRenderer(ScreenWindow, -1, 0);
		resizeWindow(Width, Height);
	}


#if 0 // FIXME: Handle HW OpenGL
	if (CreationParams.DriverType == video::EDT_OPENGL)
	{
		if (CreationParams.Bits==16)
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel?1:0 );
		}
		else
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel?8:0 );
		}
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, CreationParams.ZBufferBits);
		if (CreationParams.Doublebuffer)
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, CreationParams.Stencilbuffer ? 8 : 0);
		if (CreationParams.Stereobuffer)
			SDL_GL_SetAttribute( SDL_GL_STEREO, 1 );
		if (CreationParams.AntiAlias>1)
		{
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, CreationParams.AntiAlias );
		}
		if ( !Screen )
			Screen = SDL_SetVideoMode( Width, Height, CreationParams.Bits, SDL_Flags );
		if ( !Screen && CreationParams.AntiAlias>1)
		{
			while (--CreationParams.AntiAlias>1)
			{
				SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, CreationParams.AntiAlias );
				Screen = SDL_SetVideoMode( Width, Height, CreationParams.Bits, SDL_Flags );
				if (Screen)
					break;
			}
			if ( !Screen )
			{
				SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
				SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );
				Screen = SDL_SetVideoMode( Width, Height, CreationParams.Bits, SDL_Flags );
				if (Screen)
					os::Printer::log("AntiAliasing disabled due to lack of support!" );
			}
		}
	}
	else if ( !Screen )
		Screen = SDL_SetVideoMode( Width, Height, CreationParams.Bits, SDL_Flags );

	if ( !ScreenWindow )
	{
		os::Printer::log( "Could not initialize display!" );
		return false;
	}
#endif
	return true;
}


//! create the driver
void CIrrDeviceSDL2::createDriver()
{
	switch(CreationParams.DriverType)
	{
	case video::DEPRECATED_EDT_DIRECT3D8_NO_LONGER_EXISTS:
		os::Printer::log("DIRECT3D8 Driver is no longer supported in Irrlicht. Try another one.", ELL_ERROR);
		break;

	case video::EDT_DIRECT3D9:
		#ifdef _IRR_COMPILE_WITH_DIRECT3D_9_

		VideoDriver = video::createDirectX9Driver(CreationParams, FileSystem, HWnd);
		if (!VideoDriver)
		{
			os::Printer::log("Could not create DIRECT3D9 Driver.", ELL_ERROR);
		}
		#else
		os::Printer::log("DIRECT3D9 Driver was not compiled into this dll. Try another one.", ELL_ERROR);
		#endif // _IRR_COMPILE_WITH_DIRECT3D_9_

		break;

	case video::EDT_SOFTWARE:
		#ifdef _IRR_COMPILE_WITH_SOFTWARE_
		VideoDriver = video::createSoftwareDriver(CreationParams.WindowSize, CreationParams.Fullscreen, FileSystem, this);
		#else
		os::Printer::log("No Software driver support compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_BURNINGSVIDEO:
		#ifdef _IRR_COMPILE_WITH_BURNINGSVIDEO_
		VideoDriver = video::createBurningVideoDriver(CreationParams, FileSystem, this);
		#else
		os::Printer::log("Burning's video driver was not compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_OPENGL:
		#ifdef _IRR_COMPILE_WITH_OPENGL_
		VideoDriver = video::createOpenGLDriver(CreationParams, FileSystem, this);
		#else
		os::Printer::log("No OpenGL support compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_OGLES1:
		#if defined(_IRR_COMPILE_WITH_OGLES1_)
		{
			video::SExposedVideoData data;

			ContextManager = new irr::video::CSDLContextManager(this->ScreenWindow);
			ContextManager->initialize(CreationParams, data);

			VideoDriver = video::createOGLES1Driver(CreationParams, FileSystem, ContextManager);
		}
		#else
		os::Printer::log("No OpenGL-ES1 support compiled in.", ELL_ERROR);
		#endif
		break;
	case video::EDT_OGLES2:
		#if defined(_IRR_COMPILE_WITH_OGLES2_)
		{
			video::SExposedVideoData data;

			ContextManager = new irr::video::CSDLContextManager(this->ScreenWindow);
			ContextManager->initialize(CreationParams, data);

			VideoDriver = video::createOGLES2Driver(CreationParams, FileSystem, ContextManager);
		}
		#else
		os::Printer::log("No OpenGL-ES2 support compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_WEBGL1:
#if defined(_IRR_COMPILE_WITH_WEBGL1_)
		{
			video::SExposedVideoData data;

			ContextManager = new video::CEGLManager();
			ContextManager->initialize(CreationParams, data);

			VideoDriver = video::createWebGL1Driver(CreationParams, FileSystem, ContextManager);
		}
#else
		os::Printer::log("No WebGL1 support compiled in.", ELL_ERROR);
#endif
		break;

	case video::EDT_NULL:
		VideoDriver = video::createNullDriver(FileSystem, CreationParams.WindowSize);
		break;

	default:
		os::Printer::log("Unable to create video driver of unknown type.", ELL_ERROR);
		break;
	}

	// In case we got the size from the canvas
	if ( VideoDriver && CreationParams.WindowSize.Width == 0 && CreationParams.WindowSize.Height == 0 && Width > 0 && Height > 0 )
	{
		resizeWindow(Width, Height);
		VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
	}
}


//! runs the device. Returns false if device wants to be deleted
bool CIrrDeviceSDL2::run()
{
	os::Timer::tick();

	SEvent irrevent;
	SDL_Event SDL_event;

	while ( !Close && SDL_PollEvent( &SDL_event ) )
	{
		// os::Printer::log("event: ", core::stringc((int)SDL_event.type).c_str(),   ELL_INFORMATION);	// just for debugging

		switch ( SDL_event.type )
		{
		case SDL_MOUSEMOTION:
			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;
			MouseX = irrevent.MouseInput.X = SDL_event.motion.x;
			MouseY = irrevent.MouseInput.Y = SDL_event.motion.y;
			MouseXRel = SDL_event.motion.xrel;
			MouseYRel = SDL_event.motion.yrel;
			irrevent.MouseInput.ButtonStates = MouseButtonStates;

			postEventFromUser(irrevent);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:

			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.X = SDL_event.button.x;
			irrevent.MouseInput.Y = SDL_event.button.y;

			irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;

			switch(SDL_event.button.button)
			{
			case SDL_BUTTON_LEFT:
				if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
				{
					irrevent.MouseInput.Event = irr::EMIE_LMOUSE_PRESSED_DOWN;
					MouseButtonStates |= irr::EMBSM_LEFT;
				}
				else
				{
					irrevent.MouseInput.Event = irr::EMIE_LMOUSE_LEFT_UP;
					MouseButtonStates &= !irr::EMBSM_LEFT;
				}
				break;

			case SDL_BUTTON_RIGHT:
				if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
				{
					irrevent.MouseInput.Event = irr::EMIE_RMOUSE_PRESSED_DOWN;
					MouseButtonStates |= irr::EMBSM_RIGHT;
				}
				else
				{
					irrevent.MouseInput.Event = irr::EMIE_RMOUSE_LEFT_UP;
					MouseButtonStates &= !irr::EMBSM_RIGHT;
				}
				break;

			case SDL_BUTTON_MIDDLE:
				if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
				{
					irrevent.MouseInput.Event = irr::EMIE_MMOUSE_PRESSED_DOWN;
					MouseButtonStates |= irr::EMBSM_MIDDLE;
				}
				else
				{
					irrevent.MouseInput.Event = irr::EMIE_MMOUSE_LEFT_UP;
					MouseButtonStates &= !irr::EMBSM_MIDDLE;
				}
				break;

			case SDL_MOUSEWHEEL:
				irrevent.MouseInput.Event = irr::EMIE_MOUSE_WHEEL;
				irrevent.MouseInput.Wheel = SDL_event.wheel.y;
				break;
			}

			irrevent.MouseInput.ButtonStates = MouseButtonStates;

			if (irrevent.MouseInput.Event != irr::EMIE_MOUSE_MOVED)
			{
				postEventFromUser(irrevent);

				if ( irrevent.MouseInput.Event >= EMIE_LMOUSE_PRESSED_DOWN && irrevent.MouseInput.Event <= EMIE_MMOUSE_PRESSED_DOWN )
				{
					u32 clicks = checkSuccessiveClicks(irrevent.MouseInput.X, irrevent.MouseInput.Y, irrevent.MouseInput.Event);
					if ( clicks == 2 )
					{
						irrevent.MouseInput.Event = (EMOUSE_INPUT_EVENT)(EMIE_LMOUSE_DOUBLE_CLICK + irrevent.MouseInput.Event-EMIE_LMOUSE_PRESSED_DOWN);
						postEventFromUser(irrevent);
					}
					else if ( clicks == 3 )
					{
						irrevent.MouseInput.Event = (EMOUSE_INPUT_EVENT)(EMIE_LMOUSE_TRIPLE_CLICK + irrevent.MouseInput.Event-EMIE_LMOUSE_PRESSED_DOWN);
						postEventFromUser(irrevent);
					}
				}
			}
			break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			{
				SKeyMap mp;
				mp.SDLKey = SDL_event.key.keysym.sym;
				s32 idx = KeyMap.binary_search(mp);

				EKEY_CODE key;
				if (idx == -1)
					key = (EKEY_CODE)0;
				else
					key = (EKEY_CODE)KeyMap[idx].Win32Key;

				irrevent.EventType = irr::EET_KEY_INPUT_EVENT;
				irrevent.KeyInput.Char = SDL_event.key.keysym.sym;
				irrevent.KeyInput.Key = key;
				irrevent.KeyInput.PressedDown = (SDL_event.type == SDL_KEYDOWN);
				irrevent.KeyInput.Shift = (SDL_event.key.keysym.mod & KMOD_SHIFT) != 0;
				irrevent.KeyInput.Control = (SDL_event.key.keysym.mod & KMOD_CTRL ) != 0;
				postEventFromUser(irrevent);
			}
			break;

		case SDL_QUIT:
			Close = true;
			break;

		case SDL_WINDOWEVENT:
			switch (SDL_event.window.event)
			{
			// FIXME: Implement more precise window control
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				// FIXME: Check if the window is game window
				if ((SDL_event.window.data1 != (int)Width) || (SDL_event.window.data2 != (int)Height))
				{
					Width = SDL_event.window.data1;
					Height = SDL_event.window.data2;
					resizeWindow(Width, Height);
					if (VideoDriver)
						VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
				}
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				WindowHasFocus = true;
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				WindowHasFocus = false;
				break;
			case SDL_WINDOWEVENT_SHOWN:
				WindowMinimized = false;
				break;
			case SDL_WINDOWEVENT_MINIMIZED:
				WindowMinimized = true;
				break;
			}
			break;

		case SDL_USEREVENT:
			irrevent.EventType = irr::EET_USER_EVENT;
			irrevent.UserEvent.UserData1 = reinterpret_cast<uintptr_t>(SDL_event.user.data1);
			irrevent.UserEvent.UserData2 = reinterpret_cast<uintptr_t>(SDL_event.user.data2);

			postEventFromUser(irrevent);
			break;

		default:
			break;
		} // end switch

	} // end while

#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	// TODO: Check if the multiple open/close calls are too expensive, then
	// open/close in the constructor/destructor instead

	// update joystick states manually
	SDL_JoystickUpdate();
	// we'll always send joystick input events...
	SEvent joyevent;
	joyevent.EventType = EET_JOYSTICK_INPUT_EVENT;
	for (u32 i=0; i<Joysticks.size(); ++i)
	{
		SDL_Joystick* joystick = Joysticks[i];
		if (joystick)
		{
			int j;
			// query all buttons
			const int numButtons = core::min_(SDL_JoystickNumButtons(joystick), 32);
			joyevent.JoystickEvent.ButtonStates=0;
			for (j=0; j<numButtons; ++j)
				joyevent.JoystickEvent.ButtonStates |= (SDL_JoystickGetButton(joystick, j)<<j);

			// query all axes, already in correct range
			const int numAxes = core::min_(SDL_JoystickNumAxes(joystick), (int)SEvent::SJoystickEvent::NUMBER_OF_AXES);
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_X]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_Y]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_Z]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_R]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_U]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_V]=0;
			for (j=0; j<numAxes; ++j)
				joyevent.JoystickEvent.Axis[j] = SDL_JoystickGetAxis(joystick, j);

			// we can only query one hat, SDL only supports 8 directions
			if (SDL_JoystickNumHats(joystick)>0)
			{
				switch (SDL_JoystickGetHat(joystick, 0))
				{
					case SDL_HAT_UP:
						joyevent.JoystickEvent.POV=0;
						break;
					case SDL_HAT_RIGHTUP:
						joyevent.JoystickEvent.POV=4500;
						break;
					case SDL_HAT_RIGHT:
						joyevent.JoystickEvent.POV=9000;
						break;
					case SDL_HAT_RIGHTDOWN:
						joyevent.JoystickEvent.POV=13500;
						break;
					case SDL_HAT_DOWN:
						joyevent.JoystickEvent.POV=18000;
						break;
					case SDL_HAT_LEFTDOWN:
						joyevent.JoystickEvent.POV=22500;
						break;
					case SDL_HAT_LEFT:
						joyevent.JoystickEvent.POV=27000;
						break;
					case SDL_HAT_LEFTUP:
						joyevent.JoystickEvent.POV=31500;
						break;
					case SDL_HAT_CENTERED:
					default:
						joyevent.JoystickEvent.POV=65535;
						break;
				}
			}
			else
			{
				joyevent.JoystickEvent.POV=65535;
			}

			// we map the number directly
			joyevent.JoystickEvent.Joystick=static_cast<u8>(i);
			// now post the event
			postEventFromUser(joyevent);
			// and close the joystick
		}
	}
#endif
	return !Close;
}

//! Activate any joysticks, and generate events for them.
bool CIrrDeviceSDL2::activateJoysticks(core::array<SJoystickInfo> & joystickInfo)
{
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	joystickInfo.clear();

	// we can name up to 256 different joysticks
	const int numJoysticks = core::min_(SDL_NumJoysticks(), 256);
	Joysticks.reallocate(numJoysticks);
	joystickInfo.reallocate(numJoysticks);

	int joystick = 0;
	for (; joystick<numJoysticks; ++joystick)
	{
		Joysticks.push_back(SDL_JoystickOpen(joystick));
		SJoystickInfo info;

		info.Joystick = joystick;
		info.Axes = SDL_JoystickNumAxes(Joysticks[joystick]);
		info.Buttons = SDL_JoystickNumButtons(Joysticks[joystick]);
		info.Name = SDL_JoystickName(Joysticks[joystick]);
		info.PovHat = (SDL_JoystickNumHats(Joysticks[joystick]) > 0)
						? SJoystickInfo::POV_HAT_PRESENT : SJoystickInfo::POV_HAT_ABSENT;

		joystickInfo.push_back(info);
	}

	for(joystick = 0; joystick < (int)joystickInfo.size(); ++joystick)
	{
		char logString[256];
		(void)sprintf(logString, "Found joystick %d, %d axes, %d buttons '%s'",
		joystick, joystickInfo[joystick].Axes,
		joystickInfo[joystick].Buttons, joystickInfo[joystick].Name.c_str());
		os::Printer::log(logString, ELL_INFORMATION);
	}

	return true;

#endif // _IRR_COMPILE_WITH_JOYSTICK_EVENTS_

	return false;
}



//! pause execution temporarily
void CIrrDeviceSDL2::yield()
{
	SDL_Delay(0);
}


//! pause execution for a specified time
void CIrrDeviceSDL2::sleep(u32 timeMs, bool pauseTimer)
{
	const bool wasStopped = Timer ? Timer->isStopped() : true;
	if (pauseTimer && !wasStopped)
		Timer->stop();

	SDL_Delay(timeMs);

	if (pauseTimer && !wasStopped)
		Timer->start();
}


//! sets the caption of the window
void CIrrDeviceSDL2::setWindowCaption(const wchar_t* text)
{
	core::stringc textc = text;
	if (ScreenWindow) {
		SDL_SetWindowTitle(ScreenWindow, textc.c_str());
	}
}


//! presents a surface in the client area
bool CIrrDeviceSDL2::present(video::IImage* surface, void* windowId, core::rect<s32>* srcClip)
{
	if (!SoftwareRendered)
		return true;

	SDL_Surface *sdlSurface = SDL_CreateRGBSurfaceFrom(
			surface->getData(), surface->getDimension().Width, surface->getDimension().Height,
			surface->getBitsPerPixel(), surface->getPitch(),
			surface->getRedMask(), surface->getGreenMask(), surface->getBlueMask(), surface->getAlphaMask());

	void* pixels;
	int pitch;
	if (SDL_LockTexture(ScreenTexture, NULL, &pixels, &pitch)) {
		os::Printer::log("Failed to lock surface texture", ELL_ERROR);
		return false;
	}

	SDL_Surface *copyto = SDL_CreateRGBSurfaceWithFormatFrom(
		pixels, Width, Height, 32, pitch, SDL_PIXELFORMAT_ARGB8888);


	if (!sdlSurface)
		return false;

	if (!copyto) {
		os::Printer::log("Failed to create copyto surface", ELL_ERROR);
		return false;
	}

	SDL_BlitSurface(sdlSurface, NULL, copyto, NULL);

	SDL_UnlockTexture(ScreenTexture);
	SDL_RenderCopy(ScreenRenderer, ScreenTexture, NULL, NULL);
	SDL_RenderPresent(ScreenRenderer);
	SDL_FreeSurface(copyto);
	SDL_FreeSurface(sdlSurface);
	return true;
}


//! notifies the device that it should close itself
void CIrrDeviceSDL2::closeDevice()
{
	Close = true;
}


//! \return Pointer to a list with all video modes supported
video::IVideoModeList* CIrrDeviceSDL2::getVideoModeList()
{
	// FIXME: How do we treat multi monitors?
	if (!VideoModeList->getVideoModeCount())
	{
		// enumerate video modes.
		int modes = SDL_GetNumDisplayModes(0);
		SDL_DisplayMode m;
		if (modes < 0) {
			// FIXME: Bailout
			return NULL;
		}
		for (int i = 0; i != modes; i++)
		{
			int r = SDL_GetDisplayMode(0, i, &m);
			if (!r)
			{
				VideoModeList->addMode(core::dimension2d<u32>(m.w, m.h), SDL_BITSPERPIXEL(m.format));
			}
		}
	}

	return VideoModeList;
}


//! Sets if the window should be resizable in windowed mode.
void CIrrDeviceSDL2::setResizable(bool resize)
{
	if (ScreenWindow && resize != Resizable)
	{
		SDL_SetWindowResizable(ScreenWindow, (resize?SDL_TRUE:SDL_FALSE));
		Resizable = resize;
	}
}


//! Minimizes window if possible
void CIrrDeviceSDL2::minimizeWindow()
{
	if (ScreenWindow) {
		SDL_MinimizeWindow(ScreenWindow);
	}
}


//! Maximize window
void CIrrDeviceSDL2::maximizeWindow()
{
	if (ScreenWindow) {
		SDL_MaximizeWindow(ScreenWindow);
	}
}

//! Get the position of this window on screen
core::position2di CIrrDeviceSDL2::getWindowPosition()
{
    return core::position2di(-1, -1);
}


//! Restore original window size
void CIrrDeviceSDL2::restoreWindow()
{
	// do nothing
}

bool CIrrDeviceSDL2::isFullscreen() const
{
	return CIrrDeviceStub::isFullscreen();
}


//! returns if window is active. if not, nothing need to be drawn
bool CIrrDeviceSDL2::isWindowActive() const
{
	return (WindowHasFocus && !WindowMinimized);
}


//! returns if window has focus.
bool CIrrDeviceSDL2::isWindowFocused() const
{
	return WindowHasFocus;
}


//! returns if window is minimized.
bool CIrrDeviceSDL2::isWindowMinimized() const
{
	return WindowMinimized;
}


//! Set the current Gamma Value for the Display
bool CIrrDeviceSDL2::setGammaRamp( f32 red, f32 green, f32 blue, f32 brightness, f32 contrast )
{
	/*
	// todo: Gamma in SDL takes ints, what does Irrlicht use?
	return (SDL_SetGamma(red, green, blue) != -1);
	*/
	return false;
}

//! Get the current Gamma Value for the Display
bool CIrrDeviceSDL2::getGammaRamp( f32 &red, f32 &green, f32 &blue, f32 &brightness, f32 &contrast )
{
/*	brightness = 0.f;
	contrast = 0.f;
	return (SDL_GetGamma(&red, &green, &blue) != -1);*/
	return false;
}

//! returns color format of the window.
video::ECOLOR_FORMAT CIrrDeviceSDL2::getColorFormat() const
{
	if (ScreenSurface)
	{
		if (ScreenSurface->format->BitsPerPixel==16)
		{
			if (ScreenSurface->format->Amask != 0)
				return video::ECF_A1R5G5B5;
			else
				return video::ECF_R5G6B5;
		}
		else
		{
			if (ScreenSurface->format->Amask != 0)
				return video::ECF_A8R8G8B8;
			else
				return video::ECF_R8G8B8;
		}
	}
	else
		return CIrrDeviceStub::getColorFormat();
}


void CIrrDeviceSDL2::createKeyMap()
{
	// I don't know if this is the best method  to create
	// the lookuptable, but I'll leave it like that until
	// I find a better version.

	KeyMap.reallocate(105);

	// buttons missing

	KeyMap.push_back(SKeyMap(SDLK_BACKSPACE, KEY_BACK));
	KeyMap.push_back(SKeyMap(SDLK_TAB, KEY_TAB));
	KeyMap.push_back(SKeyMap(SDLK_CLEAR, KEY_CLEAR));
	KeyMap.push_back(SKeyMap(SDLK_RETURN, KEY_RETURN));

	// combined modifiers missing

	KeyMap.push_back(SKeyMap(SDLK_PAUSE, KEY_PAUSE));
	KeyMap.push_back(SKeyMap(SDLK_CAPSLOCK, KEY_CAPITAL));

	// asian letter keys missing

	KeyMap.push_back(SKeyMap(SDLK_ESCAPE, KEY_ESCAPE));

	// asian letter keys missing

	KeyMap.push_back(SKeyMap(SDLK_SPACE, KEY_SPACE));
	KeyMap.push_back(SKeyMap(SDLK_PAGEUP, KEY_PRIOR));
	KeyMap.push_back(SKeyMap(SDLK_PAGEDOWN, KEY_NEXT));
	KeyMap.push_back(SKeyMap(SDLK_END, KEY_END));
	KeyMap.push_back(SKeyMap(SDLK_HOME, KEY_HOME));
	KeyMap.push_back(SKeyMap(SDLK_LEFT, KEY_LEFT));
	KeyMap.push_back(SKeyMap(SDLK_UP, KEY_UP));
	KeyMap.push_back(SKeyMap(SDLK_RIGHT, KEY_RIGHT));
	KeyMap.push_back(SKeyMap(SDLK_DOWN, KEY_DOWN));

	// select missing
	KeyMap.push_back(SKeyMap(SDLK_PRINTSCREEN, KEY_PRINT));
	// execute missing
	KeyMap.push_back(SKeyMap(SDLK_PRINTSCREEN, KEY_SNAPSHOT));

	KeyMap.push_back(SKeyMap(SDLK_INSERT, KEY_INSERT));
	KeyMap.push_back(SKeyMap(SDLK_DELETE, KEY_DELETE));
	KeyMap.push_back(SKeyMap(SDLK_HELP, KEY_HELP));

	KeyMap.push_back(SKeyMap(SDLK_0, KEY_KEY_0));
	KeyMap.push_back(SKeyMap(SDLK_1, KEY_KEY_1));
	KeyMap.push_back(SKeyMap(SDLK_2, KEY_KEY_2));
	KeyMap.push_back(SKeyMap(SDLK_3, KEY_KEY_3));
	KeyMap.push_back(SKeyMap(SDLK_4, KEY_KEY_4));
	KeyMap.push_back(SKeyMap(SDLK_5, KEY_KEY_5));
	KeyMap.push_back(SKeyMap(SDLK_6, KEY_KEY_6));
	KeyMap.push_back(SKeyMap(SDLK_7, KEY_KEY_7));
	KeyMap.push_back(SKeyMap(SDLK_8, KEY_KEY_8));
	KeyMap.push_back(SKeyMap(SDLK_9, KEY_KEY_9));

	KeyMap.push_back(SKeyMap(SDLK_a, KEY_KEY_A));
	KeyMap.push_back(SKeyMap(SDLK_b, KEY_KEY_B));
	KeyMap.push_back(SKeyMap(SDLK_c, KEY_KEY_C));
	KeyMap.push_back(SKeyMap(SDLK_d, KEY_KEY_D));
	KeyMap.push_back(SKeyMap(SDLK_e, KEY_KEY_E));
	KeyMap.push_back(SKeyMap(SDLK_f, KEY_KEY_F));
	KeyMap.push_back(SKeyMap(SDLK_g, KEY_KEY_G));
	KeyMap.push_back(SKeyMap(SDLK_h, KEY_KEY_H));
	KeyMap.push_back(SKeyMap(SDLK_i, KEY_KEY_I));
	KeyMap.push_back(SKeyMap(SDLK_j, KEY_KEY_J));
	KeyMap.push_back(SKeyMap(SDLK_k, KEY_KEY_K));
	KeyMap.push_back(SKeyMap(SDLK_l, KEY_KEY_L));
	KeyMap.push_back(SKeyMap(SDLK_m, KEY_KEY_M));
	KeyMap.push_back(SKeyMap(SDLK_n, KEY_KEY_N));
	KeyMap.push_back(SKeyMap(SDLK_o, KEY_KEY_O));
	KeyMap.push_back(SKeyMap(SDLK_p, KEY_KEY_P));
	KeyMap.push_back(SKeyMap(SDLK_q, KEY_KEY_Q));
	KeyMap.push_back(SKeyMap(SDLK_r, KEY_KEY_R));
	KeyMap.push_back(SKeyMap(SDLK_s, KEY_KEY_S));
	KeyMap.push_back(SKeyMap(SDLK_t, KEY_KEY_T));
	KeyMap.push_back(SKeyMap(SDLK_u, KEY_KEY_U));
	KeyMap.push_back(SKeyMap(SDLK_v, KEY_KEY_V));
	KeyMap.push_back(SKeyMap(SDLK_w, KEY_KEY_W));
	KeyMap.push_back(SKeyMap(SDLK_x, KEY_KEY_X));
	KeyMap.push_back(SKeyMap(SDLK_y, KEY_KEY_Y));
	KeyMap.push_back(SKeyMap(SDLK_z, KEY_KEY_Z));

        // TODO:
	//KeyMap.push_back(SKeyMap(SDLK_LSUPER, KEY_LWIN));
        // TODO:
	//KeyMap.push_back(SKeyMap(SDLK_RSUPER, KEY_RWIN));
	// apps missing
	KeyMap.push_back(SKeyMap(SDLK_POWER, KEY_SLEEP)); //??

	KeyMap.push_back(SKeyMap(SDLK_KP_0, KEY_NUMPAD0));
	KeyMap.push_back(SKeyMap(SDLK_KP_1, KEY_NUMPAD1));
	KeyMap.push_back(SKeyMap(SDLK_KP_2, KEY_NUMPAD2));
	KeyMap.push_back(SKeyMap(SDLK_KP_3, KEY_NUMPAD3));
	KeyMap.push_back(SKeyMap(SDLK_KP_4, KEY_NUMPAD4));
	KeyMap.push_back(SKeyMap(SDLK_KP_5, KEY_NUMPAD5));
	KeyMap.push_back(SKeyMap(SDLK_KP_6, KEY_NUMPAD6));
	KeyMap.push_back(SKeyMap(SDLK_KP_7, KEY_NUMPAD7));
	KeyMap.push_back(SKeyMap(SDLK_KP_8, KEY_NUMPAD8));
	KeyMap.push_back(SKeyMap(SDLK_KP_9, KEY_NUMPAD9));
	KeyMap.push_back(SKeyMap(SDLK_KP_MULTIPLY, KEY_MULTIPLY));
	KeyMap.push_back(SKeyMap(SDLK_KP_PLUS, KEY_ADD));
//	KeyMap.push_back(SKeyMap(SDLK_KP_, KEY_SEPARATOR));
	KeyMap.push_back(SKeyMap(SDLK_KP_MINUS, KEY_SUBTRACT));
	KeyMap.push_back(SKeyMap(SDLK_KP_PERIOD, KEY_DECIMAL));
	KeyMap.push_back(SKeyMap(SDLK_KP_DIVIDE, KEY_DIVIDE));

	KeyMap.push_back(SKeyMap(SDLK_F1,  KEY_F1));
	KeyMap.push_back(SKeyMap(SDLK_F2,  KEY_F2));
	KeyMap.push_back(SKeyMap(SDLK_F3,  KEY_F3));
	KeyMap.push_back(SKeyMap(SDLK_F4,  KEY_F4));
	KeyMap.push_back(SKeyMap(SDLK_F5,  KEY_F5));
	KeyMap.push_back(SKeyMap(SDLK_F6,  KEY_F6));
	KeyMap.push_back(SKeyMap(SDLK_F7,  KEY_F7));
	KeyMap.push_back(SKeyMap(SDLK_F8,  KEY_F8));
	KeyMap.push_back(SKeyMap(SDLK_F9,  KEY_F9));
	KeyMap.push_back(SKeyMap(SDLK_F10, KEY_F10));
	KeyMap.push_back(SKeyMap(SDLK_F11, KEY_F11));
	KeyMap.push_back(SKeyMap(SDLK_F12, KEY_F12));
	KeyMap.push_back(SKeyMap(SDLK_F13, KEY_F13));
	KeyMap.push_back(SKeyMap(SDLK_F14, KEY_F14));
	KeyMap.push_back(SKeyMap(SDLK_F15, KEY_F15));
	// no higher F-keys

        // TODO:
	//KeyMap.push_back(SKeyMap(SDLK_NUMLOCK, KEY_NUMLOCK));
	KeyMap.push_back(SKeyMap(SDLK_SCROLLLOCK, KEY_SCROLL));
	KeyMap.push_back(SKeyMap(SDLK_LSHIFT, KEY_LSHIFT));
	KeyMap.push_back(SKeyMap(SDLK_RSHIFT, KEY_RSHIFT));
	KeyMap.push_back(SKeyMap(SDLK_LCTRL,  KEY_LCONTROL));
	KeyMap.push_back(SKeyMap(SDLK_RCTRL,  KEY_RCONTROL));
	KeyMap.push_back(SKeyMap(SDLK_LALT,  KEY_LMENU));
	KeyMap.push_back(SKeyMap(SDLK_RALT,  KEY_RMENU));

	KeyMap.push_back(SKeyMap(SDLK_PLUS,   KEY_PLUS));
	KeyMap.push_back(SKeyMap(SDLK_COMMA,  KEY_COMMA));
	KeyMap.push_back(SKeyMap(SDLK_MINUS,  KEY_MINUS));
	KeyMap.push_back(SKeyMap(SDLK_PERIOD, KEY_PERIOD));

	// some special keys missing

	KeyMap.sort();
}

void CIrrDeviceSDL2::resizeWindow(u32 x, u32 y)
{
	if (ScreenWindow)
	{
		SDL_SetWindowSize(ScreenWindow, x, y);
	}
	if (ScreenTexture)
	{
		SDL_DestroyTexture(ScreenTexture);
		ScreenTexture = NULL;
	}
	if (ScreenRenderer) {
		ScreenTexture = SDL_CreateTexture(ScreenRenderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, Width, Height);
	}
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_SDL2_DEVICE_

