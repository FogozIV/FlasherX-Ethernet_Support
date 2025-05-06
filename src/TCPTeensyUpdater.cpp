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
char data[HEX_DATA_MAX_SIZE] __attribute__((aligned(8)));
std::mutex bufferMutex;
std::mutex updateMutex;

TCPTeensyUpdater::TCPTeensyUpdater() {

}

bool TCPTeensyUpdater::addData(const char *data, uint16_t len) {
    updateMutex.lock();
    for (int i =0; i < len; i++) {
        tcp_uploader_buffer[c_index+i] = data[i];
    }
    c_index += len;
    updateMutex.unlock();
    return true;
}

bool TCPTeensyUpdater::parse() {
    updateMutex.lock();
    String line;
    uint16_t last_string = 0;
    for (int i = 0; i < c_index; i++) {
        char c = data[i];
        if (line.length() == 0 && (c == '\n' || c == '\r'))
            continue;
        if ((c == '\n' || c == '\r')) {
            last_string = c_index+1;
            if (!use_line(line))
                return false;
            line = "";
            break;
        }
        line += c;
    }
    if (last_string != 0) {
        memmove(data, &data[last_string], c_index - last_string);
    }
    c_index-=last_string;
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
    return init();
}

void TCPTeensyUpdater::abort() {
    firmware_buffer_free (buffer_addr, buffer_size);
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
    return firmware_buffer_init( &buffer_addr, &buffer_size ) != 0;
}

bool TCPTeensyUpdater::use_line(String line) {
    if (parse_hex_line( line.c_str(), hex.data, &hex.addr, &hex.num, &hex.code ) == 0) { //bad hex line
        return false;
    }
    if (process_hex_record( &hex ) != 0) { // error on bad hex code
        return false;
    }
    if (hex.code == 0) { // if data record
        uint32_t addr = buffer_addr + hex.base + hex.addr - FLASH_BASE_ADDR;
        if (hex.max > (FLASH_BASE_ADDR + buffer_size)) { //max address %08lX too large (hex.max )
            return false;
        }
        if (!IN_FLASH(buffer_addr)) {
            memcpy( (void*)addr, (void*)hex.data, hex.num );
        }
        else if (IN_FLASH(buffer_addr)) {
            int error = flash_write_block( addr, hex.data, hex.num );
            if (error) { // "abort - error %02X in flash_write_block()\r\n", error
                return false;
            }
        }
    }
    hex.lines++;
    return true;
}
