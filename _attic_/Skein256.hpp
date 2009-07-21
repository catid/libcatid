// 06/18/09 began

/*
	Bruce Schneier's SHA-3 candidate Skein hash function
	http://www.skein-hash.info/
*/

#ifndef CAT_SKEIN256_HPP
#define CAT_SKEIN256_HPP

#include <cat/crypt/hash/Skein.hpp>

namespace cat {


// Skein-256
class Skein256 : public Skein
{
	static const int BITS = 256;
	static const int WORDS = BITS / 64;
	static const int BYTES = BITS / 8;

	u64 State[WORDS];
	u8 Work[BYTES];
	int used_bytes;

	void HashComputation(const void *message, int blocks, u32 byte_count, u64 *NextState);
	void GenerateInitialState(int bits);

public:
	~Skein256();
	bool BeginKey(int bits = BITS);
	bool BeginMAC(const ICryptHash *parent = 0);
	bool BeginKDF(const ICryptHash *parent = 0);
	void Crunch(const void *message, int bytes);
	void End();
	void Generate(void *out, int bytes);
};



} // namespace cat

#endif // CAT_SKEIN256_HPP
