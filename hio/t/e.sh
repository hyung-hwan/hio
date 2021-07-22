#!/bin/sh

## curl -v --http1.0 --data-binary @/etc/group --http1.1 http://127.0.0.1:9988/home/hyung-hwan/projects/hio/t/d.sh

echo "Content-Type: text/plain"
echo

if IFS= read -r x
then
	q="${x}"
	while IFS= read -r x
	do
	q="${q}
${x}"
	done
else
	q = ""
fi

sleep 3
printf "%s" "$q"
##echo "<<EOF>>"
