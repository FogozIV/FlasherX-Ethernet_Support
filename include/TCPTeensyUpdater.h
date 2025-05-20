//
// Created by fogoz on 04/05/2025.
//

#ifndef TCPTEENSYUPDATER_H
#define TCPTEENSYUPDATER_H

#define SIZE_TCP_BUFFER 16384
#include "Arduino.h"
#include <FXUtil.h>
#include <avr/pgmspace.h>
extern "C" {
#include "FlashTxx.h"		// TLC/T3x/T4x/TMM flash primitives
}
extern char tcp_uploader_buffer[];
extern uint16_t c_index;
extern char data[];

extern bool flashing_process;

template<typename Mutex>
class TCPTeensyUpdater {
    static Mutex bufferMutex;
    static Mutex updateMutex;
    hex_info_t hex;
    bool in_flash_mode = false;
    uint32_t buffer_addr, buffer_size;

    bool init() {
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
    bool use_line(char* line) {
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

public:
    TCPTeensyUpdater() {

    }

    bool addData(const char* data, uint16_t len){
        updateMutex.lock();
        for (int i =0; i < len; i++) {
            tcp_uploader_buffer[c_index+i] = data[i];
        }
        c_index += len;
        updateMutex.unlock();
        return true;
    }


    bool parse() {
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
        updateMutex.unlock();
        return true;
    }
    bool startFlashMode() {
        if (flashing_process) {
            return false;
        }
        if (!bufferMutex.try_lock()) {
            return false;
        }
        flashing_process = true;
        return init();
    }
    void abort() {
        firmware_buffer_free (buffer_addr, buffer_size);
        flashing_process = false;
        in_flash_mode = false;
        bufferMutex.unlock();
    }

    bool isValid() {
        return check_flash_id( buffer_addr, hex.max - hex.min );
    }

    bool isDone() {
        return hex.eof;
    }

    void callDone() {
        if (isDone() && isValid()) {
            flash_move( FLASH_BASE_ADDR, buffer_addr, hex.max-hex.min);
        }
    }
    bool isFlashing() {
        return in_flash_mode;
    }

};
template<typename Mutex>
inline Mutex TCPTeensyUpdater<Mutex>::bufferMutex;

template<typename Mutex>
inline Mutex TCPTeensyUpdater<Mutex>::updateMutex;



#endif //TCPTEENSYUPDATER_H
