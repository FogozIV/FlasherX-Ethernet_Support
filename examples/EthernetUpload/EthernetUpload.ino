#include <Arduino.h>
#include <QNEthernet.h>
#include <AsyncWebServer_Teensy41.h> /* Only to be included once here to avoid multiple definitions. Other files can include the ".hpp" version */
#include <teensyupdater.hpp>

using namespace qindesign::network;

#define WAIT_FOR_LOCAL_IP_WAIT_TIME 15000
#define HOSTNAME "MyTeensyWincyProj"
#define STATIC_IP_ADDR_STRNG "192.168.1.113"
#define GATEWAY_IP_ADDR_STRING "192.168.1.1"
#define SUBNET_MASK_STRING "255.255.255.0"
#define DNS_SERVER_STRING "1.1.1.1"

#define USING_DHCP true
//#define USING_DHCP false

#if !USING_DHCP
  // Set the static IP address to use if the DHCP fails to assign
  IPAddress myIP;
  IPAddress myNetmask;
  IPAddress myGW;
  IPAddress mydnsServer;
#endif

TeensyOtaUpdater *tOtaUpdater;
AsyncWebServer   *webServer;
bool              updateAvailable;

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

void TOA_Callack()
{
  Serial.println("An update is available");
  updateAvailable = true;
}

////////////////////////// setup() ////////////////////////////////
void setup()
{
    webServer = new AsyncWebServer(80);

#if LWIP_NETIF_HOSTNAME
    Serial.printf("Setting hostname to %s\r\n", HOSTNAME);
    Ethernet.setHostname(HOSTNAME);
#endif

    // Attempt to establish a connection with the network
#if USING_DHCP
    // Start the Ethernet connection, using DHCP
    Serial.print("Initialize Ethernet using DHCP => ");
    Ethernet.begin();
#else
    // Start the Ethernet connection, using static IP
    Serial.print("Initialize Ethernet using static IP => ");
    myIP.fromString(STATIC_IP_ADDR_STRNG);
    myGW.fromString(GATEWAY_IP_ADDR_STRING);
    myNetmask.fromString(SUBNET_MASK_STRING);
    mydnsServer.fromString(DNS_SERVER_STRING);
    Ethernet.begin(myIP, myNetmask, myGW);
    Ethernet.setDNSServerIP(mydnsServer);
#endif

    if (!Ethernet.waitForLocalIP(WAIT_FOR_LOCAL_IP_WAIT_TIME)) {
        Serial.println(F("Failed to configure Ethernet"));

        if (!Ethernet.linkStatus()) {
            Serial.println(F("Ethernet cable is not connected."));
        }

        for(;;) {}
    } else {
        Serial.print(F("Connected! IP address:"));
        Serial.println(Ethernet.localIP());
    }

    webServer->onNotFound(handleNotFound);

    tOtaUpdater = new TeensyOtaUpdater(webServer, "/");

    // Register a callback to be notified of when an update is available. If
    // a callback is not registered, the update will be applied automatically
    // when one is available
    updateAvailable = false;
    tOtaUpdater->registerCallback(TOA_Callack);

    webServer->begin();

}

////////////////////////// loop() ////////////////////////////////
void loop()
{
  if (updateAvailable) {
    // Notify other layers (to display a status that about to reboot or smth)
    Serial.println("Applying udpate");

    // This function does not return
    tOtaUpdater->applyUpdate();
  }
}