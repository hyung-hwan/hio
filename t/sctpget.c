/*
 * a minimal HTTP client that speaks over SCTP, for the http-over-sctp test.
 *
 * curl has no SCTP support, so the harness cannot reach an sctp listener with
 * the tool it uses for everything else. this does the least that proves the
 * path: open a one-to-one sctp association, send a request, print the status
 * code from the reply.
 *
 * usage: sctpget <ipaddr:port> <path> [body]
 *
 * it prints the three-digit status code on success, or with 'body' the response
 * body instead. checking the body matters: the status line is sent before the
 * body is produced, so a task that fails halfway still answers 200 and only the
 * body shows it.
 *
 * it prints "nosctp" if the system cannot make an sctp socket at all, and
 * "refused" if nothing is listening - which is what a library built without
 * --enable-sctp looks like from out here, since the kernel may well have sctp
 * while hio was told not to use it. the caller treats both as a skip. anything
 * else prints "error", which is a failure.
 *
 * plain POSIX sockets on purpose - nothing here should depend on the library
 * under test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main (int argc, char* argv[])
{
	char host[64], * colon;
	struct sockaddr_in sa;
	struct timeval tv;
	char req[512], buf[2048];
	int fd, n, total = 0;

	int want_body;

	if (argc < 3 || argc > 4)
	{
		fprintf (stderr, "usage: %s <ipaddr:port> <path> [body]\n", argv[0]);
		return 2;
	}
	want_body = (argc == 4 && strcmp(argv[3], "body") == 0);

	snprintf (host, sizeof(host), "%s", argv[1]);
	colon = strrchr(host, ':');
	if (!colon) { printf("error\n"); return 2; }
	*colon = '\0';

	memset (&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(colon + 1));
	if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) { printf("error\n"); return 2; }

	/* IPPROTO_SCTP with SOCK_STREAM is the one-to-one style: an association
	 * behaves like a tcp connection, which is why the server side needs no
	 * changes beyond being told to listen this way. */
	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
	if (fd <= -1)
	{
		/* no kernel support, or the module is not loaded */
		printf ("nosctp\n");
		return 3;
	}

	/* keep a stuck server from hanging the whole test run */
	tv.tv_sec = 10; tv.tv_usec = 0;
	setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) <= -1)
	{
		/* nothing listening. the kernel has sctp - the socket above was made -
		 * so this says the server did not bind it, i.e. hio has no sctp
		 * support. distinguishable from a genuine failure, so the caller can
		 * skip on this one alone. */
		printf ("%s\n", (errno == ECONNREFUSED)? "refused": "error");
		close (fd);
		return 1;
	}

	n = snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", argv[2], argv[1]);
	if (send(fd, req, n, 0) != n)
	{
		printf ("error\n");
		close (fd);
		return 1;
	}

	/* the status line is all this needs, but read until the peer is done so a
	 * half-read response cannot be mistaken for a truncated one */
	while (total < (int)sizeof(buf) - 1)
	{
		n = recv(fd, &buf[total], sizeof(buf) - 1 - total, 0);
		if (n <= 0) break;
		total += n;
	}
	buf[total] = '\0';
	close (fd);

	if (total < 12 || memcmp(buf, "HTTP/1.", 7) != 0)
	{
		printf ("error\n");
		return 1;
	}

	if (want_body)
	{
		/* everything past the blank line that ends the header block */
		char* p = strstr(buf, "\r\n\r\n");
		if (!p) { printf("error\n"); return 1; }
		fputs (p + 4, stdout);
		return 0;
	}

	/* "HTTP/1.1 200 ..." - the code starts at offset 9 */
	printf ("%.3s\n", &buf[9]);
	return 0;
}
