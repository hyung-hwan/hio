/*
 * a deliberately slow http client, for the header-deadline tests.
 *
 * it opens a connection, sends a partial request header, and then dribbles a
 * single octet at a time without ever sending the blank line that ends the
 * block. that is slowloris: every octet refreshes an inactivity timer, so a
 * server that only measures inactivity never reaps the connection.
 *
 * usage: slowclient <ipaddr:port> <mode> <maxwait-seconds>
 *   mode 'dribble' - send a partial header, then one octet every 200ms
 *   mode 'silent'  - connect and send nothing at all
 *
 * it prints the whole seconds elapsed until the server closed the connection,
 * or "timeout" if the server was still holding it after maxwait. plain POSIX
 * sockets on purpose - the point is to behave in a way an hio client would
 * not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static double now_secs (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

/* 0 = still open, 1 = closed by the peer, -1 = error */
static int poll_closed (int fd, long usec)
{
	fd_set rs;
	struct timeval tv;
	int n;

	FD_ZERO (&rs);
	FD_SET (fd, &rs);
	tv.tv_sec = usec / 1000000;
	tv.tv_usec = usec % 1000000;

	n = select(fd + 1, &rs, NULL, NULL, &tv);
	if (n <= -1) return (errno == EINTR)? 0: -1;
	if (n == 0) return 0;

	{
		char buf[512];
		ssize_t k = recv(fd, buf, sizeof(buf), 0);
		if (k == 0) return 1;       /* orderly close */
		if (k <= -1) return (errno == ECONNRESET)? 1: 0;
		return 0;                    /* the server said something; keep waiting */
	}
}

int main (int argc, char* argv[])
{
	const char* addrstr;
	const char* mode;
	double maxwait, started;
	char host[64], * colon;
	struct sockaddr_in sa;
	int fd, dribble;

	if (argc != 4)
	{
		fprintf (stderr, "usage: %s <ipaddr:port> <dribble|silent> <maxwait>\n", argv[0]);
		return 2;
	}

	addrstr = argv[1];
	mode = argv[2];
	maxwait = atof(argv[3]);
	dribble = (strcmp(mode, "dribble") == 0);

	snprintf (host, sizeof(host), "%s", addrstr);
	colon = strrchr(host, ':');
	if (!colon) { fprintf(stderr, "bad address\n"); return 2; }
	*colon = '\0';

	memset (&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(colon + 1));
	if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) { fprintf(stderr, "bad address\n"); return 2; }

	signal (SIGPIPE, SIG_IGN); /* a write to the closed connection must not kill us */

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd <= -1) { perror("socket"); return 2; }
	if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) <= -1) { perror("connect"); return 2; }

	started = now_secs();

	if (dribble)
	{
		/* note the absence of the terminating blank line */
		static const char head[] = "GET /txt/ping HTTP/1.1\r\nHost: localhost\r\n";
		if (send(fd, head, sizeof(head) - 1, 0) <= -1) { perror("send"); return 2; }
	}

	while (now_secs() - started < maxwait)
	{
		int c = poll_closed(fd, 200000);
		if (c <= -1) break;
		if (c == 1)
		{
			printf ("%d\n", (int)(now_secs() - started));
			close (fd);
			return 0;
		}

		if (dribble)
		{
			/* one more octet of a header line that never ends. if this fails
			 * the server has gone away, which is the outcome we are after. */
			if (send(fd, "A", 1, 0) <= -1)
			{
				printf ("%d\n", (int)(now_secs() - started));
				close (fd);
				return 0;
			}
		}
	}

	printf ("timeout\n");
	close (fd);
	return 1;
}
