# include <signal.h>
# include <pwd.h>
# include <sys/types.h>
# include <sys/stat.h>
# define  _DEFINE
# include "sendmail.h"
# ifdef LOG
# include <syslog.h>
# endif LOG

static char	SccsId[] = "@(#)main.c	3.45	10/22/81";

/*
**  SENDMAIL -- Post mail to a set of destinations.
**
**	This is the basic mail router.  All user mail programs should
**	call this routine to actually deliver mail.  Sendmail in
**	turn calls a bunch of mail servers that do the real work of
**	delivering the mail.
**
**	Sendmail is driven by tables defined in conf.c.  This
**	file will be different from system to system, but the rest
**	of the code will be the same.  This table could be read in,
**	but it seemed nicer to have it compiled in, since deliver-
**	mail will potentially be exercised a lot.
**
**	Usage:
**		/etc/sendmail [-f name] [-a] [-q] [-v] [-n] [-m] addr ...
**
**	Positional Parameters:
**		addr -- the address to deliver the mail to.  There
**			can be several.
**
**	Flags:
**		-f name		The mail is from "name" -- used for
**				the header in local mail, and to
**				deliver reports of failures to.
**		-r name		Same as -f; however, this flag is
**				reserved to indicate special processing
**				for remote mail delivery as needed
**				in the future.  So, network servers
**				should use -r.
**		-Ffullname	Select what the full-name should be
**				listed as.
**		-a		This mail should be in ARPANET std
**				format (obsolete version).
**		-am		Called from an FTP "MAIL" command.
**		-af		Called from an FTP "MLFL" command.
**		-n		Don't do aliasing.  This might be used
**				when delivering responses, for
**				instance.
**		-d		Run in debug mode.
**		-em		Mail back a response if there was an
**				error in processing.  This should be
**				used when the origin of this message
**				is another machine.
**		-ew		Write back a response if the user is
**				still logged in, otherwise, act like
**				-em.
**		-eq		Don't print any error message (just
**				return exit status).
**		-ep		(default)  Print error messages
**				normally.
**		-ee		Send BerkNet style errors.  This
**				is equivalent to MailBack except
**				that it has gives zero return code
**				(unless there were errors during
**				returning).  This used to be
**				"EchoBack", but you know how the old
**				software bounces.
**		-m		In group expansion, send to the
**				sender also (stands for the Mail metoo
**				option.
**		-i		Do not terminate mail on a line
**				containing just dot.
**		-s		Save UNIX-like "From" lines on the
**				front of messages.
**		-v		Give blow-by-blow description of
**				everything that happens.
**		-t		Read "to" addresses from message.
**				Looks at To:, Cc:, and Bcc: lines.
**		-I		Initialize the DBM alias files from
**				the text format files.
**		-Cfilename	Use alternate configuration file.
**		-Afilename	Use alternate alias file.
**		-DXvalue	Define macro X to have value.
**
**	Return Codes:
**		As defined in <sysexits.h>.
**
**		These codes are actually returned from the auxiliary
**		mailers; it is their responsibility to make them
**		correct.
**
**	Compilation Flags:
**		LOG -- if set, everything is logged.
**
**	Author:
**		Eric Allman, UCB/INGRES
*/





char	InFileName[] = "/tmp/mailtXXXXXX";
char	Transcript[] = "/tmp/mailxXXXXXX";
int	NextMailer = 0;		/* "free" index into Mailer struct */
static char	*FullName;	/* sender's full name */






main(argc, argv)
	int argc;
	char **argv;
{
	register char *p;
	char *realname;
	extern char *getlogin();
	char *locname;
	extern int finis();
	extern char Version[];
	char *from;
	typedef int (*fnptr)();
	extern char *arpadate();
	extern char *AliasFile;		/* location of alias file */
	extern char *ConfFile;		/* location of configuration file */
	extern char *StatFile;		/* location of statistics summary */
	register int i;
	bool verifyonly = FALSE;	/* only verify names */
	bool safecf = TRUE;		/* this conf file is sys default */
	char pbuf[10];			/* holds pid */
	char tbuf[10];			/* holds "current" time */
	char cbuf[5];			/* holds hop count */
	char dbuf[30];			/* holds ctime(tbuf) */
	char ybuf[10];			/* holds tty id */
	char ibuf[30];			/* holds HostName */
	bool aliasinit = FALSE;
	extern char *ttyname();
	extern bool safefile();
	bool canrename;

	argv[argc] = NULL;
	InChannel = stdin;
	OutChannel = stdout;
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void) signal(SIGINT, finis);
	if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
		(void) signal(SIGHUP, finis);
	(void) signal(SIGTERM, finis);
	setbuf(stdout, (char *) NULL);
	OldUmask = umask(0);
# ifdef LOG
	openlog("sendmail", 0);
# endif LOG
# ifdef DEBUG
# ifdef DEBUGFILE
	if ((i = open(DEBUGFILE, 1)) > 0)
	{
		(void) lseek(i, 0L, 2);
		(void) close(1);
		(void) dup(i);
		(void) close(i);
		Debug++;
	}
# endif DEBUGFILE
# endif
	errno = 0;

	/*
	**  Initialize and define basic system macros.
	**	Collect should be called first, so that the time
	**	corresponds to the time that the messages starts
	**	getting sent, rather than when it is first composed.
	*/

	from = NULL;

	/* process id */
	(void) sprintf(pbuf, "%d", getpid());
	define('p', pbuf);

	/* hop count */
	(void) sprintf(cbuf, "%d", HopCount);
	define('c', cbuf);

	/* time as integer, unix time, arpa time */
	(void) time(&CurTime);
	(void) sprintf(tbuf, "%ld", &CurTime);
	define('t', tbuf);
	(void) strcpy(dbuf, ctime(&CurTime));
	*index(dbuf, '\n') = '\0';
	define('d', dbuf);
	p =  newstr(arpadate(dbuf));
	define('a', p);
	define('b', p);

	/* version */
	define('v', Version);

	/* tty name */
	p = ttyname(2);
	if (p != NULL)
	{
		if (rindex(p, '/') != NULL)
			p = rindex(p, '/') + 1;
		strcpy(ybuf, p);
		define('y', ybuf);
	}

	/*
	** Crack argv.
	*/

	while (--argc > 0 && (p = *++argv)[0] == '-')
	{
		switch (p[1])
		{
		  case 'r':	/* obsolete -f flag */
		  case 'f':	/* from address */
			p += 2;
			if (*p == '\0')
			{
				p = *++argv;
				if (--argc <= 0 || *p == '-')
				{
					syserr("No \"from\" person");
					argc++;
					argv--;
					break;
				}
			}
			if (from != NULL)
			{
				syserr("More than one \"from\" person");
				break;
			}
			from = p;
			break;

		  case 'F':	/* set full name */
			p += 2;
			if (*p == '\0')
			{
				p = *++argv;
				if (--argc <= 0 || *p == '-')
				{
					syserr("Bad -F flag");
					argc++;
					argv--;
					break;
				}
			}
			FullName = p;
			break;

		  case 'h':	/* hop count */
			p += 2;
			if (*p == '\0')
			{
				p = *++argv;
				if (--argc <= 0 || *p < '0' || *p > '9')
				{
					syserr("Bad hop count (%s)", p);
					argc++;
					argv--;
					break;
				}
			}
			HopCount = atoi(p);
			break;

		  case 'e':	/* error message disposition */
			switch (p[2])
			{
			  case 'p':	/* print errors normally */
				break;	/* (default) */

			  case 'q':	/* be silent about it */
				(void) freopen("/dev/null", "w", stdout);
				break;

			  case 'm':	/* mail back */
				MailBack++;
				openxscrpt();
				break;

			  case 'e':	/* do berknet error processing */
				BerkNet++;
				openxscrpt();
				break;

			  case 'w':	/* write back (or mail) */
				WriteBack++;
				openxscrpt();
				break;
			}
			break;

# ifdef DEBUG
		  case 'd':	/* debug */
			Debug = atoi(&p[2]);
			if (Debug <= 0)
				Debug = 1;
			printf("Version %s Debug %d\n", Version, Debug);
			break;

		  case 'M':	/* redefine internal macro */
			define(p[2], &p[3]);
			break;
# endif DEBUG

		  case 'C':	/* select configuration file */
			if (p[2] == '\0')
				ConfFile = "sendmail.cf";
			else
				ConfFile = &p[2];
			safecf = FALSE;
			break;

		  case 'A':	/* select alias file */
			if (p[2] == '\0')
				AliasFile = "aliases";
			else
				AliasFile = &p[2];
			break;
		
		  case 'n':	/* don't alias */
			NoAlias++;
			break;

# ifdef DBM
		  case 'I':	/* initialize alias DBM file */
			aliasinit = TRUE;
			Verbose = TRUE;
			break;
# endif DBM

		  case 'm':	/* send to me too */
			MeToo++;
			break;

		  case 'i':	/* don't let dot stop me */
			IgnrDot++;
			break;

		  case 'V':	/* verify only */
			verifyonly = TRUE;
			break;

		  case 'a':	/* arpanet format */
			switch (p[2])
			{
			  case 'f':	/* mail from file connection */
				ArpaMode = ARPA_FILE;
				break;

			  case 'm':	/* mail over telnet connection */
				ArpaMode = ARPA_MAIL;
				break;

			  default:
				ArpaMode = ARPA_OLD;
				break;
			}
			break;
		
		  case 's':	/* save From lines in headers */
			SaveFrom++;
			break;

		  case 'v':	/* give blow-by-blow description */
			Verbose++;
			break;

		  case 't':	/* read recipients from message */
			GrabTo = TRUE;
			break;

		  case 'D':	/* run as a daemon */
			Daemon = TRUE;
			/* explicit fall-through */

		  case 'p':	/* run SMTP protocol */
			Smtp = TRUE;
			ArpaMode = ARPA_SMTP;
			break;

		  default:
			/* at Eric Schmidt's suggestion, this will not be an error....
			syserr("Unknown flag %s", p);
			... seems that upward compatibility will be easier. */
			break;
		}
	}

	/*
	**  Read system control file.
	*/

	readcf(ConfFile, safecf);
	(void) expand("$i", ibuf, &ibuf[sizeof ibuf - 1]);
	HostName = ibuf;

	/*
	**  Initialize aliases.
	*/

	initaliases(AliasFile, aliasinit);
# ifdef DBM
	if (aliasinit)
		exit(EX_OK);
# endif DBM

# ifdef DEBUG
	if (Debug > 15)
	{
		/* print configuration table (or at least part of it) */
		printrules();
		for (i = 0; i < MAXMAILERS; i++)
		{
			register struct mailer *m = Mailer[i];

			if (m == NULL)
				continue;
			printf("mailer %d: %s %s %o %s\n", i, m->m_name,
			       m->m_mailer, m->m_flags, m->m_from);
		}
	}
# endif DEBUG

	/*
	**  If a daemon, wait for a request.
	**	getrequests will always return in a child.
	*/

	if (Daemon)
		getrequests();
	
	/*
	if (Smtp)
		smtp();

	/*
	**  Set the sender
	*/

	setsender(from);

	if (!Daemon && argc <= 0 && !GrabTo)
		usrerr("Usage: /etc/sendmail [flags] addr...");

	/*
	**  Process Hop count.
	**	The Hop count tells us how many times this message has
	**	been processed by sendmail.  If it exceeds some
	**	fairly large threshold, then we assume that we have
	**	an infinite forwarding loop and die.
	*/

	if (++HopCount > MAXHOP)
		syserr("Infinite forwarding loop (%s->%s)", From.q_paddr, *argv);

	/*
	**  Scan argv and deliver the message to everyone.
	**	Actually, suppress delivery if we are taking To:
	**	lines from the message.
	*/

	if (GrabTo)
		DontSend = TRUE;
	sendtoargv(argv);

	/* if we have had errors sofar, drop out now */
	if (Errors > 0 && ExitStat == EX_OK)
		ExitStat = EX_USAGE;
	if (ArpaMode > ARPA_OLD && ExitStat != EX_OK)
		finis();

	/*
	**  Read the input mail.
	*/

	DontSend = FALSE;
	To = NULL;
	if (!verifyonly || GrabTo)
		collect();
	errno = 0;

	/* collect statistics */
	Stat.stat_nf[From.q_mailer]++;
	Stat.stat_bf[From.q_mailer] += kbytes(MsgSize);

	/*
	**  Arrange that the person who is sending the mail
	**  will not be expanded (unless explicitly requested).
	*/

# ifdef DEBUG
	if (Debug)
		printf("From person = \"%s\"\n", From.q_paddr);
# endif DEBUG

	From.q_flags |= QDONTSEND;
	if (!MeToo)
		recipient(&From);
	To = NULL;

	/*
	**  Actually send everything.
	**	If verifying, just ack.
	*/

	sendall(verifyonly);

	/*
	** All done.
	*/

	To = NULL;
	if (Errors == 0)
	{
		switch (ArpaMode)
		{
			static char *okmsg = "Mail accepted";
			extern char Arpa_Fmsg[], Arpa_Mmsg[];

		  case ARPA_FILE:
			message(Arpa_Fmsg, okmsg);
			break;

		  case ARPA_MAIL:
			message(Arpa_Mmsg, okmsg);
			break;
		}
	}
	if (!verifyonly)
		poststats(StatFile);
	finis();
}
/*
**  SETFROM -- set the person who this message is from
**
**	Under certain circumstances allow the user to say who
**	s/he is (using -f or -r).  These are:
**	1.  The user's uid is zero (root).
**	2.  The user's login name is "network" (mail from
**	    a network server).
**	3.  The user's login name is "uucp" (mail from the
**	    uucp network).
**	4.  The address the user is trying to claim has a
**	    "!" character in it (since #3 doesn't do it for
**	    us if we are dialing out).
**	A better check to replace #3 & #4 would be if the
**	effective uid is "UUCP" -- this would require me
**	to rewrite getpwent to "grab" uucp as it went by,
**	make getname more nasty, do another passwd file
**	scan, or compile the UID of "UUCP" into the code,
**	all of which are reprehensible.
**
**	Assuming all of these fail, we figure out something
**	ourselves.
**
**	Parameters:
**		from -- the person it is from.
**		realname -- the actual person executing sendmail.
**			If NULL, then take whoever we previously
**			thought was the from person.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets sendmail's notion of who the from person is.
*/

setfrom(from, realname)
	char *from;
	char *realname;
{
	register char **pvp;
	char frombuf[MAXNAME];
	extern char **prescan();
	extern char *index();

	if (realname == NULL)
		realname = From.q_paddr;
	if (from != NULL)
	{
		if (strcmp(realname, "network") != 0 && strcmp(realname, "uucp") != 0 &&
# ifdef DEBUG
		    (Debug == 0 || getuid() != geteuid()) &&
# endif DEBUG
		    index(from, '!') == NULL && getuid() != 0)
		{
			/* network sends -r regardless (why why why?) */
			/* syserr("%s, you cannot use the -f flag", realname); */
			from = NULL;
		}
	}

	SuprErrs = TRUE;
	if (from == NULL || parse(from, &From, 0) == NULL)
	{
		from = newstr(realname);
		(void) parse(from, &From, 0);
	}
	else
		FromFlag = TRUE;
	SuprErrs = FALSE;
	From.q_uid = getuid();
	From.q_gid = getgid();
# ifndef V6
	From.q_home = getenv("HOME");
# endif V6

	/*
	**  Rewrite the from person to dispose of possible implicit
	**	links in the net.
	*/

	pvp = prescan(from, '\0');
	if (pvp == NULL)
	{
		syserr("cannot prescan from (%s)", from);
		finis();
	}
	rewrite(pvp, 1);
	cataddr(pvp, frombuf, sizeof frombuf);
	define('f', newstr(frombuf));
}
/*
**  FINIS -- Clean up and exit.
**
**	Parameters:
**		none
**
**	Returns:
**		never
**
**	Side Effects:
**		exits sendmail
*/

finis()
{
	/* mail back the transcript on errors */
	if (ExitStat != EX_OK)
		savemail();

	if (HasXscrpt)
		(void) unlink(Transcript);
	(void) unlink(InFileName);
	exit(ExitStat);
}
/*
**  OPENXSCRPT -- Open transcript file
**
**	Creates a transcript file for possible eventual mailing or
**	sending back.
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		Turns the standard output into a special file
**			somewhere.
*/

openxscrpt()
{
	extern char *mktemp();

	(void) mktemp(Transcript);
	HasXscrpt++;
	if (freopen(Transcript, "w", stdout) == NULL)
		syserr("Can't create %s", Transcript);
	(void) chmod(Transcript, 0600);
	setbuf(stdout, (char *) NULL);
}
/*
**  SETSENDER -- set sendmail's idea of the sender.
**
**	Parameters:
**		from -- the person we would like to believe this
**			is from.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets the idea of the sender.
*/

setsender(from)
	char *from;
{
	register char *p;
	extern char *getlogin();
	register struct passwd *pw;
	char *realname;
	char cfbuf[40];

	/*
	**  Figure out the real user executing us.
	**	Getlogin can return errno != 0 on non-errors.
	*/

	if (!Smtp)
	{
		errno = 0;
		p = getlogin();
		errno = 0;
	}
	else
		p = from;
	if (p != NULL)
	{
		extern struct passwd *getpwnam();

		pw = getpwnam(p);
		if (pw == NULL)
		{
			if (!Smtp)
				syserr("Who are you? (name=%s)", p);
			p = NULL;
		}
	}
	if (p == NULL)
	{
		extern struct passwd *getpwuid();
		int uid;

		uid = getruid();
		pw = getpwuid(uid);
		if (pw == NULL)
			syserr("Who are you? (uid=%d)", uid);
		else
			p = pw->pw_name;
	}
	if (p == NULL || p[0] == '\0' || pw == NULL)
		finis();

	realname = p;

	/*
	**  Process passwd file entry.
	*/

	/* run user's .mailcf file */
	define('z', pw->pw_dir);
	(void) expand("$z/.mailcf", cfbuf, &cfbuf[sizeof cfbuf - 1]);
	if (safefile(cfbuf, getruid(), S_IREAD))
		readcf(cfbuf, FALSE);

	/* extract full name from passwd file */
	if ((FullName == NULL || FullName[0] == '\0') &&
	    pw != NULL && pw->pw_gecos != NULL)
	{
		char nbuf[MAXNAME];

		buildfname(pw->pw_gecos, realname, nbuf);
		if (nbuf[0] != '\0')
			FullName = newstr(nbuf);
	}
	if (FullName != NULL && FullName[0] != '\0')
		define('x', FullName);

	setfrom(from, realname);
}
