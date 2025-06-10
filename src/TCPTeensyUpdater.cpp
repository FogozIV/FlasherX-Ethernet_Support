//
// Created by fogoz on 04/05/2025.
//

#include "TCPTeensyUpdater.h"
extern "C" {
#include "FlashTxx.h"		// TLC/T3x/T4x/TMM flash primitives
}
#define SIZE_TCP_BUFFER 32768

DMAMEM char tcp_uploader_buffer[SIZE_TCP_BUFFER] __attribute__((aligned(32)));
uint16_t c_index = 0;
DMAMEM char data[HEX_DATA_MAX_SIZE] __attribute__((aligned(32)));
std::mutex bufferMutex;
std::mutex updateMutex;

TCPTeensyUpdater::TCPTeensyUpdater() {

}
extern "C" {
    extern char _heap_start, _heap_end, _sbss, _ebss, _sdata, _edata;
}
FASTRUN bool TCPTeensyUpdater::addData(const char *data, uint16_t len) {
    updateMutex.lock();
    assert(reinterpret_cast<uintptr_t>(tcp_uploader_buffer) % 32 == 0); // 32-byte aligned
    assert(reinterpret_cast<uintptr_t>(data) % 4 == 0); // At least 4-byte aligned
    assert(c_index + len <= SIZE_TCP_BUFFER);
    memcpy(&tcp_uploader_buffer[c_index], data, len);
    c_index += len;
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
        assert(size < sizeof(line) - 1);
        line[size++] = c;
    }
    assert(last_string <= c_index);
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
    interrupts();
    bufferMutex.unlock();
}

bool TCPTeensyUpdater::isValid() {
    return check_flash_id( buffer_addr, hex.max - hex.min );
}

bool TCPTeensyUpdater::isDone() {
    return hex.eof;
}

RAMFUNC void flash_move_nw(uint32_t dst, uint32_t src, uint32_t size) {
    __asm volatile ("MSR faultmask, %0" : : "r" (1) : "memory"); //should disable interrupt once and for all
    uint32_t offset = 0;
    uint32_t error = 0;
    size_t original_size = 64;
    size_t alignment = 64;

    // Allocate enough for original size plus alignment-1 bytes
    char* raw_buffer = (char*) malloc(original_size + alignment - 1);
    assert(raw_buffer!= NULL);

    // Align the pointer
    char* buffer = (char*)(((uintptr_t)raw_buffer + alignment - 1) & ~(alignment - 1));

    // Main copy loop: 64-byte chunks
    while ((offset + 64) <= size && error == 0) {
        uint32_t addr = dst + offset;
        /*
        if (addr % 1024 == 0) {
            Serial.printf("%p\r\n", (void*)addr);
        }*/
        /*
        if ((addr & (FLASH_SECTOR_SIZE - 1)) == 0 && flash_sector_not_erased(addr)) {
            Serial.println("flash_sector_not_erased");
            assert((addr % FLASH_SECTOR_SIZE) == 0);
            eepromemu_flash_erase_sector((void*)addr);
        }*/

        memcpy(buffer, (void*)(src + offset), 64);

        if (memcmp((void*)addr, buffer, 64) != 0) {
            eepromemu_flash_write((void*)addr, buffer, 64);
        }

        offset += 64;
    }
    // Handle remaining bytes (less than 64)
    if (offset < size && error == 0) {
        uint32_t addr = dst + offset;
        uint32_t remaining = size - offset;

        if ((addr & (FLASH_SECTOR_SIZE - 1)) == 0 && flash_sector_not_erased(addr)) {
            eepromemu_flash_erase_sector((void*)addr);
        }

        memset(buffer, 0xFF, 64);  // Fill unused part with erased value
        memcpy(buffer, (void*)(src + offset), remaining);

        if (memcmp((void*)addr, buffer, 64) != 0) {
            eepromemu_flash_write((void*)addr, buffer, 64);
        }
    }
    /*
    Serial.println("flash_sector_erased");
    Serial.printf("end: %p\r\n", (void*)(dst+size));
    */
    // Optional: erase remainder of flash if source is in flash
    if (IN_FLASH(src)) {
      offset = (size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);  // align up
      while (offset < (FLASH_SIZE - FLASH_RESERVE) && error == 0) {
        uint32_t addr = dst + offset;
        if ((addr & (FLASH_SECTOR_SIZE - 1)) == 0 && flash_sector_not_erased(addr)) {
          eepromemu_flash_erase_sector((void*)addr);
        }
        offset += FLASH_SECTOR_SIZE;
      }
    }

    REBOOT;
    for (;;) {}
}
void FASTRUN TCPTeensyUpdater::callDone() {
    if (isDone() && isValid()) {
        flash_move_nw( FLASH_BASE_ADDR, buffer_addr, hex.max-hex.min);
    }
}

bool TCPTeensyUpdater::isFlashing() {
    return in_flash_mode;
}

FASTRUN bool TCPTeensyUpdater::init() {
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
    bool was_able_to_create = firmware_buffer_init( &buffer_addr, &buffer_size ) != 0;
    return was_able_to_create;
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
