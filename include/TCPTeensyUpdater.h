//
// Created by fogoz on 04/05/2025.
//

#ifndef TCPTEENSYUPDATER_H
#define TCPTEENSYUPDATER_H
#include "Arduino.h"
#include <FXUtil.h>
#include <TeensyThreads.h>
#include <avr/pgmspace.h>

extern bool flashing_process;

class TCPTeensyUpdater {
    hex_info_t hex;
    bool in_flash_mode;
    uint32_t buffer_addr, buffer_size;

    bool init();

    bool use_line(String line);

public:
    TCPTeensyUpdater();

    bool addData(const char* data, uint16_t len);

    bool parse();

    bool startFlashMode();

    void abort();

    bool isValid();

    bool isDone();

    void callDone();
};




#endif //TCPTEENSYUPDATER_H
