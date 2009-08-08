#!/bin/sh

echo "-- This requires fasm to be in your path."
echo "-- Grab a copy from http://flatassembler.net or from your package manager."

fasm ../src/math/big_x64_elf.asm ../lib/cat/big_x64.o
