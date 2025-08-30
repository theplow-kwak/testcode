#!/bin/bash

TARGET=${1:-"op_copy"}
g++ -std=c++2a -Wall -g -O0 -static -o $TARGET $TARGET.cpp -luring -lnvme
