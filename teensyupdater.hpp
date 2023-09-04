/* ********************************************************************************************
 * teensyupdater.hpp
 *
 * Author: Shawn Saenger
 *
 * Created: Aug 30, 2023
 *
 * Description: TeensyOtaUpdater Class
 *
 * ********************************************************************************************
 */

#ifndef _TEENSYUPDATER_HPP_
#define _TEENSYUPDATER_HPP_

#include <AsyncWebServer_Teensy41.hpp>
#include <FXUtil.h> // read_ascii_line(), hex file support
extern "C"{
    #include <FlashTxx.h> // TLC/T3x/T4x/TMM flash primitives
}

#if !(defined(CORE_TEENSY) && defined(__IMXRT1062__) && defined(ARDUINO_TEENSY41))
#error Only Teensy 4.1 supported
#endif

/* --------------------------------------------------------------------------------------------
 *  DEFINITIONS
 * --------------------------------------------------------------------------------------------
 */
/* --------------------------------------------------------------------------------------------
 * TOU_NO_UPDATE def
 *
 * Uncomment to test firmware upload without actually overwriting your current firmware.
 * If a firmware upload passes all the internal checks for validity, the Teensy will simply
 * reboot as oppose to writing to flash.
 *
 */
//#define TOU_NO_UPDATE 1

/* --------------------------------------------------------------------------------------------
 *  TYPES
 * --------------------------------------------------------------------------------------------
 */

/* --------------------------------------------------------------------------------------------
 * TOU_CB type
 *
 * A callback function type. See TeensyOtaUpdater::registerCallback() for more info.
 */
typedef std::function<void(void)> TOU_CB;

/* --------------------------------------------------------------------------------------------
 *  CLASSES
 * --------------------------------------------------------------------------------------------
 */
class TeensyOtaUpdater
{
public:

    TeensyOtaUpdater(AsyncWebServer *WebServ, const char *UrlPath);
    ~TeensyOtaUpdater();

    // Register a callback to be notified when a firmware is pending an update
    void registerCallback(TOU_CB Callback);

    // This func only has meaning if a callback was registered. Returns true if an update
    // is waiting to be applied. Call applyUpdate().
    bool isUpdateReady();

    // Applies the update with the firmware that was received earlier. Does not
    // return because the hardware will be rebooted.
    void applyUpdate();

private:

    // Server request callbacks
    void EndOta(AsyncWebServerRequest *request);
    void StartOta(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

    // Skips newline characters in the hex file
    unsigned stripNewLine(const char *data, unsigned int dataLen);

    // Helper functions for sending responses
    void DisplayUpdatePage(AsyncWebServerRequest *Request);
    void SendStatusPage(AsyncWebServerRequest *Request, const char *Message, const int Code = 400);
    void sendOtaResponse(AsyncWebServerRequest *UploadRequest, bool Success);

    // Web server instance
    AsyncWebServer *webServer;
    const char *urlPath;

    // Fields to manage parsing and saving data from a hex file that was uploaded
    char dataBuff[HEX_DATA_MAX_SIZE] __attribute__((aligned(8))); // buffer for hex data
    hex_info_t hexInfo;
    uint32_t buffer_addr, buffer_size;

    // Callback to upperlayer or 0
    TOU_CB callbackFunc;

    enum
    {
        Idle,
        SkipNewline,
        ParseLine,
        ProcessLine,
        CopyLine,
        Complete,
        Apply,
        Error
    } otaState;
};

#endif /* _TEENSYUPDATER_HPP_ */