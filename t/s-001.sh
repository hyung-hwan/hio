#!/bin/sh

##srcdir=$(dirname "$0")
##srcdir="${0%/*}"
[ -z "$srcdir" ]
. "${srcdir}/tap.inc"

test_default_index() 
{
	local msg="default index.html"
	local srvaddr=127.0.0.1:54321
	local tmpdir="/tmp/s-001.$$"
	../bin/hio-webs "${srvaddr}" "${tmpdir}" 2>/dev/null &
	local jid=$!

	mkdir -p "${tmpdir}"
	cat >"${tmpdir}/index.html" <<EOF
<html>
<body>
hello
</body>
</html>
EOF
	sleep 0.5
	local hc=$(curl -s -w '%{http_code}\n' -o /dev/null "http://${srvaddr}")
	tap_ensure "$hc" "200" "$msg - got $hc"

	rm -rf "${tmpdir}"
	local hc=$(curl -s -w '%{http_code}\n' -o /dev/null "http://${srvaddr}")
	tap_ensure "$hc" "404" "$msg - got $hc"

	kill -TERM ${jid}
	wait ${jid}
}

test_default_index
tap_end