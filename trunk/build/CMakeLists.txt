cmake_minimum_required(VERSION 2.6) # or other version

project(LIBCAT)

if (CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif (CMAKE_COMPILER_IS_GNUCXX)

SET(SRC ../src/)
SET(LIB ../lib/)
SET(INC ../include/)
SET(TESTS ../tests/)

include_directories(${INC})
link_directories(${LIB}/cat)

add_library(libcatcommon STATIC
${SRC}/port/AlignedAlloc.cpp
${SRC}/time/Clock.cpp
${SRC}/rand/MersenneTwister.cpp
${SRC}/rand/StdRand.cpp
${SRC}/hash/MurmurHash2.cpp)

add_library(libcatcodec STATIC
${SRC}/codec/RangeCoder.cpp)

add_dependencies(libcatcodec libcatcommon)

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

add_dependencies(libcatcrypt libcatcommon)

add_library(libcatmath STATIC
${SRC}/math/BigRTL.cpp
${SRC}/math/BigPseudoMersenne.cpp
${SRC}/math/BigTwistedEdward.cpp)

add_dependencies(libcatmath libcatcommon)

if (WIN32)
  set(big_x64_files ${LIB}/cat/big_x64.obj)
else (WIN32)
  set(big_x64_files ${LIB}/cat/big_x64.o)
endif (WIN32)

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

add_dependencies(libcattunnel libcatcommon libcatcrypt libcatmath)

add_executable(TEST_ECC
${TESTS}/ECC_Test/ecc_unit_test.cpp
${TESTS}/ECC_Test/SecureClientDemo.cpp
${TESTS}/ECC_Test/SecureServerDemo.cpp)

add_dependencies(TEST_ECC libcattunnel)

if (WIN32)
  target_link_libraries (libcatcommon winmm.lib)
  target_link_libraries (libcatcrypt rpcrt4.lib iphlpapi.lib psapi.lib advapi32.lib)
endif (WIN32)

if (WIN32)
  target_link_libraries (TEST_ECC libcattunnel libcatcommon libcatcrypt libcatmath)
else (WIN32)
  target_link_libraries (TEST_ECC libcattunnel libcatcommon libcatcrypt libcatmath big_x64_obj)
endif (WIN32)
