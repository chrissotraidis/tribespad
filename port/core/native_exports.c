/* Immutable mapping of original 32-bit ABI tokens to precompiled LLVM fastcc
   wrappers. Function pointers are returned to LLVM, never called through C ABI. */
#include "guest_dinput.h"
extern void tbimp_DirectInputCreateA(void);
extern void tbimp_DI_QueryInterface(void),tbimp_DI_AddRef(void),tbimp_DI_Release(void);
extern void tbimp_DI_CreateDevice(void),tbimp_DI_EnumDevices(void),tbimp_DI_GetDeviceStatus(void);
extern void tbimp_DI_RunControlPanel(void),tbimp_DI_Initialize(void),tbimp_DI_FindDevice(void);
extern void tbimp_DID_QueryInterface(void);
extern void tbimp_DID_AddRef(void);
extern void tbimp_DID_Release(void);
extern void tbimp_DID_GetCapabilities(void);
extern void tbimp_DID_EnumObjects(void);
extern void tbimp_DID_GetProperty(void);
extern void tbimp_DID_SetProperty(void);
extern void tbimp_DID_Acquire(void);
extern void tbimp_DID_Unacquire(void);
extern void tbimp_DID_GetDeviceState(void);
extern void tbimp_DID_GetDeviceData(void);
extern void tbimp_DID_SetDataFormat(void);
extern void tbimp_DID_SetEventNotification(void);
extern void tbimp_DID_SetCooperativeLevel(void);
extern void tbimp_DID_GetObjectInfo(void);
extern void tbimp_DID_GetDeviceInfo(void);
extern void tbimp_DID_RunControlPanel(void);
extern void tbimp_DID_Initialize(void);
extern void tbimp_DID_CreateEffect(void);
extern void tbimp_DID_EnumEffects(void);
extern void tbimp_DID_GetEffectInfo(void);
extern void tbimp_DID_GetForceFeedbackState(void);
extern void tbimp_DID_SendForceFeedbackCommand(void);
extern void tbimp_DID_EnumCreatedEffectObjects(void);
extern void tbimp_DID_Escape(void);
extern void tbimp_DID_Poll(void);
extern void tbimp_DID_SendDeviceData(void);
/* Standalone input tests need no system providers. The full runtime links
 * the strong finite system-provider table. No arbitrary host pointer registry. */
__attribute__((weak,noinline)) void *tribes_system_native_lookup(uint32_t address) {(void)address;return 0;}
void *tribes_native_lookup(uint32_t address) {
    switch(address) {
    case TB_DI_CREATE:return (void *)tbimp_DirectInputCreateA;
    case TB_DI_METHOD(0):return (void *)tbimp_DI_QueryInterface;
    case TB_DI_METHOD(1):return (void *)tbimp_DI_AddRef;
    case TB_DI_METHOD(2):return (void *)tbimp_DI_Release;
    case TB_DI_METHOD(3):return (void *)tbimp_DI_CreateDevice;
    case TB_DI_METHOD(4):return (void *)tbimp_DI_EnumDevices;
    case TB_DI_METHOD(5):return (void *)tbimp_DI_GetDeviceStatus;
    case TB_DI_METHOD(6):return (void *)tbimp_DI_RunControlPanel;
    case TB_DI_METHOD(7):return (void *)tbimp_DI_Initialize;
    case TB_DI_METHOD(8):return (void *)tbimp_DI_FindDevice;
    case TB_DIDEV_METHOD(0):return (void *)tbimp_DID_QueryInterface;
    case TB_DIDEV_METHOD(1):return (void *)tbimp_DID_AddRef;
    case TB_DIDEV_METHOD(2):return (void *)tbimp_DID_Release;
    case TB_DIDEV_METHOD(3):return (void *)tbimp_DID_GetCapabilities;
    case TB_DIDEV_METHOD(4):return (void *)tbimp_DID_EnumObjects;
    case TB_DIDEV_METHOD(5):return (void *)tbimp_DID_GetProperty;
    case TB_DIDEV_METHOD(6):return (void *)tbimp_DID_SetProperty;
    case TB_DIDEV_METHOD(7):return (void *)tbimp_DID_Acquire;
    case TB_DIDEV_METHOD(8):return (void *)tbimp_DID_Unacquire;
    case TB_DIDEV_METHOD(9):return (void *)tbimp_DID_GetDeviceState;
    case TB_DIDEV_METHOD(10):return (void *)tbimp_DID_GetDeviceData;
    case TB_DIDEV_METHOD(11):return (void *)tbimp_DID_SetDataFormat;
    case TB_DIDEV_METHOD(12):return (void *)tbimp_DID_SetEventNotification;
    case TB_DIDEV_METHOD(13):return (void *)tbimp_DID_SetCooperativeLevel;
    case TB_DIDEV_METHOD(14):return (void *)tbimp_DID_GetObjectInfo;
    case TB_DIDEV_METHOD(15):return (void *)tbimp_DID_GetDeviceInfo;
    case TB_DIDEV_METHOD(16):return (void *)tbimp_DID_RunControlPanel;
    case TB_DIDEV_METHOD(17):return (void *)tbimp_DID_Initialize;
    case TB_DIDEV_METHOD(18):return (void *)tbimp_DID_CreateEffect;
    case TB_DIDEV_METHOD(19):return (void *)tbimp_DID_EnumEffects;
    case TB_DIDEV_METHOD(20):return (void *)tbimp_DID_GetEffectInfo;
    case TB_DIDEV_METHOD(21):return (void *)tbimp_DID_GetForceFeedbackState;
    case TB_DIDEV_METHOD(22):return (void *)tbimp_DID_SendForceFeedbackCommand;
    case TB_DIDEV_METHOD(23):return (void *)tbimp_DID_EnumCreatedEffectObjects;
    case TB_DIDEV_METHOD(24):return (void *)tbimp_DID_Escape;
    case TB_DIDEV_METHOD(25):return (void *)tbimp_DID_Poll;
    case TB_DIDEV_METHOD(26):return (void *)tbimp_DID_SendDeviceData;
    default:return tribes_system_native_lookup(address);
    }
}
