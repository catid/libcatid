cmake_minimum_required(VERSION 2.6) # or other version

project(LIBCAT)

SET(SRC ../src/)
SET(LIB ../lib/)
SET(INC ../include/)
SET(TESTS ../tests/)

include_directories(${INC})
link_directories(${LIB}/cat)

add_library(libcatcommon STATIC
${SRC}/port/AlignedAlloc.cpp
${SRC}/threads/Atomic.cpp
${SRC}/math/BitMath.cpp
${SRC}/time/Clock.cpp
${SRC}/rand/MersenneTwister.cpp
${SRC}/rand/StdRand.cpp
${SRC}/hash/MurmurHash2.cpp)

add_library(libcatcodec STATIC
${SRC}/codec/RangeCoder.cpp)

add_library(libcatcrypt STATIC
${SRC}/crypt/privatekey/ChaCha.cpp
${SRC}/crypt/cookie/CookieJar.cpp
${SRC}/crypt/rand/EntropyLinux.cpp
${SRC}/crypt/rand/EntropyWindows.cpp
${SRC}/crypt/rand/Fortuna.cpp
${SRC}/crypt/hash/HMAC_MD5.cpp
${SRC}/crypt/hash/Skein.cpp
${SRC}/crypt/hash/Skein256.cpp
${SRC}/crypt/hash/Skein512.cpp
${SRC}/crypt/SecureCompare.cpp)

add_library(libcatmath STATIC
${SRC}/math/BigRTL.cpp
${SRC}/math/BigPseudoMersenne.cpp
${SRC}/math/BigTwistedEdward.cpp)

set(big_x64_files ${LIB}/cat/big_x64.obj)
add_library(big_x64_obj STATIC EXCLUDE_FROM_ALL ${big_x64_files})
set_source_files_properties(${big_x64_files} PROPERTIES EXTERNAL_OBJECT true GENERATED true)
set_target_properties(big_x64_obj PROPERTIES LINKER_LANGUAGE C ARCHIVE_OUTPUT_DIRECTORY ${LIB}/cat)

add_dependencies(libcatmath big_x64_obj)

add_library(libcattunnel STATIC
${SRC}/crypt/tunnel/KeyMaker.cpp
${SRC}/crypt/tunnel/KeyAgreement.cpp
${SRC}/crypt/tunnel/KeyAgreementInitiator.cpp
${SRC}/crypt/tunnel/KeyAgreementResponder.cpp
${SRC}/crypt/tunnel/AuthenticatedEncryption.cpp)

add_library(libcatframework STATIC
${SRC}/parse/BitStream.cpp
${SRC}/parse/BufferTok.cpp
${SRC}/parse/MessageRouter.cpp
${SRC}/net/IOCPSockets.cpp
${SRC}/io/Logging.cpp
${SRC}/io/MMapFile.cpp
${SRC}/io/Settings.cpp
${SRC}/threads/Mutex.cpp
${SRC}/threads/RegionAllocator.cpp)

add_executable(TEST_ECC
${TESTS}/ECC_Test/ecc_unit_test.cpp
${TESTS}/ECC_Test/SecureClientDemo.cpp
${TESTS}/ECC_Test/SecureServerDemo.cpp)

add_executable(TEST_COMPRESS
${TESTS}/TextCompress/text_compress_test.cpp)

add_dependencies(libcatcodec libcatcommon)
add_dependencies(libcatcrypt libcatcommon)
add_dependencies(libcatmath libcatcommon)
add_dependencies(libcattunnel libcatcommon libcatcrypt libcatmath)
add_dependencies(libcatframework libcattunnel libcatcodec)
add_dependencies(TEST_ECC libcattunnel)
add_dependencies(TEST_COMPRESS libcatframework)

