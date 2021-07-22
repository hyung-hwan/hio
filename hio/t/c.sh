#!/bin/sh

file=/home/hyung-hwan/projects/hawk/lib/run.c

echo "Content-Type: text/plain"
echo "Custom-Attribute: abcdef"
#echo "Content-Length: " `stat -c %s "${file}"`
echo "Content-Length: iurtoitre"
echo

echo "abc"
exec cat "${file}"
