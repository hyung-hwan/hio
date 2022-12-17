tap_total=0

function tap_end()
{
	printf "1..%d\n" tap_total
}

function tap_ok(msg)
{
	tap_total++;
	printf "ok %d - %s\n" tap_total msg
}

function tap_fail(msg)
{
	tap_total++;
	printf "not ok %d - %s\n" tap_total msg
}

function tap_skip(msg)
{
	tap_total++;
	printf "ok %d - # skip%s%s\n" tap_total (length(msg) > 0? " ": "") msg
}

function tap_ensure(a, b, desc, line)
{
	id = sprintf("%s[%d]", desc, line);
	if (a != b) tap_fail (id);
	else tap_ok (id);
}
