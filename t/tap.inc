tap_total=0

tap_end()
{
	printf "1..%d\n" $tap_total
}

tap_ok() 
{
	local msg="$@"
	tap_total=$((tap_total + 1))
	printf "ok %d - %s\n" $tap_total "$msg"
}

tap_fail()
{
	local msg="$@"
	tap_total=$((tap_total + 1))
	printf "not ok %d - %s\n" $tap_total "$msg"
}

tap_skip()
{
	local msg="$@"
	tap_total=$((tap_total + 1))
	[ -n "$msg" ] && msg=" $msg"
	printf "ok %d - # skip%s\n" $tap_total "$msg"
}

tap_ensure()
{
	local a="$1"
	local b="$2"
	local desc="$3"
	[ "x$a" = "x$b" ] && tap_ok "$desc" || tap_fail "$desc"
}
