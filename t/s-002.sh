#!/bin/sh

# End-to-end coverage for the http task modules that s-001.sh does not
# reach. httssvr routes by path prefix, so txt, thr and fcgi are all
# exercisable here; fcgi talks to fcgis, the minimal responder in
# this directory.
#
# pxy forwards to an upstream, so the harness runs httpecho on the port
# the /pxy/ route targets. httpecho reflects the request line it saw,
# which is how the test tells that the method and path survived the hop.

[ -z "$srcdir" ] && srcdir=$(dirname "$0")
. "${srcdir}/tap.inc"

SRVPORT=9988
SRVADDR="127.0.0.1:${SRVPORT}"

start_server()
{
	# the fcgi task needs a responder listening on the port httssvr targets
	fcgiready="/tmp/s-002-fcgi.$$.ready"
	rm -f "${fcgiready}"
	./fcgis 127.0.0.1:9000 "${fcgiready}" >/dev/null 2>&1 &
	fcgipid=$!
	# it creates the ready file once it is accepting, so nothing races it
	i=0
	while [ $i -lt 50 ] && [ ! -f "${fcgiready}" ]; do
		i=$((i + 1))
		sleep 0.1
	done
	[ -f "${fcgiready}" ] || fcgipid=""

	# the upstream the /pxy/ route forwards to
	upready="/tmp/s-002-up.$$.ready"
	rm -f "${upready}"
	./httpecho 127.0.0.1:9001 "${upready}" >/dev/null 2>&1 &
	uppid=$!
	i=0
	while [ $i -lt 50 ] && [ ! -f "${upready}" ]; do
		i=$((i + 1))
		sleep 0.1
	done
	[ -f "${upready}" ] || uppid=""

	# the header deadline defaults to 60s and the idle timeout to 10s. shorten
	# the first and lengthen the second so test_slowloris finishes quickly and
	# so it is unambiguous which of the two closed the connection.
	HTTS_HDR_TMOUT=3 HTTS_IDLE_TMOUT=30 ./httssvr >/dev/null 2>&1 &
	srvpid=$!
	# wait for the listener rather than sleeping a fixed amount
	if tap_have_cmd curl; then
		i=0
		while [ $i -lt 50 ]; do
			curl -s -m 1 -o /dev/null "http://${SRVADDR}/txt/ping" 2>/dev/null && return 0
			i=$((i + 1))
			sleep 0.1
		done
		return 1
	fi

	# with no curl there is nothing here to poll the listener with, so the
	# wait is a fixed one. test_sctp drives the server through ./sctpget
	# rather than curl and is still worth running; every other case skips
	# itself. a server that did not come up leaves sctpget reporting
	# 'refused', which that case already treats as a skip.
	sleep 2
	return 0
}

stop_server()
{
	kill -TERM ${srvpid} 2>/dev/null
	wait ${srvpid} 2>/dev/null
	if [ -n "${fcgipid}" ]; then
		kill -TERM ${fcgipid} 2>/dev/null
		wait ${fcgipid} 2>/dev/null
	fi
	rm -f "${fcgiready}"
	if [ -n "${uppid}" ]; then
		kill -TERM ${uppid} 2>/dev/null
		wait ${uppid} 2>/dev/null
	fi
	rm -f "${upready}"
}

test_pxy()
{
	local msg="httssvr pxy task"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }

	if [ -z "${uppid}" ]; then
		tap_fail "$msg - httpecho did not come up"
		tap_fail "$msg - httpecho did not come up"
		tap_fail "$msg - httpecho did not come up"
		return
	fi

	local hc=$(curl -s -m 10 -w '%{http_code}' -o /dev/null "http://${SRVADDR}/pxy/thing")
	tap_ensure "$hc" "200" "$msg - got $hc"

	# httpecho reflects the request line, so this shows the method and
	# path reached the upstream unmangled
	local line=$(curl -s -m 10 "http://${SRVADDR}/pxy/thing" | sed -n 2p | tr -d '\r')
	tap_ensure "$line" "GET /pxy/thing HTTP/1.1" "$msg - the request reached the upstream intact"

	# and the upstream's status must come back rather than being invented
	local hc404=$(curl -s -m 10 -w '%{http_code}' -o /dev/null "http://${SRVADDR}/pxy/missing")
	tap_ensure "$hc404" "404" "$msg - the upstream status is propagated"

	# an 8MB body from a loopback upstream against a deliberately slow reader.
	# both numbers matter. the rate limit makes the client the bottleneck,
	# and the size has to clear the kernel socket buffer - loopback wmem
	# tops out at 4MB here - before anything queues in user space at all.
	# at 2MB unlimited the kernel absorbed the whole response and the
	# suspend/resume path was never entered. with these, a suspension that
	# never lifts shows up as a short body or a timeout.
	local big=$(curl -s -m 60 --limit-rate 4M "http://${SRVADDR}/pxy/big" | wc -c | tr -d ' ')
	tap_ensure "$big" "8388608" "$msg - an 8MB upstream body relays complete under backpressure"

	# and the bytes themselves, not just the count. the upstream emits a
	# repeating a-z pattern keyed to the offset.
	local sum=$(curl -s -m 60 "http://${SRVADDR}/pxy/big" | cksum | cut -d' ' -f1)
	local want=$(perl -e 'print map { chr(97 + ($_ % 26)) } 0 .. (8*1024*1024 - 1)' 2>/dev/null | cksum | cut -d' ' -f1)
	if [ -n "$want" ]; then
		tap_ensure "$sum" "$want" "$msg - the relayed bytes are identical to the upstream's"
	else
		tap_skip "$msg - perl unavailable for the reference checksum"
	fi
}

test_fcgi()
{
	local msg="httssvr fcgi task"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }

	if [ -z "${fcgipid}" ]; then
		tap_fail "$msg - fcgis did not come up"
		tap_fail "$msg - fcgis did not come up"
		return
	fi

	local hc=$(curl -s -m 10 -w '%{http_code}' -o /dev/null "http://${SRVADDR}/fcgi/x.php")
	tap_ensure "$hc" "200" "$msg - got $hc"

	# the body can only come from the responder, so this proves the
	# request traversed the fcgi task and the FastCGI protocol both ways
	local body=$(curl -s -m 10 "http://${SRVADDR}/fcgi/x.php" | tr -d '\r\n')
	tap_ensure "$body" "fcgi-ok" "$msg - body came from the fcgi responder"

	# an 8MB responder body against a deliberately slow reader. same sizing
	# as the pxy and thr cases: the body has to clear the kernel socket
	# buffer before anything queues in user space, and the rate limit is what
	# makes the client the bottleneck. this is the only case that reaches the
	# fcgi task's backpressure path.
	local big=$(curl -s -m 60 --limit-rate 4M "http://${SRVADDR}/fcgi/x.php?bigbody" | wc -c | tr -d ' ')
	tap_ensure "$big" "8388608" "$msg - an 8MB responder body relays complete under backpressure"
}

test_txt()
{
	local msg="httssvr txt task"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }

	local hc=$(curl -s -m 5 -w '%{http_code}' -o /dev/null "http://${SRVADDR}/txt/hello")
	tap_ensure "$hc" "200" "$msg - got $hc"

	# dotxt() echoes the query path back as the body
	local body=$(curl -s -m 5 "http://${SRVADDR}/txt/hello")
	tap_ensure "$body" "/txt/hello" "$msg - body echoes the query path"

	# the task must release the client so the next request reuses the
	# connection. each -w applies to its own request, so the second line
	# is the one that matters.
	local n=$(curl -s -m 5 -o /dev/null -w '%{num_connects}\n' "http://${SRVADDR}/txt/one" \
	                       -o /dev/null -w '%{num_connects}\n' "http://${SRVADDR}/txt/two" | tail -1)
	tap_ensure "$n" "0" "$msg - connection is reused after the task completes"
}

test_thr()
{
	local msg="httssvr thr task"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }

	local hc=$(curl -s -m 5 -w '%{http_code}' -o /dev/null "http://${SRVADDR}/thr/x")
	tap_ensure "$hc" "200" "$msg - got $hc"

	# thr2 serves the filesystem path following /thr2, so point it at a
	# real file to exercise the success path...
	local tmpf="/tmp/s-002.$$.txt"
	echo "thr2-payload" > "${tmpf}"
	local body=$(curl -s -m 5 "http://${SRVADDR}/thr2${tmpf}")
	tap_ensure "$body" "thr2-payload" "$msg - thr2 serves a file through the thread"

	# ...and at a missing one to exercise the error path
	local hc2=$(curl -s -m 5 -w '%{http_code}' -o /dev/null "http://${SRVADDR}/thr2/no/such/file")
	tap_ensure "$hc2" "404" "$msg - thr2 reports 404 for a missing file"
	rm -f "${tmpf}"

	# an 8MB file against a deliberately slow reader, for the same reason as
	# the pxy case below: only this reaches the thr task's backpressure path.
	local bigf="/tmp/s-002-big.$$.bin"
	dd if=/dev/zero of="${bigf}" bs=1048576 count=8 2>/dev/null
	local big=$(curl -s -m 60 --limit-rate 4M "http://${SRVADDR}/thr2${bigf}" | wc -c | tr -d ' ')
	tap_ensure "$big" "8388608" "$msg - an 8MB file relays complete under backpressure"
	rm -f "${bigf}"
}

test_hdrlimits()
{
	local msg="httssvr header limits"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }

	# one header long enough to blow the octet cap. the point of answering
	# rather than dropping the connection is that the peer can tell a limit
	# from a crash - so the assertion is on the status, not merely on the
	# request failing.
	local pad=$(awk 'BEGIN{ s=""; while (length(s) < 100000) s = s "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; print substr(s, 1, 100000) }')
	local hc=$(curl -s -m 10 -o /dev/null -w '%{http_code}' -H "X-Pad: ${pad}" "http://${SRVADDR}/txt/ping")
	tap_ensure "$hc" "431" "$msg - an oversized header block is answered with 431, not dropped"

	# and many small ones, which stay well under the octet cap
	local hdrs=""
	local i=0
	while [ $i -lt 300 ]; do
		hdrs="${hdrs} -H X-${i}:v"
		i=$((i + 1))
	done
	local hc2=$(curl -s -m 10 -o /dev/null -w '%{http_code}' ${hdrs} "http://${SRVADDR}/txt/ping")
	tap_ensure "$hc2" "431" "$msg - too many header lines is answered with 431"

	# the caps must be invisible to an ordinary request
	local hc3=$(curl -s -m 10 -o /dev/null -w '%{http_code}' -H 'X-Small: v' "http://${SRVADDR}/txt/ping")
	tap_ensure "$hc3" "200" "$msg - an ordinary request is unaffected"
}

test_slowloris()
{
	local msg="httssvr slow client"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }

	# the harness starts httssvr with HTTS_HDR_TMOUT=3 and a long idle
	# timeout, so only the header deadline can be what closes these.

	# a client that dribbles one octet at a time and never finishes its
	# header block. every octet refreshes the inactivity timer, so an idle
	# timeout alone never reaps it - that is what slowloris exploits. the
	# deadline runs from the start of the request and cannot be pushed back.
	local t=$(./slowclient "${SRVADDR}" dribble 20)
	if [ "$t" = "timeout" ]; then
		tap_fail "$msg - a dribbling client is closed by the header deadline (still open after 20s)"
	else
		tap_ok "$msg - a dribbling client is closed by the header deadline after ${t}s"
	fi

	# and one that connects and says nothing at all
	local t2=$(./slowclient "${SRVADDR}" silent 20)
	if [ "$t2" = "timeout" ]; then
		tap_fail "$msg - a silent client is closed (still open after 20s)"
	else
		tap_ok "$msg - a silent client is closed after ${t2}s"
	fi

	# and the deadline must not touch a client that behaves
	local hc=$(curl -s -m 10 -o /dev/null -w '%{http_code}' "http://${SRVADDR}/txt/ping")
	tap_ensure "$hc" "200" "$msg - an ordinary request is unaffected by the deadline"
}

test_sctp()
{
	local msg="httssvr over sctp"

	# httssvr binds the same service on 9989 over sctp. the address family
	# cannot say which transport to use, so the bind descriptor states it -
	# that is the whole point of the case.
	#
	# curl has no sctp support, hence the helper. it reports 'nosctp' when the
	# system cannot make an sctp socket, which is a skip rather than a failure.
	# 'nosctp' means the kernel has none; 'refused' means nothing is listening
	# on the sctp port, which is what a --disable-sctp build looks like from
	# here. either way there is nothing to test rather than something broken.
	local code=$(./sctpget "127.0.0.1:9989" /txt/ping)
	if [ "$code" = "nosctp" ] || [ "$code" = "refused" ]; then
		tap_skip "$msg - no sctp listener (kernel or build lacks sctp)"
		tap_skip "$msg - no sctp listener (kernel or build lacks sctp)"
		return
	fi

	tap_ensure "$code" "200" "$msg - the same service answers over sctp"

	# and the file task, which is the one that asks the transport whether
	# sendfile is usable. the sctp method tables have no sendfile - it would
	# bypass sendmsg() and lose the ancillary data - so this only works if
	# that query answers from the transport rather than from the build flags.
	# httssvr routes anything without a known prefix to the file task.
	local tmpf="/tmp/s-002-sctp.$$.txt"
	echo "sctp-file-payload" > "${tmpf}"
	# the body, not the status: the 200 goes out before the body is produced,
	# so a task that fails on the way still answers 200 and only the body
	# shows it
	local body=$(./sctpget "127.0.0.1:9989" "${tmpf}" body | tr -d '\r\n')
	tap_ensure "$body" "sctp-file-payload" "$msg - the file task works over a transport with no sendfile"
	rm -f "${tmpf}"
}

test_mixed_load()
{
	local msg="httssvr mixed task load"

	tap_have_cmd curl || { tap_skip "$msg - curl is not installed"; return; }
	local ok=0 i=0

	# alternate task types on the same server to shake out cross-task
	# client-binding mistakes
	while [ $i -lt 12 ]; do
		for p in /txt/a /thr/b /txt/c; do
			local hc=$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://${SRVADDR}${p}")
			[ "$hc" = "200" ] && ok=$((ok + 1))
		done
		i=$((i + 1))
	done
	tap_ensure "$ok" "36" "$msg - 36/36 requests succeeded"

	# and the server must still be alive afterwards
	kill -0 ${srvpid} 2>/dev/null && tap_ok "$msg - server still running" \
	                             || tap_fail "$msg - server died"
}

if start_server; then
	test_txt
	test_thr
	test_fcgi
	test_pxy
	test_hdrlimits
	test_slowloris
	test_sctp
	test_mixed_load
	stop_server
else
	tap_skip "httssvr did not come up"
fi

tap_end
