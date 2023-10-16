/*
 *	afpdhxlogin.cpp
 *	Created 05/04/09
 *
 *	Copyright © 2003-09, Michael J. Conrad. All Rights Reserved.
 *
 *	Implement the server-side DHCAST128 UAM. Details for how it's supposed
 *	to work can found at http://support.apple.com/kb/TA43945.
 *
 */

// ReSharper disable CppInconsistentNaming
// ReSharper disable StringLiteralTypo
// ReSharper disable CppCStyleCast
// ReSharper disable CppZeroConstantCanBeReplacedWithNullptr
// ReSharper disable CppClangTidyCppcoreguidelinesAvoidGoto
// ReSharper disable IdentifierTypo

// ReSharper disable CppClangTidyClangDiagnosticMicrosoftGoto
#include "StdAfx.h"
#include "strsafe.h"

#include "afpdhxlogin.h"
#include "afp_net_buffer.h"
#include "afp_session.h"


/*
 * DHXLogin()
 *
 * Description:
 *		Begin the login sequence for the DHCAST128 UAM.
 *
 * Returns: AFPERROR
 */

AFPERROR DHXLogin(
	IN const afp_net_buffer& afpRequest,
	IN afp_net_buffer& afpReply,
	IN dsi::dsi_connection* dsiSession,
	IN DHXInfo* dhxInfo
)
{
	unsigned char iv[] = "CJalbert";
	uint8 p[] = {
		0xBA, 0x28, 0x73, 0xDF, 0xB0, 0x60, 0x57, 0xD4,
		0x3F, 0x20, 0x24, 0x74, 0x4C, 0xEE, 0xE7, 0x5B
	};
	uint8 g = 0x07;
	BIGNUM* gbn = NULL;
	BIGNUM* pbn = NULL;
	DH* dh = NULL;
	AFPERROR afpError = afpParmErr;
	uint8 randbuf[DHX_RANDBUFSIZE];
	CAST_KEY castkey;
	int i;

	if (dhxInfo == NULL)
	{
		DBGWRITE(dbg_level_info, L"dhxInfo invalid\n");
		return (afpParmErr);
	}

	//
	//Obtain the client's public key
	//
	BIGNUM* bn = BN_bin2bn((unsigned char*)afpRequest.get_current_pos_ptr(), DHX_KEYSIZE, NULL);

	if (bn == NULL)
	{
		DBGWRITE(dbg_level_info, L"BN_bin2bn() failed obtaining the client's key\n");
		goto exit;
	}

	//
	//Obtain our primes
	//
	gbn = BN_bin2bn(&g, sizeof(g), NULL);

	if (gbn == NULL)
	{
		DBGWRITE(dbg_level_info, L"BN_bin2bn() failed obtaining obtaining our primes1\n");
		goto exit;
	}

	pbn = BN_bin2bn(p, sizeof(p), NULL);

	if (pbn == NULL)
	{
		DBGWRITE(dbg_level_info, L"BN_bin2bn() failed obtaining our primes2\n");
		goto exit;
	}

	dh = DH_new();

	if (dh == NULL)
	{
		DBGWRITE(dbg_level_info, L"DH_new() failed creating new DH context\n");
		goto exit;
	}

	//
	//Generate the key and ensure we have enough space
	//
	const BIGNUM* qs;
	const BIGNUM* ps;
	const BIGNUM* gs;

	DH_get0_pqg(dh, &ps, &qs, &gs);
	DH_set0_pqg(dh, pbn, const_cast<BIGNUM*>(qs), gbn);

	if (!DH_generate_key(dh) || (BN_num_bytes(DH_get0_pub_key(dh)) > DHX_KEYSIZE))
	{
		DBGWRITE(dbg_level_info, L"DH_generate_key() failed generating our public keys\n");
		goto exit;
	}

	pbn = NULL;
	gbn = NULL;

	//
	//Figure out the key, use the reply buffer as a temporary buffer.
	//
	i = DH_compute_key((unsigned char*)afpReply.get_current_pos_ptr(), bn, dh);

	//
	//Now, set the key
	//
	CAST_set_key(&castkey, i, (unsigned char*)afpReply.get_current_pos_ptr());

	//
	//Compute and set the session id which is based on our session object
	//
	const uint16 sessid = dhxhash((ULONG_PTR)dsiSession);
	afpReply.push_num(sessid);

	//
	//Send our public key along to the client
	//
	BN_bn2bin(DH_get0_pub_key(dh), (unsigned char*)afpReply.get_current_pos_ptr());
	afpReply.advance_pos_ptr(DHX_KEYSIZE);

	//
	//Now we setup our buffer to be encrypted.
	//
	afpGetRandomNumber(dsiSession, (char*)randbuf, sizeof(randbuf));

	memcpy(afpReply.get_current_pos_ptr(), &randbuf, sizeof(randbuf));
	memset((unsigned char*)afpReply.get_current_pos_ptr() + DHX_RANDBUFSIZE, 0, DHX_RANDBUFSIZE);

	CAST_cbc_encrypt(
		(unsigned char*)afpReply.get_current_pos_ptr(),
		(unsigned char*)afpReply.get_current_pos_ptr(),
		DHX_CRYPTBUFLEN,
		&castkey,
		iv,
		CAST_ENCRYPT
	);

	afpReply.advance_pos_ptr(DHX_CRYPTBUFLEN);

	//
	//Save off all the needed information for the login continue
	//API call. This is stored in a blob in the afpSession object.
	//
	memcpy(dhxInfo->randomBuffer, randbuf, sizeof(randbuf));
	memcpy(&dhxInfo->castkey, &castkey, sizeof(castkey));
	dhxInfo->sessionID = sessid;

	afpError = afpAuthContinue;

exit:
	if (bn) BN_free(bn);
	if (gbn) BN_free(gbn);
	if (pbn) BN_free(pbn);
	if (dh) DH_free(dh);

	return (afpError);
}


/*
 * DHXLoginContinue()
 *
 * Description:
 *		Continue a login sequence for the DHCAST128 UAM.
 *
 *		Note that this funciton expects the afpReqBuffer to point at
 *		the positino just past the afp command and flags fields.
 *
 * Returns: AFPERROR
 */

AFPERROR DHXLoginContinue(
	IN afp_net_buffer& afpRequest,
	IN afp_net_buffer& afpReply,
	IN LPSESSION dsiSession,
	IN char* afpPassword,
	IN int16 afpPasswordLen,
	IN DHXInfo* dhxInfo
)
{
	UNREFERENCED_PARAMETER(dsiSession);

	unsigned char iv[] = "LWallace";
	char* tempBuf = afpReply.get_current_pos_ptr(); //use as temporary buffer space

	if (dhxInfo == NULL)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXLoginContinue]dhxInfo invalid\n");
		return (afpMiscErr);
	}

	//
	//Nab the session ID that we created in the initial login step above
	//and make sure the session ID matches what we sent over. Note that
	//we save the session ID in the afpSession login blob storage.
	//
	const auto sessionID = afpRequest.pull_num<INT16>();

	if (sessionID != dhxInfo->sessionID)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXLoginContinue]Session ID's don't match! (%d vs. %d)\n", sessionID,
		         dhxhash((ULONG_PTR)dsiSession));
		return (afpParmErr);
	}

	//
	//Decrypt the buffer containing the random number and password. We'll
	//use the reply buffer as temporary buffer space for now.
	//
	CAST_cbc_encrypt(
		(unsigned char*)afpRequest.get_current_pos_ptr(),
		(unsigned char*)tempBuf,
		DHX_CRYPT2BUFLEN,
		&dhxInfo->castkey,
		iv,
		CAST_DECRYPT
	);

	//
	//Ensure the random number we were sent back is the same. The client
	//sends back the same number incremented by 1.
	//
	AFPERROR afpError = DHXCheckRandnum((unsigned char*)tempBuf, dhxInfo->randomBuffer);

	if (!AFP_SUCCESS(afpError))
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXLoginContinue]Randnum check failed, exiting\n");
		goto exit;
	}

	//
	//We don't require the random number buffer or the key anymore.
	//
	SecureZeroMemory(tempBuf, DHX_RANDBUFSIZE);

	tempBuf += DHX_RANDBUFSIZE;
	tempBuf[DHX_PASSWDLEN] = '\0';

	//
	//Copy the CLEARTEXT password into the supplied buffer. Don't
	//forget to clear it later!!
	//
	if (DHX_PASSWDLEN > afpPasswordLen)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXLoginContinue]Password buffer too small\n");

		afpError = afpParmErr;
		goto exit;
	}

	memcpy(afpPassword, tempBuf, DHX_PASSWDLEN);

	DBGWRITE(dbg_level_info, L"[afpuser:DHXLoginContinue]Successful continued login for DHCAST128\n");
	afpError = AFP_OK;

exit:
	//
	//Clear the password out of the temporary buffer.
	//
	SecureZeroMemory(tempBuf, DHX_CRYPT2BUFLEN);

	return (afpError);
}


/*
 * DHXChangePassword()
 *
 * Description:
 *		Grab the encrypted old and new passwords.
 *
 * Returns: AFPERROR
 */

AFPERROR DHXChangePassword(
	IN afp_net_buffer& afpRequest,
	IN afp_net_buffer& afpReply,
	IN dsi::dsi_connection* dsiSession,
	IN char* oldPassword,
	IN int16 cbOldPassword,
	IN char* newPassword,
	IN int16 cbNewPassword,
	IN DHXInfo* dhxInfo
)
{
	unsigned char iv[] = "LWallace";
	int16 sessionID = 0;
	AFPERROR afpError = afpParmErr;
	char* tempBuff = afpReply.get_current_pos_ptr(); //user as temporary buffer space

	if (dhxInfo == NULL)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]dhxInfo invalid\n");
		return (afpMiscErr);
	}

	//
	//Nab the session ID that we created in the initial login step above
	//and make sure the session ID mataches what we sent over. Note that
	//we save the session ID in the afpSession login blob storage.
	//
	sessionID = afpRequest.pull_num<INT16>();

	//
	//If there is no ID in the buffer, then this is the first time through
	//the change pswrd process. We need to call the login stuff again to
	//setup all the key exchanges.
	//
	if (sessionID == 0)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]First time through, calling DHXLogin()\n");

		return (DHXLogin(
			afpRequest,
			afpReply,
			dsiSession,
			dhxInfo));
	}

	DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]Second time through, completing decryption\n");

	//
	//If we have a sessionID, then that means we're on the second time
	//through here.
	//
	if (sessionID != dhxInfo->sessionID)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]Session ID's don't match! (%d vs. %d)\n", sessionID,
		         dhxhash((ULONG_PTR)dsiSession));
		return (afpParmErr);
	}

	//
	//Decrypt the buffer containing the random number and password. We'll
	//use the reply buffer as temporary buffer space for now.
	//
	CAST_cbc_encrypt(
		(unsigned char*)afpRequest.get_current_pos_ptr(),
		(unsigned char*)tempBuff,
		DHX_CHNGPSWDBUFLEN,
		&dhxInfo->castkey,
		iv,
		CAST_DECRYPT
	);

	//
	//Ensure the random number we were sent back is the same. The client
	//sends back the same number incremented by 1.
	//
	afpError = DHXCheckRandnum((unsigned char*)tempBuff, dhxInfo->randomBuffer);

	if (!AFP_SUCCESS(afpError))
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]Randnum check failed, exiting with parmErr\n");
		goto exit;
	}

	//
	//Clear the random number from the buffer
	//
	memset(tempBuff, 0, DHX_RANDBUFSIZE);
	tempBuff += DHX_RANDBUFSIZE;

	//
	//Extract the old and new passwords (now in the clear) from the buffer
	//
	tempBuff[DHX_PASSWDLEN * 2] = '\0';

	//
	//Copy the old password into the supplied buffer
	//
	StringCbCopyA(oldPassword, cbOldPassword, &tempBuff[DHX_PASSWDLEN]);

	//
	//Now get the new password from the buffer
	//
	tempBuff[DHX_PASSWDLEN] = '\0';
	StringCbCopyA(newPassword, cbNewPassword, tempBuff);

	DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]Old password: %s\n", oldPassword);
	DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]New password: %s\n", newPassword);

	//
	//If we get to this point, everything should be peachy
	//
	afpError = AFP_OK;

exit:
	//
	//Clear the password out of the temporary buffer.
	//
	memset(tempBuff, 0, DHX_PASSWDLEN * 2);

	DBGWRITE(dbg_level_info, L"[afpuser:DHXChangePassword]Returning: %d\n", afpError);

	return (afpError);
}


/*
 * DHXCheckRandnum()
 *
 * Description:
 *		For DHX, the client takes a 16 byte random number we pass in DHXLogin()
 *		and increments it by 1. Check to make sure it is incremented.
 *
 * Returns: AFPERROR if not incremented
 */

AFPERROR DHXCheckRandnum(
	IN unsigned char* clientBuf,
	IN unsigned char* serverBuf
)
{
	BIGNUM* bn1 = NULL;
	BIGNUM* bn2 = NULL;
	BIGNUM* bnResult = NULL;
	AFPERROR afpError = afpParmErr;

	//
	//Get the two buffers into BIGNUM structures for the math
	//
	bn1 = BN_bin2bn(clientBuf, DHX_RANDBUFSIZE, NULL);

	if (bn1 == NULL)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXCheckRandnum]Failed to extract random number key\n");
		goto exit;
	}

	bn2 = BN_bin2bn(serverBuf, DHX_RANDBUFSIZE, NULL);

	if (bn2 == NULL)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXCheckRandnum]Failed to get random number\n");
		goto exit;
	}

	//
	//Now do the math to ensure our random number was incremented by 1.
	//
	bnResult = BN_new();

	if (bnResult == NULL)
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXCheckRandnum]Failed to create BIGNUM 3\n");
		goto exit;
	}

	BN_sub(bnResult, bn1, bn2);

	if (!BN_is_one(bnResult))
	{
		DBGWRITE(dbg_level_info, L"[afpuser:DHXCheckRandnum]Random was not incremented by 1 by client!\n");
		goto exit;
	}

	//
	//If we get here, all has succeeded
	//
	afpError = AFP_OK;

exit:

	if (bn1) BN_free(bn1);
	if (bn2) BN_free(bn2);
	if (bnResult) BN_free(bnResult);

	DBGWRITE(dbg_level_info, L"[afpuser:DHXCheckRandnum]Returning: %d\n", afpError);

	return (afpError);
}


/*
 * afpGetRandomNumber()
 *
 * Description:
 *		Returns a random number for use in encrypted UAMs
 *
 * Returns: AFPERROR
 */

void afpGetRandomNumber(
	IN dsi::dsi_connection* dsiSession,
	IN char* buf,
	IN int16 cbBuf
)
{
	uint32 result;

	srand((ULONG_PTR)dsiSession & 0x0000FFFF);

	for (int i = 0; i < cbBuf; i += sizeof(result))
	{
		result = rand();  // NOLINT(concurrency-mt-unsafe)

		memcpy(buf + i, &result, sizeof(result));
	}
}
