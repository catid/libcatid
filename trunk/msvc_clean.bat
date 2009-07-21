@ECHO OFF

REM This is used to clean all the cruft that MSVC adds after building

ECHO Deleting cruft...

DEL /Q lib\cat\*.lib

RMDIR /S /Q release
RMDIR /S /Q x64
RMDIR /S /Q debug
DEL /Q *.ncb
DEL /Q /AH *.suo

RMDIR /S /Q build\Common\Debug
RMDIR /S /Q build\Common\Release
RMDIR /S /Q build\Common\x64
DEL /Q build\Common\*.user

RMDIR /S /Q build\Math\Debug
RMDIR /S /Q build\Math\Release
RMDIR /S /Q build\Math\x64
DEL /Q build\Math\*.user

RMDIR /S /Q build\Codec\Debug
RMDIR /S /Q build\Codec\Release
RMDIR /S /Q build\Codec\x64
DEL /Q build\Codec\*.user

RMDIR /S /Q build\Crypt\Debug
RMDIR /S /Q build\Crypt\Release
RMDIR /S /Q build\Crypt\x64
DEL /Q build\Crypt\*.user

RMDIR /S /Q build\Tunnel\Debug
RMDIR /S /Q build\Tunnel\Release
RMDIR /S /Q build\Tunnel\x64
DEL /Q build\Tunnel\*.user

RMDIR /S /Q build\Framework\Debug
RMDIR /S /Q build\Framework\Release
RMDIR /S /Q build\Framework\x64
DEL /Q build\Framework\*.user

RMDIR /S /Q tests\ECC_Test\Debug
RMDIR /S /Q tests\ECC_Test\Release
RMDIR /S /Q tests\ECC_Test\x64
DEL /Q tests\ECC_Test\*.user

RMDIR /S /Q tests\TextCompress\Debug
RMDIR /S /Q tests\TextCompress\Release
RMDIR /S /Q tests\TextCompress\x64
DEL /Q tests\TextCompress\*.user
