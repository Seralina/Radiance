/* Copyright (c) 1990 Regents of the University of California */

#ifndef lint
static char SCCSid[] = "$SunId$ LBL";
#endif

/*
 *  x11image.c - driver for X-windows
 *
 *     3/1/90
 */

/*
 *  Modified for X11
 *
 *  January 1990
 *
 *  Anat Grynberg  and Greg Ward
 */


#include  "standard.h"

#include  <X11/Xlib.h>
#include  <X11/cursorfont.h>
#include  <X11/Xutil.h>

#include  "color.h"
#include  "view.h"
#include  "pic.h"
#include  "x11raster.h"
#include  "random.h"

#define  FONTNAME	"8x13"		/* text font we'll use */

#define  CTRL(c)	('c'-'@')

#define  BORWIDTH	5		/* border width */

#define  ourscreen	DefaultScreen(thedisplay)
#define  ourblack	BlackPixel(thedisplay,ourscreen)
#define  ourwhite	WhitePixel(thedisplay,ourscreen)
#define  ourroot	RootWindow(thedisplay,ourscreen)
#define  ourgc		DefaultGC(thedisplay,ourscreen)

#define  redraw(x,y,w,h) patch_raster(wind,(x)-xoff,(y)-yoff,x,y,w,h,ourras)

double  gamcor = 2.2;			/* gamma correction */

int  dither = 1;			/* dither colors? */
int  fast = 0;				/* keep picture in Pixmap? */

Window  wind = 0;			/* our output window */
Font  fontid;				/* our font */

int  maxcolors = 0;			/* maximum colors */
int  greyscale = 0;			/* in grey */

int  scale = 0;				/* scalefactor; power of two */

int  xoff = 0;				/* x image offset */
int  yoff = 0;				/* y image offset */

VIEW  ourview = STDVIEW;		/* image view parameters */
int  gotview = 0;			/* got parameters from file */

COLR  *scanline;			/* scan line buffer */

int  xmax, ymax;			/* picture resolution */
int  width, height;			/* window size */
char  *fname = NULL;			/* input file name */
FILE  *fin = stdin;			/* input file */
long  *scanpos = NULL;			/* scan line positions in file */
int  cury = 0;				/* current scan location */

double  exposure = 1.0;			/* exposure compensation used */

XRASTER	*ourras;			/* our stored image */
unsigned char	*ourdata;		/* our image data */

struct {
	int  xmin, ymin, xsiz, ysiz;
}  box = {0, 0, 0, 0};			/* current box */

char  *geometry = NULL;			/* geometry specification */

char  *progname;

char  errmsg[128];

extern long  ftell();

extern char  *malloc(), *calloc();

extern double  atof(), pow(), log();

Display  *thedisplay;

main(argc, argv)
int  argc;
char  *argv[];
{
	int  headline();
	int  i;
	
	progname = argv[0];

	for (i = 1; i < argc; i++)
		if (argv[i][0] == '-')
			switch (argv[i][1]) {
			case 'c':
				maxcolors = atoi(argv[++i]);
				break;
			case 'b':
				greyscale = !greyscale;
				break;
			case 'm':
				maxcolors = 2;
				break;
			case 'd':
				dither = !dither;
				break;
			case 'f':
				fast = !fast;
				break;
			case 'e':
				if (argv[i+1][0] != '+' && argv[i+1][0] != '-')
					goto userr;
				scale = atoi(argv[++i]);
				break;
			case 'g':
				if (!strcmp(argv[i], "-geometry"))
					geometry = argv[++i];
				else
					gamcor = atof(argv[++i]);
				break;
			default:
				goto userr;
			}
		else if (argv[i][0] == '=')
			geometry = argv[i];
		else
			break;

	if (i == argc-1) {
		fname = argv[i];
		fin = fopen(fname, "r");
		if (fin == NULL) {
			sprintf(errmsg, "can't open file \"%s\"", fname);
			quiterr(errmsg);
		}
	} else if (i != argc)
		goto userr;
				/* get header */
	getheader(fin, headline);
				/* get picture dimensions */
	if (fgetresolu(&xmax, &ymax, fin) != (YMAJOR|YDECR))
		quiterr("bad picture size");
				/* set view parameters */
	if (gotview && setview(&ourview) != NULL)
		gotview = 0;
	if ((scanline = (COLR *)malloc(xmax*sizeof(COLR))) == NULL)
		quiterr("out of memory");

	init();			/* get file and open window */

	for ( ; ; )
		getevent();		/* main loop */
userr:
	fprintf(stderr,
	"Usage: %s [-geometry spec][-b][-m][-d][-f][-c ncolors][-e +/-stops] file\n",
			progname);
	quit(1);
}


headline(s)		/* get relevant info from header */
char  *s;
{
	static char  *altname[] = {"rview","rpict","pinterp",VIEWSTR,NULL};
	register char  **an;

	if (isexpos(s))
		exposure *= exposval(s);
	else
		for (an = altname; *an != NULL; an++)
			if (!strncmp(*an, s, strlen(*an))) {
				if (sscanview(&ourview, s+strlen(*an)) > 0)
					gotview++;
				return;
			}
}


init()			/* get data and open window */
{
	XSizeHints  oursizhints;
	register int  i;
	
	if (fname != NULL) {
		scanpos = (long *)malloc(ymax*sizeof(long));
		if (scanpos == NULL)
			goto memerr;
		for (i = 0; i < ymax; i++)
			scanpos[i] = -1;
	}
	if ((thedisplay = XOpenDisplay(NULL)) == NULL)
		quiterr("can't open display; DISPLAY variable set?");
	wind = XCreateSimpleWindow(thedisplay, 
		ourroot, 0, 0, xmax, ymax, BORWIDTH, ourblack, ourwhite);
	if (wind == 0)
		quiterr("can't create window");
	if (maxcolors == 0) {		/* get number of available colors */
		i = DisplayPlanes(thedisplay,ourscreen);
		maxcolors = i > 8 ? 256 : 1<<i;
		if (maxcolors > 4) maxcolors -= 2;
	}
	fontid = XLoadFont(thedisplay, FONTNAME);
	if (fontid == 0)
		quiterr("can't get font");
	XSetFont(thedisplay, ourgc, fontid);
	XStoreName(thedisplay, wind, fname == NULL ? progname : fname);
	XDefineCursor(thedisplay, wind, XCreateFontCursor(thedisplay, 
			XC_diamond_cross));
	if (geometry != NULL) {
		bzero((char *)&oursizhints, sizeof(oursizhints));
		i = XParseGeometry(geometry, &oursizhints.x, &oursizhints.y,
				&oursizhints.width, &oursizhints.height);
		if ((i&(WidthValue|HeightValue)) == (WidthValue|HeightValue))
			oursizhints.flags |= USSize;
		else {
			oursizhints.width = xmax;
			oursizhints.height = ymax;
			oursizhints.flags |= PSize;
		}
		if ((i&(XValue|YValue)) == (XValue|YValue)) {
			oursizhints.flags |= USPosition;
			if (i & XNegative)
				oursizhints.x += DisplayWidth(thedisplay,
				ourscreen)-1-oursizhints.width-2*BORWIDTH;
			if (i & YNegative)
				oursizhints.y += DisplayHeight(thedisplay,
				ourscreen)-1-oursizhints.height-2*BORWIDTH;
		}
		XSetNormalHints(thedisplay, wind, &oursizhints);
	}
				/* store image */
	getras();
	XSelectInput(thedisplay, wind, ButtonPressMask|ButtonReleaseMask
			|ButtonMotionMask|StructureNotifyMask
			|KeyPressMask|ExposureMask);
	XMapWindow(thedisplay, wind);
	return;
memerr:
	quiterr("out of memory");
} /* end of init */


quiterr(err)		/* print message and exit */
char  *err;
{
	if (err != NULL) {
		fprintf(stderr, "%s: %s\n", progname, err);
		exit(1);
	}
	exit(0);
}


eputs(s)
char	*s;
{
	fputs(s, stderr);
}


quit(code)
int  code;
{
	exit(code);
}


getras()				/* get raster file */
{
	colormap	ourmap;
	XVisualInfo	vinfo;

	if (maxcolors <= 2) {		/* monochrome */
		ourdata = (unsigned char *)malloc(ymax*((xmax+7)/8));
		if (ourdata == NULL)
			goto fail;
		ourras = make_raster(thedisplay, ourscreen, 1, ourdata,
				xmax, ymax, 8);
		if (ourras == NULL)
			goto fail;
		getmono();
	} else if (XMatchVisualInfo(thedisplay,ourscreen,24,TrueColor,&vinfo)) {
		ourdata = (unsigned char *)malloc(xmax*ymax*3);
		if (ourdata == NULL)
			goto fail;
		ourras = make_raster(thedisplay, ourscreen, 24, ourdata,
				xmax, ymax, 8);
		if (ourras == NULL)
			goto fail;
		getfull();
	} else {
		ourdata = (unsigned char *)malloc(xmax*ymax);
		if (ourdata == NULL)
			goto fail;
		ourras = make_raster(thedisplay, ourscreen, 8, ourdata,
				xmax, ymax, 8);
		if (ourras == NULL)
			goto fail;
		if (greyscale)
			biq(dither,maxcolors,1,ourmap);
		else
			ciq(dither,maxcolors,1,ourmap);
		if (init_rcolors(ourras, ourmap[0], ourmap[1], ourmap[2]) == 0)
			goto fail;
	}
	return;
fail:
	quit("could not create raster image");
}


getevent()				/* process the next event */
{
	union {
		XEvent  u;
		XConfigureEvent  c;
		XExposeEvent  e;
		XButtonPressedEvent  b;
		XKeyPressedEvent  k;
	} e;

	XNextEvent(thedisplay, &e.u);
	switch (e.u.type) {
	case KeyPress:
		docom(&e.k);
		break;
	case ConfigureNotify:
		width = e.c.width;
		height = e.c.height;
		break;
	case MapNotify:
		map_rcolors(ourras, wind);
		if (fast)
			make_rpixmap(ourras);
		break;
	case UnmapNotify:
		unmap_rcolors(ourras);
		break;
	case Expose:
		redraw(e.e.x, e.e.y, e.e.width, e.e.height);
		break;
	case ButtonPress:
		if (e.b.state & (ShiftMask|ControlMask))
			moveimage(&e.b);
		else
			getbox(&e.b);
		break;
	}
}


docom(ekey)					/* execute command */
XKeyPressedEvent  *ekey;
{
	char  buf[80];
	COLOR  cval;
	XColor  cvx;
	int  com, n;
	double  comp;
	FVECT  rorg, rdir;

	n = XLookupString(ekey, buf, sizeof(buf), NULL, NULL); 
	if (n == 0)
		return(0);
	com = buf[0];
	switch (com) {			/* interpret command */
	case 'q':
	case CTRL(D):				/* quit */
		quit(0);
	case '\n':
	case '\r':
	case 'l':
	case 'c':				/* value */
		if (avgbox(cval) == -1)
			return(-1);
		switch (com) {
		case '\n':
		case '\r':				/* radiance */
			sprintf(buf, "%.3f", intens(cval)/exposure);
			break;
		case 'l':				/* luminance */
			sprintf(buf, "%.0fn", bright(cval)*683.0/exposure);
			break;
		case 'c':				/* color */
			comp = pow(2.0, (double)scale);
			sprintf(buf, "(%.2f,%.2f,%.2f)",
					colval(cval,RED)*comp,
					colval(cval,GRN)*comp,
					colval(cval,BLU)*comp);
			break;
		}
		XDrawImageString(thedisplay, wind, ourgc,
				box.xmin, box.ymin+box.ysiz, buf, strlen(buf)); 
		return(0);
	case 'i':				/* identify (contour) */
		if (ourras->pixels == NULL)
			return(-1);
		n = ourdata[ekey->x-xoff+xmax*(ekey->y-yoff)];
		n = ourras->pmap[n];
		cvx.pixel = ourras->cdefs[n].pixel;
		cvx.red = random() & 65535;
		cvx.green = random() & 65535;
		cvx.blue = random() & 65535;
		cvx.flags = DoRed|DoGreen|DoBlue;
		XStoreColor(thedisplay, ourras->cmap, &cvx);
		return(0);
	case 'p':				/* position */
		sprintf(buf, "(%d,%d)", ekey->x-xoff, ymax-1-ekey->y+yoff);
		XDrawImageString(thedisplay, wind, ourgc, ekey->x, ekey->y,
					buf, strlen(buf));
		return(0);
	case 't':				/* trace */
		if (!gotview) {
			XBell(thedisplay, 0);
			return(-1);
		}
		viewray(rorg, rdir, &ourview,
				(ekey->x-xoff+.5)/xmax,
				(ymax-1-ekey->y+yoff+.5)/ymax);
		printf("%e %e %e ", rorg[0], rorg[1], rorg[2]);
		printf("%e %e %e\n", rdir[0], rdir[1], rdir[2]);
		fflush(stdout);
		return(0);
	case '=':				/* adjust exposure */
		if (avgbox(cval) == -1)
			return(-1);
		n = log(.5/bright(cval))/.69315 - scale;	/* truncate */
		if (n == 0)
			return(0);
		scale_rcolors(ourras, pow(2.0, (double)n));
		scale += n;
		sprintf(buf, "%+d", scale);
		XDrawImageString(thedisplay, wind, ourgc,
				box.xmin, box.ymin+box.ysiz, buf, strlen(buf));
		XFlush(thedisplay);
		free(ourdata);
		free_raster(ourras);
		getras();
	/* fall through */
	case CTRL(R):				/* redraw */
	case CTRL(L):
		unmap_rcolors(ourras);
		XClearWindow(thedisplay, wind);
		map_rcolors(ourras, wind);
		if (fast)
			make_rpixmap(ourras);
		redraw(0, 0, width, height);
		return(0);
	case ' ':				/* clear */
		redraw(box.xmin, box.ymin, box.xsiz, box.ysiz);
		return(0);
	default:
		XBell(thedisplay, 0);
		return(-1);
	}
}


moveimage(ebut)				/* shift the image */
XButtonPressedEvent  *ebut;
{
	union {
		XEvent  u;
		XButtonReleasedEvent  b;
		XPointerMovedEvent  m;
	}  e;
	int	nxo, nyo;

	XMaskEvent(thedisplay, ButtonReleaseMask|ButtonMotionMask, &e.u);
	while (e.u.type == MotionNotify) {
		nxo = xoff + e.m.x - ebut->x;
		nyo = yoff + e.m.y - ebut->y;
		revbox(nxo, nyo, nxo+xmax, nyo+ymax);
		XMaskEvent(thedisplay,ButtonReleaseMask|ButtonMotionMask,&e.u);
		revbox(nxo, nyo, nxo+xmax, nyo+ymax);
	}
	xoff += e.b.x - ebut->x;
	yoff += e.b.y - ebut->y;
	XClearWindow(thedisplay, wind);
	redraw(0, 0, width, height);
}


getbox(ebut)				/* get new box */
XButtonPressedEvent  *ebut;
{
	union {
		XEvent  u;
		XButtonReleasedEvent  b;
		XPointerMovedEvent  m;
	}  e;

	XMaskEvent(thedisplay, ButtonReleaseMask|ButtonMotionMask, &e.u);
	while (e.u.type == MotionNotify) {
		revbox(ebut->x, ebut->y, box.xmin = e.m.x, box.ymin = e.m.y);
		XMaskEvent(thedisplay,ButtonReleaseMask|ButtonMotionMask,&e.u);
		revbox(ebut->x, ebut->y, box.xmin, box.ymin);
	}
	box.xmin = e.b.x<0 ? 0 : (e.b.x>=width ? width-1 : e.b.x);
	box.ymin = e.b.y<0 ? 0 : (e.b.y>=height ? height-1 : e.b.y);
	if (box.xmin > ebut->x) {
		box.xsiz = box.xmin - ebut->x + 1;
		box.xmin = ebut->x;
	} else {
		box.xsiz = ebut->x - box.xmin + 1;
	}
	if (box.ymin > ebut->y) {
		box.ysiz = box.ymin - ebut->y + 1;
		box.ymin = ebut->y;
	} else {
		box.ysiz = ebut->y - box.ymin + 1;
	}
}


revbox(x0, y0, x1, y1)			/* draw box with reversed lines */
int  x0, y0, x1, y1;
{
	static GC	mygc = 0;

	if (mygc == 0) {
		mygc = XCreateGC(thedisplay, wind, 0, 0);
		XSetFunction(thedisplay, mygc, GXinvert);
	}
	XDrawLine(thedisplay, wind, mygc, x0, y0, x1, y0);
	XDrawLine(thedisplay, wind, mygc, x0, y1, x1, y1);
	XDrawLine(thedisplay, wind, mygc, x0, y0, x0, y1);
	XDrawLine(thedisplay, wind, mygc, x1, y0, x1, y1);
} /* end of revbox */


avgbox(clr)				/* average color over current box */
COLOR  clr;
{
	int  left, right, top, bottom;
	int  y;
	double  d;
	COLOR  ctmp;
	register int  x;

	setcolor(clr, 0.0, 0.0, 0.0);
	left = box.xmin - xoff;
	right = left + box.xsiz;
	if (left < 0)
		left = 0;
	if (right > xmax)
		right = xmax;
	if (left >= right)
		return(-1);
	top = box.ymin - yoff;
	bottom = top + box.ysiz;
	if (top < 0)
		top = 0;
	if (bottom > ymax)
		bottom = ymax;
	if (top >= bottom)
		return(-1);
	for (y = top; y < bottom; y++) {
		if (getscan(y) == -1)
			return(-1);
		for (x = left; x < right; x++) {
			colr_color(ctmp, scanline[x]);
			addcolor(clr, ctmp);
		}
	}
	d = 1.0/((right-left)*(bottom-top));
	scalecolor(clr, d);
	return(0);
}


getmono()			/* get monochrome data */
{
	register unsigned char	*dp;
	register int	x, err;
	int	y;
	rgbpixel	*inline;
	short	*cerr;

	if ((inline = (rgbpixel *)malloc(xmax*sizeof(rgbpixel))) == NULL
			|| (cerr = (short *)calloc(xmax,sizeof(short))) == NULL)
		quit("out of memory in getmono");
	dp = ourdata - 1;
	for (y = 0; y < ymax; y++) {
		picreadline3(y, inline);
		err = 0;
		for (x = 0; x < xmax; x++) {
			if (!(x&7))
				*++dp = 0;
			err += rgb_bright(&inline[x]) + cerr[x];
			if (err > 127)
				err -= 255;
			else
				*dp |= 1<<(7-(x&07));
			cerr[x] = err >>= 1;
		}
	}
	free((char *)inline);
	free((char *)cerr);
}


getfull()			/* get full (24-bit) data */
{
	int	y;

	for (y = 0; y < ymax; y++)
		picreadline3(y, (rgbpixel *)(ourdata+y*xmax*3));
}


scale_rcolors(xr, sf)			/* scale color map */
register XRASTER	*xr;
double	sf;
{
	register int	i;
	long	maxv;

	if (xr->pixels == NULL)
		return;

	sf = pow(sf, 1.0/gamcor);
	maxv = 65535/sf;

	for (i = xr->ncolors; i--; ) {
		xr->cdefs[i].red = xr->cdefs[i].red > maxv ?
				65535 :
				xr->cdefs[i].red * sf;
		xr->cdefs[i].green = xr->cdefs[i].green > maxv ?
				65535 :
				xr->cdefs[i].green * sf;
		xr->cdefs[i].blue = xr->cdefs[i].blue > maxv ?
				65535 :
				xr->cdefs[i].blue * sf;
	}
	XStoreColors(thedisplay, xr->cmap, xr->cdefs, xr->ncolors);
}


getscan(y)
int  y;
{
	if (y != cury) {
		if (scanpos == NULL || scanpos[y] == -1)
			return(-1);
		if (fseek(fin, scanpos[y], 0) == -1)
			quit("fseek error");
		cury = y;
	} else if (scanpos != NULL)
		scanpos[y] = ftell(fin);

	if (freadcolrs(scanline, xmax, fin) < 0)
		quiterr("read error");

	cury++;
	return(0);
}


picreadline3(y, l3)			/* read in 3-byte scanline */
int  y;
register rgbpixel  *l3;
{
	register int	i;
							/* read scanline */
	if (getscan(y) < 0)
		quiterr("cannot seek for picreadline");
							/* convert scanline */
	normcolrs(scanline, xmax, scale);
	for (i = 0; i < xmax; i++) {
		l3[i].r = scanline[i][RED];
		l3[i].g = scanline[i][GRN];
		l3[i].b = scanline[i][BLU];
	}
}


picwriteline(y, l)		/* add 8-bit scanline to image */
int  y;
pixel  *l;
{
	bcopy((char *)l, (char *)ourdata+y*xmax, xmax);
}


picreadcm(map)			/* do gamma correction */
colormap  map;
{
	extern double  pow();
	register int  i, val;

	for (i = 0; i < 256; i++) {
		val = pow(i/256.0, 1.0/gamcor) * 256.0;
		map[0][i] = map[1][i] = map[2][i] = val;
	}
}


picwritecm(map)			/* handled elsewhere */
colormap  map;
{
#ifdef DEBUG
	register int i;

	for (i = 0; i < 256; i++)
		printf("%d %d %d\n", map[0][i],map[1][i],map[2][i]);
#endif
}
