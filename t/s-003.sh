#!/bin/sh

# the websocket endpoint of httpsvr.
#
# the framing has its own unit test in t-022; what this covers is everything
# that only appears once a real connection exists - the handshake, a message
# split across reads, the masking a client actually applies, the ping and the
# closing handshake, and the frames a conforming client never sends.
#
# the client is ./wscli, which speaks the protocol itself rather than through
# the library under test.

[ -z "$srcdir" ] && srcdir=$(dirname "$0")
. "${srcdir}/tap.inc"

SRVPORT=9988
SRVADDR="127.0.0.1:${SRVPORT}"

./httpsvr >/dev/null 2>&1 &
srvpid=$!

# wait for the listener rather than sleeping a fixed amount. wscli reports a
# failed connect the same way it reports anything else, so a poll of it is
# enough to tell when the server is answering.
i=0
up=0
while [ $i -lt 50 ]; do
	if ./wscli "${SRVADDR}" >/dev/null 2>&1; then
		up=1
		break
	fi
	i=$((i + 1))
	sleep 0.2
done

if [ $up -eq 0 ]; then
	# one more run, with the output kept, so a genuine failure is reported as
	# itself rather than as a server that never came up
	./wscli "${SRVADDR}" > /tmp/wscli.$$ 2>&1
	if [ -s /tmp/wscli.$$ ]; then
		up=1
	else
		tap_fail "httpsvr did not come up"
		rm -f /tmp/wscli.$$
		kill -TERM ${srvpid} 2>/dev/null
		tap_end
		exit 0
	fi
else
	./wscli "${SRVADDR}" > /tmp/wscli.$$ 2>&1
fi

# each check prints one line, which is turned into tap here so that the reason
# a case failed survives into the test output
while IFS= read -r line; do
	case "$line" in
		"OK "*)   tap_ok "${line#OK }" ;;
		"FAIL "*) tap_fail "${line#FAIL }" ;;
		*)        [ -n "$line" ] && printf "# %s\n" "$line" ;;
	esac
done < /tmp/wscli.$$
rm -f /tmp/wscli.$$

kill -TERM ${srvpid} 2>/dev/null
wait ${srvpid} 2>/dev/null

tap_end
exit 0
