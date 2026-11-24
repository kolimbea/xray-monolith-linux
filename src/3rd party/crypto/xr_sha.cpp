#include "xr_sha.h"
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
// OpenSSL 1.1+ dropped SHA-0 (what SHA_* computes). Own implementation keeps the digests identical;
// m_sha_ctx (declared as OpenSSL's SHAstate_st*) only ever points to an xr_sha0_ctx here.
#include "sha0.h"
#define SHA_CTX_NEW() reinterpret_cast<SHAstate_st*>(xr_new<xr_sha0_ctx>())
#define SHA_CTX_DELETE(c) do { xr_sha0_ctx* p_ = reinterpret_cast<xr_sha0_ctx*>(c); xr_delete(p_); c = nullptr; } while (0)
#define SHA_Init(c) xr_sha0_init(*reinterpret_cast<xr_sha0_ctx*>(c))
#define SHA_Update(c, d, n) xr_sha0_update(*reinterpret_cast<xr_sha0_ctx*>(c), d, n)
#define SHA_Final(o, c) xr_sha0_final(o, *reinterpret_cast<xr_sha0_ctx*>(c))
#else
#include <openssl/sha.h>
#define SHA_CTX_NEW() xr_new<SHA_CTX>()
#define SHA_CTX_DELETE(c) xr_delete(c)
#endif

namespace crypto
{

xr_sha256::xr_sha256()
{
	m_sha_ctx		= SHA_CTX_NEW();
}

xr_sha256::~xr_sha256()
{
	SHA_CTX_DELETE(m_sha_ctx);
}

void	xr_sha256::start_calculate		(u8 const * data, u32 data_size)
{
	SHA_Init		(m_sha_ctx);
	ZeroMemory		(m_result, sizeof(m_result));
	VERIFY			(data_size);
	m_data_src		= data;
	m_data_size		= data_size;
}

bool	xr_sha256::continue_calculate	()
{
	u32 const to_calc	= m_data_size >= calc_chunk_size ? calc_chunk_size : m_data_size;

	SHA_Update			(m_sha_ctx, m_data_src, to_calc);

	m_data_src			+= to_calc;
	m_data_size			-= to_calc;

	if (!m_data_size)
	{
		SHA_Final(m_result, m_sha_ctx);
		return true;
	}
	return false;
}

} //namespace crypto
