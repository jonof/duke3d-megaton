// SDL interface layer
// for the Build Engine
// by Jonathon Fowler (jf@jonof.id.au)
//
// Use SDL1.2 from http://www.libsdl.org

#define WIN32_LEAN_AND_MEAN

#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "SDL.h"
#include "compat.h"
#include "sdlayer.h"
#include "cache1d.h"
#include "pragmas.h"
#include "a.h"
#include "build.h"
#include "osd.h"
#include "duke3d.h"

#ifdef USE_OPENGL
# include "glbuild.h"
# include "gui.h"
#endif

#include "csteam.h"
#ifdef _WIN32
#include "wintools.h"
#endif

#include "dnAPI.h"
#include "smpeg/smpeg.h"

#if 1

typedef struct {
    double x, y;
} vector2;

vector2 pointer = { 0, 0 };
vector2 sensitivity = { 10.0, 13.0 };
vector2 max_velocity = { 1000.0, 1000.0 };

#endif

int skip_next_motion = 0;

int startwin_open(void) { return 0; }
int startwin_close(void) { return 0; }
int startwin_puts(const char *s) { s=s; return 0; }
int startwin_idle(void *s) { return 0; }
int startwin_settitle(const char *s) { s=s; return 0; }
int startwin_run(void) {
    return 1;
}

#define SURFACE_FLAGS	(SDL_SWSURFACE|SDL_HWPALETTE|SDL_HWACCEL)

// undefine to restrict windowed resolutions to conventional sizes
#define ANY_WINDOWED_SIZE

int   _buildargc = 1;
const char **_buildargv = NULL;
extern long app_main(long argc, char *argv[]);

char quitevent=0, appactive=1;

// video
static SDL_Surface *sdl_surface;
long xres=-1, yres=-1, bpp=0, fullscreen=0, bytesperline, imageSize;
long frameplace=0, lockcount=0;
char modechange=1;
char offscreenrendering=0;
char videomodereset = 0;
static unsigned short sysgamma[3][256];
extern long curbrightness, gammabrightness;

#ifdef USE_OPENGL
// OpenGL stuff
static char nogl=0;
#endif

// input
char inputdevices=0;
char keystatus[256], keyfifo[KEYFIFOSIZ], keyfifoplc, keyfifoend;
unsigned char keyasciififo[KEYFIFOSIZ], keyasciififoplc, keyasciififoend;
static unsigned char keynames[256][24];
long mousex=0,mousey=0,mouseb=0;
long *joyaxis = NULL, joyb=0, *joyhat = NULL;
char joyisgamepad=0, joynumaxes=0, joynumbuttons=0, joynumhats=0;
long joyaxespresent=0;


void (*keypresscallback)(long,long) = 0;
void (*mousepresscallback)(long,long) = 0;
void (*joypresscallback)(long,long) = 0;

static unsigned char keytranslation[SDLK_LAST];
static int buildkeytranslationtable(void);

//static SDL_Surface * loadtarga(const char *fn);		// for loading the icon
static SDL_Surface * loadappicon(void);


int osx_msgbox(char* name, char* buf){
    return 1;
}

int osx_ynbox(char* name, char* buf){
    return 1;
}



int wm_msgbox(char *name, char *fmt, ...)
{
	char buf[1000];
	va_list va;

	va_start(va,fmt);
	vsprintf(buf,fmt,va);
	va_end(va);

#if defined(__APPLE__)
	return osx_msgbox(name, buf);
#elif defined HAVE_GTK2
	if (gtkbuild_msgbox(name, buf) >= 0) return 1;
#elif defined _WIN32 && !defined _DEBUG
	return Win32_ShowErrorMessage(name, buf);
#endif
	puts(buf);
	puts("   (press Enter to continue)");
	getchar();

	return 0;
}

int wm_ynbox(char *name, char *fmt, ...)
{
	char buf[1000];
	char c;
	va_list va;

	va_start(va,fmt);
	vsprintf(buf,fmt,va);
	va_end(va);

#if defined __APPLE__
	return osx_ynbox(name, buf);
#elif defined HAVE_GTK2
	if ((r = gtkbuild_ynbox(name, buf)) >= 0) return r;
#endif
	puts(buf);
	puts("   (type 'Y' or 'N', and press Return or Enter to continue)");
	do c = getchar(); while (c != 'Y' && c != 'y' && c != 'N' && c != 'n');
	if (c == 'Y' || c == 'y') return 1;

	return 0;
}

void wm_setapptitle(char *name)
{
	if (name) {
		Bstrncpy(apptitle, name, sizeof(apptitle)-1);
		apptitle[ sizeof(apptitle)-1 ] = 0;
	}

	SDL_WM_SetCaption(apptitle, NULL);
	startwin_settitle(apptitle);
}


//
//
// ---------------------------------------
//
// System
//
// ---------------------------------------
//
//



int APIENTRY gluBuild2DMipmaps (
    GLenum      target, 
    GLint       components, 
    GLint       width, 
    GLint       height, 
    GLenum      format, 
    GLenum      type, 
    const void  *data);

void DrawIMG(SDL_Surface *img, int x, int y)
{
    GLuint texture;
    
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    
    glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,0);
    
    gluBuild2DMipmaps(GL_TEXTURE_2D, 4, img->w, img->h, GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);
    
    
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f( -1, -1,  0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(  1, -1,  0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(  1,  1,  0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f( -1,  1,  0.0f);
    glEnd();

    glDeleteTextures(1, &texture);
}

static
void smpeg_callback(SDL_Surface* dst, int x, int y,
                    unsigned int w, unsigned int h) {
    
}


void play_video(const char * filename) {
    SMPEG *movie = NULL;
    SDL_Surface *movieSurface = 0;
    SMPEGstatus mpgStatus;
    SMPEG_Info movieInfo;
	char *error;
	int done;
    
    movie = SMPEG_new(filename, &movieInfo, true);
    
    error = SMPEG_error(movie);
    
    if( error != NULL || movie == NULL ) {
        printf( "Error loading MPEG: %s\n", error );
        return;
    }
    
    movieSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, 512, 512, 32, 0xFF, 0xFF00, 0xFF0000, 0);
    
    SMPEG_setdisplay(movie, movieSurface, 0, &smpeg_callback);
    SDL_ShowCursor(SDL_DISABLE);
    
    SMPEG_play(movie);
//    SMPEG_loop(movie, -1);
    SMPEG_getinfo(movie, &movieInfo);
    
    glEnable(GL_TEXTURE_2D);
	glClearColor(1, 0, 1, 1);

    glDisable(GL_DEPTH_TEST);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    
	glViewport(0, 0, ScreenWidth, ScreenHeight);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
    glOrtho(-1, 1, 1, -1, 0, 1);
    glScalef((16.0f/9.0f)/(ScreenWidth/(float)ScreenHeight), 1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
    
    done = 0;
    
    while(done == 0) {
        SDL_Event event;
        while (SDL_PollEvent(&event)){
            if (event.type == SDL_QUIT)  {
                done = 1;
            }
            if (event.type == SDL_KEYDOWN){
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_RETURN
                    || event.key.keysym.sym == SDLK_SPACE) {
                    done = 1;
                }
            }
        }
        glClear(GL_COLOR_BUFFER_BIT);
        
        DrawIMG(movieSurface, 0, 0);
        mpgStatus = SMPEG_status(movie);
        if(mpgStatus != SMPEG_PLAYING) {
            done = 1;
        }
        SDL_GL_SwapBuffers();
    
    }

    SMPEG_stop(movie);
    SMPEG_delete(movie);
    movie = NULL;
    SDL_FreeSurface(movieSurface);
    //SDL_ShowCursor(SDL_ENABLE);
}


int main(int argc, char *argv[])
{
	int r;
	
	if (!CSTEAM_Init()) {
		wm_msgbox("Error", "Could not initialize Steam. Please check that your Steam client is up and running.");
		return -1;
	}
	buildkeytranslationtable();
	
#ifdef HAVE_GTK2
	gtkbuild_init(&argc, &argv);
#endif
	startwin_open();

	_buildargc = argc;
	_buildargv = (const char **)argv;
    
	baselayer_init();
	r = app_main(argc, argv);

	startwin_close();
#ifdef HAVE_GTK2
	gtkbuild_exit(r);
#endif
	return r;
}


//
// initsystem() -- init SDL systems
//
int initsystem(void)
{
	const SDL_version *linked = SDL_Linked_Version();
	SDL_version compiled;
	char drvname[32];

	SDL_VERSION(&compiled);

	initprintf("Initialising SDL system interface "
		  "(compiled with SDL version %d.%d.%d, DLL version %d.%d.%d)\n",
		linked->major, linked->minor, linked->patch,
		compiled.major, compiled.minor, compiled.patch);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER
#ifdef NOSDLPARACHUTE
			| SDL_INIT_NOPARACHUTE
#endif
		)) {
		initprintf("Initialisation failed! (%s)\n", SDL_GetError());
		return -1;
	}

    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(SDL_DISABLE);
	atexit(uninitsystem);

	frameplace = 0;
	lockcount = 0;

#ifdef USE_OPENGL
	if (loadgldriver(getenv("BUILD_GLDRV"))) {
		initprintf("Failed loading OpenGL driver. GL modes will be unavailable.\n");
		nogl = 1;
	}
#endif

/*
#ifndef __APPLE__
	{
		SDL_Surface *icon;
		//icon = loadtarga("icon.tga");
		icon = loadappicon();
		if (icon) {
			SDL_WM_SetIcon(icon, 0);
			SDL_FreeSurface(icon);
		}
	}
#endif
*/

	if (SDL_VideoDriverName(drvname, 32))
		initprintf("Using \"%s\" video driver\n", drvname);

	// dump a quick summary of the graphics hardware
#ifdef DEBUGGINGAIDS
	vid = SDL_GetVideoInfo();
	initprintf("Video device information:\n");
	initprintf("  Can create hardware surfaces?          %s\n", (vid->hw_available)?"Yes":"No");
	initprintf("  Window manager available?              %s\n", (vid->wm_available)?"Yes":"No");
	initprintf("  Accelerated hardware blits?            %s\n", (vid->blit_hw)?"Yes":"No");
	initprintf("  Accelerated hardware colourkey blits?  %s\n", (vid->blit_hw_CC)?"Yes":"No");
	initprintf("  Accelerated hardware alpha blits?      %s\n", (vid->blit_hw_A)?"Yes":"No");
	initprintf("  Accelerated software blits?            %s\n", (vid->blit_sw)?"Yes":"No");
	initprintf("  Accelerated software colourkey blits?  %s\n", (vid->blit_sw_CC)?"Yes":"No");
	initprintf("  Accelerated software alpha blits?      %s\n", (vid->blit_sw_A)?"Yes":"No");
	initprintf("  Accelerated colour fills?              %s\n", (vid->blit_fill)?"Yes":"No");
	initprintf("  Total video memory:                    %dKB\n", vid->video_mem);
#endif

	return 0;
}


//
// uninitsystem() -- uninit SDL systems
//
void uninitsystem(void)
{
	uninitinput();
	uninitmouse();
	uninittimer();

	SDL_Quit();

#ifdef USE_OPENGL
	unloadgldriver();
#endif
}


//
// initprintf() -- prints a string to the intitialization window
//
void initprintf(const char *f, ...)
{
	va_list va;
	char buf[1024];
	
	va_start(va, f);
	vprintf(f,va);
	va_end(va);

	va_start(va, f);
	Bvsnprintf(buf, 1024, f, va);
	va_end(va);
	OSD_Printf(buf);

	startwin_puts(buf);
	startwin_idle(NULL);
}


//
// debugprintf() -- prints a debug string to stderr
//
void debugprintf(const char *f, ...)
{
#ifdef DEBUGGINGAIDS
	va_list va;

	va_start(va,f);
	Bvfprintf(stderr, f, va);
	va_end(va);
#endif
}


//
//
// ---------------------------------------
//
// All things Input
//
// ---------------------------------------
//
//

static char mouseacquired=0,moustat=0;
static long joyblast=0;
static SDL_Joystick *joydev = NULL;

//
// initinput() -- init input system
//
int initinput(void)
{
	int i,j;
	
#ifdef __APPLE__
	// force OS X to operate in >1 button mouse mode so that LMB isn't adulterated
	if (!getenv("SDL_HAS3BUTTONMOUSE")) putenv("SDL_HAS3BUTTONMOUSE=1");
#endif
	
	if (SDL_EnableKeyRepeat(250, 30)) initprintf("Error enabling keyboard repeat.\n");
	inputdevices = 1|2;	// keyboard (1) and mouse (2)
	mouseacquired = 0;

	SDL_EnableUNICODE(1);	// let's hope this doesn't hit us too hard

	memset(keynames,0,sizeof(keynames));
	for (i=0; i<SDLK_LAST; i++) {
		if (!keytranslation[i]) continue;
		strncpy((char *)keynames[ keytranslation[i] ], SDL_GetKeyName(i), sizeof(keynames[i])-1);
	}

	if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
		i = SDL_NumJoysticks();
		initprintf("%d joystick(s) found\n",i);
		for (j=0;j<i;j++) initprintf("  %d. %s\n", j+1, SDL_JoystickName(j));
		joydev = SDL_JoystickOpen(0);
		if (joydev) {
			SDL_JoystickEventState(SDL_ENABLE);
			inputdevices |= 4;

			joynumaxes    = SDL_JoystickNumAxes(joydev);
			joynumbuttons = min(32,SDL_JoystickNumButtons(joydev));
			joynumhats    = SDL_JoystickNumHats(joydev);
			initprintf("Joystick 1 has %d axes, %d buttons, and %d hat(s).\n",
				joynumaxes,joynumbuttons,joynumhats);

			joyaxis = (long *)Bcalloc(joynumaxes, sizeof(long));
			joyhat = (long *)Bcalloc(joynumhats, sizeof(long));
		}
	}

	return 0;
}

//
// uninitinput() -- uninit input system
//
void uninitinput(void)
{
	uninitmouse();

	if (joydev) {
		SDL_JoystickClose(joydev);
		joydev = NULL;
	}
}

const unsigned char *getkeyname(int num)
{
	if ((unsigned)num >= 256) return NULL;
	return keynames[num];
}

const unsigned char *getjoyname(int what, int num)
{
	static char tmp[64];

	switch (what) {
		case 0:	// axis
			if ((unsigned)num > (unsigned)joynumaxes) return NULL;
			sprintf(tmp,"Axis %d",num);
			return (unsigned char *)tmp;

		case 1: // button
			if ((unsigned)num > (unsigned)joynumbuttons) return NULL;
			sprintf(tmp,"Button %d",num);
			return (unsigned char *)tmp;

		case 2: // hat
			if ((unsigned)num > (unsigned)joynumhats) return NULL;
			sprintf(tmp,"Hat %d",num);
			return (unsigned char *)tmp;

		default:
			return NULL;
	}
}

//
// bgetchar, bkbhit, bflushchars -- character-based input functions
//
unsigned char bgetchar(void)
{
	unsigned char c;
	if (keyasciififoplc == keyasciififoend) return 0;
	c = keyasciififo[keyasciififoplc];
	keyasciififoplc = ((keyasciififoplc+1)&(KEYFIFOSIZ-1));
	return c;
}

int bkbhit(void)
{
	return (keyasciififoplc != keyasciififoend);
}

void bflushchars(void)
{
	keyasciififoplc = keyasciififoend = 0;
}


//
// set{key|mouse|joy}presscallback() -- sets a callback which gets notified when keys are pressed
//
void setkeypresscallback(void (*callback)(long, long)) { keypresscallback = callback; }
void setmousepresscallback(void (*callback)(long, long)) { mousepresscallback = callback; }
void setjoypresscallback(void (*callback)(long, long)) { joypresscallback = callback; }

//
// initmouse() -- init mouse input
//
int initmouse(void)
{
	moustat=1;
	grabmouse(1);
	return 0;
}

//
// uninitmouse() -- uninit mouse input
//
void uninitmouse(void)
{
	grabmouse(0);
	moustat=0;
}


//
// grabmouse() -- show/hide mouse cursor
//
void grabmouse(char a)
{
	if (appactive && moustat) {
		if (a != mouseacquired) {
#ifndef DEBUGGINGAIDS
			SDL_GrabMode g;
			
			g = SDL_WM_GrabInput( a ? SDL_GRAB_ON : SDL_GRAB_OFF );
			mouseacquired = (g == SDL_GRAB_ON);

			SDL_ShowCursor(mouseacquired ? SDL_DISABLE : SDL_ENABLE);
#else
			mouseacquired = a;
#endif
		}
	} else {
		mouseacquired = a;
	}
	mousex = mousey = 0;
	pointer.x = pointer.y = 0;
}


//
// readmousexy() -- return mouse motion information
//
void readmousexy(long *x, long *y)
{
	if (!mouseacquired || !appactive || !moustat) { *x = *y = 0; return; }
	*x = mousex;
	*y = mousey;
	mousex = mousey = 0;
}

//
// readmousebstatus() -- return mouse button information
//
void readmousebstatus(long *b)
{
	if (!mouseacquired || !appactive || !moustat) *b = 0;
	else {
        *b = mouseb;
    }
}

//
// setjoydeadzone() -- sets the dead and saturation zones for the joystick
//
void setjoydeadzone(int axis, unsigned short dead, unsigned short satur)
{
}


//
// getjoydeadzone() -- gets the dead and saturation zones for the joystick
//
void getjoydeadzone(int axis, unsigned short *dead, unsigned short *satur)
{
	*dead = *satur = 0;
}


//
// releaseallbuttons()
//
void releaseallbuttons(void)
{
}


//
//
// ---------------------------------------
//
// All things Timer
// Ken did this
//
// ---------------------------------------
//
//

#ifdef _WIN32

Uint32 WIN_GetTicks() {
	return (Uint32)Win32_GetQPC();
}

#define SDL_GetTicks() WIN_GetTicks()

#endif

static Uint32 timerfreq=0;
static Uint32 timerlastsample=0;
static Uint32 timerticspersec=0;
static void (*usertimercallback)(void) = NULL;

//
// inittimer() -- initialise timer
//
int inittimer(int tickspersecond)
{
	if (timerfreq) return 0;	// already installed

#ifdef _WIN32
	Win32_InitQPC();
#endif

	initprintf("Initialising timer\n");

	timerfreq = 1000;
	timerticspersec = tickspersecond;
	timerlastsample = SDL_GetTicks() * timerticspersec / timerfreq;

	usertimercallback = NULL;

	return 0;
}

//
// uninittimer() -- shut down timer
//
void uninittimer(void)
{
	if (!timerfreq) return;

	timerfreq=0;
}

//
// sampletimer() -- update totalclock
//
void sampletimer(void)
{
	Uint32 i;
	long n;
	
	if (!timerfreq) return;
	
	i = SDL_GetTicks();
	n = (long)(i * timerticspersec / timerfreq) - timerlastsample;
	if (n>0) {
		totalclock += n;
		timerlastsample += n;
	}

	if (usertimercallback) for (; n>0; n--) usertimercallback();
}

//
// getticks() -- returns a millisecond ticks count
//
unsigned long getticks(void)
{
	return (unsigned long)SDL_GetTicks();
}

//
// getusecticks() -- returns a microsecond ticks count
//
unsigned long getusecticks(void)
{
#ifdef _WIN32
	return (unsigned long)(Win32_GetQPC() * 1000.0);
#else
	return (unsigned long)SDL_GetTicks() * 1000;
#endif
}


//
// gettimerfreq() -- returns the number of ticks per second the timer is configured to generate
//
int gettimerfreq(void)
{
	return timerticspersec;
}


//
// installusertimercallback() -- set up a callback function to be called when the timer is fired
//
void (*installusertimercallback(void (*callback)(void)))(void)
{
	void (*oldtimercallback)(void);

	oldtimercallback = usertimercallback;
	usertimercallback = callback;

	return oldtimercallback;
}



//
//
// ---------------------------------------
//
// All things Video
//
// ---------------------------------------
//
// 


//
// getvalidmodes() -- figure out what video modes are available
//
static int sortmodes(const struct validmode_t *a, const struct validmode_t *b)
{
	int x;

	if ((x = a->fs   - b->fs)   != 0) return x;
	if ((x = a->bpp  - b->bpp)  != 0) return x;
	if ((x = a->xdim - b->xdim) != 0) return x;
	if ((x = a->ydim - b->ydim) != 0) return x;

	return 0;
}
static char modeschecked=0;
void getvalidmodes(void)
{
	static int cdepths[] = {
#ifdef USE_SWRENDER
		8,
#endif
#ifdef USE_OPENGL
		24,32,
#endif
		0 };
	static int defaultres[][2] = {
		{1280,1024},{1280,960},{1280,800},{1280,720},{1152,864},{1024,768},{800,600},{640,480},
		{640,400},{512,384},{480,360},{400,300},{320,240},{320,200},{0,0}
	};
	SDL_Rect **modes;
	SDL_PixelFormat pf = { NULL, 8, 1, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0 };
	int i, j, maxx=0, maxy=0;

	if (modeschecked) return;

	validmodecnt=0;
	initprintf("Detecting video modes:\n");

#define ADDMODE(x,y,c,f) if (validmodecnt<MAXVALIDMODES) { \
	int mn; \
	for(mn=0;mn<validmodecnt;mn++) \
		if (validmode[mn].xdim==x && validmode[mn].ydim==y && \
		    validmode[mn].bpp==c  && validmode[mn].fs==f) break; \
	if (mn==validmodecnt) { \
		validmode[validmodecnt].xdim=x; \
		validmode[validmodecnt].ydim=y; \
		validmode[validmodecnt].bpp=c; \
		validmode[validmodecnt].fs=f; \
		validmodecnt++; \
		initprintf("  - %dx%d %d-bit %s\n", x, y, c, (f&1)?"fullscreen":"windowed"); \
	} \
}	

#define CHECK(w,h) if ((w < maxx) && (h < maxy))

	// do fullscreen modes first
	for (j=0; cdepths[j]; j++) {
#ifdef USE_OPENGL
		if (nogl && cdepths[j] > 8) continue;
#endif
		pf.BitsPerPixel = cdepths[j];
		pf.BytesPerPixel = cdepths[j] >> 3;

		modes = SDL_ListModes(&pf, SURFACE_FLAGS | SDL_FULLSCREEN);
		if (modes == (SDL_Rect **)0) {
			if (cdepths[j] > 8) cdepths[j] = -1;
			continue;
		}

		if (modes == (SDL_Rect **)-1) {
			for (i=0; defaultres[i][0]; i++)
				ADDMODE(defaultres[i][0],defaultres[i][1],cdepths[j],1)
		} else {
			for (i=0; modes[i]; i++) {
				if ((modes[i]->w > MAXXDIM) || (modes[i]->h > MAXYDIM)) continue;

				ADDMODE(modes[i]->w, modes[i]->h, cdepths[j], 1)

				if ((modes[i]->w > maxx) && (modes[i]->h > maxy)) {
					maxx = modes[i]->w;
					maxy = modes[i]->h;
				}
			}
		}
	}

	if (maxx == 0 && maxy == 0) {
		initprintf("No fullscreen modes available!\n");
		maxx = MAXXDIM; maxy = MAXYDIM;
	}

	// add windowed modes next
	for (j=0; cdepths[j]; j++) {
#ifdef USE_OPENGL
		if (nogl && cdepths[j] > 8) continue;
#endif
		if (cdepths[j] < 0) continue;
		for (i=0; defaultres[i][0]; i++)
			CHECK(defaultres[i][0],defaultres[i][1])
				ADDMODE(defaultres[i][0],defaultres[i][1],cdepths[j],0)
	}

#undef CHECK
#undef ADDMODE

	qsort((void*)validmode, validmodecnt, sizeof(struct validmode_t), (int(*)(const void*,const void*))sortmodes);

	modeschecked=1;
}


//
// checkvideomode() -- makes sure the video mode passed is legal
//
int checkvideomode(int *x, int *y, int c, int fs, int forced)
{
	int i, nearest=-1, dx, dy, odx=9999, ody=9999;

	getvalidmodes();

	if (c>8
#ifdef USE_OPENGL
	    && nogl
#endif
	   ) return -1;

	// fix up the passed resolution values to be multiples of 8
	// and at least 320x200 or at most MAXXDIMxMAXYDIM
	if (*x < 320) *x = 320;
	if (*y < 200) *y = 200;
	if (*x > MAXXDIM) *x = MAXXDIM;
	if (*y > MAXYDIM) *y = MAXYDIM;
	*x &= 0xfffffff8l;

	for (i=0; i<validmodecnt; i++) {
		if (validmode[i].bpp != c) continue;
		if (validmode[i].fs != fs) continue;
		dx = klabs(validmode[i].xdim - *x);
		dy = klabs(validmode[i].ydim - *y);
		if (!(dx | dy)) { 	// perfect match
			nearest = i;
			break;
		}
		if ((dx <= odx) && (dy <= ody)) {
			nearest = i;
			odx = dx; ody = dy;
		}
	}
		
#ifdef ANY_WINDOWED_SIZE
	if (!forced && (fs&1) == 0 && (nearest < 0 || (validmode[nearest].xdim!=*x || validmode[nearest].ydim!=*y)))
		return 0x7fffffffl;
#endif

	if (nearest < 0) {
		// no mode that will match (eg. if no fullscreen modes)
		return -1;
	}

	*x = validmode[nearest].xdim;
	*y = validmode[nearest].ydim;

	return nearest;		// JBF 20031206: Returns the mode number
}


//
// setvideomode() -- set SDL video mode
//
int setvideomode(int x, int y, int c, int fs)
{
	int regrab = 0;
	
	if ((fs == fullscreen) && (x == xres) && (y == yres) && (c == bpp) &&
	    !videomodereset) {
		OSD_ResizeDisplay(xres,yres);
		return 0;
	}

	if (checkvideomode(&x,&y,c,fs,0) < 0) return -1;

	startwin_close();

	if (mouseacquired) {
		regrab = 1;
		grabmouse(0);
	}

	if (lockcount) while (lockcount) enddrawing();

#if defined(USE_OPENGL)
	if (bpp > 8 && sdl_surface) polymost_glreset();
#endif

	// restore gamma before we change video modes if it was changed
	if (sdl_surface && gammabrightness) {
		SDL_SetGammaRamp(sysgamma[0], sysgamma[1], sysgamma[2]);
		gammabrightness = 0;	// redetect on next mode switch
	}
	
#if defined(USE_OPENGL)
	if (c > 8) {
		int i, j, multisamplecheck = (glmultisample > 0);
		struct {
			SDL_GLattr attr;
			int value;
		} attributes[] = {
#if 1
			{ SDL_GL_RED_SIZE, 8 },
			{ SDL_GL_GREEN_SIZE, 8 },
			{ SDL_GL_BLUE_SIZE, 8 },
			{ SDL_GL_ALPHA_SIZE, 8 },
			{ SDL_GL_BUFFER_SIZE, c },
			{ SDL_GL_STENCIL_SIZE, 0 },
			{ SDL_GL_ACCUM_RED_SIZE, 0 },
			{ SDL_GL_ACCUM_GREEN_SIZE, 0 },
			{ SDL_GL_ACCUM_BLUE_SIZE, 0 },
			{ SDL_GL_ACCUM_ALPHA_SIZE, 0 },
			{ SDL_GL_DEPTH_SIZE, 24 },
#endif
			{ SDL_GL_DOUBLEBUFFER, 1 },
			{ SDL_GL_MULTISAMPLEBUFFERS, glmultisample > 0 },
			{ SDL_GL_MULTISAMPLESAMPLES, glmultisample },
			{ SDL_GL_SWAP_CONTROL, ud.vsync ? 1 : 0 },
		};

		if (nogl) return -1;
		
		initprintf("Setting video mode %dx%d (%d-bpp %s)\n",
				x,y,c, ((fs&1) ? "fullscreen" : "windowed"));
		do {
			for (i=0; i < (int)(sizeof(attributes)/sizeof(attributes[0])); i++) {
				j = attributes[i].value;
				if (!multisamplecheck &&
				    (attributes[i].attr == SDL_GL_MULTISAMPLEBUFFERS ||
				     attributes[i].attr == SDL_GL_MULTISAMPLESAMPLES)
				   ) {
					j = 0;
				}
				SDL_GL_SetAttribute(attributes[i].attr, j);
			}

			GUI_PreModeChange();
			sdl_surface = SDL_SetVideoMode(x, y, c, SDL_OPENGL | ((fs&1)?SDL_FULLSCREEN:0));
			if (!sdl_surface) {
				if (multisamplecheck) {
					initprintf("Multisample mode not possible. Retrying without multisampling.\n");
					glmultisample = 0;
					continue;
				}
				initprintf("Unable to set video mode!\n");
				return -1;
			}
			GUI_PostModeChange(x, y);
		} while (multisamplecheck--);
	} else
#endif
	{
		initprintf("Setting video mode %dx%d (%d-bpp %s)\n",
				x,y,c, ((fs&1) ? "fullscreen" : "windowed"));
		sdl_surface = SDL_SetVideoMode(x, y, c, SURFACE_FLAGS | ((fs&1)?SDL_FULLSCREEN:0));
		if (!sdl_surface) {
			initprintf("Unable to set video mode!\n");
			return -1;
		}
	}

#if 0
	{
	char flags[512] = "";
#define FLAG(x,y) if ((sdl_surface->flags & x) == x) { strcat(flags, y); strcat(flags, " "); }
	FLAG(SDL_HWSURFACE, "HWSURFACE") else
	FLAG(SDL_SWSURFACE, "SWSURFACE")
	FLAG(SDL_ASYNCBLIT, "ASYNCBLIT")
	FLAG(SDL_ANYFORMAT, "ANYFORMAT")
	FLAG(SDL_HWPALETTE, "HWPALETTE")
	FLAG(SDL_DOUBLEBUF, "DOUBLEBUF")
	FLAG(SDL_FULLSCREEN, "FULLSCREEN")
	FLAG(SDL_OPENGL, "OPENGL")
	FLAG(SDL_OPENGLBLIT, "OPENGLBLIT")
	FLAG(SDL_RESIZABLE, "RESIZABLE")
	FLAG(SDL_HWACCEL, "HWACCEL")
	FLAG(SDL_SRCCOLORKEY, "SRCCOLORKEY")
	FLAG(SDL_RLEACCEL, "RLEACCEL")
	FLAG(SDL_SRCALPHA, "SRCALPHA")
	FLAG(SDL_PREALLOC, "PREALLOC")
#undef FLAG
	initprintf("SDL Surface flags: %s\n", flags);
	}
#endif

	{
		//static char t[384];
		//sprintf(t, "%s (%dx%d %s)", apptitle, x, y, ((fs) ? "fullscreen" : "windowed"));
		SDL_WM_SetCaption(apptitle, 0);
	}

#ifdef USE_OPENGL
	if (c > 8) {
		polymost_glreset();
		baselayer_setupopengl();
	}
#endif
	
	xres = x;
	yres = y;
	bpp = c;
	fullscreen = fs;
	//bytesperline = sdl_surface->pitch;
	//imageSize = bytesperline*yres;
	numpages = c > 8 ? 2 : 1;
	frameplace = 0;
	lockcount = 0;
	modechange=1;
	videomodereset = 0;
	OSD_ResizeDisplay(xres,yres);
	
	// save the current system gamma to determine if gamma is available
	if (!gammabrightness) {
		float f = 1.0f + ((float)curbrightness / 10.0f);
		if (SDL_GetGammaRamp(sysgamma[0], sysgamma[1], sysgamma[2]) >= 0)
			gammabrightness = 1;

		// see if gamma really is working by trying to set the brightness
		if (gammabrightness && SDL_SetGamma(f,f,f) < 0)
			gammabrightness = 0;	// nope
	}

	// setpalettefade will set the palette according to whether gamma worked
	setpalettefade(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);
	
	//if (c==8) setpalette(0,256,0);
	//baselayer_onvideomodechange(c>8);
	if (regrab) grabmouse(1);

	return 0;
}


//
// resetvideomode() -- resets the video system
//
void resetvideomode(void)
{
	videomodereset = 1;
	modeschecked = 0;
}


//
// begindrawing() -- locks the framebuffer for drawing
//
void begindrawing(void)
{
	long i,j;

	if (bpp > 8) {
		if (offscreenrendering) return;
		frameplace = 0;
		bytesperline = 0;
		imageSize = 0;
		modechange = 0;
		return;
	}

	// lock the frame
	if (lockcount++ > 0)
		return;

	if (offscreenrendering) return;
	
	if (SDL_MUSTLOCK(sdl_surface)) SDL_LockSurface(sdl_surface);
	frameplace = (long)sdl_surface->pixels;
	
	if (sdl_surface->pitch != bytesperline || modechange) {
		bytesperline = sdl_surface->pitch;
		imageSize = bytesperline*yres;
		setvlinebpl(bytesperline);

		j = 0;
		for(i=0;i<=ydim;i++) ylookup[i] = j, j += bytesperline;
		modechange=0;
	}
}


//
// enddrawing() -- unlocks the framebuffer
//
void enddrawing(void)
{
	if (bpp > 8) {
		if (!offscreenrendering) frameplace = 0;
		return;
	}

	if (!frameplace) return;
	if (lockcount > 1) { lockcount--; return; }
	if (!offscreenrendering) frameplace = 0;
	if (lockcount == 0) return;
	lockcount = 0;

	if (offscreenrendering) return;

	if (SDL_MUSTLOCK(sdl_surface)) SDL_UnlockSurface(sdl_surface);
}


//
// showframe() -- update the display
//
void showframe(int w)
{

#ifdef USE_OPENGL
	if (bpp > 8) {
		if (palfadedelta) {
			bglMatrixMode(GL_PROJECTION);
			bglPushMatrix();
			bglLoadIdentity();
			bglMatrixMode(GL_MODELVIEW);
			bglPushMatrix();
			bglLoadIdentity();

			bglDisable(GL_DEPTH_TEST);
			bglDisable(GL_ALPHA_TEST);
			bglDisable(GL_TEXTURE_2D);

			bglEnable(GL_BLEND);
			bglColor4ub(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);

			bglBegin(GL_QUADS);
			bglVertex2i(-1, -1);
			bglVertex2i(1, -1);
			bglVertex2i(1, 1);
			bglVertex2i(-1, 1);
			bglEnd();
			
			bglMatrixMode(GL_MODELVIEW);
			bglPopMatrix();
			bglMatrixMode(GL_PROJECTION);
			bglPopMatrix();
		}

		GUI_Render();

		SDL_GL_SwapBuffers();
        if (ps[myconnectindex].gm&MODE_EOL) {
            clearview(0L);
        }
		return;
	}
#endif
	
	if (offscreenrendering) return;

	if (lockcount) {
		printf("Frame still locked %ld times when showframe() called.\n", lockcount);
		while (lockcount) enddrawing();
	}

	SDL_Flip(sdl_surface);
}


//
// setpalette() -- set palette values
//
int setpalette(int start, int num, char *dapal)
{
	SDL_Color pal[256];
	int i,n;

	if (bpp > 8) return 0;	// no palette in opengl

	copybuf(curpalettefaded, pal, 256);
	
	for (i=start, n=num; n>0; i++, n--) {
		/*
		pal[i].b = dapal[0] << 2;
		pal[i].g = dapal[1] << 2;
		pal[i].r = dapal[2] << 2;
		*/
		curpalettefaded[i].f = pal[i].unused = 0;
		dapal += 4;
	}

	//return SDL_SetPalette(sdl_surface, SDL_LOGPAL|SDL_PHYSPAL, pal, 0, 256);
	return SDL_SetColors(sdl_surface, pal, 0, 256);
}

//
// getpalette() -- get palette values
//
/*
int getpalette(int start, int num, char *dapal)
{
	int i;
	SDL_Palette *pal;

	// we shouldn't need to lock the surface to get the palette
	pal = sdl_surface->format->palette;

	for (i=num; i>0; i--, start++) {
		dapal[0] = pal->colors[start].b >> 2;
		dapal[1] = pal->colors[start].g >> 2;
		dapal[2] = pal->colors[start].r >> 2;
		dapal += 4;
	}

	return 1;
}
*/

//
// setgamma
//
int setgamma(float ro, float go, float bo)
{
	return SDL_SetGamma(ro,go,bo);
}

#ifndef __APPLE__
extern struct sdlappicon sdlappicon;
static SDL_Surface * loadappicon(void)
{
	SDL_Surface *surf;

	surf = SDL_CreateRGBSurfaceFrom((void*)sdlappicon.pixels,
			sdlappicon.width, sdlappicon.height, 32, sdlappicon.width*4,
			0xffl,0xff00l,0xff0000l,0xff000000l);

	return surf;
}
#endif

//
//
// ---------------------------------------
//
// Miscellany
//
// ---------------------------------------
//
// 

static
Uint32 WheelTimerCallback(Uint32 interval, void *param) {
    int32 button = (int32)param;
    mouseb &= ~(1<<button);
    printf("Release %d\n", button);
    return 0;
}

//
// handleevents() -- process the SDL message queue
//   returns !0 if there was an important event worth checking (like quitting)
//
int handleevents(void)
{
	int code, rv=0, j;
	SDL_Event ev;
	static int firstcall = 1;

#define SetKey(key,state) { \
	keystatus[key] = state; \
		if (state) { \
	keyfifo[keyfifoend] = key; \
	keyfifo[(keyfifoend+1)&(KEYFIFOSIZ-1)] = state; \
	keyfifoend = ((keyfifoend+2)&(KEYFIFOSIZ-1)); \
		} \
}

	while (SDL_PollEvent(&ev)) {
		if (GUI_InjectEvent(&ev)) {
			/* GUI ate the event, don't let it pass through */
            pointer.x = pointer.y = 0;
			continue;
		}
		switch (ev.type) {
#if 0

			case SDL_KEYDOWN:
			case SDL_KEYUP:
				code = keytranslation[ev.key.keysym.sym];

				if (ev.key.keysym.unicode != 0 && ev.key.type == SDL_KEYDOWN &&
				    (ev.key.keysym.unicode & 0xff80) == 0 &&
				    ((keyasciififoend+1)&(KEYFIFOSIZ-1)) != keyasciififoplc) {
					keyasciififo[keyasciififoend] = ev.key.keysym.unicode & 0x7f;
					keyasciififoend = ((keyasciififoend+1)&(KEYFIFOSIZ-1));
				}

				// hook in the osd
				if (OSD_HandleKey(code, (ev.key.type == SDL_KEYDOWN)) == 0)
					break;

				if (ev.key.type == SDL_KEYDOWN) {
					if (!keystatus[code]) {
						SetKey(code, 1);
						if (keypresscallback)
							keypresscallback(code, 1);
					}
				} else {
					SetKey(code, 0);
					if (keypresscallback)
						keypresscallback(code, 0);
				}
				break;
#else
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    keystatus[1] = 1;
                } else {
                    dnPressKey(ev.key.keysym.sym);
                }
				if (ev.key.keysym.unicode != 0 &&
				    (ev.key.keysym.unicode & 0xff80) == 0 &&
				    ((keyasciififoend+1)&(KEYFIFOSIZ-1)) != keyasciififoplc) {
					keyasciififo[keyasciififoend] = ev.key.keysym.unicode & 0x7f;
					keyasciififoend = ((keyasciififoend+1)&(KEYFIFOSIZ-1));
				}
				code = keytranslation[ev.key.keysym.sym];
                SetKey(code, 1);
                if (keypresscallback) {
                    keypresscallback(code, 1);
                }
                break;
            
            case SDL_KEYUP:
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    keystatus[1] = 0;
                } else {
                    dnReleaseKey(ev.key.keysym.sym);
                }
                code = keytranslation[ev.key.keysym.sym];
                SetKey(code, 0);
                if (keypresscallback) {
                    keypresscallback(code, 0);
                }
                break;
#endif
            
			case SDL_ACTIVEEVENT:
				if (ev.active.state & SDL_APPINPUTFOCUS) {
                    SDL_WM_GrabInput(SDL_GRAB_ON);
                    SDL_ShowCursor(SDL_DISABLE);
					appactive = ev.active.gain;
					if (mouseacquired && moustat) {
						if (appactive && !GUI_IsEnabled()) {
//							SDL_WM_GrabInput(SDL_GRAB_ON);
//							SDL_ShowCursor(SDL_DISABLE);
						} else {
//							SDL_WM_GrabInput(SDL_GRAB_OFF);
//							SDL_ShowCursor(SDL_ENABLE);
						}
					}
					rv=-1;
				}
				break;
                
#if 0

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				switch (ev.button.button) {
					case SDL_BUTTON_LEFT: j = 0; break;
					case SDL_BUTTON_RIGHT: j = 1; break;
					case SDL_BUTTON_MIDDLE: j = 2; break;
					default: j = -1; break;
				}
				if (j<0) break;
				if (ev.button.state == SDL_PRESSED)
					mouseb |= (1<<j);
				else {
                    mouseb &= ~(1<<j);
                }

				if (mousepresscallback)
					mousepresscallback(j+1, ev.button.state == SDL_PRESSED);
				break;
#else
            case SDL_MOUSEBUTTONDOWN:
                dnPressKey(DNK_MOUSE0+ev.button.button-1);
                break;
                
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button != 4 && ev.button.button != 5) {
                    dnReleaseKey(DNK_MOUSE0+ev.button.button-1);
                }
                break;
#endif

			case SDL_MOUSEMOTION:
                if (0 && skip_next_motion) {
                    skip_next_motion = 0;
                } else {
                    if (!firstcall) {
                        if (appactive) {
                            mousex += ev.motion.xrel;
                            mousey += ev.motion.yrel;
                            pointer.x += ev.motion.xrel;
                            pointer.y += ev.motion.yrel;
                        } else {
                            pointer.x = pointer.y = 0;
                        }
                    }
                }
				break;

			case SDL_JOYAXISMOTION:
				if (appactive && ev.jaxis.axis < joynumaxes)
					joyaxis[ ev.jaxis.axis ] = ev.jaxis.value * 10000 / 32767;
				break;

			case SDL_JOYHATMOTION: {
				int hatvals[16] = {
					-1,	// centre
					0, 	// up 1
					9000,	// right 2
					4500,	// up+right 3
					18000,	// down 4
					-1,	// down+up!! 5
					13500,	// down+right 6
					-1,	// down+right+up!! 7
					27000,	// left 8
					27500,	// left+up 9
					-1,	// left+right!! 10
					-1,	// left+right+up!! 11
					22500,	// left+down 12
					-1,	// left+down+up!! 13
					-1,	// left+down+right!! 14
					-1,	// left+down+right+up!! 15
				};
				if (appactive && ev.jhat.hat < joynumhats)
					joyhat[ ev.jhat.hat ] = hatvals[ ev.jhat.value & 15 ];
				break;
			}

			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				if (appactive && ev.jbutton.button < joynumbuttons) {
					if (ev.jbutton.state == SDL_PRESSED)
						joyb |= 1 << ev.jbutton.button;
					else
						joyb &= ~(1 << ev.jbutton.button);
				}
				break;

			case SDL_QUIT:
				quitevent = 1;
				rv=-1;
				break;

			default:
				//printOSD("Got event (%d)\n", ev.type);
				break;
		}
	}
    
	GUI_TimePulse();
	sampletimer();
	startwin_idle(NULL);
#undef SetKey

	firstcall = 0;
	
	return rv;
}


static int buildkeytranslationtable(void)
{
	memset(keytranslation,0,sizeof(keytranslation));
	
#define MAP(x,y) keytranslation[x] = y
	MAP(SDLK_BACKSPACE,	0xe);
	MAP(SDLK_TAB,		0xf);
	MAP(SDLK_RETURN,	0x1c);
	MAP(SDLK_PAUSE,		0x59);	// 0x1d + 0x45 + 0x9d + 0xc5
	MAP(SDLK_ESCAPE,	0x1);
	MAP(SDLK_SPACE,		0x39);
	MAP(SDLK_EXCLAIM,	0x2);	// '1'
	MAP(SDLK_QUOTEDBL,	0x28);	// '''
	MAP(SDLK_HASH,		0x4);	// '3'
	MAP(SDLK_DOLLAR,	0x5);	// '4'
	MAP(37,			0x6);	// '5' <-- where's the keysym SDL guys?
	MAP(SDLK_AMPERSAND,	0x8);	// '7'
	MAP(SDLK_QUOTE,		0x28);	// '''
	MAP(SDLK_LEFTPAREN,	0xa);	// '9'
	MAP(SDLK_RIGHTPAREN,	0xb);	// '0'
	MAP(SDLK_ASTERISK,	0x9);	// '8'
	MAP(SDLK_PLUS,		0xd);	// '='
	MAP(SDLK_COMMA,		0x33);
	MAP(SDLK_MINUS,		0xc);
	MAP(SDLK_PERIOD,	0x34);
	MAP(SDLK_SLASH,		0x35);
	MAP(SDLK_0,		0xb);
	MAP(SDLK_1,		0x2);
	MAP(SDLK_2,		0x3);
	MAP(SDLK_3,		0x4);
	MAP(SDLK_4,		0x5);
	MAP(SDLK_5,		0x6);
	MAP(SDLK_6,		0x7);
	MAP(SDLK_7,		0x8);
	MAP(SDLK_8,		0x9);
	MAP(SDLK_9,		0xa);
	MAP(SDLK_COLON,		0x27);
	MAP(SDLK_SEMICOLON,	0x27);
	MAP(SDLK_LESS,		0x33);
	MAP(SDLK_EQUALS,	0xd);
	MAP(SDLK_GREATER,	0x34);
	MAP(SDLK_QUESTION,	0x35);
	MAP(SDLK_AT,		0x3);	// '2'
	MAP(SDLK_LEFTBRACKET,	0x1a);
	MAP(SDLK_BACKSLASH,	0x2b);
	MAP(SDLK_RIGHTBRACKET,	0x1b);
	MAP(SDLK_CARET,		0x7);	// '7'
	MAP(SDLK_UNDERSCORE,	0xc);
	MAP(SDLK_BACKQUOTE,	0x29);
	MAP(SDLK_a,		0x1e);
	MAP(SDLK_b,		0x30);
	MAP(SDLK_c,		0x2e);
	MAP(SDLK_d,		0x20);
	MAP(SDLK_e,		0x12);
	MAP(SDLK_f,		0x21);
	MAP(SDLK_g,		0x22);
	MAP(SDLK_h,		0x23);
	MAP(SDLK_i,		0x17);
	MAP(SDLK_j,		0x24);
	MAP(SDLK_k,		0x25);
	MAP(SDLK_l,		0x26);
	MAP(SDLK_m,		0x32);
	MAP(SDLK_n,		0x31);
	MAP(SDLK_o,		0x18);
	MAP(SDLK_p,		0x19);
	MAP(SDLK_q,		0x10);
	MAP(SDLK_r,		0x13);
	MAP(SDLK_s,		0x1f);
	MAP(SDLK_t,		0x14);
	MAP(SDLK_u,		0x16);
	MAP(SDLK_v,		0x2f);
	MAP(SDLK_w,		0x11);
	MAP(SDLK_x,		0x2d);
	MAP(SDLK_y,		0x15);
	MAP(SDLK_z,		0x2c);
	MAP(SDLK_DELETE,	0xd3);
	MAP(SDLK_KP0,		0x52);
	MAP(SDLK_KP1,		0x4f);
	MAP(SDLK_KP2,		0x50);
	MAP(SDLK_KP3,		0x51);
	MAP(SDLK_KP4,		0x4b);
	MAP(SDLK_KP5,		0x4c);
	MAP(SDLK_KP6,		0x4d);
	MAP(SDLK_KP7,		0x47);
	MAP(SDLK_KP8,		0x48);
	MAP(SDLK_KP9,		0x49);
	MAP(SDLK_KP_PERIOD,	0x53);
	MAP(SDLK_KP_DIVIDE,	0xb5);
	MAP(SDLK_KP_MULTIPLY,	0x37);
	MAP(SDLK_KP_MINUS,	0x4a);
	MAP(SDLK_KP_PLUS,	0x4e);
	MAP(SDLK_KP_ENTER,	0x9c);
	//MAP(SDLK_KP_EQUALS,	);
	MAP(SDLK_UP,		0xc8);
	MAP(SDLK_DOWN,		0xd0);
	MAP(SDLK_RIGHT,		0xcd);
	MAP(SDLK_LEFT,		0xcb);
	MAP(SDLK_INSERT,	0xd2);
	MAP(SDLK_HOME,		0xc7);
	MAP(SDLK_END,		0xcf);
	MAP(SDLK_PAGEUP,	0xc9);
	MAP(SDLK_PAGEDOWN,	0xd1);
	MAP(SDLK_F1,		0x3b);
	MAP(SDLK_F2,		0x3c);
	MAP(SDLK_F3,		0x3d);
	MAP(SDLK_F4,		0x3e);
	MAP(SDLK_F5,		0x3f);
	MAP(SDLK_F6,		0x40);
	MAP(SDLK_F7,		0x41);
	MAP(SDLK_F8,		0x42);
	MAP(SDLK_F9,		0x43);
	MAP(SDLK_F10,		0x44);
	MAP(SDLK_F11,		0x57);
	MAP(SDLK_F12,		0x58);
	MAP(SDLK_NUMLOCK,	0x45);
	MAP(SDLK_CAPSLOCK,	0x3a);
	MAP(SDLK_SCROLLOCK,	0x46);
	MAP(SDLK_RSHIFT,	0x36);
	MAP(SDLK_LSHIFT,	0x2a);
	MAP(SDLK_RCTRL,		0x9d);
	MAP(SDLK_LCTRL,		0x1d);
	MAP(SDLK_RALT,		0xb8);
	MAP(SDLK_LALT,		0x38);
	MAP(SDLK_LSUPER,	0xdb);	// win l
	MAP(SDLK_RSUPER,	0xdc);	// win r
	MAP(SDLK_PRINT,		-2);	// 0xaa + 0xb7
	MAP(SDLK_SYSREQ,	0x54);	// alt+printscr
	MAP(SDLK_BREAK,		0xb7);	// ctrl+pause
	MAP(SDLK_MENU,		0xdd);	// win menu?
#undef MAP

	return 0;
}

#if defined _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#elif defined __linux || defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__
# include <sys/mman.h>
#endif

void makeasmwriteable(void)
{
#ifndef ENGINE_USING_A_C
	extern int dep_begin, dep_end;
# if defined _WIN32
	DWORD oldprot;
	if (!VirtualProtect((LPVOID)&dep_begin, (SIZE_T)&dep_end - (SIZE_T)&dep_begin, PAGE_EXECUTE_READWRITE, &oldprot)) {
		initprint("Error making code writeable\n");
		return;
	}
# elif defined __linux || defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__
	int pagesize;
	size_t dep_begin_page;
	pagesize = sysconf(_SC_PAGE_SIZE);
	if (pagesize == -1) {
		initprintf("Error getting system page size\n");
		return;
	}
	dep_begin_page = ((size_t)&dep_begin) & ~(pagesize-1);
	if (mprotect((const void *)dep_begin_page, (size_t)&dep_end - dep_begin_page, PROT_READ|PROT_WRITE) < 0) {
		initprintf("Error making code writeable (errno=%d)\n", errno);
		return;
	}
# else
#  error Dont know how to unprotect the self-modifying assembly on this platform!
# endif
#endif
}

/* dnAPI */

void dnGetCurrentVideoMode(VideoMode *videomode) {
	assert(videomode != NULL);
	videomode->width = sdl_surface->w;
	videomode->height = sdl_surface->h;
	videomode->fullscreen = sdl_surface->flags & SDL_FULLSCREEN ? 1 : 0;
	videomode->bpp = sdl_surface->format->BitsPerPixel;
}

/*
int dnTranslateSDLKey(SDLKey key) {
    return keytranslation[key];
}

const char* dnGetKeyName(int key) {
    const char* name = KB_ScanCodeToString(key);
    return name[0] ? name : "None";
}
*/
static int clampi(int v, int min, int max) {
    if (v > max) return max;
    if (v < min) return min;
    return v;
}

static double clampd(double v, double min, double max) {
    if (v > max) return max;
    if (v < min) return min;
    return v;
}

void dnOverrideInput(input *loc) {
    
    vector2 total_velocity = { 0.0, 0.0 };
    vector2 mouse_velocity;
       
    mouse_velocity.x = pointer.x*sensitivity.x;
    mouse_velocity.y = pointer.y*sensitivity.y*(((double)xdim)/((double)ydim));

    total_velocity.x += mouse_velocity.x;
    total_velocity.y += mouse_velocity.y;

    total_velocity.x = clampd(total_velocity.x, -max_velocity.x, max_velocity.x);
    total_velocity.y = clampd(total_velocity.y, -max_velocity.y, max_velocity.y);

    loc->avel = (signed char) clampi((int)(128*total_velocity.x/max_velocity.x) + loc->avel, -127, 127);
    if (loc->avel != 0) {
        pointer.x = 0.0;
    }
    
    loc->horz = (signed char) clampi((int)(128*total_velocity.y/max_velocity.y) + loc->horz, -127, 127);
    if (loc->horz != 0) {
        pointer.y = 0.0;
    }
    if (!ud.mouseflip) {
        loc->horz *= -1;
    }
}

void dnSetMouseSensitivity(int sens) {
    sensitivity.x = 1.0 + sens/65536.0*16.0;
    sensitivity.y = sensitivity.x*1.2;
}

int dnGetMouseSensitivity() {
    return (int)((sensitivity.x-1.0)*65536.0/16.0);
}

void dnResetMouse() {
	pointer.x = pointer.y = 0;
}
