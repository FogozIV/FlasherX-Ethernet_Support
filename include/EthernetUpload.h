//
// Created by fogoz on 02/05/2025.
//

#ifndef PAMITEENSY_ETHERNETUPLOAD_H
#define PAMITEENSY_ETHERNETUPLOAD_H

#include <AsyncWebServer_Teensy41.hpp>
#include "teensyupdater.hpp"

void handleNotFound(AsyncWebServerRequest *Request);

std::tuple<std::shared_ptr<AsyncWebServer>, std::shared_ptr<TeensyOtaUpdater>> setupWebServer(String location="/", uint16_t port=80);


#endif //PAMITEENSY_ETHERNETUPLOAD_H
