#pragma once

#include <windows.h>

// Shows the "Send Message" dialog. Returns the number of clients that
// received the message, or -1 if the dialog was cancelled.
int send_message_from_ui(HWND h_parent);
