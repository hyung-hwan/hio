/* minimal terminal emulator - based on eduterm (https://www.uninformativ.de/git/eduterm/file/README.html) */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <hio-shw.h>
#include <hio-pty.h>


struct X11
{
	int fd;
	Display *dpy;
	int screen;
	Window root;

	Window termwin;
	GC termgc;
	unsigned long col_fg, col_bg;
	int w, h;

	XFontStruct *xfont;
	int font_width, font_height;

	char *buf;
	int buf_w, buf_h;
	int buf_x, buf_y;
};

struct pair_t
{
	struct X11* x11;
	hio_syshnd_t pty;
};

#if 0
int term_set_size(struct PTY *pty, struct X11 *x11)
{
    struct winsize ws = {
	   .ws_col = x11->buf_w,
	   .ws_row = x11->buf_h,
    };

    /* This is the very same ioctl that normal programs use to query the
	* window size. Normal programs are actually able to do this, too,
	* but it makes little sense: Setting the size has no effect on the
	* PTY driver in the kernel (it just keeps a record of it) or the
	* terminal emulator. IIUC, all that's happening is that subsequent
	* ioctls will report the new size -- until another ioctl sets a new
	* size.
	*
	* I didn't see any response to ioctls of normal programs in any of
	* the popular terminals (XTerm, VTE, st). They are not informed by
	* the kernel when a normal program issues an ioctl like that.
	*
	* On the other hand, if we were to issue this ioctl during runtime
	* and the size actually changed, child programs would get a
	* SIGWINCH. */
    if (ioctl(pty->master, TIOCSWINSZ, &ws) == -1)
    {
	   perror("ioctl(TIOCSWINSZ)");
	   return 0;
    }

    return 1;
}
#endif

void x11_key (XKeyEvent *ev, struct X11* x11, int pty_fd)
{
	char buf[32];
	KeySym ksym;
	int len;

	len = XLookupString(ev, buf, HIO_SIZEOF(buf), &ksym, 0);
	if (len > 0) write (pty_fd, buf, len);
}

void x11_redraw(struct X11 *x11)
{
	int x, y;
	char buf[1];

	XSetForeground(x11->dpy, x11->termgc, x11->col_bg);
	XFillRectangle(x11->dpy, x11->termwin, x11->termgc, 0, 0, x11->w, x11->h);

	XSetForeground(x11->dpy, x11->termgc, x11->col_fg);
	for (y = 0; y < x11->buf_h; y++)
	{
	#if 0
		for (x = 0; x < x11->buf_w; x++)
		{
			buf[0] = x11->buf[y * x11->buf_w + x];
			if (!iscntrl(buf[0]))
			{
				 XDrawString(x11->dpy, x11->termwin, x11->termgc,
						x * x11->font_width,
						y * x11->font_height + x11->xfont->ascent,
						buf, 1);
			}
		}
	#else
		XDrawString(x11->dpy, x11->termwin, x11->termgc,
			0 * x11->font_width,
			y * x11->font_height + x11->xfont->ascent,
			&x11->buf[y * x11->buf_w], x11->buf_w);
	#endif
    }

	/* draw a cursor */
	XSetForeground(x11->dpy, x11->termgc, x11->col_fg);
	XFillRectangle(x11->dpy, x11->termwin, x11->termgc,
		x11->buf_x * x11->font_width,
		x11->buf_y * x11->font_height,
		x11->font_width, x11->font_height);

	XSync(x11->dpy, False);
	//XFlush (x11->dpy);
}

int x11_setup(struct X11 *x11)
{
	Colormap cmap;
	XColor color;
	XSetWindowAttributes wa = {
		.background_pixmap = ParentRelative,
		.event_mask = KeyPressMask | KeyReleaseMask | ExposureMask,
	};

	x11->dpy = XOpenDisplay(NULL);
	if (x11->dpy == NULL)
	{
		fprintf(stderr, "Cannot open display\n");
		return -1;
	}

	x11->screen = DefaultScreen(x11->dpy);
	x11->root = RootWindow(x11->dpy, x11->screen);
	x11->fd = ConnectionNumber(x11->dpy);

	x11->xfont = XLoadQueryFont(x11->dpy, "fixed");
	if (x11->xfont == NULL)
	{
		fprintf(stderr, "Could not load font\n");
		return -1;
	}
	x11->font_width = XTextWidth(x11->xfont, "m", 1);
	x11->font_height = x11->xfont->ascent + x11->xfont->descent;

	cmap = DefaultColormap(x11->dpy, x11->screen);

	if (!XAllocNamedColor(x11->dpy, cmap, "#000000", &color, &color))
	{
		fprintf(stderr, "Could not load bg color\n");
		return -1;
	}
	x11->col_bg = color.pixel;

	if (!XAllocNamedColor(x11->dpy, cmap, "#aaaaaa", &color, &color))
	{
		fprintf(stderr, "Could not load fg color\n");
		return -1;
	}
	x11->col_fg = color.pixel;

    /* The terminal will have a fixed size of 80x25 cells. This is an
	* arbitrary number. No resizing has been implemented and child
	* processes can't even ask us for the current size (for now).
	*
	* buf_x, buf_y will be the current cursor position. */
	x11->buf_w = 80;
	x11->buf_h = 25;
	x11->buf_x = 0;
	x11->buf_y = 0;
	x11->buf = malloc(x11->buf_w * x11->buf_h);
	if (!x11->buf )
	{
		fprintf(stderr, "Could not allocate terminal buffer\n");
		return -1;
	}
	memset (x11->buf, ' ', x11->buf_w * x11->buf_h);

	x11->w = x11->buf_w * x11->font_width;
	x11->h = x11->buf_h * x11->font_height;

	x11->termwin = XCreateWindow(x11->dpy, x11->root,
						   0, 0,
						   x11->w, x11->h,
						   0,
						   DefaultDepth(x11->dpy, x11->screen),
						   CopyFromParent,
						   DefaultVisual(x11->dpy, x11->screen),
						   CWBackPixmap | CWEventMask,
						   &wa);
	XStoreName(x11->dpy, x11->termwin, "et");
	XMapWindow(x11->dpy, x11->termwin);
	x11->termgc = XCreateGC(x11->dpy, x11->termwin, 0, NULL);

	XSync(x11->dpy, False);

    return 0;
}

void x11_cleanup (struct X11* x11)
{
	XFreeGC (x11->dpy, x11->termgc);
	XDestroyWindow (x11->dpy, x11->termwin);
	XFreeFont (x11->dpy, x11->xfont);
	XCloseDisplay (x11->dpy);
	free (x11->buf);
}

int pty_on_read (hio_dev_pty_t* dev, const void* data, hio_iolen_t len)
{
	hio_iolen_t x;
	const hio_uint8_t* buf = (const hio_uint8_t*)data;
	int just_wrapped = 0;
	struct pair_t* p = hio_dev_pty_getxtn(dev);
	struct X11* x11 = p->x11;

	for (x = 0; x < len; x++)
	{
		if (buf[x] == '\r')
		{
			x11->buf_x = 0;
		}
		else if (buf[x] == '\b')
		{
			if(x11->buf_x > 0) 
			{
				x11->buf[x11->buf_y * x11->buf_w + x11->buf_x] = ' ';
				x11->buf_x--;
			}
		}
		else if (buf[x] == '\7')
		{
			/* beep */
		}
		else
		{
			if (buf[x] != '\n')
			{
				/* If this is a regular byte, store it and advance
				 * the cursor one cell "to the right". This might
				 * actually wrap to the next line, see below. */
				x11->buf[x11->buf_y * x11->buf_w + x11->buf_x] = buf[x];
				x11->buf_x++;

				if (x11->buf_x >= x11->buf_w)
				{
					x11->buf_x = 0;
					x11->buf_y++;
					just_wrapped = 1;
				}
				else
					just_wrapped = 0;
			}
			else if (!just_wrapped)
			{
				/* We read a newline and we did *not* implicitly
				 * wrap to the next line with the last byte we read.
				 * This means we must *now* advance to the next
				 * line.
				 *
				 * This is the same behaviour that most other
				 * terminals have: If you print a full line and then
				 * a newline, they "ignore" that newline. (Just
				 * think about it: A full line of text could always
				 * wrap to the next line implicitly, so that
				 * additional newline could cause the cursor to jump
				 * to the next line *again*.) */
				x11->buf_y++;
				just_wrapped = 0;
			}

			/* We now check if "the next line" is actually outside
			 * of the buffer. If it is, we shift the entire content
			 * one line up and then stay in the very last line.
			 *
			 * After the memmove(), the last line still has the old
			 * content. We must clear it. */
			if (x11->buf_y >= x11->buf_h)
			{
				int i;
				memmove(x11->buf, &x11->buf[x11->buf_w], x11->buf_w * (x11->buf_h - 1));
				x11->buf_y = x11->buf_h - 1;
				for (i = 0; i < x11->buf_w; i++) x11->buf[x11->buf_y * x11->buf_w + i] = ' ';
			}
		}
	}

	x11_redraw (x11);
	return 0;
}

int pty_on_write (hio_dev_pty_t* dev, hio_iolen_t wrlen, void*  wrctx)
{
	return 0;
}

void pty_on_close (hio_dev_pty_t* dev)
{
	printf (">> pty closed....\n");
	hio_stop (hio_dev_pty_gethio(dev), HIO_STOPREQ_TERMINATION);
}


int shw_on_ready (hio_dev_shw_t* dev, int events)
{
	struct pair_t* p = hio_dev_shw_getxtn(dev);
	struct X11* x11 = p->x11;

	/* consume data here beforehand. don't let the hio loop
	 * read the data */
	XEvent ev;
	while (XPending(x11->dpy))
	{
		XNextEvent(x11->dpy, &ev);
		switch (ev.type)
		{
			case Expose:
				x11_redraw (x11);
				break;
			case KeyPress:
				x11_key (&ev.xkey, x11, p->pty);
				break;
		}
	}

	/* no output via hio on the x11 connection. so let's not care about it. */
	return 0; /*  don't invoke the read method */
}

int shw_on_read (hio_dev_shw_t* dev, const void* data, hio_iolen_t len)
{
	return 0;
}

int shw_on_write (hio_dev_shw_t* dev, hio_iolen_t wrlen, void*  wrctx)
{
	return 0;
}

void shw_on_close (hio_dev_shw_t* dev)
{
	printf (">> shw closed....\n");
}

int main()
{
	hio_t* hio = HIO_NULL;
	struct X11 x11;
	hio_dev_pty_t* pty;
	hio_dev_shw_t* shw;

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		fprintf (stderr, "Error: Unable to open hio\n");
		return -1;
	}

	hio_setoption (hio, HIO_LOG_TARGET_BCSTR, "/dev/stderr");

	if (x11_setup(&x11) <= -1)  return -1;
	
	{
		hio_dev_pty_make_t pi;
		struct pair_t* pair;

		memset (&pi, 0, HIO_SIZEOF(pi));
		pi.cmd = "/bin/ksh";
		pi.flags = 0;
		pi.on_write = pty_on_write;
		pi.on_read = pty_on_read;
		pi.on_close = pty_on_close;
		pty = hio_dev_pty_make(hio, HIO_SIZEOF(*pair), &pi);
		if (!pty)
		{
			fprintf (stderr, "Error: Unable to create pty - %s\n",  hio_geterrbmsg(hio));
			return -1;
		}

		pair = hio_dev_pty_getxtn(pty);
		pair->x11 = &x11;
		pair->pty = hio_dev_pty_getsyshnd(pty);

		/*
		if (!term_set_size(&pty, &x11))
		   return 1;
		*/
	}

	{
		hio_dev_shw_make_t si;
		struct pair_t* pair;

		memset (&si, 0, HIO_SIZEOF(si));
		si.hnd = ConnectionNumber(x11.dpy);
		si.flags = HIO_DEV_SHW_KEEP_OPEN_ON_CLOSE | HIO_DEV_SHW_DISABLE_OUT | HIO_DEV_SHW_DISABLE_STREAM;
		si.on_ready = shw_on_ready;
		si.on_write = shw_on_write;
		si.on_read = shw_on_read;
		si.on_close = shw_on_close;
		shw = hio_dev_shw_make(hio, HIO_SIZEOF(*pair), &si);

		pair = hio_dev_shw_getxtn(shw);
		pair->x11 = &x11;
		pair->pty = hio_dev_pty_getsyshnd(pty);
	}

	hio_loop (hio);

	if (hio) hio_close (hio);

	x11_cleanup (&x11);
	return 0;
}
