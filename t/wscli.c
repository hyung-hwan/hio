/*
 * a websocket client for the s-003.sh checks.
 *
 * plain POSIX sockets and its own framing, deliberately: a server checked only
 * against the library it is built from passes whether or not that library is
 * right. nothing here calls into hio.
 *
 * the handshake needs no digest either. RFC 6455 section 1.3 publishes a key
 * and the answer a correct server gives for it, so sending that key and
 * comparing against the published answer checks the computation without
 * repeating it.
 *
 * usage: wscli <ipaddr:port> [ready]
 *
 * with 'ready' it only opens a connection and completes the handshake, which
 * is what a harness polls with while waiting for the server to bind. the
 * checks themselves are far too slow to poll with.
 *
 * each check prints one line - "OK " or "FAIL " and what was checked - which
 * s-003.sh turns into tap output. the exit status is 1 if anything failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* the key and its answer, both from RFC 6455 section 1.3 */
#define WS_KEY    "dGhlIHNhbXBsZSBub25jZQ=="
#define WS_ACCEPT "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

/* the sizes the checks move. they only have to be large enough to cross the
 * server's internal chunking many times over and to put several sessions in
 * flight at once - past that they only cost time, and a debug build on a slow
 * machine pays that cost for every frame it logs. */
#define BIG_MSG_LEN      (256 * 1024)
#define RAPID_MSG_COUNT  50
#define CONCURRENT_COUNT 8
#define SESSION_MSG_LEN  20000

#define OP_CONT  0x0
#define OP_TEXT  0x1
#define OP_BIN   0x2
#define OP_CLOSE 0x8
#define OP_PING  0x9
#define OP_PONG  0xA

static int fails = 0;
static const char* g_addr;

static void ck (int cond, const char* what)
{
	printf("%s%s\n", cond? "OK ": "FAIL ", what);
	fflush (stdout);
	if (!cond) fails++;
}

/* ------------------------------------------------------------------ */

static int conn_to_server (void)
{
	struct sockaddr_in sa;
	char host[64];
	const char* colon;
	int fd, on = 1;

	colon = strrchr(g_addr, ':');
	if (!colon || (size_t)(colon - g_addr) >= sizeof(host)) return -1;
	memcpy (host, g_addr, colon - g_addr);
	host[colon - g_addr] = '\0';

	memset (&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(colon + 1));
	if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) return -1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
	setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	return fd;
}

static int xsend (int fd, const void* ptr, size_t len)
{
	const unsigned char* p = ptr;
	while (len > 0)
	{
		ssize_t n = send(fd, p, len, 0);
		if (n <= 0)
		{
			if (n < 0 && errno == EINTR) continue;
			return -1;
		}
		p += n; len -= n;
	}
	return 0;
}

/* read exactly len octets, or fail */
static int xrecv (int fd, void* ptr, size_t len)
{
	unsigned char* p = ptr;
	while (len > 0)
	{
		ssize_t n = recv(fd, p, len, 0);
		if (n <= 0)
		{
			if (n < 0 && errno == EINTR) continue;
			return -1;
		}
		p += n; len -= n;
	}
	return 0;
}

static int wait_readable (int fd, int secs)
{
	fd_set rs;
	struct timeval tv;
	FD_ZERO (&rs);
	FD_SET (fd, &rs);
	tv.tv_sec = secs; tv.tv_usec = 0;
	return select(fd + 1, &rs, NULL, NULL, &tv) > 0;
}

/* ------------------------------------------------------------------ */
/* framing, written here rather than taken from the library under test */
/* ------------------------------------------------------------------ */

/* a client masks everything it sends. the key is fixed rather than random -
 * the server must not care what it is, and a fixed one makes a failure
 * reproducible. */
static int send_frame (int fd, int fin, int op, const void* payload, size_t len, int masked, int rsv)
{
	unsigned char hdr[14];
	static const unsigned char key[4] = { 0x3a, 0x7f, 0x11, 0xc2 };
	size_t h = 0, i;
	unsigned char* body = NULL;
	int rc;

	hdr[h++] = (unsigned char)((fin? 0x80: 0x00) | (rsv << 4) | op);
	if (len < 126) hdr[h++] = (unsigned char)((masked? 0x80: 0x00) | len);
	else if (len <= 0xFFFF)
	{
		hdr[h++] = (unsigned char)((masked? 0x80: 0x00) | 126);
		hdr[h++] = (unsigned char)(len >> 8);
		hdr[h++] = (unsigned char)len;
	}
	else
	{
		int k;
		hdr[h++] = (unsigned char)((masked? 0x80: 0x00) | 127);
		for (k = 7; k >= 0; k--) hdr[h++] = (unsigned char)((unsigned long long)len >> (k * 8));
	}
	if (masked) { memcpy(&hdr[h], key, 4); h += 4; }

	if (xsend(fd, hdr, h) <= -1) return -1;
	if (len == 0) return 0;

	if (!masked) return xsend(fd, payload, len);

	body = malloc(len);
	if (!body) return -1;
	for (i = 0; i < len; i++) body[i] = ((const unsigned char*)payload)[i] ^ key[i & 3];
	rc = xsend(fd, body, len);
	free (body);
	return rc;
}

/* one frame from the server. a server never masks, so there is no key to
 * undo. returns 0 on success, -1 if the connection ended. */
static int recv_frame (int fd, int* fin, int* op, unsigned char* buf, size_t capa, size_t* len)
{
	unsigned char h[8];
	unsigned char b0, b1;
	size_t n;

	if (xrecv(fd, h, 2) <= -1) return -1;
	/* kept aside, because the extended length is read back over h[] and the
	 * mask bit would otherwise be looked for in a length octet */
	b0 = h[0];
	b1 = h[1];

	*fin = (b0 >> 7) & 1;
	*op = b0 & 0x0F;
	n = b1 & 0x7F;

	if (n == 126)
	{
		if (xrecv(fd, h, 2) <= -1) return -1;
		n = ((size_t)h[0] << 8) | h[1];
	}
	else if (n == 127)
	{
		int k;
		if (xrecv(fd, h, 8) <= -1) return -1;
		n = 0;
		for (k = 0; k < 8; k++) n = (n << 8) | h[k];
	}

	if (b1 & 0x80) return -1; /* a server must not mask */
	if (n > capa) return -1;
	*len = n;
	if (n > 0 && xrecv(fd, buf, n) <= -1) return -1;
	return 0;
}

/* a whole message, with the fragments joined and any control frame answered
 * on the way - a ping may arrive between the fragments of a message */
static int recv_msg (int fd, int* op, unsigned char* buf, size_t capa, size_t* len)
{
	size_t total = 0;
	int first = 1;

	for (;;)
	{
		unsigned char fbuf[131072];
		size_t flen;
		int ffin, fop;

		if (recv_frame(fd, &ffin, &fop, fbuf, sizeof(fbuf), &flen) <= -1) return -1;

		if (fop == OP_PING) { if (send_frame(fd, 1, OP_PONG, fbuf, flen, 1, 0) <= -1) return -1; continue; }
		if (fop == OP_PONG) continue;
		if (fop == OP_CLOSE) { *op = OP_CLOSE; *len = flen; if (flen <= capa) memcpy(buf, fbuf, flen); return 0; }

		if (first) { *op = fop; first = 0; }
		if (total + flen > capa) return -1;
		memcpy (&buf[total], fbuf, flen);
		total += flen;
		if (ffin) break;
	}

	*len = total;
	return 0;
}

/* ------------------------------------------------------------------ */

/* the handshake, and the proof that the server computed the answer rather
 * than echoing something */
static int ws_open (int* fdp, int check_accept)
{
	char req[512], resp[2048];
	int fd;
	size_t got = 0;
	char* end;

	fd = conn_to_server();
	if (fd < 0) return -1;

	snprintf (req, sizeof(req),
		"GET /ws/echo HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n", g_addr, WS_KEY);
	if (xsend(fd, req, strlen(req)) <= -1) { close(fd); return -1; }

	/* read until the end of the header block, and no further - what comes
	 * after it is already frames */
	while (got < sizeof(resp) - 1)
	{
		ssize_t n;
		if (!wait_readable(fd, 5)) break;
		n = recv(fd, &resp[got], 1, 0);
		if (n <= 0) break;
		got += n;
		resp[got] = '\0';
		if (got >= 4 && memcmp(&resp[got - 4], "\r\n\r\n", 4) == 0) break;
	}
	resp[got] = '\0';

	if (!strstr(resp, "101")) { close(fd); return -1; }
	if (check_accept)
	{
		end = strstr(resp, "Sec-WebSocket-Accept: " WS_ACCEPT "\r\n");
		if (!end) { close(fd); return -1; }
	}

	*fdp = fd;
	return 0;
}

/* drain whatever the server says on its way out and report the close code */
static int read_close_code (int fd)
{
	unsigned char buf[256];
	size_t len;
	int fin, op;

	for (;;)
	{
		if (!wait_readable(fd, 5)) return -1;
		if (recv_frame(fd, &fin, &op, buf, sizeof(buf), &len) <= -1) return -1;
		if (op == OP_CLOSE) return (len >= 2)? (((int)buf[0] << 8) | buf[1]): 0;
	}
}

/* ------------------------------------------------------------------ */
/* the checks                                                          */
/* ------------------------------------------------------------------ */

/* an echo endpoint writes the reply while this is still writing the message.
 * a client that sends the whole of a large one before reading any of it fills
 * the server's send buffer, which makes the server stop reading, which blocks
 * this in send() - a deadlock neither side is at fault for, and one a browser
 * never hits because it reads and writes at once.
 *
 * the send is handed to a child so that this side does both at once as well.
 * it only matters for a message too large for the socket buffers to absorb,
 * which is exactly the message worth sending. */
static int echo_big (int fd, int op, const void* msg, size_t len, const char* what)
{
	unsigned char* buf;
	size_t rlen;
	int rop, ok, st;
	pid_t pid;

	buf = malloc(len + 16);
	if (!buf) { ck(0, what); return -1; }

	pid = fork();
	if (pid == 0)
	{
		_exit (send_frame(fd, 1, op, msg, len, 1, 0) == 0? 0: 1);
	}
	if (pid < 0) { free(buf); ck(0, what); return -1; }

	ok = (recv_msg(fd, &rop, buf, len + 16, &rlen) == 0);
	if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) ok = 0;

	ok = ok && (rop == op && rlen == len && memcmp(buf, msg, len) == 0);
	free (buf);
	ck (ok, what);
	return 0;
}

static int echo_once (int fd, int op, const void* msg, size_t len, const char* what)
{
	unsigned char* buf;
	size_t rlen;
	int rop, ok;

	buf = malloc(len + 16);
	if (!buf) { ck(0, what); return -1; }

	if (send_frame(fd, 1, op, msg, len, 1, 0) <= -1 ||
	    recv_msg(fd, &rop, buf, len + 16, &rlen) <= -1)
	{
		free (buf);
		ck (0, what);
		return -1;
	}

	ok = (rop == op && rlen == len && (len == 0 || memcmp(buf, msg, len) == 0));
	free (buf);
	ck (ok, what);
	return 0;
}

static void test_conforming (void)
{
	unsigned char buf[64];
	size_t len;
	int fd, op;

	if (ws_open(&fd, 1) <= -1)
	{
		ck (0, "the handshake completes and the endpoint's greeting arrives");
		return;
	}

	if (recv_msg(fd, &op, buf, sizeof(buf), &len) <= -1) len = 0;
	ck (op == OP_TEXT && len == 7 && memcmp(buf, "welcome", 7) == 0,
	    "the handshake completes and the endpoint's greeting arrives");

	echo_once (fd, OP_TEXT, "hello", 5, "a text message echoes back");
	echo_once (fd, OP_BIN, "\x00\x01\x02\xff", 4, "a binary message echoes back, still binary");
	echo_once (fd, OP_TEXT, "", 0, "an empty message echoes back");
	echo_once (fd, OP_TEXT, "\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80", 9,
	           "a multi-byte utf-8 message survives unchanged");

	/* larger than any single read, so it crosses the server's buffering and
	 * comes back as a message rather than as the pieces it travelled in */
	{
		size_t n = BIG_MSG_LEN;
		char* big = malloc(n);
		if (big)
		{
			memset (big, 'x', n);
			echo_big (fd, OP_TEXT, big, n, "a message far larger than one read echoes back intact");
			free (big);
		}
		else ck(0, "a message far larger than one read echoes back intact");
	}

	/* a fragmented message, ended by a continuation frame carrying nothing
	 * but the fin bit. that last frame is the case that says the pieces of a
	 * message are tracked rather than the frames. */
	{
		int ok = (send_frame(fd, 0, OP_TEXT, "abc", 3, 1, 0) == 0 &&
		          send_frame(fd, 0, OP_CONT, "def", 3, 1, 0) == 0 &&
		          send_frame(fd, 0, OP_CONT, "ghi", 3, 1, 0) == 0 &&
		          send_frame(fd, 1, OP_CONT, "", 0, 1, 0) == 0);
		if (ok) ok = (recv_msg(fd, &op, buf, sizeof(buf), &len) == 0);
		ck (ok && op == OP_TEXT && len == 9 && memcmp(buf, "abcdefghi", 9) == 0,
		    "a fragmented message ended by an empty continuation frame is echoed as one message");
	}

	/* a ping must come back as a pong carrying the same body */
	{
		unsigned char pbuf[64];
		size_t plen;
		int pfin, pop, ok;

		ok = (send_frame(fd, 1, OP_PING, "ping-body", 9, 1, 0) == 0);
		if (ok) ok = (recv_frame(fd, &pfin, &pop, pbuf, sizeof(pbuf), &plen) == 0);
		ck (ok && pop == OP_PONG && plen == 9 && memcmp(pbuf, "ping-body", 9) == 0,
		    "a ping is answered with a pong carrying the same body");
	}

	/* the closing handshake: the code sent comes back */
	{
		unsigned char cbuf[2];
		int code;

		cbuf[0] = 0x03; cbuf[1] = 0xe8; /* 1000 */
		send_frame (fd, 1, OP_CLOSE, cbuf, 2, 1, 0);
		code = read_close_code(fd);
		ck (code == 1000, "the closing handshake completes with the code that was sent");
	}

	close (fd);
}

static void test_many_messages (void)
{
	unsigned char buf[64];
	size_t len;
	int fd, op, i, ok = 1;

	if (ws_open(&fd, 0) <= -1) { ck(0, "messages sent back to back on one connection all echo correctly"); return; }
	if (recv_msg(fd, &op, buf, sizeof(buf), &len) <= -1) ok = 0;

	for (i = 0; ok && i < RAPID_MSG_COUNT; i++)
	{
		char m[32];
		int mlen = snprintf(m, sizeof(m), "m%d", i);
		if (send_frame(fd, 1, OP_TEXT, m, mlen, 1, 0) <= -1 ||
		    recv_msg(fd, &op, buf, sizeof(buf), &len) <= -1 ||
		    len != (size_t)mlen || memcmp(buf, m, mlen) != 0) ok = 0;
	}

	ck (ok, "messages sent back to back on one connection all echo correctly");
	close (fd);
}

/* one session, run in a child so that several are genuinely in flight at once
 * rather than one after another */
static int one_session (int i)
{
	unsigned char buf[BIG_MSG_LEN + 1024];
	size_t len, n;
	int fd, op, j;
	char* big;

	if (ws_open(&fd, 0) <= -1) return -1;
	if (recv_msg(fd, &op, buf, sizeof(buf), &len) <= -1) { close(fd); return -1; }

	for (j = 0; j < 10; j++)
	{
		char m[64];
		int mlen = snprintf(m, sizeof(m), "c%d-m%d", i, j);
		if (send_frame(fd, 1, OP_TEXT, m, mlen, 1, 0) <= -1 ||
		    recv_msg(fd, &op, buf, sizeof(buf), &len) <= -1 ||
		    len != (size_t)mlen || memcmp(buf, m, mlen) != 0) { close(fd); return -1; }
	}

	n = SESSION_MSG_LEN;
	big = malloc(n);
	if (!big) { close(fd); return -1; }
	memset (big, 'a' + (i % 26), n);
	{
		/* the same interleaving the large message needs, for the same reason */
		pid_t w = fork();
		int st, ok;

		if (w == 0) _exit(send_frame(fd, 1, OP_BIN, big, n, 1, 0) == 0? 0: 1);
		if (w < 0) { free(big); close(fd); return -1; }

		ok = (recv_msg(fd, &op, buf, sizeof(buf), &len) == 0);
		if (waitpid(w, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) ok = 0;
		if (!ok || len != n || memcmp(buf, big, n) != 0) { free(big); close(fd); return -1; }
	}
	free (big);

	close (fd);
	return 0;
}

static void test_concurrent (void)
{
	int i, ok = 1;
	pid_t pid[CONCURRENT_COUNT];

	for (i = 0; i < CONCURRENT_COUNT; i++)
	{
		pid[i] = fork();
		if (pid[i] == 0) _exit(one_session(i) == 0? 0: 1);
		if (pid[i] < 0) { ok = 0; break; }
	}

	for (i = 0; i < CONCURRENT_COUNT; i++)
	{
		int st;
		if (pid[i] > 0)
		{
			if (waitpid(pid[i], &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) ok = 0;
		}
	}

	ck (ok, "concurrent sessions each echo everything correctly");
}

/* ------------------------------------------------------------------ */

/* send one frame a conforming client never would, and see what the server
 * closes with */
static void refuse_check (int fin, int op, size_t len, int masked, int rsv, int want, const char* what)
{
	unsigned char payload[200];
	int fd, code;

	if (ws_open(&fd, 0) <= -1) { ck(0, what); return; }

	memset (payload, 'x', sizeof(payload));
	if (len > sizeof(payload)) len = sizeof(payload);
	send_frame (fd, fin, op, payload, len, masked, rsv);

	code = read_close_code(fd);
	ck (code == want, what);
	close (fd);
}

static void test_refusals (void)
{
	refuse_check (1, OP_TEXT, 2, 0, 0, 1002, "an unmasked frame from a client is refused with 1002");
	refuse_check (1, 3, 2, 1, 0, 1002, "an opcode the protocol has not assigned is refused with 1002");
	refuse_check (1, OP_TEXT, 2, 1, 4, 1002, "a reserved bit set with no extension negotiated is refused with 1002");
	refuse_check (1, OP_CONT, 2, 1, 0, 1002, "a continuation with nothing to continue is refused with 1002");
	refuse_check (1, OP_PING, 126, 1, 0, 1002, "a control frame over 125 octets is refused with 1002");

	/* a second message started before the first one finished */
	{
		int fd, code;
		if (ws_open(&fd, 0) <= -1) { ck(0, "a new message before the last one finished is refused with 1002"); return; }
		send_frame (fd, 0, OP_TEXT, "a", 1, 1, 0);
		send_frame (fd, 0, OP_TEXT, "b", 1, 1, 0);
		code = read_close_code(fd);
		ck (code == 1002, "a new message before the last one finished is refused with 1002");
		close (fd);
	}

	/* a length past the cap the endpoint configured. only the header is sent -
	 * the server must refuse on the length alone rather than wait for octets
	 * it has already decided it will not take. */
	{
		unsigned char hdr[14];
		unsigned long long n = 17ULL * 1024 * 1024;
		int fd, code, k;
		size_t h = 0;

		if (ws_open(&fd, 0) <= -1) { ck(0, "a message past the endpoint's cap is refused with 1009"); return; }
		hdr[h++] = 0x80 | OP_BIN;
		hdr[h++] = 0x80 | 127;
		for (k = 7; k >= 0; k--) hdr[h++] = (unsigned char)(n >> (k * 8));
		hdr[h++] = 0x11; hdr[h++] = 0x22; hdr[h++] = 0x33; hdr[h++] = 0x44;
		xsend (fd, hdr, h);
		code = read_close_code(fd);
		ck (code == 1009, "a message past the endpoint's cap is refused with 1009");
		close (fd);
	}
}

static void test_bad_handshakes (void)
{
	char req[512], resp[2048];
	int fd;
	ssize_t n;

	/* no key at all */
	fd = conn_to_server();
	if (fd >= 0)
	{
		snprintf (req, sizeof(req),
			"GET /ws/echo HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
			"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n\r\n", g_addr);
		xsend (fd, req, strlen(req));
		n = wait_readable(fd, 5)? recv(fd, resp, sizeof(resp) - 1, 0): -1;
		if (n > 0) resp[n] = '\0'; else resp[0] = '\0';
		ck (strstr(resp, "400") != NULL, "a handshake with no key is refused with 400");
		close (fd);
	}
	else ck(0, "a handshake with no key is refused with 400");

	/* a version this does not speak */
	fd = conn_to_server();
	if (fd >= 0)
	{
		snprintf (req, sizeof(req),
			"GET /ws/echo HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 8\r\n\r\n", g_addr, WS_KEY);
		xsend (fd, req, strlen(req));
		n = wait_readable(fd, 5)? recv(fd, resp, sizeof(resp) - 1, 0): -1;
		if (n > 0) resp[n] = '\0'; else resp[0] = '\0';
		ck (strstr(resp, "426") != NULL && strstr(resp, "Sec-WebSocket-Version: 13") != NULL,
		    "a version this does not speak is refused with 426 naming the one it does");
		close (fd);
	}
	else ck(0, "a version this does not speak is refused with 426 naming the one it does");
}

/* ------------------------------------------------------------------ */

int main (int argc, char* argv[])
{
	if (argc < 2 || argc > 3)
	{
		fprintf (stderr, "usage: %s <ipaddr:port> [ready]\n", argv[0]);
		return 2;
	}
	g_addr = argv[1];

	/* writing to a connection the server has closed is an expected part of
	 * the refusal checks */
	signal (SIGPIPE, SIG_IGN);

	if (argc == 3 && strcmp(argv[2], "ready") == 0)
	{
		/* is the server answering yet? nothing is printed - the exit status
		 * is the whole answer */
		int fd;
		if (ws_open(&fd, 0) <= -1) return 1;
		close (fd);
		return 0;
	}

	test_conforming ();
	test_many_messages ();
	test_concurrent ();
	test_refusals ();
	test_bad_handshakes ();

	return fails? 1: 0;
}
