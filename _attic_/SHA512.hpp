// 06/15/09 began

#ifndef CAT_SHA512_HPP
#define CAT_SHA512_HPP

#include <cat/crypt/hash/ICryptHash.hpp>

namespace cat {


// SHA-512 and SHA-384
class SHA512 : public ICryptHash
{
	static const int BITS = 512;
	static const int BYTES = BITS*2 / sizeof(u8); // 1024 bits at a time

	u64 State[8], Tweak[2];
	u64 Work[80];
	u32 used_bytes;

	void HashComputation();

public:
	~SHA512();
	bool BeginKey(int bits = BITS);
	bool BeginMAC(const ICryptHash *key_hash);
	bool BeginKDF(const ICryptHash *key_hash);
	void Crunch(const void *message, int bytes);
	void End();
	void Generate(void *out, int bytes);
};


} // namespace cat

#endif // CAT_SHA512_HPP
