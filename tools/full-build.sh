#!/bin/bash

make clean > doc/errors.txt 2>&1
make static >> doc/errors.txt 2>&1
make shared >> doc/errors.txt 2>&1
make stubs >> doc/errors.txt 2>&1
make shell >> doc/errors.txt 2>&1
make testlib >> doc/errors.txt 2>&1
make bridge >> doc/errors.txt 2>&1
make static-shell >> doc/errors.txt 2>&1
