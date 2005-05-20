/*
    bbs100 2.2 WJ105
    Copyright (C) 2005  Walter de Jong <walter@heiho.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
	Display.c	WJ105
*/

#include "Display.h"
#include "Memory.h"
#include "defines.h"

#include <stdio.h>
#include <stdlib.h>


Display *new_Display(void) {
Display *d;

	if ((d = (Display *)Malloc(sizeof(Display), TYPE_DISPLAY)) == NULL)
		return NULL;

	if ((d->buf = new_StringIO()) == NULL) {
		destroy_Display(d);
		return NULL;
	}
	d->term_width = TERM_WIDTH;
	d->term_height = TERM_HEIGHT;
	return d;
}

void destroy_Display(Display *d) {
	if (d == NULL)
		return;

	destroy_StringIO(d->buf);
	d->buf = NULL;

	Free(d);
}

void enable_more_prompt(Display *d) {
	if (d == NULL)
		return;

	free_StringIO(d->buf);
	d->more_prompt = 1;
}

void disable_more_prompt(Display *d) {
	if (d == NULL)
		return;

	free_StringIO(d->buf);
	d->more_prompt = 0;
}

/* EOB */