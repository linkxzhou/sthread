#!/bin/sh

scp tests/makefile root@39.106.117.2:/root/source/tests/
scp -r tests/*.cpp root@39.106.117.2:/root/source/tests/
scp -r include/* root@39.106.117.2:/root/source/include
scp -r *.cpp root@39.106.117.2:/root/source/