#!/bin/sh

## curl -v --http1.0 --data-binary @/etc/group --http1.1 http://127.0.0.1:9988/home/hyung-hwan/projects/mio/t/d.sh

echo "Content-Type: text/plain"
echo

while read x
do
	echo $x
done
