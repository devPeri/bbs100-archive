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

#include "Telnet.h"
#include "Memory.h"
#include "edit.h"
#include "debug.h"
#include "cstring.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/telnet.h>

#ifndef TELOPT_NAWS
#define TELOPT_NAWS 31			/* negotiate about window size */
#endif

#ifndef TELOPT_NEW_ENVIRON
#define TELOPT_NEW_ENVIRON 39	/* set new environment variable */
#endif


Telnet *new_Telnet(void) {
Telnet *t;

	if ((t = (Telnet *)Malloc(sizeof(Telnet), TYPE_TELNET)) == NULL)
		return NULL;

	t->state = TS_DATA;
	t->term_height = TERM_HEIGHT;		/* hard-coded defaults; may be set by TELOPT_NAWS */
	t->term_width = TERM_WIDTH;
	return t;
}

void destroy_Telnet(Telnet *t) {
	Free(t);
}

/*
	this is a funny construction with the window_event handler, but
	I want to keep the telnet code separated from the rest as much as
	possible
*/
int telnet_negotiations(Telnet *t, int sock, unsigned char c, void (*window_event)(Telnet *)) {
char buf[20];

	if (t == NULL)
		return -1;

	Enter(telnet_negotiations);

	switch(t->state) {
		case TS_DATA:
			switch(c) {
				case 0:
				case '\n':
					Return -1;

				case 0x7f:			/* DEL/BS conversion */
					c = KEY_BS;
					break;

				case IAC:
					t->state = TS_IAC;
					Return -1;

				default:
					if (c > 0x7f) {
						Return -1;
					}
			}
			Return c;

		case TS_IAC:
			switch(c) {
				case IAC:				/* IAC IAC received */
					t->state = TS_DATA;
					Return 0;			/* return 0, although the BBS probably won't like it much ... */

				case AYT:
					if (sock > 0)
						write(sock, "\n[YeS]\n", 8);
					t->state = TS_DATA;
					Return -1;

				case NOP:
					t->state = TS_DATA;
					Return -1;

				case SB:
					t->in_sub++;
					Return -1;

				case SE:
					t->in_sub--;
					if (t->in_sub < 0)
						t->in_sub = 0;
					t->state = TS_DATA;
					Return -1;

				case WILL:
					t->state = TS_WILL;
					Return -1;

				case DO:
					t->state = TS_DO;
					Return -1;

/* after a SB we can have... */
				case TELOPT_NAWS:
					t->state = TS_NAWS;
					Return -1;

				case TELOPT_NEW_ENVIRON:
					t->state = TS_NEW_ENVIRON;
					Return -1;
			}
			t->state = TS_ARG;
			Return -1;


		case TS_WILL:
			switch(c) {
				case TELOPT_NAWS:
					break;

				case TELOPT_NEW_ENVIRON:		/* NEW-ENVIRON SEND */
					sprintf(buf, "%c%c%c%c%c%c", IAC, SB, TELOPT_NEW_ENVIRON, 1, IAC, SE);
					if (sock > 0)
						write(sock, buf, 6);
					break;

				default:
					sprintf(buf, "%c%c%c", IAC, DONT, c);
					if (sock > 0)
						write(sock, buf, 3);
			}
			t->state = TS_DATA;
			Return -1;

		case TS_DO:
			switch(c) {
				case TELOPT_SGA:
					break;

				case TELOPT_ECHO:
					break;

				default:
					sprintf(buf, "%c%c%c", IAC, WONT, c);
					if (sock > 0)
						write(sock, buf, 3);
			}
			t->state = TS_DATA;
			Return -1;


		case TS_NAWS:
			if (t->in_sub <= 4)				/* expect next NAWS byte */
				t->in_sub_buf[t->in_sub++] = c;
			else {
				int width, height;

				width = (unsigned int)t->in_sub_buf[1] & 0xff;
				width <<= 8;
				width |= ((unsigned int)t->in_sub_buf[2] & 0xff);

				height = (unsigned int)t->in_sub_buf[3] & 0xff;
				height <<= 8;
				height |= ((unsigned int)t->in_sub_buf[4] & 0xff);

				t->term_width = width;
				if (t->term_width < 1)
					t->term_width = TERM_WIDTH;
				if (t->term_width > MAX_TERM)
					t->term_width = MAX_TERM;

				t->term_height = height-1;
				if (t->term_height < 1)
					t->term_height = TERM_HEIGHT;
				if (t->term_height > MAX_TERM)
					t->term_height = MAX_TERM;

				t->in_sub = 0;
				t->state = TS_IAC;			/* expect SE */

				if (window_event != NULL)
					window_event(t);
			}
			Return -1;

/*
	Environment variables
*/
		case TS_NEW_ENVIRON:
			if (c == 0)								/* IS */
				t->state = TS_NEW_ENVIRON_IS;

			t->in_sub = 1;
			t->in_sub_buf[t->in_sub] = 0;
			Return -1;

		case TS_NEW_ENVIRON_IS:
			switch(c) {
				case 0:								/* expect variable */
				case 2:
				case 3:
					t->in_sub = 1;
					t->in_sub_buf[t->in_sub] = 0;
					t->state = TS_NEW_ENVIRON_VAR;
					break;

				case IAC:
					t->state = TS_IAC;				/* expect SE */
					break;

				default:
					t->state = TS_DATA;				/* must be wrong */
			}
			Return -1;

		case TS_NEW_ENVIRON_VAR:
			if (c == 1) {
				t->in_sub_buf[t->in_sub++] = 0;
				t->state = TS_NEW_ENVIRON_VAL;		/* expect value */
				Return -1;
			}
			if (c == IAC) {
				t->state = TS_IAC;					/* expect SE */
				Return -1;
			}
			if (t->in_sub < MAX_SUB_BUF - 2) {
				t->in_sub_buf[t->in_sub++] = c;
				t->in_sub_buf[t->in_sub] = 0;
			}
			Return -1;

		case TS_NEW_ENVIRON_VAL:
			if (c <= 3 || c == IAC) {				/* next variable or end of list */
				t->in_sub_buf[t->in_sub] = 0;

/* variable has been processed ; get next one or end on IAC */

				if (c == IAC)
					t->state = TS_IAC;
				else
					t->state = TS_NEW_ENVIRON_VAR;

				if (!strcmp(t->in_sub_buf+1, "USER")) {		/* entered a user name */
					t->in_sub = 1;
					t->in_sub_buf[t->in_sub] = 0;
					Return KEY_RETURN;
				}
				t->in_sub = 1;
				t->in_sub_buf[t->in_sub] = 0;
			} else {
				if (t->in_sub < MAX_SUB_BUF - 2) {
					t->in_sub_buf[t->in_sub++] = c;
					t->in_sub_buf[t->in_sub] = 0;

/* setting username, let through */
					if (!strcmp(t->in_sub_buf+1, "USER")) {
						Return c;
					}
				}
			}
			Return -1;

		case TS_ARG:
			t->state = t->in_sub ? TS_IAC : TS_DATA;
			Return -1;
	}
	Return -1;
}

/* EOB */