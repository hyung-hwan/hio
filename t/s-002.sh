#!/bin/sh

# End-to-end coverage for the http task modules that s-001.sh does not
# reach. hio-t06 routes by path prefix, so txt and thr are exercisable
# without an upstream server. fcgi and prxy still need one and stay
# uncovered here.

[ -z "$srcdir" ] && srcdir=$(dirname "$0")
. "${srcdir}/tap.inc"

SRVPORT=9988
SRVADDR="127.0.0.1:${SRVPORT}"

start_server()
{
	../bin/hio-t06 >/dev/null 2>&1 &
	srvpid=$!
	# wait for the listener rather than sleeping a fixed amount
	i=0
	while [ $i -lt 50 ]; do
		curl -s -m 1 -o /dev/null "http://${SRVADDR}/txt/ping" 2>/dev/null && return 0
		i=$((i + 1))
		sleep 0.1
	done
	return 1
}

stop_server()
{
	kill -TERM ${srvpid} 2>/dev/null
	wait ${srvpid} 2>/dev/null
}

test_txt()
{
	local msg="hio-t06 txt task"

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
	local msg="hio-t06 thr task"

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
}

test_mixed_load()
{
	local msg="hio-t06 mixed task load"
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
	test_mixed_load
	stop_server
else
	tap_skip "hio-t06 did not come up"
fi

tap_end
