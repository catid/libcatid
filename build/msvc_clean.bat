@ECHO OFF

REM This is used to clean all the cruft that MSVC adds after building

ECHO Deleting cruft...

DEL /Q ..\lib\cat\*.lib

RMDIR /S /Q release
RMDIR /S /Q x64
RMDIR /S /Q debug
DEL /Q *.ncb
DEL /Q /AH *.suo

RMDIR /S /Q Common\Debug
RMDIR /S /Q Common\Release
RMDIR /S /Q Common\x64
DEL /Q Common\*.user

RMDIR /S /Q Math\Debug
RMDIR /S /Q Math\Release
RMDIR /S /Q Math\x64
DEL /Q Math\*.user

RMDIR /S /Q Codec\Debug
RMDIR /S /Q Codec\Release
RMDIR /S /Q Codec\x64
DEL /Q Codec\*.user

RMDIR /S /Q Crypt\Debug
RMDIR /S /Q Crypt\Release
RMDIR /S /Q Crypt\x64
DEL /Q Crypt\*.user

RMDIR /S /Q Tunnel\Debug
RMDIR /S /Q Tunnel\Release
RMDIR /S /Q Tunnel\x64
DEL /Q Tunnel\*.user

RMDIR /S /Q Framework\Debug
RMDIR /S /Q Framework\Release
RMDIR /S /Q Framework\x64
DEL /Q Framework\*.user

RMDIR /S /Q ..\tests\ECC_Test\Debug
RMDIR /S /Q ..\tests\ECC_Test\Release
RMDIR /S /Q ..\tests\ECC_Test\x64
DEL /Q ..\tests\ECC_Test\*.user

RMDIR /S /Q ..\tests\TextCompress\Debug
RMDIR /S /Q ..\tests\TextCompress\Release
RMDIR /S /Q ..\tests\TextCompress\x64
DEL /Q ..\tests\TextCompress\*.user
