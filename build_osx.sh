#!/bin/sh

mkdir -p build
clang++ -std=c++11 `pkg-config hidapi --cflags` `pkg-config hidapi --libs` -o build/pm8 main.cpp
