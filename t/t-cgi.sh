#!/bin/sh
##echo -n -e "Content-type: text/plain\r\n\r\n"
printf "Content-type: text/plain\r\n\r\n"

## a body large enough to back up the write queue to the client. the cgi task
## reads from the child faster than a rate-limited client drains, which is the
## only way to reach its backpressure path.
case "$QUERY_STRING" in
*big*)
	i=0
	while [ $i -lt 8192 ]; do
		printf '%01024d' $i
		i=$((i + 1))
	done
	exit 0
	;;
esac

echo "REQUEST_METHOD:$REQUEST_METHOD"
echo "REQUEST_URI:$REQUEST_URI"
echo "QUERY_STRING:$QUERY_STRING"
echo "REMOTE_ADDR:$REMOTE_ADDR"
echo "REMOTE_PORT:$REMOTE_PORT"
echo "CONTENT_LENGTH:$CONTENT_LENGTH"
