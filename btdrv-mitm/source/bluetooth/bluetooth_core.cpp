#include "bluetooth_core.hpp"

#include <atomic>
#include <mutex>
#include <cstring>
#include "../btdrv_mitm_flags.hpp"
#include "../controllermanager.hpp"

#include "../btdrv_mitm_logging.hpp"

namespace ams::bluetooth::core {

    namespace {

        std::atomic<bool> g_isInitialized(false);

        os::ThreadType g_eventHandlerThread;
        alignas(os::ThreadStackAlignment) u8 g_eventHandlerThreadStack[0x2000];

        os::Mutex g_eventDataLock(false);
        u8 g_eventDataBuffer[0x400];
        BluetoothEventType g_currentEventType;

        os::SystemEventType g_btSystemEvent;
        os::SystemEventType g_btSystemEventFwd;
        os::SystemEventType g_btSystemEventUser;

        void EventThreadFunc(void *arg) {
            while (true) {
                os::WaitSystemEvent(&g_btSystemEvent);
                HandleEvent();
            }
        }

        void _LogEvent(BluetoothEventType type, BluetoothEventData *eventData) {
        
            size_t dataSize;
            switch (type) {
                case BluetoothEvent_DeviceFound:
                    dataSize = sizeof(eventData->deviceFound);
                    break;
                case BluetoothEvent_DiscoveryStateChanged:
                    dataSize = sizeof(eventData->discoveryState);
                    break;
                case BluetoothEvent_PinRequest:
                    dataSize = sizeof(eventData->pinReply);
                    break;
                case BluetoothEvent_SspRequest:
                    dataSize = sizeof(eventData->sspReply);
                    break;
                case BluetoothEvent_BondStateChanged:
                    if (hos::GetVersion() < hos::Version_9_0_0)
                        dataSize = sizeof(eventData->bondState);
                    else
                        dataSize = sizeof(eventData->bondState.v2);                  
                    break;
                default:
                    dataSize = sizeof(g_eventDataBuffer);
                    break;
            }

            BTDRV_LOG_DATA(eventData, dataSize);
        }

    }

    bool IsInitialized(void) {
        return g_isInitialized;
    }

    os::SystemEventType *GetSystemEvent(void) {
        return &g_btSystemEvent;
    }

    os::SystemEventType *GetForwardEvent(void) {
        return &g_btSystemEventFwd;
    }

    os::SystemEventType *GetUserForwardEvent(void) {
        return &g_btSystemEventUser;
    }

    Result Initialize(Handle eventHandle) {
        os::AttachReadableHandleToSystemEvent(&g_btSystemEvent, eventHandle, false, os::EventClearMode_AutoClear);

        R_TRY(os::CreateSystemEvent(&g_btSystemEventFwd, os::EventClearMode_AutoClear, true));
        R_TRY(os::CreateSystemEvent(&g_btSystemEventUser, os::EventClearMode_AutoClear, true));

        R_TRY(os::CreateThread(&g_eventHandlerThread, 
            EventThreadFunc, 
            nullptr, 
            g_eventHandlerThreadStack, 
            sizeof(g_eventHandlerThreadStack), 
            9
        ));

        os::StartThread(&g_eventHandlerThread); 

        g_isInitialized = true;

        return ams::ResultSuccess();
    }

    void Finalize(void) {
        os::DestroyThread(&g_eventHandlerThread);

        os::DestroySystemEvent(&g_btSystemEventUser);
        os::DestroySystemEvent(&g_btSystemEventFwd);

        g_isInitialized = false;
    }

    void handleDeviceFoundEvent(BluetoothEventData *eventData) {
        if (ams::mitm::btdrv::IsController(&eventData->deviceFound.cod) && !ams::mitm::btdrv::IsValidSwitchControllerName(eventData->deviceFound.name)) {
            std::strncpy(eventData->deviceFound.name, "Lic Pro Controller", sizeof(BluetoothName) - 1);
            eventData->pinReply.cod = {0x00, 0x25, 0x08};
        }
    }

    void handlePinRequesEvent(BluetoothEventData *eventData) {
        if (ams::mitm::btdrv::IsController(&eventData->pinReply.cod) && !ams::mitm::btdrv::IsValidSwitchControllerName(eventData->pinReply.name)) {
            std::strncpy(eventData->pinReply.name, "Lic Pro Controller", sizeof(BluetoothName) - 1);
            eventData->pinReply.cod = {0x00, 0x25, 0x08};
        }
    }

    void handleSspRequesEvent(BluetoothEventData *eventData) {
        if (ams::mitm::btdrv::IsController(&eventData->sspReply.cod) && !ams::mitm::btdrv::IsValidSwitchControllerName(eventData->sspReply.name)) {
            std::strncpy(eventData->sspReply.name, "Lic Pro Controller", sizeof(BluetoothName) - 1);
            eventData->pinReply.cod = {0x00, 0x25, 0x08};
        }
    }

    Result GetEventInfo(ncm::ProgramId program_id, BluetoothEventType *type, u8* buffer, size_t size) {
        std::scoped_lock lk(g_eventDataLock);
        {
            *type = g_currentEventType;
            std::memcpy(buffer, g_eventDataBuffer, size);

            BluetoothEventData *eventData = reinterpret_cast<BluetoothEventData *>(buffer);

            if (program_id == ncm::SystemProgramId::Btm) {
                
                switch (g_currentEventType) {
                    case BluetoothEvent_DeviceFound:
                        handleDeviceFoundEvent(eventData);
                        break;
                    case BluetoothEvent_DiscoveryStateChanged:
                        break;
                    case BluetoothEvent_PinRequest:
                        handlePinRequesEvent(eventData);
                        break;
                    case BluetoothEvent_SspRequest:
                        handleSspRequesEvent(eventData);
                        break;
                    case BluetoothEvent_BondStateChanged:
                        break;
                    default:
                        break;
                }
            }

            _LogEvent(g_currentEventType, eventData);

        }
        
        return ams::ResultSuccess();
    }

    void HandleEvent(void) {

        std::scoped_lock lk(g_eventDataLock);
        
        R_ABORT_UNLESS(btdrvGetEventInfo(&g_currentEventType, g_eventDataBuffer, sizeof(g_eventDataBuffer)));

        BTDRV_LOG_FMT("[%02d] Bluetooth Core Event", g_currentEventType);
        //BTDRV_LOG_DATA(g_eventDataBuffer, sizeof(g_eventDataBuffer));

        //BluetoothEventData *eventData = reinterpret_cast<BluetoothEventData *>(g_eventDataBuffer);

        //_LogEvent(g_currentEventType, eventData);

        /*
        if (g_currentEventType == BluetoothEvent_DeviceFound) {

            if (ams::mitm::btdrv::IsController(&eventData->deviceFound.cod)) {
                if (std::strncmp(eventData->deviceFound.name, "Nintendo RVL-CNT-01",    sizeof(BluetoothName)) == 0 ||
                    std::strncmp(eventData->deviceFound.name, "Nintendo RVL-CNT-01-UC", sizeof(BluetoothName)) == 0) 
                {
                    BTDRV_LOG_FMT("!!!!! Calling CreateBond");
                    btdrvCreateBond(&eventData->deviceFound.address, BluetoothTransport_Auto);
                    return;
                }
            }
        }
        */

        /*
        if (g_currentEventType == BluetoothEvent_PinRequest) {
            BTDRV_LOG_FMT("!!!!! Calling PinReply");
            BluetoothPinCode pincode = {};
            btdrvRespondToPinRequest(&eventData->pinReply.address, false, &pincode, sizeof(BluetoothAddress));
            return;
        }
        */

        // Signal our forwarder events
        if (!g_redirectEvents || g_preparingForSleep)
            os::SignalSystemEvent(&g_btSystemEventFwd);
        else
            os::SignalSystemEvent(&g_btSystemEventUser);
    }

}