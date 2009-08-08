@ECHO OFF

ECHO ------------------------------------------------------------------
ECHO Assumes FASM is located at C:\FASM\FASM.EXE or in the system path.
ECHO Download FASM from http://flatassembler.net/

PATH=%PATH%;C:\FASM

ECHO Assembling 64-bit Pseudo-Mersenne library...
ECHO ------------------------------------------------------------------

FASM.EXE "..\src\math\big_x64_mscoff.asm" "..\lib\cat\big_x64.obj"

ECHO ------------------------------------------------------------------

PAUSE
