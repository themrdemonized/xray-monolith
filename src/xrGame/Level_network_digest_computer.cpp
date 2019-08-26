#include "stdafx.h"
#include "Level.h"

#define DEFAULT_MODULE_HASH "3CAABCFCFF6F3A810019C6A72180F166"

extern void GetCDKey_FromRegistry(char* CDKeyStr);

char const* ComputeClientDigest(string128& dest)
{
	xr_strcpy(dest, sizeof(dest), DEFAULT_MODULE_HASH);
	return dest;
};

void CLevel::SendClientDigestToServer()
{
	string128 tmp_digest;
	NET_Packet P;
	P.w_begin(M_SV_DIGEST);
	m_client_digest = ComputeClientDigest(tmp_digest);
	P.w_stringZ(m_client_digest);
	SecureSend(P, net_flags(TRUE, TRUE, TRUE, TRUE));
}
