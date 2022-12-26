#!/bin/sh

[ -z "$srcdir" ] && srcdir=$(dirname "$0")
. "${srcdir}/tap.inc"

test_default_index() 
{
	local msg="hio-webs default index.html"
	local srvaddr=127.0.0.1:54321
	local tmpdir="/tmp/s-001.$$"

	mkdir -p "${tmpdir}"

	## check if index.html is retrieved
	../bin/hio-webs "${srvaddr}" "${tmpdir}" 2>/dev/null &
	local jid=$!

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

	## check if 404 is returned if the document root is not found
	rm -rf "${tmpdir}"
	local hc=$(curl -s -w '%{http_code}\n' -o /dev/null "http://${srvaddr}")
	tap_ensure "$hc" "404" "$msg - got $hc"

	kill -TERM ${jid}
	wait ${jid}
}


test_file_list_dir()
{
	local msg="hio-webs file-list-dir"
	local srvaddr=127.0.0.1:54321
	local tmpdir="/tmp/s-001.$$"

	mkdir -p "${tmpdir}"

	## check directory listing against an empty directory
	../bin/hio-webs --file-list-dir "${srvaddr}" "${tmpdir}" 2>/dev/null &
	local jid=$!
	sleep 0.5

	local hc=$(curl -s -w '%{http_code}\n' -o /dev/null "http://${srvaddr}")
	tap_ensure "$hc" "200" "$msg - got $hc"

	rm -rf "${tmpdir}"

	kill -TERM ${jid}
	wait ${jid}
}

test_default_index
test_file_list_dir
tap_end
