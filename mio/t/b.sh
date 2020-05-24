#!/bin/sh

echo "Content-Type: text/plain"
echo "Custom-Attribute: abcdef"
echo

printenv
exec cat /home/hyung-hwan/projects/hawk/lib/run.c
##exec cat /home/hyung-hwan/projects/hawk/lib/uch-prop.h
##exec cat /tmp/qq

