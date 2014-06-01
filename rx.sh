#!/bin/bash

./klient -s localhost  "$@"  | \
   aplay -t raw -f cd -B 5000 -v - -D sysdefault -
