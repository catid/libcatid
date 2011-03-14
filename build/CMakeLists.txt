cmake_minimum_required(VERSION 2.6) # or other version

project(LIBCAT)

OPTION(BUILD_ECC_TEST "Build Elliptic Curve Cryptography Test" ON)
OPTION(BUILD_NETCODE_TEST "Build MMO NetCode Test" ON)

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
if (WIN32)
    target_link_libraries(libcatcommon winmm.lib)
else (WIN32)
    target_link_libraries(libcatcommon pthread)
endif (WIN32)

# Codec
add_library(libcatcodec STATIC
${SRC}/codec/RangeCoder.cpp)
target_link_libraries(libcatcodec libcatcommon)

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
target_link_libraries(libcatcrypt libcatcommon)
if (WIN32)
    target_link_libraries(libcatcrypt rpcrt4.lib iphlpapi.lib psapi.lib advapi32.lib)
endif (WIN32)

# Math
add_library(libcatmath STATIC
${SRC}/math/BigRTL.cpp
${SRC}/math/BigPseudoMersenne.cpp
${SRC}/math/BigTwistedEdwards.cpp
${SRC}/math/BigBinaryExtension.cpp
${SRC}/math/BigMontgomery.cpp)
target_link_libraries(libcatmath libcatcommon)

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
target_link_libraries(libcattunnel libcatcrypt libcatmath)

# AsyncIO
add_library(libcatasyncio STATIC
${SRC}/iocp/IOThreads.cpp
${SRC}/iocp/UDPEndpoint.cpp
${SRC}/net/Sockets.cpp
${SRC}/io/IOLayer.cpp
${SRC}/crypt/tunnel/AuthenticatedEncryption.cpp)
target_link_libraries(libcatasyncio libcatcommon)
if (WIN32)
    target_link_libraries(libcatasyncio ws2_32.lib)
endif (WIN32)

# Sphynx
add_library(libcatsphynx STATIC
${SRC}/net/DNSClient.cpp
${SRC}/sphynx/FlowControl.cpp
${SRC}/sphynx/Server.cpp
${SRC}/sphynx/Transport.cpp
${SRC}/sphynx/Client.cpp
${SRC}/sphynx/ConnexionMap.cpp
${SRC}/sphynx/Connexion.cpp
${SRC}/sphynx/SphynxLayer.cpp
${SRC}/sphynx/FileTransfer.cpp)
target_link_libraries(libcatsphynx libcattunnel libcatasyncio)

if (BUILD_ECC_TEST)

# ECC Test
add_executable(TEST_ECC
${TESTS}/ECC_Test/ecc_unit_test.cpp
${TESTS}/ECC_Test/SecureClientDemo.cpp
${TESTS}/ECC_Test/SecureServerDemo.cpp)
target_link_libraries(TEST_ECC libcattunnel)

endif (BUILD_ECC_TEST)

if (BUILD_NETCODE_TEST)

# ChatServer Test
add_executable(ChatServer
${TESTS}/SecureChatServer/ChatServer.cpp)
target_link_libraries(ChatServer libcatsphynx)

# ChatClient Test
add_executable(ChatClient
${TESTS}/SecureChatClient/ChatClient.cpp)
target_link_libraries(ChatClient libcatsphynx)

endif (BUILD_NETCODE_TEST)
