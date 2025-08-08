#!/bin/bash

TARGET=${1:-"op_copy"}
g++ -std=c++2a -Wall -g -O0 -DNDEBUG -static -o $TARGET $TARGET.cpp -luring -lnvme
