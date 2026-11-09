#pragma once

#define AFPWM_NOTIFYICON			(WM_APP+100)
#define AFPWM_INDEXING_COMPLETE		(WM_APP+101)
// Sent (SendMessageTimeoutW) by a duplicate launch to the primary instance's
// main window to request that it show and foreground its configuration
// window. Both parameters are always zero: never carries process-local data.
#define AFPWM_SHOW_WINDOW			(WM_APP+200)
