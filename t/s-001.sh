#!/bin/sh

[ -z "$srcdir" ] && srcdir=$(dirname "$0")
. "${srcdir}/tap.inc"

test_default_index()
{
	local msg="hio-webs default index.html under a directory"
	local srvaddr=127.0.0.1:54321
	local tmpdir="/tmp/s-001.$$"

	mkdir -p "${tmpdir}"

	## check if index.html is retrieved
	../bin/hio-webs --file-no-list-dir "${srvaddr}" "${tmpdir}" 2>/dev/null &
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
	../bin/hio-webs "${srvaddr}" "${tmpdir}" 2>/dev/null &
	local jid=$!
	sleep 0.5

	local hc=$(curl -s -w '%{http_code}\n' -o /dev/null "http://${srvaddr}")
	tap_ensure "$hc" "200" "$msg - got $hc"

	rm -rf "${tmpdir}"

	kill -TERM ${jid}
	wait ${jid}
}


test_cgi()
{
	local msg="hio-webs cgi"
	local srvaddr=127.0.0.1:54321
	local tmpdir="/tmp/s-001.$$"

	mkdir -p "${tmpdir}"
	cp -pf t-cgi "${tmpdir}/t.cgi"

	## check directory listing against an empty directory
	../bin/hio-webs "${srvaddr}" "${tmpdir}" 2>/dev/null &
	local jid=$!
	sleep 0.5

	local hc=$(curl -s -w '%{http_code}\n' -o "${tmpdir}/t.out" "http://${srvaddr}/t.cgi?abc=def")
	tap_ensure "$hc" "200" "$msg - got $hc"

ls -ld ${tmpdir}
ls -l ${tmpdir}
	local request_method=$(grep -E "^REQUEST_METHOD:" "${tmpdir}/t.out" | cut -d: -f2)
	local request_uri=$(grep -E "^REQUEST_URI:" "${tmpdir}/t.out" | cut -d: -f2)
	local query_string=$(grep -E "^QUERY_STRING:" "${tmpdir}/t.out" | cut -d: -f2)

	tap_ensure "$request_method", "GET", "$msg - request_method"
	tap_ensure "$request_uri", "/t.cgi", "$msg - request_uri"
	tap_ensure "$query_string", "abc=def", "$msg - query_string"


	local hc=$(curl -s -w '%{http_code}\n' -X POST --data-binary "hello world" -o "${tmpdir}/t.out" "http://${srvaddr}/t.cgi?abc=def")
	tap_ensure "$hc" "200" "$msg - got $hc"

	local request_method=$(grep -E "^REQUEST_METHOD:" "${tmpdir}/t.out" | cut -d: -f2)
	local request_uri=$(grep -E "^REQUEST_URI:" "${tmpdir}/t.out" | cut -d: -f2)
	local query_string=$(grep -E "^QUERY_STRING:" "${tmpdir}/t.out" | cut -d: -f2)

	tap_ensure "$request_method", "POST", "$msg - request_method"
	tap_ensure "$request_uri", "/t.cgi", "$msg - request_uri"
	tap_ensure "$query_string", "abc=def", "$msg - query_string"

## TODO: write more...
	rm -rf "${tmpdir}"

	kill -TERM ${jid}
	wait ${jid}
}

test_default_index
test_file_list_dir
test_cgi

tap_end
