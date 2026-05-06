#!/bin/bash
echo "$1"
read a
if [ "$a" != "$1" ]; then echo "Expected $1 got $a" >&2; exit 2; fi
exit 0