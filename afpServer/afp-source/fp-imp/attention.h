//===================================================================
// attention.h
//===================================================================

#include "dsi.h"
#include "afp.h"

//
//This is how long the attention thread will sleep before
//checking for another ocurrance requiring attention.
//
#define	ATTN_SLEEP_INTERVAL		5000

DWORD WINAPI AFPAttentionThread(
		IN LPVOID	pParam
);
