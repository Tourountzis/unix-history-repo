/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)rexecd.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <err.h>
#ifdef DEBUG
#include <fcntl.h>
#endif
#include <libutil.h>
#include <opie.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

char	username[MAXLOGNAME + 5 + 1] = "USER=";
char	homedir[MAXPATHLEN + 5 + 1] = "HOME=";
char	shell[MAXPATHLEN + 6 + 1] = "SHELL=";
char	path[sizeof(_PATH_DEFPATH) + sizeof("PATH=")] = "PATH=";
char	*envinit[] =
	    {homedir, shell, path, username, 0};
char	**environ;
char	remote[MAXHOSTNAMELEN];

struct	sockaddr_storage asin;

void doit(int, struct sockaddr *);
void getstr(char *, int, char *);
void error(const char *fmt, ...);

int no_uid_0 = 1;

void
usage(void)
{
	syslog(LOG_ERR, "usage: rexecd [-i]");
	exit(1);
}

/*
 * remote execute server:
 *	username\0
 *	password\0
 *	command\0
 *	data
 */
/*ARGSUSED*/
int
main(int argc, char *argv[])
{
	struct sockaddr_storage from;
	int fromlen;
	int ch;

	openlog("rexecd", LOG_PID, LOG_AUTH);

	while ((ch = getopt(argc, argv, "i")) != -1)
		switch (ch) {
		case 'i':
			no_uid_0 = 0;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	fromlen = sizeof (from);
	if (getpeername(0, (struct sockaddr *)&from, &fromlen) < 0)
		err(1, "getpeername");

	realhostname_sa(remote, sizeof(remote) - 1,
			(struct sockaddr *)&from, fromlen);

	doit(0, (struct sockaddr *)&from);
	return(0);
}

void
doit(int f, struct sockaddr *fromp)
{
	FILE *fp;
	char cmdbuf[NCARGS+1], *cp;
	const char *namep;
	char user[16];
#ifdef OPIE
	struct opie opiedata;
	char pass[OPIE_RESPONSE_MAX+1], opieprompt[OPIE_CHALLENGE_MAX+1];
#else /* OPIE */
	char pass[16];
#endif /* OPIE */
	struct passwd *pwd;
	int s;
	u_short port;
	int pv[2], pid, ready, readfrom, cc;
	char buf[BUFSIZ], sig;
	int one = 1;
	int authenticated = 0;

	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
	(void) signal(SIGTERM, SIG_DFL);
#ifdef DEBUG
	{ int t = open(_PATH_TTY, 2);
	  if (t >= 0) {
		ioctl(t, TIOCNOTTY, (char *)0);
		(void) close(t);
	  }
	}
#endif
	dup2(f, 0);
	dup2(f, 1);
	dup2(f, 2);
	(void) alarm(60);
	port = 0;
	for (;;) {
		char c;
		if (read(f, &c, 1) != 1)
			exit(1);
		if (c == 0)
			break;
		port = port * 10 + c - '0';
	}
	(void) alarm(0);
	if (port != 0) {
		s = socket(fromp->sa_family, SOCK_STREAM, 0);
		if (s < 0)
			exit(1);
		bzero(&asin, sizeof(asin));
		asin.ss_family = fromp->sa_family;
		asin.ss_len = fromp->sa_len;
		if (bind(s, (struct sockaddr *)&asin, asin.ss_len) < 0)
			exit(1);
		(void) alarm(60);
		switch (fromp->sa_family) {
		case AF_INET:
			((struct sockaddr_in *)fromp)->sin_port = htons(port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)fromp)->sin6_port = htons(port);
			break;
		default:
			exit(1);
		}
		if (connect(s, fromp, fromp->sa_len) < 0)
			exit(1);
		(void) alarm(0);
	}
	getstr(user, sizeof(user), "username");
	getstr(pass, sizeof(pass), "password");
	getstr(cmdbuf, sizeof(cmdbuf), "command");
	setpwent();
	pwd = getpwnam(user);
	if (pwd == NULL) {
		error("Login incorrect.\n");
		exit(1);
	}
	endpwent();
	if (*pwd->pw_passwd != '\0') {
#ifdef OPIE
		opiechallenge(&opiedata, user, opieprompt);
		if (!opieverify(&opiedata, pass))
			authenticated = 1;
#endif /* OPIE */
		if (!authenticated) {
			namep = crypt(pass, pwd->pw_passwd);
			if (!strcmp(namep, pwd->pw_passwd))
				authenticated = 1;
		}
		if (!authenticated) {
			syslog(LOG_ERR, "LOGIN FAILURE from %s, %s",
			       remote, user);
			error("Login incorrect.\n");
			exit(1);
		}
	}

	if ((pwd->pw_uid == 0 && no_uid_0) || *pwd->pw_passwd == '\0' ||
	    (pwd->pw_expire && time(NULL) >= pwd->pw_expire)) {
		syslog(LOG_ERR, "%s LOGIN REFUSED from %s", user, remote);
		error("Login incorrect.\n");
		exit(1);
	}

	if ((fp = fopen(_PATH_FTPUSERS, "r")) != NULL) {
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			if ((cp = index(buf, '\n')) != NULL)
				*cp = '\0';
			if (strcmp(buf, pwd->pw_name) == 0) {
				syslog(LOG_ERR, "%s LOGIN REFUSED from %s",
				       user, remote);
				error("Login incorrect.\n");
				exit(1);
			}
		}
	}
	(void) fclose(fp);

	syslog(LOG_INFO, "login from %s as %s", remote, user);

	(void) write(STDERR_FILENO, "\0", 1);
	if (port) {
		(void) pipe(pv);
		pid = fork();
		if (pid == -1)  {
			error("Try again.\n");
			exit(1);
		}
		if (pid) {
			(void) close(0); (void) close(1); (void) close(2);
			(void) close(f); (void) close(pv[1]);
			readfrom = (1<<s) | (1<<pv[0]);
			ioctl(pv[1], FIONBIO, (char *)&one);
			/* should set s nbio! */
			do {
				ready = readfrom;
				(void) select(16, (fd_set *)&ready,
				    (fd_set *)NULL, (fd_set *)NULL,
				    (struct timeval *)NULL);
				if (ready & (1<<s)) {
					if (read(s, &sig, 1) <= 0)
						readfrom &= ~(1<<s);
					else
						killpg(pid, sig);
				}
				if (ready & (1<<pv[0])) {
					cc = read(pv[0], buf, sizeof (buf));
					if (cc <= 0) {
						shutdown(s, 1+1);
						readfrom &= ~(1<<pv[0]);
					} else
						(void) write(s, buf, cc);
				}
			} while (readfrom);
			exit(0);
		}
		setpgrp(0, getpid());
		(void) close(s); (void)close(pv[0]);
		dup2(pv[1], 2);
	}
	if (*pwd->pw_shell == '\0')
		pwd->pw_shell = _PATH_BSHELL;
	if (f > 2)
		(void) close(f);
	if (setlogin(pwd->pw_name) < 0)
		syslog(LOG_ERR, "setlogin() failed: %m");
	(void) setgid((gid_t)pwd->pw_gid);
	initgroups(pwd->pw_name, pwd->pw_gid);
	(void) setuid((uid_t)pwd->pw_uid);
	(void)strcat(path, _PATH_DEFPATH);
	environ = envinit;
	strncat(homedir, pwd->pw_dir, sizeof(homedir)-6);
	strncat(shell, pwd->pw_shell, sizeof(shell)-7);
	strncat(username, pwd->pw_name, sizeof(username)-6);
	cp = strrchr(pwd->pw_shell, '/');
	if (cp)
		cp++;
	else
		cp = pwd->pw_shell;
	if (chdir(pwd->pw_dir) < 0) {
		error("No remote directory.\n");
		exit(1);
	}
	execl(pwd->pw_shell, cp, "-c", cmdbuf, (char *)0);
	err(1, "%s", pwd->pw_shell);
}

void
error(const char *fmt, ...)
{
	char buf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	buf[0] = 1;
	(void)vsnprintf(buf + 1, sizeof(buf) - 1, fmt, ap);
	(void)write(STDERR_FILENO, buf, strlen(buf));
	va_end(ap);
}

void
getstr(char *buf, int cnt, char *err)
{
	char c;

	do {
		if (read(STDIN_FILENO, &c, 1) != 1)
			exit(1);
		*buf++ = c;
		if (--cnt == 0) {
			error("%s too long\n", err);
			exit(1);
		}
	} while (c != 0);
}
