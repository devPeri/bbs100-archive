/*
    bbs100 1.2.1 WJ103
    Copyright (C) 2003  Walter de Jong <walter@heiho.net>

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
	state_login.c	WJ99
*/

#include "config.h"
#include "defines.h"
#include "debug.h"
#include "state_login.h"
#include "state.h"
#include "state_msg.h"
#include "state_sysop.h"
#include "edit.h"
#include "inet.h"
#include "util.h"
#include "log.h"
#include "passwd.h"
#include "Stats.h"
#include "timeout.h"
#include "CallStack.h"
#include "screens.h"
#include "cstring.h"
#include "mkdir.h"
#include "Param.h"
#include "copyright.h"
#include "access.h"
#include "Memory.h"
#include "OnlineUser.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

StringList *banished = NULL;

char *Str_Really_Logout[] = {
	"Really logout",
	"Are you sure",
	"Are you sure you are sure",
	"Are you sure you want to logout",
	"Do you really wish to logout",
	"Really logout from the BBS"
};


void state_login_prompt(User *usr, char c) {
int r;

	if (usr == NULL)
		return;

	Enter(state_login_prompt);

	if (c == INIT_STATE) {
		Put(usr, "Enter your name: ");
		usr = reset_User(usr);

		Free(usr->tmpbuf[TMP_NAME]);
		usr->tmpbuf[TMP_NAME] = NULL;
	}
	r = edit_name(usr, c);

	if (r == EDIT_BREAK) {
		Print(usr, "\nBye, and have a nice life!\n");
		close_connection(usr, "user hit break on the login prompt");
		Return;
	}
	if (r == EDIT_RETURN) {
		Free(usr->tmpbuf[TMP_NAME]);
		if ((usr->tmpbuf[TMP_NAME] = cstrdup(usr->edit_buf)) == NULL) {
			Perror(usr, "Out of memory");
			close_connection(usr, "out of memory");
			Return;
		}
		usr->edit_buf[0] = 0;
		usr->edit_pos = 0;

		if (!usr->tmpbuf[TMP_NAME][0]) {
			Put(usr, "\nPress Ctrl-D to exit\n"
				"Enter your name: ");

			usr->login_time++;
			if (usr->login_time >= MAX_LOGIN_ATTEMPTS) {
				Put(usr, "Bye! Come back when you've figured it out..!\n");
				close_connection(usr, "too many returns on login prompt");
			}
			Return;
		}
/*
	check for PARAM_NAME_SYSOP as well, by Richard of MatrixBBS
*/
		if (!strcmp(usr->tmpbuf[TMP_NAME], "Sysop") || !strcmp(usr->tmpbuf[TMP_NAME], PARAM_NAME_SYSOP)) {
			Print(usr, "You are not a Sysop, nor a %s. Go away!\n", PARAM_NAME_SYSOP);
			close_connection(usr, "attempt to login as Sysop");
			Return;
		}
/*
	Note: it's possible to banish 'New', so no new users can be added
*/
		if (in_StringList(banished, usr->tmpbuf[TMP_NAME]) != NULL) {
			Put(usr, "\nYou have been denied access to this BBS\n");
			close_connection(usr, "user %s has been banished", usr->tmpbuf[TMP_NAME]);
			Return;
		}
		if (!strcmp(usr->tmpbuf[TMP_NAME], "New")) {
			JMP(usr, STATE_NEW_LOGIN_PROMPT);
			Return;
		}
		if (is_guest(usr->tmpbuf[TMP_NAME])) {
/* give Guest an appropriate login name */
			if (is_online(PARAM_NAME_GUEST) == NULL)
				strcpy(usr->name, PARAM_NAME_GUEST);
			else {
				for(r = 2; r < 1024; r++) {
					sprintf(usr->tmpbuf[TMP_NAME], "%s %d", PARAM_NAME_GUEST, r);
					if (is_online(usr->tmpbuf[TMP_NAME]) == NULL)
						break;
				}
				if (r >= 1024) {
					Print(usr, "There are too many %s users online, please try again later\n", PARAM_NAME_GUEST);
					close_connection(usr, "too many guest users online");
					Return;
				}
				strcpy(usr->name, usr->tmpbuf[TMP_NAME]);
			}
			log_auth("LOGIN %s (%s)", usr->name, usr->from_ip);

			usr->doing = cstrdup("is just looking around");
			usr->flags |= USR_X_DISABLED;
			usr->login_time = usr->online_timer = (unsigned long)rtc;

			JMP(usr, STATE_ANSI_PROMPT);
			Return;
		}
		if (!user_exists(usr->tmpbuf[TMP_NAME])) {
			Put(usr, "No such user. ");
			usr->login_time++;
			if (usr->login_time >= MAX_LOGIN_ATTEMPTS) {
				Put(usr, "Come back when you've figured it out..!\n");
				close_connection(usr, "too many login attempts");
				Return;
			}
/* I said, it's possible to banish 'New', so no new users can be added */
			if (in_StringList(banished, "New") == NULL) {
				JMP(usr, STATE_NEW_ACCOUNT_YESNO);		/* unknown user; create new account? */
			}
		} else {
			if (load_User(usr, usr->tmpbuf[TMP_NAME], LOAD_USER_PASSWORD)) {
				Perror(usr, "Failed to load user file");
				CURRENT_STATE(usr);
				Return;
			}
			JMP(usr, STATE_PASSWORD_PROMPT);
		}
	}
	Return;
}

void state_new_account_yesno(User *usr, char c) {
	if (usr == NULL)
		return;

	Enter(state_new_account_yesno);

	if (c == INIT_STATE) {
		Put(usr, "Do you wish to create a new account? (y/N): ");
		Return;
	}
	switch(yesno(usr, c, 'N')) {
		case YESNO_YES:
			if (!allow_Wrapper(wrappers, usr->ipnum)) {
				Put(usr, "\nSorry, but you're connecting from a site that has been "
					"locked out of the BBS.\n\n");
				close_connection(usr, "new user login closed by wrapper");
				Return;
			}
			strcpy(usr->edit_buf, usr->tmpbuf[TMP_NAME]);
			usr->edit_pos = strlen(usr->edit_buf);

			MOV(usr, STATE_NEW_LOGIN_PROMPT);
			state_new_login_prompt(usr, KEY_RETURN);
			break;

		case YESNO_NO:
			Put(usr, "\n");
			JMP(usr, STATE_LOGIN_PROMPT);
			break;

		case YESNO_UNDEF:
			CURRENT_STATE(usr);
	}
	Return;
}

void state_password_prompt(User *usr, char c) {
int r;

	if (usr == NULL)
		return;

	Enter(state_password_prompt);

	if (c == INIT_STATE)
		Put(usr, "Enter password: ");

	r = edit_password(usr, c);

	if (r == EDIT_BREAK) {
		Print(usr, "\nBye, and have a nice life!\n");
		close_connection(usr, "user hit break on the login prompt");
		Return;
	}
	if (r == EDIT_RETURN) {
		if (!verify_phrase(usr->edit_buf, usr->passwd)) {
			User *u;

			if (load_User(usr, usr->tmpbuf[TMP_NAME], LOAD_USER_ALL)) {
				Perror(usr, "failed to load user data");
				close_connection(usr, "failed to load user file");
				Return;
			}
			Put(usr, "<normal>");
			if ((u = is_online(usr->tmpbuf[TMP_NAME])) != NULL) {
				Print(u, "\n<red>Connection closed by another login from %s\n", usr->from_ip);
				close_connection(u, "connection closed by another login");
			}
			strcpy(usr->name, usr->tmpbuf[TMP_NAME]);
			log_auth("LOGIN %s (%s)", usr->name, usr->from_ip);

			if (u == NULL)				/* if (u != NULL) killed by another login */
				notify_login(usr);		/* tell friends we're here */

			JMP(usr, STATE_DISPLAY_MOTD);
		} else {
			Put(usr, "Wrong password!\n\n");
			log_auth("WRONGPASSWD %s (%s)", usr->tmpbuf[TMP_NAME], usr->from_ip);

			usr->login_time++;
			if (usr->login_time >= MAX_LOGIN_ATTEMPTS) {
				Put(usr, "Come back when you've figured it out..!\n");
				close_connection(usr, "too many login attempts");
				Return;
			}
			JMP(usr, STATE_LOGIN_PROMPT);
		}
	}
	Return;
}


void state_logout_prompt(User *usr, char c) {
	if (usr == NULL)
		return;

	Enter(state_logout_prompt);

	if (c == INIT_STATE) {
		if ((usr->runtime_flags & RTF_HOLD) && usr->held_msgs != NULL)
			Print(usr, "<green>You have unread messages held\n");
		else {
			User *u;

			for(u = AllUsers; u != NULL; u = u->next) {
				if (u->runtime_flags & RTF_BUSY) {
					if ((u->runtime_flags & RTF_BUSY_SENDING)
						&& in_StringList(u->recipients, usr->name) != NULL)
/*
	warn follow-up mode by Richard of MatrixBBS
*/
						Print(usr, "<yellow>%s<green> is busy sending you a message%s\n",
							u->name, (u->flags & USR_FOLLOWUP) ? " in follow-up mode" : "");
					else {
						if ((u->runtime_flags & RTF_BUSY_MAILING)
							&& u->new_message != NULL
							&& in_StringList(u->new_message->to, usr->name) != NULL)
							Print(usr, "<yellow>%s<green> is busy mailing you a message\n", u->name);
					}
				}
			}
		}
		Print(usr, "<cyan>%s? <white>(<cyan>y<white>/<cyan>N<white>): ", RND_STR(Str_Really_Logout));
		usr->runtime_flags |= RTF_BUSY;
		Return;
	}
	switch(yesno(usr, c, 'N')) {
		case YESNO_YES:
			notify_logout(usr);
			if (logout_screen != NULL) {
				StringList *sl;

				Put(usr, "\n");
				for(sl = logout_screen; sl != NULL; sl = sl->next)
					Print(usr, "%s\n", sl->str);
			}
			log_auth("LOGOUT %s (%s)", usr->name, usr->from_ip);
			close_connection(usr, "%s has logged out from %s", usr->name, usr->from_ip);
			break;

		case YESNO_NO:
			RET(usr);
			break;

		default:
			Print(usr, "<cyan>%s? <white>(<cyan>y<white>/<cyan>N<white>): ", RND_STR(Str_Really_Logout));
	}
	Return;
}

void state_ansi_prompt(User *usr, char c) {
	if (usr == NULL)
		return;

	Enter(state_ansi_prompt);

	if (c == INIT_STATE) {
		Put(usr, "<yellow>Are you on an ANSI terminal? <white>(<yellow>Y<white>/<yellow>n<white>): ");
		usr->runtime_flags |= RTF_BUSY;
		Return;
	}
	switch(yesno(usr, c, 'Y')) {
		case YESNO_YES:
			usr->flags |= (USR_ANSI | USR_BOLD);		/* assume bold */
			Put(usr, "<normal>");
			break;

		case YESNO_NO:
			usr->flags &= ~(USR_ANSI | USR_BOLD);
			break;

		default:
			Put(usr, "\n<yellow>Are you on an ANSI terminal, <hotkey>Yes or <hotkey>No? <white>(<yellow>Y<white>/<yellow>n<white>): ");
			Return;
	}
	if (first_login != NULL) {
		listdestroy_StringList(usr->more_text);
		if ((usr->more_text = copy_StringList(first_login)) != NULL) {
/*
	for the new users, we reset the timeout timer here so they have some time
	to read the displayed text
*/
			if (usr->timer != NULL) {
				usr->timer->sleeptime = usr->timer->maxtime = PARAM_IDLE_TIMEOUT * 60;
				usr->timer->restart = TIMEOUT_USER;
				usr->timer->action = user_timeout;
			}
			MOV(usr, STATE_DISPLAY_MOTD);
			PUSH(usr, STATE_PRESS_ANY_KEY);
			read_more(usr);
			Return;
		}
	}
	JMP(usr, STATE_DISPLAY_MOTD);
	Return;
}

void state_display_motd(User *usr, char c) {
	Enter(state_display_motd);

	if (usr->timer != NULL) {			/* reset the 'timeout timer' */
		usr->timer->sleeptime = usr->timer->maxtime = PARAM_IDLE_TIMEOUT * 60;
		usr->timer->restart = TIMEOUT_USER;
		usr->timer->action = user_timeout;
	}
	if (motd_screen != NULL
		&& (usr->more_text = copy_StringList(motd_screen)) != NULL) {
		Put(usr, "\n");

		PUSH(usr, STATE_GO_ONLINE);
		read_more(usr);
		Return;
	}
	JMP(usr, STATE_GO_ONLINE);
	Return;
}

void state_go_online(User *usr, char c) {
int num_users = 0, num_friends = 0, i, new_mail;
Joined *j;
User *u;

	if (usr == NULL)
		return;

	Enter(state_go_online);

	add_Timer(&usr->timer, new_Timer(PARAM_SAVE_TIMEOUT * 60, save_timeout, TIMER_RESTART));

/*
	give the user a Mail> room
	this is already done by load_User(), but Guests and New users
*/
	if (usr->mail == NULL) {					/* happens to Guest and New users */
		if ((usr->mail = load_Mail(usr->name)) == NULL) {
			Perror(usr, "Out of memory");
			close_connection(usr, "out of memory");
			Return;
		}
	}
/*
	fix last_read field if too large (fix screwed up mail rooms)
*/

	if ((j = in_Joined(usr->rooms, 1)) != NULL) {
		MsgIndex *m;

		m = unwind_MsgIndex(usr->mail->msgs);

		if (m == NULL)
			j->last_read = 0UL;
		else
			if (j->last_read > m->number)
				j->last_read = m->number;
	}
	usr->runtime_flags &= ~RTF_BUSY;
	usr->edit_buf[0] = 0;
	usr->login_time = usr->online_timer = (unsigned long)rtc;
	usr->logins++;

	stats.num_logins++;
	update_stats(usr);

	Put(usr, "\n");
	if (usr->logins > 1)
		Print(usr, "<green>Welcome back, <yellow>%s<green>! ", usr->name);
	else {
		if (usr->doing == NULL) {
			char buf[MAX_LINE*3];

			sprintf(buf, "is new to <white>%s", PARAM_BBS_NAME);
			buf[MAX_LINE] = 0;
			usr->doing = cstrdup(buf);
		}
	}
	Print(usr, "<green>This is your <yellow>%s<green> login\n", print_numberth(usr->logins));

/*
	note that the last IP was stored in tmpbuf[TMP_FROM_HOST] by load_User() in User.c
	note that new users do not have a last_logout time
*/
	if (usr->last_logout > (time_t)0UL) {
		if (usr->tmpbuf[TMP_FROM_HOST])
			Print(usr, "<green>Last login was on <cyan>%s<green>\n"
				"From host<yellow>: %s\n",
				print_date(usr, usr->last_logout), usr->tmpbuf[TMP_FROM_HOST]);
		else
			Print(usr, "<green>Last login was on <cyan>%s\n", print_date(usr, usr->last_logout));
	}
/* free the tmp buffers as they won't be used anymore for a long time */
	for(i = 0; i < NUM_TMP; i++) {
		Free(usr->tmpbuf[i]);
		usr->tmpbuf[i] = NULL;
	}
	if (usr->flags & USR_HELPING_HAND)
		Put(usr, "<magenta>You are available to help others\n");

/* count number of users online */
	for(u = AllUsers; u != NULL; u = u->next) {
		if (u == usr)
			continue;

		if (u->socket > 0 && u->name[0])
			num_users++;

		if (in_StringList(usr->friends, u->name) != NULL)
			num_friends++;
	}
	if (!num_users)
		Put(usr, "<green>You are the one and only user online right now...\n");
	else {
		if (num_users == 1) {
			if (num_friends == 1)
				Put(usr, "<green>There is one friend online\n");
			else
				Put(usr, "<green>There is one other user online\n");
		} else {
			if (num_friends > 0) {
				num_users -= num_friends;
				Print(usr, "<green>There are <yellow>%d<green> friend%s and <yellow>%d<green> other user%s online\n",
					num_friends, (num_friends == 1) ? "" : "s",
					num_users, (num_users == 1) ? "" : "s");
			} else
				Print(usr, "<green>There are <yellow>%d<green> other users online\n", num_users);
		}
	}
	if (usr->reminder != NULL && usr->reminder[0])
		Print(usr, "\n<magenta>Reminder<yellow>: %s\n", usr->reminder);

/* if booting/shutting down, inform the user */
	if (shutdown_timer != NULL && shutdown_timer->maxtime <= 60)
		Put(usr, "\n<white>NOTE<yellow>: <red>The system is shutting down\n");
	else
		if (reboot_timer != NULL && reboot_timer->maxtime <= 60)
			Put(usr, "\n<white>NOTE<yellow>: <red>The system is rebooting\n");

	MOV(usr, STATE_ROOM_PROMPT);

/*
	if there are new Lobby posts, go to the Lobby> first regardless
	of whether you have new mail or not. New Mail> will be read right
	after having read the Lobby> anyway

	as suggested by Mz Boobala and Lightspeed of MatrixBBS
*/
	new_mail = 0;
	if (usr->mail != NULL && (j = in_Joined(usr->rooms, 1)) != NULL
		&& newMsgs(usr->mail, j->last_read) != NULL) {
		new_mail = 1;
		Put(usr, "\n<beep><cyan>You have new mail\n");
	}
	if ((j = in_Joined(usr->rooms, 0)) != NULL && newMsgs(Lobby_room, j->last_read) != NULL)
		goto_room(usr, Lobby_room);
	else {
		if (new_mail)
			goto_room(usr, usr->mail);
		else
			goto_room(usr, Lobby_room);
	}
	add_OnlineUser(usr);		/* add user to hash of online users */
	Return;
}


void state_new_login_prompt(User *usr, char c) {
int r;

	if (usr == NULL)
		return;

	Enter(state_new_login_prompt);

	if (c == INIT_STATE) {
		if (!allow_Wrapper(wrappers, usr->ipnum)) {
			Put(usr, "\nSorry, but you're connecting from a site that has been "
				"locked out of the BBS.\n\n");
			close_connection(usr, "new user login closed by wrapper");
			Return;
		}
		log_auth("NEWLOGIN (%s)", usr->from_ip);

		Put(usr, "\nHello there, new user! You may choose a name that suits you well.\n"
			"This name will be your alias for the rest of your BBS life.\n"
			"Enter your name: ");

		usr = reset_User(usr);

		Free(usr->tmpbuf[TMP_NAME]);
		usr->tmpbuf[TMP_NAME] = NULL;
	}
	r = edit_name(usr, c);

	if (r == EDIT_BREAK) {
		Print(usr, "\nBye, and have a nice life!\n");
		close_connection(usr, "user hit break on the login prompt");
		Return;
	}
	if (r == EDIT_RETURN) {
		Free(usr->tmpbuf[TMP_NAME]);
		if ((usr->tmpbuf[TMP_NAME] = cstrdup(usr->edit_buf)) == NULL) {
			Perror(usr, "Out of memory");
			close_connection(usr, "out of memory");
			Return;
		}
		usr->edit_buf[0] = 0;
		usr->edit_pos = 0;

		if (!usr->tmpbuf[TMP_NAME][0]) {
			Put(usr, "\nPress Ctrl-D to exit\n");
			JMP(usr, STATE_LOGIN_PROMPT);
			Return;
		}
		if (!usr->tmpbuf[TMP_NAME][1]) {
			Put(usr, "\nThat name is too short\n"
				"Enter your name: ");

			Free(usr->tmpbuf[TMP_NAME]);
			usr->tmpbuf[TMP_NAME] = NULL;
			Return;
		}
		if (in_StringList(banished, usr->tmpbuf[TMP_NAME]) != NULL) {
			Put(usr, "\nYou have been denied access to this BBS.\n");
			close_connection(usr, "user has been banished");
			Return;
		}
		if (!strcmp(usr->tmpbuf[TMP_NAME], "New")) {
			Put(usr, "\nYou cannot use 'New' as login name, choose another login name\n"
				"Enter your login name: ");

			Free(usr->tmpbuf[TMP_NAME]);
			usr->tmpbuf[TMP_NAME] = NULL;
			Return;
		}
		if (!strcmp(usr->tmpbuf[TMP_NAME], "Sysop")
			|| !strcmp(usr->tmpbuf[TMP_NAME], PARAM_NAME_SYSOP)
			|| is_guest(usr->tmpbuf[TMP_NAME])) {
			Print(usr, "\nYou cannot use '%s' as login name, choose another login name\n"
				"Enter your login name: ", usr->tmpbuf[TMP_NAME]);

			Free(usr->tmpbuf[TMP_NAME]);
			usr->tmpbuf[TMP_NAME] = NULL;
			Return;
		}
		if (user_exists(usr->tmpbuf[TMP_NAME])) {
			Put(usr, "\nThat name already is in use, please choose an other login name\n"
				"Enter your login name: ");

			Free(usr->tmpbuf[TMP_NAME]);
			usr->tmpbuf[TMP_NAME] = NULL;
			Return;
		}
		Put(usr, "Now to choose a password. Passwords can be 79 characters long and can contain\n"
			"spaces and punctuation characters. Be sure not to use a password that can be\n"
			"guessed easily by anyone. Also be sure not to forget your own password..!\n");
		JMP(usr, STATE_NEW_PASSWORD_PROMPT);
	}
	Return;
}

void state_new_password_prompt(User *usr, char c) {
int r;

	if (usr == NULL)
		return;

	Enter(state_new_password_prompt);

	if (c == INIT_STATE)
		Put(usr, "Enter new password: ");

	r = edit_password(usr, c);

	if (r == EDIT_BREAK) {
		JMP(usr, STATE_LOGIN_PROMPT);
		Return;
	}
	if (r == EDIT_RETURN) {
		if (!usr->edit_buf[0]) {
			JMP(usr, STATE_LOGIN_PROMPT);
			Return;
		}
		if (strlen(usr->edit_buf) < 5) {
			Put(usr, "\nThat password is too short\n");
			JMP(usr, STATE_NEW_PASSWORD_PROMPT);
			Return;
		}
/* check passwd same as username */
		if (!cstricmp(usr->edit_buf, usr->tmpbuf[TMP_NAME])) {
			Put(usr, "\nThat password is not good enough\n");
			JMP(usr, STATE_NEW_PASSWORD_PROMPT);
			Return;
		}
		Free(usr->tmpbuf[TMP_PASSWD]);
		if ((usr->tmpbuf[TMP_PASSWD] = cstrdup(usr->edit_buf)) == NULL) {
			Perror(usr, "Out of memory");
			close_connection(usr, "out of memory");
			Return;
		}
		JMP(usr, STATE_NEW_PASSWORD_AGAIN);
	}
	Return;
}

void state_new_password_again(User *usr, char c) {
int r;

	if (usr == NULL)
		return;

	Enter(state_new_password_again);

	if (c == INIT_STATE)
		Put(usr, "Enter it again (for verification): ");

	r = edit_password(usr, c);

	if (r == EDIT_BREAK) {
		Print(usr, "\nBye, and have a nice life!\n");
		close_connection(usr, "user hit break on the login prompt");
		Return;
	}
	if (r == EDIT_RETURN) {
		char *crypted;
		int i;

		Put(usr, "\n");

		if (!usr->edit_buf[0]) {
			JMP(usr, STATE_NEW_LOGIN_PROMPT);
			Return;
		}
		if (strcmp(usr->edit_buf, usr->tmpbuf[TMP_PASSWD])) {
			Put(usr, "Passwords didn't match!\n");
			JMP(usr, STATE_NEW_PASSWORD_PROMPT);
			Return;
		}
/* from here we have a name -- from here on others can see the new user online */
		strcpy(usr->name, usr->tmpbuf[TMP_NAME]);
		i = strlen(usr->name)-1;
		if (usr->name[i] == ' ')
			usr->name[i] = 0;

		stats.youngest_birth = usr->birth = usr->login_time = usr->online_timer = rtc;
		strcpy(stats.youngest, usr->name);

		crypted = crypt_phrase(usr->edit_buf);
		crypted[MAX_CRYPTED_PASSWD-1] = 0;

		if (verify_phrase(usr->edit_buf, crypted)) {
			Perror(usr, "bug in password encryption -- please choose an other password");
			JMP(usr, STATE_NEW_PASSWORD_PROMPT);
			Return;
		}
		strcpy(usr->passwd, crypted);

		sprintf(usr->edit_buf, "%s/%c/%s", PARAM_USERDIR, usr->name[0], usr->name);
		if (mkdir(usr->edit_buf, (mode_t)0750)) {
			Perror(usr, "Failed to create user directory");
		}
		log_auth("NEWUSER %s (%s)", usr->name, usr->from_ip);

/* save user here, or we're not able to X/profile him yet! */
		if (usr->logins <= 1 && save_User(usr)) {
			Perror(usr, "Failed to save userfile");
		}
		JMP(usr, STATE_ANSI_PROMPT);
	}
	Return;
}

/*
	Damn, it's too much work to reset each member of the User struct,
	so just destroy the thing and allocate a new User
*/
User *reset_User(User *usr) {
User *u;

	Enter(reset_User);

	if ((u = new_User()) == NULL) {
		Perror(usr, "Out of memory");
		close_connection(usr, "Out of memory in reset_User()");
		Return NULL;
	}
	u->socket = usr->socket;
	u->telnet_state = usr->telnet_state;
	u->in_sub = usr->in_sub;
	u->output_idx = usr->output_idx;
	memcpy(u->in_sub_buf, usr->in_sub_buf, MAX_SUB_BUF);
	memcpy(u->outputbuf, usr->outputbuf, MAX_OUTPUTBUF);

	u->term_width = usr->term_width;
	u->term_height = usr->term_height;
	u->runtime_flags = usr->runtime_flags;

	u->login_time = usr->login_time;
	strcpy(u->from_ip, usr->from_ip);
	u->ipnum = usr->ipnum;

	u->callstack = usr->callstack;		/* steal a callstack ;) */
	usr->callstack = NULL;

	usr->socket = -1;		/* dangling user will be removed by mainloop() */

	default_colors(u);

	add_User(&AllUsers, u);
	Return u;
}

/* EOB */
