// 06/12/09 fix: digest is now endian-neutral
// 06/11/09 part of libcat-1.0

#ifndef CAT_SHA256_HPP
#define CAT_SHA256_HPP

#include <cat/crypt/hash/ICryptHash.hpp>

namespace cat {


// SHA-256
class SHA256 : public ICryptHash
{
	static const int BITS = 256;
	static const int BYTES = BITS*2 / sizeof(u8); // 512 bits at a time

	u32 State[8], Tweak[2];
	u32 Work[64];
	u32 free_bytes;

	void HashComputation();

public:
	~SHA256();
	bool BeginKey(int bits = BITS);
	bool BeginMAC(const ICryptHash *key_hash);
	bool BeginKDF(const ICryptHash *key_hash);
	void Crunch(const void *message, int bytes);
	void End();
	void Generate(void *out, int bytes);
};


} // namespace cat

#endif // CAT_SHA256_HPP
