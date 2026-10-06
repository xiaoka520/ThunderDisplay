#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdint.h>

typedef struct TDHIDConnection TDHIDConnection;
bool TDHIDAuthorized(void);
TDHIDConnection *TDHIDOpen(int32_t *status);
void TDHIDClose(TDHIDConnection *connection);
int32_t TDHIDPost(TDHIDConnection *connection, CGEventRef event);
int32_t TDHIDCheck(TDHIDConnection *connection);
bool TDPreLoginAppMarkerPresent(void);
int32_t TDSessionPostMouse(CGEventRef event, uint8_t *buttons);
