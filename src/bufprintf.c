/*
    bbs100 3.0 WJ105
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
	bufprintf.c	WJ105

	place-holder functions for snprintf() and vsnprintf(), which may not be
	present everywhere yet

	Do not call these function directly
	Use the defines bufprintf() and bufvprintf() instead
*/

#include "bufprintf.h"

#include <stdio.h>


int buf_printf(char *buf, int size, char *fmt, ...) {
va_list args;

	va_start(args, fmt);
	return buf_vprintf(buf, size, fmt, args);
}

/*
	Note: the size parameter is not used at all ...
*/
int buf_vprintf(char *buf, int size, char *fmt, va_list args) {
int ret;

	ret = vsprintf(buf, fmt, args);
	va_end(args);
	return ret;
}

/* EOB */
