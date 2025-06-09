//
// Created by fogoz on 04/05/2025.
//

#include "TCPTeensyUpdater.h"
extern "C" {
#include "FlashTxx.h"		// TLC/T3x/T4x/TMM flash primitives
}
#define SIZE_TCP_BUFFER 16384

DMAMEM char tcp_uploader_buffer[SIZE_TCP_BUFFER];
uint16_t c_index = 0;
DMAMEM char data[HEX_DATA_MAX_SIZE] __attribute__((aligned(8)));
std::mutex bufferMutex;
std::mutex updateMutex;

TCPTeensyUpdater::TCPTeensyUpdater() {

}

FASTRUN bool TCPTeensyUpdater::addData(const char *data, uint16_t len) {
    updateMutex.lock();
    noInterrupts()
    digitalWrite(LED_BUILTIN, HIGH);
    for (int i =0; i < len; i++) {
        tcp_uploader_buffer[c_index+i] = data[i];
    }
    c_index += len;
    digitalWrite(LED_BUILTIN, LOW);
    interrupts()
    updateMutex.unlock();
    return true;
}

FASTRUN bool TCPTeensyUpdater::parse() {
    updateMutex.lock();
    char line[128];
    uint8_t size = 0;
    uint16_t last_string = 0;
    for (int i = 0; i < c_index; i++) {
        char c = tcp_uploader_buffer[i];
        if (size == 0 && (c == '\n' || c == '\r'))
            continue;
        if ((c == '\n' || c == '\r')) {
            last_string = i+1;
            line[size] = '\0';
            if (!use_line(line)) {
                updateMutex.unlock();
                abort();
                return false;
            }
            size = 0;
            continue;
        }
        line[size++] = c;
    }
    if (last_string != 0) {
        memmove(tcp_uploader_buffer, &tcp_uploader_buffer[last_string], c_index - last_string);
    }
    c_index-=last_string;
    digitalWrite(LED_BUILTIN, LOW);
    updateMutex.unlock();
    return true;
}

bool TCPTeensyUpdater::startFlashMode() {
    if (flashing_process) {
        return false;
    }
    if (!bufferMutex.try_lock()) {
        return false;
    }
    flashing_process = true;
    return init();
}

void TCPTeensyUpdater::abort() {
    firmware_buffer_free (buffer_addr, buffer_size);
    flashing_process = false;
    in_flash_mode = false;
    bufferMutex.unlock();
}

bool TCPTeensyUpdater::isValid() {
    return check_flash_id( buffer_addr, hex.max - hex.min );
}

bool TCPTeensyUpdater::isDone() {
    return hex.eof;
}

void TCPTeensyUpdater::callDone() {
    if (isDone() && isValid()) {
        flash_move( FLASH_BASE_ADDR, buffer_addr, hex.max-hex.min);
    }
}

bool TCPTeensyUpdater::isFlashing() {
    return in_flash_mode;
}

bool TCPTeensyUpdater::init() {
    hex.data = data;
    hex.addr = 0;
    hex.code = 0;
    hex.num = 0;
    hex.base = 0;
    hex.min = 0xFFFFFFFF;
    hex.max = 0;
    hex.eof = 0;
    hex.lines = 0;
    hex.prevDataLen = 0;
    in_flash_mode = true;
    return firmware_buffer_init( &buffer_addr, &buffer_size ) != 0;
}

FASTRUN bool TCPTeensyUpdater::use_line(char* line) {
    if (parse_hex_line( line, hex.data, &hex.addr, &hex.num, &hex.code ) == 0) { //bad hex line
        Serial.println("Bad hex line");
        return false;
    }
    if (process_hex_record( &hex ) != 0) { // error on bad hex code
        Serial.println("Bad hex code");
        return false;
    }
    if (hex.code == 0) { // if data record
        uint32_t addr = buffer_addr + hex.base + hex.addr - FLASH_BASE_ADDR;
        if (hex.max > (FLASH_BASE_ADDR + buffer_size)) { //max address %08lX too large (hex.max )
            Serial.printf("max address %08lX too large\r\n", hex.max);
            return false;
        }
        if (!IN_FLASH(buffer_addr)) {
            memcpy( (void*)addr, (void*)hex.data, hex.num );
        }
        else if (IN_FLASH(buffer_addr)) {
            int error = flash_write_block( addr, hex.data, hex.num );
            if (error) { // "abort - error %02X in flash_write_block()\r\n", error
                Serial.printf("abort - error %02X in flash_write_block()\r\n", error);
                return false;
            }
        }
    }
    hex.lines++;
    return true;
}
