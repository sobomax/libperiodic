#!/bin/sh

set -e

CC=${CC:-cc}

uname -a
${CC} --version

./configure
make all
cd src
./findskew.sh