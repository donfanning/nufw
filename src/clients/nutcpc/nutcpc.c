/*
 ** Copyright 2004-2007 - INL
 ** Written by Eric Leblond <eric.leblond@inl.fr>
 **            Vincent Deffontaines <vincent@inl.fr>
 ** INL http://www.inl.fr/
 **
 ** $Id$
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, version 2 of the License.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "../lib/nuclient.h"
#include <locale.h>
#include <config.h>
#include <sys/resource.h>	/* setrlimit() */
#include <langinfo.h>
#include <stdarg.h>
#include "proto.h"
#include "security.h"
#define NUTCPC_VERSION PACKAGE_VERSION " $Revision$"

#ifdef FREEBSD
#include <readpassphrase.h>
#endif

#define MAX_RETRY_TIME 30

struct termios orig;
nuauth_session_t *session = NULL;
nuclient_error_t *err = NULL;
struct sigaction old_sigterm;
struct sigaction old_sigint;

typedef struct {
	char port[10];		/*!< Port (service) number / name */
	unsigned long interval;	/*!< Number of millisecond for sleep in main loop (default value: 100ms) */
	unsigned char donotuselock;	/*!< Do not user lock */
	char srv_addr[512];	/*!< Nuauth server hostname */
	char password[100];
	char nuauthdn[512];
	unsigned char debug_mode;	/*!< Debug mode enabled if different than zero */
	int tempo;		/*!< Number of second between each connection retry */
} nutcpc_context_t;

/**
 * Panic: function called on fatal error.
 * Display error message and then exit client (using exit()).
 */
void panic(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf("\n");
	printf("Fatal error: ");
	vprintf(fmt, args);
	printf("\n");
	fflush(stdout);
	exit(EXIT_FAILURE);
	va_end(args);
}

/**
 * Compure run pid filename: "$HOME/.nufw/nutcpc"
 */
char *compute_run_pid()
{
	char path_dir[254];
	char *home = nu_get_home_dir();
	if (home == NULL)
		return NULL;
	snprintf(path_dir, sizeof(path_dir), " %s/.nufw", home);
	if (access(path_dir, R_OK) != 0) {
		mkdir(path_dir, S_IRWXU);
	}
	snprintf(path_dir, sizeof(path_dir), "%s/.nufw/nutcpc", home);
	free(home);
	return strdup(path_dir);
}

/**
 * Kill existing instance of nutcpc: read pid file,
 * and then send SIGTERM to the process.
 *
 * Exit the program at the end of this function.
 */
void kill_nutcpc()
{
	pid_t pid;
	FILE *fd;
	int ret;
	int ok = 0;
	char *runpid = compute_run_pid();

	if (runpid) {
		fd = fopen(runpid, "r");
		if (fd) {
			fscanf(fd, "%d", &pid);
			fclose(fd);
			ret = kill(pid, SIGTERM);
			ok = (ret == 0);
			if (ok) {
				printf("nutcpc process killed (pid %lu)\n",
				       (unsigned long) pid);
			} else {
				printf
				    ("Fail to kill process: remove pid file\n");
				unlink(runpid);
			}
		}
		free(runpid);
	}
	if (!ok) {
		printf("No nutcpc seems to be running\n");
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}

/**
 * Leave the client:
 *   - Restore ECHO mode ;
 *   - Free memory of the library ;
 *   - Unlink pid file ;
 *   - deinit. library ;
 *   - free memory.
 */
void leave_client()
{
	char *runpid;
	struct termios term;

	/* restore ECHO mode */
	if (tcgetattr(fileno(stdin), &term) == 0) {
		term.c_lflag |= ECHO;
		(void) tcsetattr(fileno(stdin), TCSAFLUSH, &term);
	}

	if (session) {
		nu_client_delete(session);
	}

	runpid = compute_run_pid();
	if (runpid != NULL) {
		unlink(runpid);
		free(runpid);
	}
	nu_client_global_deinit();
	nu_client_error_destroy(err);
}

/**
 * Signal handler: catch SIGINT or SIGTERM. This function will exit nutcpc:
 * deinit libnuclient, free memory, and then exit the process.
 *
 * The function will first reinstall old handlers.
 */
void exit_clean()
{
	/* reinstall old signal handlers */
	(void) sigaction(SIGINT, &old_sigint, NULL);
	(void) sigaction(SIGTERM, &old_sigterm, NULL);

	/* quit nutcpc */
	printf("\nQuit client\n");
	leave_client();
	exit(EXIT_SUCCESS);
}

#ifdef FREEBSD
ssize_t getline(char **lineptr, size_t * n, FILE * stream)
{
	char *line;
	size_t len;

	/* call fgetln(): read line from stdin */
	line = fgetln(stream, &len);
	if (!line)
		return -1;

	/* buffer need to grow up? */
	if (len >= *n) {
		char *tmp = realloc(*lineptr, len + 1);
		if (tmp == NULL)
			return -1;
		*lineptr = tmp;
		*n = len + 1;
	}
	memcpy(*lineptr, line, len);
	(*lineptr)[len] = 0;
	return len;
}
#endif

#ifndef FREEBSD
/**
 * Read a password on terminal. Given buffer may grow up (resized by realloc).
 *
 * \param lineptr Pointer to buffer
 * \param linelen Initial length (including nul byte) of the buffer
 * \return Number of characters of the password,
 *         or -1 if fails
 */
ssize_t my_getpass(char **lineptr, size_t * linelen)
{
	struct termios new;
	int nread;

	/* Turn echoing off and fail if we can't. */
	if (tcgetattr(fileno(stdin), &orig) != 0)
		return -1;
	new = orig;
	new.c_lflag &= ~ECHO;
	if (tcsetattr(fileno(stdin), TCSAFLUSH, &new) != 0)
		return -1;

	/* Read the password. */
	nread = getline(lineptr, linelen, stdin);

	/* Restore terminal. */
	(void) tcsetattr(fileno(stdin), TCSAFLUSH, &orig);

	/* remove new line if needed */
	if (0 < nread) {
		char *line = *lineptr;
		if (line[nread - 1] == '\n') {
			line[nread - 1] = '\0';
			nread--;
		}
	}
	printf("\n");
	return nread;
}
#endif

/**
 * Callback used in nu_client_connect() call: read password
 *
 * \return New allocated buffer containing the password,
 *         or NULL if it fails
 */
char *get_password()
{
	size_t password_size = 32;
	char *new_pass;
	char *question = "Enter password: ";
#ifdef FREEBSD
	char *ret;
#else
	int ret;
#endif

	new_pass = (char *) calloc(password_size, sizeof(char));
#ifdef FREEBSD
	ret =
	    readpassphrase(question, new_pass, password_size,
			   RPP_REQUIRE_TTY);
	if (ret == NULL) {
		fprintf(stderr, "unable to read passphrase");
	}
#else
	printf(question);
	ret = my_getpass(&new_pass, &password_size);
	if (ret < 0) {
		free(new_pass);
		return NULL;
	}
#endif
	return new_pass;
}

/**
 * Callback used in nu_client_connect() call: read user name
 *
 * \return New allocated buffer containing the name,
 *         or NULL if it fails
 */
char *get_username()
{
	char *username;
	int nread;
	size_t username_size = 32;

	printf("Enter username: ");
	username = (char *) calloc(username_size, sizeof(char));
	nread = getline(&username, &username_size, stdin);
	if (nread < 0) {
		free(username);
		return NULL;
	}
	if (0 < nread && username[nread - 1] == '\n') {
		username[nread - 1] = 0;
	}
	return username;
}

/**
 * Print client usage.
 */
static void usage(void)
{
	fprintf(stderr, "usage: nutcpc [-qkldV] "
		"[-U username ] [-H nuauth_srv] [-a nuauth_dn]"
		"[-P password] [-p port] [-I interval]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, "-V: display version\n");
	fprintf(stderr, "-k: kill active client\n");
	fprintf(stderr, "-l: don't create lock file\n");
	fprintf(stderr,
		"-d: debug mode (don't go to foreground, daemon)\n");
	fprintf(stderr,
		"-q: do not display running nutcpc options on \"ps\"\n");
	exit(EXIT_FAILURE);
}

/**
 * Install signal handlers:
 *   - SIGINT: call exit_clean() ;
 *   - SIGTERM: call exit_clean().
 */
void install_signals()
{
	int err;
	struct sigaction action;
	action.sa_handler = exit_clean;
	sigemptyset(&(action.sa_mask));
	action.sa_flags = 0;

	/* install handlers */
	err = sigaction(SIGINT, &action, &old_sigint);
	if (err == 0)
		err = sigaction(SIGTERM, &action, &old_sigterm);

	/* error? */
	if (err != 0) {
		fprintf(stderr, "Unable to  install signal handlers!\n");
		exit(EXIT_FAILURE);
	}
}

/**
 * Daemonize the process
 */
void daemonize_process(nutcpc_context_t * context, char *runpid)
{
	pid_t p;

	/* 1st fork */
	p = fork();
	if (p < 0) {
		fprintf(stderr, "nutcpc: fork failure: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* kill 1st process (keep 2nd) */
	if (p != 0) {
		exit(0);
	}

	/* 2nd fork */
	p = fork();
	if (p < 0) {
		fprintf(stderr, "nutcpc: fork falure: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* kill 2nd process (keep 3rd) */
	if (p != 0) {
		fprintf(stderr, "nutcpc started (pid %d)\n", (int) p);
		if (context->donotuselock == 0) {
			FILE *RunD;
			RunD = fopen(runpid, "w");
			free(runpid);
			fprintf(RunD, "%d", p);
			fclose(RunD);
		}
		exit(EXIT_SUCCESS);
	}

	/* Fix process user identifier, close stdin, stdout, stderr,
	 * set currente directory to root directory */
	setsid();
	ioctl(STDIN_FILENO, TIOCNOTTY, NULL);
	(void) close(STDIN_FILENO);
	(void) close(STDOUT_FILENO);
	(void) close(STDERR_FILENO);
	setpgid(0, 0);
}

void wipe(void *data, size_t datalen)
{
	memset(data, 0, datalen);
}

/**
 * Try to connect to nuauth.
 *
 * \return The client session, or NULL on error (get description from ::err)
 */
nuauth_session_t *do_connect(nutcpc_context_t * context, char *username)
{
	nuauth_session_t *session;
#if 0
	char *username_utf8;
	char *password;
	char *password_utf8;

	/* read username and password */
	if (username == NULL) {
		username = get_username();
	}
	if (context->password[0] != 0) {
		password = strdup(context->password);
		wipe(context->password, sizeof(password));
	} else {
		password = get_password();
	}

	/* convert to UTF-8 */
	username_utf8 = nu_client_to_utf8(username, locale_charset);
	password_utf8 = nu_client_to_utf8(password, locale_charset);
	wipe(password, strlen(password));
	wipe(username, strlen(username));
	free(username);
	free(password);
#endif

	session = nu_client_new_callback(get_username, get_password, 1, err);
	if (session == NULL) {
		return NULL;
	}

	if (username) {
		nu_client_set_username(session, username);
	}
	if (context->password[0] != 0) {
		nu_client_set_password(session, context->password);
	}


#if 0
	/* wipe out username and password, and then free memory */
	wipe(username_utf8, strlen(username_utf8));
	wipe(password_utf8, strlen(password_utf8));
	free(username_utf8);
	free(password_utf8);
#endif

	nu_client_set_debug(session, context->debug_mode);

	if (!nu_client_setup_tls(session, NULL, NULL, NULL, NULL, err)) {
		nu_client_delete(session);
		return NULL;
	}

	if (context->nuauthdn) {
		if (!nu_client_set_nuauth_cert_dn(session, 
						  context->nuauthdn,
						  err)) {
			nu_client_delete(session);
			return NULL;
		}
	}

	if (!nu_client_connect
	    (session, context->srv_addr, context->port, err)) {
		nu_client_delete(session);
		return NULL;
	}
	return session;
}

/**
 * Main loop: program stay in this loop until it stops.
 */
void main_loop(nutcpc_context_t * context)
{
	int connected = 1;
	int ret;
	for (;;) {
		usleep(context->interval * 1000);
		if (!connected) {
			usleep((unsigned long) context->tempo * 1000000);
			if (context->tempo < MAX_RETRY_TIME) {
				context->tempo *= 2;
			}

			/* try to reconnect to nuauth */
			if (nu_client_connect
			    (session, context->srv_addr, context->port,
			     err) != 0) {
				connected = 1;
				context->tempo = 1;	/* second */
			} else {
				printf("Reconnection error: %s\n",
				       nu_client_strerror(err));
				nu_client_reset(session);
			}
		} else {
			ret = nu_client_check(session, err);
			if (ret < 0) {
				/* on error: reset the session */
				nu_client_reset(session);
				connected = 0;
				printf("%s\n", nu_client_strerror(err));
			}
		}
	}
}

/**
 * Parse command line options
 */
void parse_cmdline_options(int argc, char **argv,
			   nutcpc_context_t * context, char **username)
{
	int ch;
	int index;
	int stealth = 0;

	/* set default values */
	SECURE_STRNCPY(context->port, USERPCKT_PORT,
		       sizeof(context->port));
	SECURE_STRNCPY(context->srv_addr, NUAUTH_IP,
		       sizeof(context->srv_addr));
	context->password[0] = 0;
	context->interval = 100;
	context->donotuselock = 0;
	context->debug_mode = 0;
	context->tempo = 1;
	context->nuauthdn[0] = 0;

	/* Parse all command line arguments */
	opterr = 0;
	while ((ch = getopt(argc, argv, "kldqVu:H:I:U:p:P:a:")) != -1) {
		switch (ch) {
		case 'H':
			SECURE_STRNCPY(context->srv_addr, optarg,
				       sizeof(context->srv_addr));
			break;
		case 'P':
			SECURE_STRNCPY(context->password, optarg,
				       sizeof(context->password));
			stealth = 1;
			break;
		case 'd':
			context->debug_mode = 1;
			break;
		case 'I':
			context->interval = atoi(optarg);
			if (context->interval == 0) {
				fprintf(stderr, "nutcpc: bad interval\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'l':
			context->donotuselock = 1;
			break;
		case 'U':
			*username = strdup(optarg);
			break;
		case 'k':
			kill_nutcpc();
			break;
		case 'V':
			printf("nutcpc (version " NUTCPC_VERSION ")\n");
			exit(0);
		case 'p':
			SECURE_STRNCPY(context->port, optarg,
				       sizeof(context->port));
			break;
		case 'q':
			stealth = 1;
			break;
		case 'a':
			SECURE_STRNCPY(context->nuauthdn, optarg,
				       sizeof(context->nuauthdn));
			break;
		default:
			usage();
		}
	}
	if (context->password[0] != 0 && !context->debug_mode) {
		fprintf(stderr,
			"Don't use -P option outside debugging, it's not safe!\n");
		exit(1);
	}

	/* fill argument with nul byte */
	if (stealth == 1) {
		for (index = argc; 1 < index; index--) {
			memset(argv[index - 1], '\0',
			       strlen(argv[index - 1]));
		}
	}
}

/**
 * Initialize nuclient library
 */
void init_library(nutcpc_context_t * context, char *username)
{
	struct rlimit core_limit;

	/* Avoid creation of core file which may contains username and password */
	if (getrlimit(RLIMIT_CORE, &core_limit) == 0) {
		core_limit.rlim_cur = 0;
		setrlimit(RLIMIT_CORE, &core_limit);
	}

	/* Move to root directory to not block current working directory */
	(void) chdir("/");

	/* Prepare error structure */
	if (nu_client_error_init(&err) != 0) {
		printf("Cannot init error structure!\n");
		exit(EXIT_FAILURE);
	}

	/* global libnuclient init */
	if (!nu_client_global_init(err)) {
		printf("Unable to initiate nuclient library!\n");
		printf("Problem: %s\n", nu_client_strerror(err));
		exit(EXIT_FAILURE);
	}

	/* Init. library */
	printf("Connecting to NuFW gateway (%s)\n", context->srv_addr);
	session = do_connect(context, username);

	/* Library failure? */
	if (session == NULL) {
		printf("Unable to initiate connection to NuFW gateway\n");
		printf("Problem: %s\n", nu_client_strerror(err));
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	char *runpid = compute_run_pid();
	char *username = NULL;
	nutcpc_context_t context;

	/* stdout: flush buffer on new line */
	setvbuf(stdout, NULL, _IOLBF, 0);

	/* needed by iconv */
	setlocale(LC_ALL, "");

	if (!nu_check_version(NUCLIENT_VERSION)) {
		fprintf(stderr,
			"Wrong version of libnuclient (%s instead of %s)\n",
			nu_get_version(), NUCLIENT_VERSION);
		exit(EXIT_FAILURE);
	}

	if (runpid == NULL) {
		fprintf(stderr, "Can not determine runpid, leaving\n");
		exit(EXIT_FAILURE);
	}

	

	/* parse command line options */
	parse_cmdline_options(argc, argv, &context, &username);

	if (!context.debug_mode) {
		if (context.donotuselock == 0) {
			if (!access(runpid, R_OK)) {
				FILE *fd;
				printf("Lock file found: %s\n", runpid);
				if ((fd = fopen(runpid, "r"))) {
					char line[20];
					if (fgets(line, 19, fd)) {
						pid_t pid =
						    (pid_t) atoi(line);
						fclose(fd);
						if (kill(pid, 0)) {
							printf
							    ("No running process, starting anyway (deleting lockfile)\n");
							unlink(runpid);
							free(runpid);
						} else {
							printf
							    ("Kill existing process with \"-k\" or ignore it with \"-l\" option\n");
							exit(EXIT_FAILURE);
						}
					}
				}
			}
		}
	}

	install_signals();

	init_library(&context, username);

	/*
	 * Become a daemon by double-forking and detaching completely from
	 * the terminal.
	 */

	if (!context.debug_mode) {
		daemonize_process(&context, runpid);
	} else {
		fprintf(stderr,
			"nutcpc " NUTCPC_VERSION " started (debug)\n");
	}
	free(runpid);

	main_loop(&context);
	leave_client();
	exit(EXIT_SUCCESS);
}
