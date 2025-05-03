//
// Created by fogoz on 02/05/2025.
//

#include "EthernetUpload.h"

void handleNotFound(AsyncWebServerRequest *Request)
{
    String message = "File Not Found\n\n";
    uint8_t i;

    message += "URI: ";
    message += Request->url();
    message += "\nMethod: ";
    message += (Request->method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += Request->args();
    message += "\n";

    for (i = 0; i < Request->args(); i++) {
        message += " " + Request->argName(i) + ": " + Request->arg(i) + "\n";
    }

    Request->send(404, "text/plain", message);
}

std::tuple<std::shared_ptr<AsyncWebServer>, std::shared_ptr<TeensyOtaUpdater>> setupWebServer(String location, uint16_t port){
    auto result =  std::make_shared<AsyncWebServer>(port);
    result->onNotFound(handleNotFound);
    auto updater = std::make_shared<TeensyOtaUpdater>(result, location.c_str());
    return std::tuple(result, updater);
}
