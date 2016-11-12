#!/bin/sh

mkdir -p build
g++ main.cpp -std=c++11 `pkg-config hidapi-libusb --cflags` `pkg-config hidapi-libusb --libs` -lusb -o build/pm8
