cmake_minimum_required(VERSION 2.6) # or other version

project(LIBCAT)

# Define some shortcuts
SET(SRC ../src/)
SET(LIB ../lib/)
SET(INC ../include/)
SET(TESTS ../tests/)

include_directories(${INC})
link_directories(${LIB}/cat)
set(LIBRARY_OUTPUT_PATH ${LIB}/cat)

# Common
add_library(libcatcommon STATIC
${SRC}/port/EndianNeutral.cpp
${SRC}/port/SystemInfo.cpp
${SRC}/threads/WorkerThreads.cpp
${SRC}/threads/Thread.cpp
${SRC}/threads/Mutex.cpp
${SRC}/threads/RWLock.cpp
${SRC}/threads/WaitableFlag.cpp
${SRC}/time/Clock.cpp
${SRC}/lang/Strings.cpp
${SRC}/io/Settings.cpp
${SRC}/io/Logging.cpp
${SRC}/io/MappedFile.cpp
${SRC}/rand/MersenneTwister.cpp
${SRC}/rand/StdRand.cpp
${SRC}/mem/AlignedAllocator.cpp
${SRC}/mem/BufferAllocator.cpp
${SRC}/mem/LargeAllocator.cpp
${SRC}/mem/StdAllocator.cpp
${SRC}/mem/IAllocator.cpp
${SRC}/parse/BufferTok.cpp
${SRC}/parse/Base64.cpp
${SRC}/CommonLayer.cpp
${SRC}/hash/Murmur.cpp)

# Codec
add_library(libcatcodec STATIC
${SRC}/codec/RangeCoder.cpp)

# Crypt
add_library(libcatcrypt STATIC
${SRC}/crypt/privatekey/ChaCha.cpp
${SRC}/crypt/cookie/CookieJar.cpp
${SRC}/crypt/rand/EntropyLinux.cpp
${SRC}/crypt/rand/EntropyWindows.cpp
${SRC}/crypt/rand/EntropyWindowsCE.cpp
${SRC}/crypt/rand/EntropyGeneric.cpp
${SRC}/crypt/rand/Fortuna.cpp
${SRC}/crypt/hash/HMAC_MD5.cpp
${SRC}/crypt/hash/Skein.cpp
${SRC}/crypt/hash/Skein256.cpp
${SRC}/crypt/hash/Skein512.cpp
${SRC}/crypt/SecureCompare.cpp)

# Math
add_library(libcatmath STATIC
${SRC}/math/BigRTL.cpp
${SRC}/math/BigPseudoMersenne.cpp
${SRC}/math/BigTwistedEdwards.cpp
${SRC}/math/BigBinaryExtension.cpp
${SRC}/math/BigMontgomery.cpp)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)

    # x64-bit assembly code
    if (WIN32)
        set(big_x64_files ${LIB}/cat/big_x64.obj)
    else (WIN32)
        set(big_x64_files ${LIB}/cat/big_x64.o)
    endif (WIN32)

    # wrap the pre-built code in a static link library
    add_library(big_x64_obj STATIC EXCLUDE_FROM_ALL ${big_x64_files})
    set_source_files_properties(${big_x64_files} PROPERTIES EXTERNAL_OBJECT true GENERATED true)
    set_target_properties(big_x64_obj PROPERTIES LINKER_LANGUAGE C ARCHIVE_OUTPUT_DIRECTORY ${LIB}/cat)

    target_link_libraries(libcatmath big_x64_obj)

endif (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)

# Tunnel
add_library(libcattunnel STATIC
${SRC}/crypt/tunnel/Keys.cpp
${SRC}/crypt/tunnel/KeyAgreement.cpp
${SRC}/crypt/tunnel/KeyAgreementInitiator.cpp
${SRC}/crypt/tunnel/KeyAgreementResponder.cpp
${SRC}/crypt/tunnel/EasyHandshake.cpp
${SRC}/crypt/tunnel/AuthenticatedEncryption.cpp)

# AsyncIO
add_library(libcatasyncio STATIC

if (WIN32)
    ${SRC}/iocp/IOThreads.cpp
    ${SRC}/iocp/UDPEndpoint.cpp
else (WIN32)
    ${SRC}/net/IOThreads.cpp
    ${SRC}/net/UDPEndpoint.cpp
endif (WIN32)

${SRC}/net/Sockets.cpp
${SRC}/io/IOLayer.cpp
${SRC}/crypt/tunnel/AuthenticatedEncryption.cpp)

# ECC Test
add_executable(TEST_ECC
${TESTS}/ECC_Test/ecc_unit_test.cpp
${TESTS}/ECC_Test/SecureClientDemo.cpp
${TESTS}/ECC_Test/SecureServerDemo.cpp)

# ChatServer Test
add_executable(ChatServer
${TESTS}/SecureChatServer/ChatServer.cpp)

# ChatClient Test
add_executable(ChatClient
${TESTS}/SecureChatClient/ChatClient.cpp)

# Library linkage
target_link_libraries(libcatasyncio libcatcommon)
target_link_libraries(libcatcodec libcatcommon)
target_link_libraries(libcatcrypt libcatcommon)
target_link_libraries(libcatmath libcatcommon)
target_link_libraries(libcattunnel libcatcrypt libcatmath)
target_link_libraries(libcatsphynx libcattunnel libcatasyncio)
target_link_libraries(TEST_ECC libcattunnel)
target_link_libraries(ChatServer libcatsphynx)
target_link_libraries(ChatClient libcatsphynx)

# OS-dependent linkage
if (WIN32)
  target_link_libraries(libcatcommon winmm.lib)
  target_link_libraries(libcatcrypt rpcrt4.lib iphlpapi.lib psapi.lib advapi32.lib)
else (WIN32)
  target_link_libraries(libcatcommon pthread)
endif (WIN32)
