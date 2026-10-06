#include "HIDBridge.h"
#include <Carbon/Carbon.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>

// A graphical pre-login executable must identify its Quartz connection.
// Chromium and RustDesk use this Mach-O section for login-screen input.
// This is not an entitlement; ordinary event-post authorization still applies.
__attribute__((used, section("__CGPreLoginApp,__cgpreloginapp")))
static const char TDPreLoginAppMarker[] = "";

bool TDPreLoginAppMarkerPresent(void) {
    unsigned long size = 0;
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    return header && getsectiondata(header, "__CGPreLoginApp", "__cgpreloginapp", &size) && size > 0;
}

int32_t TDSessionPostMouse(CGEventRef event, uint8_t *buttons) {
    if (!event || !buttons) return kCGErrorIllegalArgument;
    CGEventType type = CGEventGetType(event);
    bool down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown;
    bool up = type == kCGEventLeftMouseUp || type == kCGEventRightMouseUp || type == kCGEventOtherMouseUp;
    bool motion = type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
        type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged;
    if (!down && !up && !motion) return kCGErrorIllegalArgument;
    CGPoint point = CGEventGetLocation(event);
    if (!isfinite(point.x) || !isfinite(point.y)) return kCGErrorIllegalArgument;
    uint8_t next = *buttons;
    if (down || up) {
        int64_t button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        if (button < 0 || button > 2) return kCGErrorIllegalArgument;
        if (down) next |= (uint8_t)(1u << button); else next &= (uint8_t)~(1u << button);
    }
    // This public API generates movement, dragging and click transitions in
    // the current graphical session and returns an actual posting error.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGError result = CGPostMouseEvent(point, true, 3, (next & 1) != 0, (next & 2) != 0, (next & 4) != 0);
#pragma clang diagnostic pop
    if (result == kCGErrorSuccess) *buttons = next;
    return result;
}

struct TDHIDConnection {
    io_connect_t handle;
    UInt32 deadKey;
    UInt16 characters[128], originals[128];
    int16_t mouseSequence, mouseDown[3];
};

bool TDHIDAuthorized(void) {
    return IOHIDCheckAccess(kIOHIDRequestTypePostEvent) == kIOHIDAccessTypeGranted;
}

TDHIDConnection *TDHIDOpen(int32_t *status) {
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOHIDSystem"));
    if (!service) { *status = kIOReturnNoDevice; return NULL; }
    TDHIDConnection *connection = calloc(1, sizeof(*connection));
    if (!connection) { IOObjectRelease(service); *status = kIOReturnNoMemory; return NULL; }
    *status = IOServiceOpen(service, mach_task_self(), kIOHIDParamConnectType, &connection->handle);
    IOObjectRelease(service);
    if (*status != kIOReturnSuccess) { free(connection); return NULL; }
    return connection;
}

void TDHIDClose(TDHIDConnection *connection) {
    if (!connection) return;
    IOServiceClose(connection->handle);
    // Character/dead-key state is private to this controller, never persisted.
    memset(connection, 0, sizeof(*connection));
    free(connection);
}

static UInt16 translate(TDHIDConnection *connection, UInt16 key, UInt32 modifiers,
                        UInt32 keyboardType, bool original, bool repeat) {
    TISInputSourceRef input = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef data = input ? TISGetInputSourceProperty(input, kTISPropertyUnicodeKeyLayoutData) : NULL;
    if (!data) {
        if (input) CFRelease(input);
        input = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
        data = input ? TISGetInputSourceProperty(input, kTISPropertyUnicodeKeyLayoutData) : NULL;
    }
    UInt16 character = 0;
    if (data && CFDataGetLength(data) >= (CFIndex)sizeof(UCKeyboardLayout)) {
        UInt32 deadKey = original ? 0 : connection->deadKey;
        UniChar characters[4] = {0}; UniCharCount count = 0;
        UInt32 carbon = ((modifiers & NX_COMMANDMASK) ? cmdKey : 0) |
            ((modifiers & NX_SHIFTMASK) ? shiftKey : 0) |
            ((modifiers & NX_ALPHASHIFTMASK) ? alphaLock : 0) |
            ((modifiers & NX_ALTERNATEMASK) ? optionKey : 0) |
            ((modifiers & NX_CONTROLMASK) ? controlKey : 0);
        OSStatus result = UCKeyTranslate((const UCKeyboardLayout *)CFDataGetBytePtr(data), key,
            repeat ? kUCKeyActionAutoKey : kUCKeyActionDown, original ? 0 : (carbon >> 8),
            keyboardType, original ? kUCKeyTranslateNoDeadKeysMask : 0, &deadKey, 4, &count, characters);
        if (!original) connection->deadKey = deadKey;
        if (result == noErr && count) character = characters[0];
    }
    if (input) CFRelease(input);
    return character;
}

// This public, deprecated API is retained for the explicit Aqua diagnostic.
// Production pre-login input uses the marked Quartz session above.
// The diagnostic returns status and enforces macOS HID authorization.
// User-session input continues using CGEvent; no raw user-client selectors or
// entitlement/TCC bypasses are used.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static int32_t post(TDHIDConnection *connection, UInt32 type, IOGPoint location,
                    const NXEventData *data, UInt32 flags, UInt32 options) {
    if (!connection) return kIOReturnBadArgument;
    return IOHIDPostEvent(connection->handle, type, location, data, kNXEventDataVersion, flags, options);
}
#pragma clang diagnostic pop

int32_t TDHIDCheck(TDHIDConnection *connection) {
    NXEventData data = {0};
    // Complete the same public authorization request that IOHIDPostEvent makes
    // for real input, before accepting a client. A null event leaves the cursor,
    // modifiers and text untouched; actual input still checks every return code.
    // Never query Quartz global input state in the LoginWindow preparation path.
    if (!IOHIDRequestAccess(kIOHIDRequestTypePostEvent)) return kIOReturnNotPermitted;
    return post(connection, NX_NULLEVENT, (IOGPoint){0, 0}, &data, 0, 0);
}

int32_t TDHIDPost(TDHIDConnection *connection, CGEventRef event) {
    if (!connection || !event) return kIOReturnBadArgument;
    NXEventData data = {0};
    UInt32 type = (UInt32)CGEventGetType(event), flags = (UInt32)CGEventGetFlags(event);
    UInt32 options = kIOHIDSetGlobalEventFlags;
    IOGPoint location = {0, 0};
    bool mouse = type == NX_LMOUSEDOWN || type == NX_LMOUSEUP || type == NX_RMOUSEDOWN || type == NX_RMOUSEUP ||
        type == NX_OMOUSEDOWN || type == NX_OMOUSEUP || type == NX_MOUSEMOVED ||
        type == NX_LMOUSEDRAGGED || type == NX_RMOUSEDRAGGED || type == NX_OMOUSEDRAGGED;
    if (mouse) {
        // Send the absolute desktop position through the same authorized HID
        // connection. CGWarpMouseCursorPosition opens another Quartz input path,
        // which must not be required for a LoginWindow mouse event.
        CGPoint point = CGEventGetLocation(event);
        if (!isfinite(point.x) || !isfinite(point.y) || point.x < INT16_MIN || point.x > INT16_MAX ||
            point.y < INT16_MIN || point.y > INT16_MAX) return kIOReturnBadArgument;
        location.x = (SInt16)lround(point.x); location.y = (SInt16)lround(point.y);
        options |= kIOHIDSetCursorPosition;
    }
    switch (type) {
        case NX_KEYDOWN: case NX_KEYUP: case NX_FLAGSCHANGED: {
            UInt16 key = (UInt16)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
            if (key >= 128) return kIOReturnBadArgument;
            data.key.keyCode = key;
            data.key.keyboardType = LMGetKbdType();
            data.key.repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
            if (type == NX_KEYDOWN) {
                connection->characters[key] = translate(connection, key, flags, data.key.keyboardType, false, data.key.repeat);
                connection->originals[key] = translate(connection, key, 0, data.key.keyboardType, true, false);
            }
            data.key.charSet = data.key.origCharSet = NX_ASCIISET;
            data.key.charCode = type == NX_FLAGSCHANGED ? 0 : connection->characters[key];
            data.key.origCharCode = type == NX_FLAGSCHANGED ? 0 : connection->originals[key];
            // Also deliver the keyboard flags and physical key through the HID
            // manager, including secure-input consumers at the login window.
            options |= kIOHIDPostHIDManagerEvent;
            break;
        }
        case NX_LMOUSEDOWN: case NX_LMOUSEUP: case NX_RMOUSEDOWN: case NX_RMOUSEUP:
        case NX_OMOUSEDOWN: case NX_OMOUSEUP: {
            UInt32 button = (UInt32)CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (button >= 3) return kIOReturnBadArgument;
            bool down = type == NX_LMOUSEDOWN || type == NX_RMOUSEDOWN || type == NX_OMOUSEDOWN;
            if (down) {
                connection->mouseSequence = connection->mouseSequence == INT16_MAX ? 1 : connection->mouseSequence + 1;
                connection->mouseDown[button] = connection->mouseSequence;
            }
            data.mouse.eventNum = connection->mouseDown[button];
            data.mouse.buttonNumber = (UInt8)button;
            data.mouse.click = (SInt32)CGEventGetIntegerValueField(event, kCGMouseEventClickState);
            data.mouse.pressure = down ? 255 : 0;
            break;
        }
        case NX_MOUSEMOVED: case NX_LMOUSEDRAGGED: case NX_RMOUSEDRAGGED: case NX_OMOUSEDRAGGED:
            break;
        case NX_SCROLLWHEELMOVED:
            data.scrollWheel.deltaAxis1 = (SInt16)CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
            data.scrollWheel.deltaAxis2 = (SInt16)CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
            data.scrollWheel.fixedDeltaAxis1 = (SInt32)CGEventGetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1);
            data.scrollWheel.fixedDeltaAxis2 = (SInt32)CGEventGetIntegerValueField(event, kCGScrollWheelEventFixedPtDeltaAxis2);
            data.scrollWheel.pointDeltaAxis1 = (SInt32)CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1);
            data.scrollWheel.pointDeltaAxis2 = (SInt32)CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2);
            break;
        default: return kIOReturnUnsupported;
    }
    return post(connection, type, location, &data, flags, options);
}
