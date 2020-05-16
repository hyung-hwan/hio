#!/bin/sh

echo "Content-Type: text/plain"
echo "Custom-Attribute: abcdef"
echo

exec cat /home/hyung-hwan/projects/hawk/lib/run.c
