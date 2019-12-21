#!/bin/sh

set -e
set -x

CC=${CC:-cc}

uname -a
ARCH=`uname -m`
${CC} --version

./configure
make all
./configure --enable-coverage
make clean all
sudo make install
python3 setup.py build
sudo python3 setup.py install

if [ "${ARCH}" = "arm64" ]
then
  exit 0
fi

cd src

for badargs in '--foobar' '-?' 'x'
do
  ./testskew ${badarg} 2>/dev/null && false
done

for arg in '' '-w' '-p' '-p -w'
do
  ./testskew ${arg} -m 997.983 0.1
  ./testskew ${arg} -L 200 5
  ./testskew ${arg} 10 5.0 20 2.5 40 2.5 80 2.5 160 2.5 320 2.5
  ./testskew ${arg} -f 0.75 -v 0.1 100
  sh ./findskew.sh ${arg}
done
