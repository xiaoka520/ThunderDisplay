#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdint.h>

typedef struct TDHIDConnection TDHIDConnection;
bool TDHIDAuthorized(void);
TDHIDConnection *TDHIDOpen(int32_t *status);
void TDHIDClose(TDHIDConnection *connection);
int32_t TDHIDPost(TDHIDConnection *connection, CGEventRef event);
int32_t TDHIDCheck(TDHIDConnection *connection);
int32_t TDLoginWindowKeyboardTarget(void);
bool TDIsLoginWindowKeyboardTarget(int32_t pid);
