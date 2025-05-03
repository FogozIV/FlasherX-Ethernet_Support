/* ********************************************************************************************
 * TeensyOtaUpdater.cpp
 *
 * Author: Shawn Saenger
 *
 * Created: Aug 30, 2023
 *
 * Description: TeensyOtaUpdater implementation. Handles onupload events from the web server.
 *              Bits of this code was adapted from Olivier Merlet on
 *              https://forum.pjrc.com/threads/72233-OTA-through-Ethernet-with-Teensy-4-1
 *              While unlikely, writing to flash with this program can brick your device.
 *              There are safety measures to prevent that, and even if something did go wrong,
 *              you should be able to fix the Teensy 4.1 by holding the reset button for 15
 *              seconds.
 *
 *              If you are getting a multiple definitions warning with Teensy41_AsyncTCP, then
 *              place "#include <AsyncWebServer_Teensy41.h>" in the .ino file. Other files
 *              should not include this but instead include "<AsyncWebServer_Teensy41.hpp>"
 *
 *              I, Shawn Saenger, give no warranty, expressed or implied for this software and/or
 *              documentation provided, including, without limitation, warranty of
 *              merchantability and fitness for a particular purpose.
 *
 * Requires: A fix to the AsyncWebServer_Teensy41 available at
 *           https://github.com/ssaenger/AsyncWebServer_Teensy41. Bug was found by
 *           Olivier (Shuptuu)
 *           https://forum.pjrc.com/threads/72220-AsyncWebServer_Teensy41-bug-onUpload
 *
 * ********************************************************************************************
 */

#include <teensyUpdater.hpp>

#define HTTP_MAX_MESSAGE_RESP 512

/* --------------------------------------------------------------------------------------------
 *                 TeensyOtaUpdater()
 * --------------------------------------------------------------------------------------------
 * Description:    TeensyOtaUpdater contructor
 *
 * Parameters:     WebServer - An initialized webserver instance
 *                 UrlPath - URL path to be handled by this class
 *
 * Returns:        void
 */
TeensyOtaUpdater::TeensyOtaUpdater(std::shared_ptr<AsyncWebServer> WebServer, const char *UrlPath) :
                                                        webServer(WebServer), urlPath(UrlPath)
{
    otaState      = Idle;
    callbackFunc = 0;

    webServer->on(urlPath, HTTP_GET, [&](AsyncWebServerRequest *request)
        {
            Serial.println("TeensyOtaUpdater::onGet");
            this->DisplayUpdatePage(request);
        });

    webServer->on(
        urlPath, HTTP_POST, [&](AsyncWebServerRequest *request)
        {
            Serial.println("TeensyOtaUpdater::onUpload");
            this->EndOta(request);
        },
        [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
        {
            Serial.println("TeensyOtaUpdater::onUpload");
            Serial.printf("FILE[%s]: %s, size: %u\n", filename.c_str(), data, len);
            Serial.printf("index: %u, final: %u\n", index, final);
            this->StartOta(request, filename, index, data, len, final);
        });

    Serial.println("TeensyOtaUpdater initialized");
}

/* --------------------------------------------------------------------------------------------
 *                 ~TeensyOtaUpdater()
 * --------------------------------------------------------------------------------------------
 * Description:    TeensyOtaUpdater decontructor
 *
 * Parameters:     void
 *
 * Returns:        void
 */
TeensyOtaUpdater::~TeensyOtaUpdater()
{
    Serial.println("TeensyOtaUpdater deinitialized");
    if (buffer_addr) {
        firmware_buffer_free(buffer_addr, buffer_size);
    }
}

/* --------------------------------------------------------------------------------------------
 *                 registerCallback()
 * --------------------------------------------------------------------------------------------
 * Description:    Registers a callback to alert upper layer that an update is pending. The
 *                 caller must call applyUpdate() to apply the update and reboot.
 *                 If no callback is registered, the update will automatically be applied and
 *                 a reboot will occur. Set to 0 to unset the callback func.
 *
 * Parameters:     Callback - a pointer to the TOA_Updater callback function. Set to 0 to
 *                            unregister a callback func.
 *
 * Returns:        void
 */
void TeensyOtaUpdater::registerCallback(TOU_CB Callback)
{
    callbackFunc = Callback;
}

/* --------------------------------------------------------------------------------------------
 *                 isUpdateReady()
 * --------------------------------------------------------------------------------------------
 * Description:    Checks if the updater is ready to apply an update. If so, applyUpdate()
 *                 should be called. applyUpdate() will already be called if a callback
 *                 func was never registered with registerCallback().
 *
 * Parameters:     void
 *
 * Returns:        true if an update is pending. Call applyUpdate(). false otherwise.
 */
bool TeensyOtaUpdater::isUpdateReady()
{
    return (otaState == Apply);
}

/* --------------------------------------------------------------------------------------------
 *                 applyUpdate()
 * --------------------------------------------------------------------------------------------
 * Description:    Applies an update from the firmware the client uploaded. Must be called
 *                 if a handler was registered and it received a callback.
 *                 The Teensy board will reboot.
 *
 * Parameters:     void
 *
 * Returns:        void. Note: This function does not return. Causes a reboot.
 */
void TeensyOtaUpdater::applyUpdate()
{
    if (otaState == Apply) {
#ifdef TOU_NO_UPDATE
        Serial.println("Testing only. No actual firmware update. Rebooting...");
#else
        Serial.println("Calling flash_move() to load new firmware and reboot...");
        flash_move(FLASH_BASE_ADDR, buffer_addr, hexInfo.max - hexInfo.min);
#endif /* TOU_NO_UPDATE */
    } else {
        Serial.printf("No update available!! otaState: %d. Rebooting...\r\n", otaState);
    }

    Serial.flush();
    REBOOT;
    for (;;) {
    }
}

/* --------------------------------------------------------------------------------------------
 *                 EndOta()
 * --------------------------------------------------------------------------------------------
 * Description:    Verifies the final firmware upload. Sends a response to client of upload
 *                 status
 *
 * Parameters:     request - The server request
 *
 * Returns:        void
 */
void TeensyOtaUpdater::EndOta(AsyncWebServerRequest *request)
{
    AsyncWebParameter *p = request->getParam(0);
    Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
    if (!buffer_addr) {
        otaState = Error;
    }

    if (otaState == Complete) {
        Serial.printf("\nhex file: %1d lines %1lu bytes (%08lX - %08lX)\n", hexInfo.lines, hexInfo.max - hexInfo.min, hexInfo.min, hexInfo.max);
#if defined(KINETISK) || defined(KINETISL)
        // check FSEC value in new code -- abort if incorrect
        uint32_t value = *(uint32_t *)(0x40C + buffer_addr);
        if (value != 0xfffff9de) {
            out->printf("new code contains correct FSEC value %08lX\n", value);
        } else {
            out->printf("abort - FSEC value %08lX should be FFFFF9DE\n", value);
            otaState = Error;
        }
#endif
    }

    if (otaState == Complete) {
        // check FLASH_ID in new code - abort if not found
        if (!check_flash_id(buffer_addr, hexInfo.max - hexInfo.min)) {
            Serial.printf("Abort - firmware missing string %s\n", FLASH_ID);
            otaState = Error;
        }
    }

    if (otaState == Complete) {
        otaState = Apply;
    } else {
        otaState = Idle;
        if (buffer_addr) {
            Serial.printf("Erase FLASH buffer / free RAM buffer...\n");
            Serial.flush();
            firmware_buffer_free(buffer_addr, buffer_size);
            buffer_addr = 0;
        }
    }

    sendOtaResponse(request, (otaState == Apply));

    if (otaState == Apply) {
        if (callbackFunc) {
            callbackFunc();
        } else {
            // No callback func registered. Need to apply the update now.
            delay(500);
            applyUpdate();
        }
    }
}

/* --------------------------------------------------------------------------------------------
 *                 StartOta()
 * --------------------------------------------------------------------------------------------
 * Description:    Parses the firmware file and checks for validity along the way. Firmware
 *                 is moved into unused flash memory. Later it will be moved into the right
 *                 place in flash, overwritting the existing code.
 * 
 *                 This function can be called multiple times with fragmented data.
 *
 * Parameters:     request - The server request
 *                 filename - Name of the file
 *                 index - The total length of data received so far
 *                 data - The file's data
 *                 len - Length of the data
 *                 final - Final frame of data
 *
 * Returns:        void
 */
void TeensyOtaUpdater::StartOta(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    int          lineLength;
    int          errorVal;
    unsigned int off = 0;
    char *abortMsg = (char*)"";

    // Serial.printf("OTA: len, %d, index: %d\r\n", len, index);

    while (off < len) {
        switch (otaState) {
        case Idle:
            if (index == 0) {
                // Begining of new data
                hexInfo.data        = dataBuff;
                hexInfo.addr        = 0;
                hexInfo.code        = 0;
                hexInfo.num         = 0;
                hexInfo.base        = 0;
                hexInfo.min         = 0xFFFFFFFF;
                hexInfo.max         = 0;
                hexInfo.eof         = 0;
                hexInfo.lines       = 0;
                hexInfo.prevDataLen = 0;
                memset((void *)hexInfo.prevData, 0, HEX_LINE_MAX_SIZE);

                otaState    = SkipNewline;

                Serial.println("Starting OTA...");
                Serial.printf("Starting OTA update with file %s\r\n", filename.c_str());
                if (firmware_buffer_init(&buffer_addr, &buffer_size) != 0) {
                    Serial.printf("Created buffer = %1luK %s (%08lX - %08lX)\n",
                                buffer_size / 1024, IN_FLASH(buffer_addr) ? "FLASH" : "RAM",
                                buffer_addr, buffer_addr + buffer_size);
                } else {
                    abortMsg = (char*)"Unable to create buffer";
                    Serial.printf("%s\r\n", abortMsg);
                    otaState = Error;
                }
            } else {
                // Ignore lingering data
                goto done;
            }
            break;

        case SkipNewline:
            off += stripNewLine((const char *)&data[off], len - off);
            otaState = ParseLine;
            break;

        case ParseLine:
            lineLength = parse_hex_line((const char *)&data[off], len - off, &hexInfo);
            if (lineLength > 0) {
                off += lineLength;
                otaState = ProcessLine;
            } else if ((lineLength == 0) || final) {
                abortMsg = (char *)"Abort - bad hex line";
                Serial.printf("%s #: %d\r\n", abortMsg, hexInfo.lines);
                otaState = Error;
            } else {
                // The line was too short and between frames. More will come and be added to the previous
                // data.
                goto done;
            }
            break;

        case ProcessLine:
            if (process_hex_record(&hexInfo) == 0) {
                // Good hex code
                otaState = CopyLine;
            } else {
                // Error with hex code
                abortMsg = (char *)"Abort - invalid hex code";
                Serial.printf("%s: %d\r\n", abortMsg, hexInfo.code);
                otaState = Error;
            }
            break;

        case CopyLine:
            otaState = SkipNewline;
            if (hexInfo.code == IRT_DATA) {
                uint32_t addr = buffer_addr + hexInfo.base + hexInfo.addr - FLASH_BASE_ADDR;
                if (hexInfo.max > (FLASH_BASE_ADDR + buffer_size)) {
                    abortMsg = (char *)"Abort - max address too large";
                    Serial.printf("%s: 0x%08lX\r\n", abortMsg, hexInfo.max);
                    otaState = Error;
                } else if (!IN_FLASH(buffer_addr)) {
                    memcpy((void *)addr, (void *)hexInfo.data, hexInfo.num);
                } else if (IN_FLASH(buffer_addr)) {
                    errorVal = flash_write_block(addr, hexInfo.data, hexInfo.num);
                    if (errorVal) {
                        abortMsg = (char *)"Abort - error in flash_write_block()";
                        Serial.printf("%s: 0x%02X\r\n", abortMsg, errorVal);
                        otaState = Error;
                    }
                }
            }
            break;

        case Apply:
            abortMsg = (char *)"Abort - Applying previous firmware";
            Serial.printf("%s otaState: %d\r\n", abortMsg, otaState);
            SendStatusPage(request, abortMsg);
            return;

        case Error:
            SendStatusPage(request, abortMsg);
            goto done;

        default:
            /* Shouldn't have gone in here */
            abortMsg = (char *)"Abort - internal error";
            Serial.printf("%s otaState: %d\r\n", abortMsg, otaState);
            otaState = Error;
            break;
        }
    }

done:
    if (otaState == Error) {
        // Go back to idle state and wait for new data
        if (buffer_addr) {
            Serial.printf("Erase FLASH buffer / free RAM buffer...\n");
            firmware_buffer_free(buffer_addr, buffer_size);
            buffer_addr = 0;
        }
        otaState = Idle;
    } else if (final) {
        // If the final flag is set then this was the last frame of data
        Serial.println("Transfer finished");
        otaState = Complete;
    } else if (otaState != Idle) {
        /* Start back in the skipnewline state for when more data arrives */
        otaState = SkipNewline;
    }
}

/* --------------------------------------------------------------------------------------------
 *                 stripNewLine()
 * --------------------------------------------------------------------------------------------
 * Description:    Skips any newline characters like \r and \n
 *
 * Parameters:     The hex data
 *
 * Returns:        void
 */
unsigned TeensyOtaUpdater::stripNewLine(const char *data, unsigned int dataLen)
{
    unsigned int off = 0;
    if ((dataLen > 0) && (data[off] == '\r' || data[off] == '\n')) {
        off++;
        if (dataLen > 1) {
            if ((dataLen > 1) && (data [off] == '\r' || data[off] == '\n')) {
                off++;
            }
        }
    }
    return off;
}

/* --------------------------------------------------------------------------------------------
 *                 DisplayUpdatePage()
 * --------------------------------------------------------------------------------------------
 * Description:    Display the main upload page
 *
 * Parameters:     Request - A client request
 *                 UrlPath - Status message
 *
 * Returns:        void
 */
void TeensyOtaUpdater::DisplayUpdatePage(AsyncWebServerRequest *Request)
{
    char pageOut[HTTP_MAX_MESSAGE_RESP];
    unsigned int len;

    len = snprintf(pageOut, HTTP_MAX_MESSAGE_RESP, "<body><h1>Ethernet OTA update for Teensy4.1</h1>");
    len += snprintf(pageOut + len, HTTP_MAX_MESSAGE_RESP - len, "<br><h2>Select and send your .hex firmware file:</h2><br>");
    len += snprintf(pageOut + len, HTTP_MAX_MESSAGE_RESP - len, "<div><form method='POST' enctype='multipart/form-data' action='%s'>", urlPath);
    len += snprintf(pageOut + len, HTTP_MAX_MESSAGE_RESP - len, "<input type='file' name='file'><button type='submit'>Send</button></form></div></body>");

    Request->send(200, "text/html", pageOut);
}

/* --------------------------------------------------------------------------------------------
 *                 SendStatusPage()
 * --------------------------------------------------------------------------------------------
 * Description:    Sends a basic status response message
 *
 * Parameters:     Request - A client request
 *                 Message - Status message
 *                 Code = 200 - HTTP response status codes
 *                              https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 *
 * Returns:        void
 */
void TeensyOtaUpdater::SendStatusPage(AsyncWebServerRequest *Request, const char *Message, const int Code)
{
    char pageOut[HTTP_MAX_MESSAGE_RESP];
    unsigned int len;

    len = snprintf(pageOut, HTTP_MAX_MESSAGE_RESP, "<html><head><meta http-equiv=\"refresh\" content=\"10\"></head>");
    len += snprintf(pageOut + len, HTTP_MAX_MESSAGE_RESP - len, "<body><h1>%s</h1></body></html>", Message);

    Request->send(Code, "text/html", pageOut);
}

/* --------------------------------------------------------------------------------------------
 *                 sendOtaResponse()
 * --------------------------------------------------------------------------------------------
 * Description:    Sends a response to an upload
 *
 * Parameters:     UploadRequest - A client upload request
 *                 Success - Whether that upload request succeeded or not
 *
 * Returns:        void
 */
void TeensyOtaUpdater::sendOtaResponse(AsyncWebServerRequest *UploadRequest, bool Success)
{
    AsyncWebServerResponse *response;
    char pageOut[HTTP_MAX_MESSAGE_RESP];
    unsigned int len;

    len = snprintf(pageOut, HTTP_MAX_MESSAGE_RESP, "<html><head><meta http-equiv=\"refresh\" content=\"20\"></head>");
    len += snprintf(pageOut + len, HTTP_MAX_MESSAGE_RESP - len,
                    "<body><h1>%s</h1></body></html>",
                    (Success) ? "OTA Success! Rebooting..." : "OTA Failed...");

    response = UploadRequest->beginResponse(Success ? 200 : 500, "text/html", pageOut);
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    UploadRequest->send(response);
}