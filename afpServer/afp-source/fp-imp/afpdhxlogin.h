/*
 *	afpdhxlogin.h
 *	Created 04/27/09
 *
 *	Copyright © 2003-09, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpdhxlogin__
#define __afpdhxlogin__

#include "stdlib.h"

#include "afp.h"
#include "afpDebug.h"
#include "afphelp.h"
#include "afp_net_buffer.h"

#include "openssl/bn.h"
#include "openssl/dh.h"
#include "openssl/cast.h"

#define DHX_KEYSIZE 		16
#define DHX_PASSWDLEN 		64
#define DHX_RANDBUFSIZE		16
#define DHX_CRYPTBUFLEN  	(DHX_KEYSIZE*2)
#define DHX_CRYPT2BUFLEN 	(DHX_KEYSIZE + DHX_PASSWDLEN)
#define DHX_CHNGPSWDBUFLEN	(DHX_KEYSIZE + (DHX_PASSWDLEN*2))

//hash a number to a 16-bit integer
#define dhxhash(a) ((((unsigned long) (a) >> 8) ^ (unsigned long) (a)) & 0xffff)

// The function getrandom returns a random number between 
// min and max, which must be in integer range.
#define getrandom( min, max ) (SHORT)((rand() % (int)(((max) + 1) - (min))) + (min))

//
//The following is the blob we store between FPLogin() and 
//FPLoginCont() function calls.
//
typedef struct
{
	int16		sessionID;
	uint8		randomBuffer[16];
	CAST_KEY 	castkey;
}DHXInfo;

AFPERROR DHXLogin(
	IN	afp_net_buffer&		afpRequest,
	IN	afp_net_buffer&		afpReply,
	IN	LPSESSION		dsiSession,
	IN	DHXInfo*		dhxInfo
);

AFPERROR DHXLoginContinue(
	IN	afp_net_buffer&		afpRequest,
	IN	afp_net_buffer&		afpReply,
	IN	LPSESSION		dsiSession,
	IN	char*			afpPassword,
	IN	int16			afpPasswordLen,
	IN	DHXInfo*		dhxInfo
);

AFPERROR DHXChangePassword(
	IN	afp_net_buffer&		afpRequest,
	IN	afp_net_buffer&		afpReply,
	IN	LPSESSION		dsiSession,
	IN	char*			oldPassword,
	IN	int16			cbOldPassword,
	IN	char*			newPassword,
	IN	int16			cbNewPassword,
	IN	DHXInfo*		dhxInfo
);

AFPERROR DHXCheckRandnum(
	IN	unsigned char*	clientBuf,
	IN	unsigned char*	serverBuf
);

void afpGetRandomNumber(
	IN	LPSESSION		dsiSession,
	IN	char* 			buf,
	IN	int16 			cbBuf
);

#endif //__afpdhxlogin__
