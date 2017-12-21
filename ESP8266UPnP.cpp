#include "ESP8266UPnP.h"
#include "ESP8266TrueRandom.h"
#include "ESP8266HTTPClient.h"

#include <ArduinoLog.h>
#include <tinyxml2.h>
using namespace tinyxml2;

ESP8266UPnP::ESP8266UPnP(ESP8266WebServer* webServer)
: _webServer(webServer)
, _actionEndpoint("/upnp/control")
, _actionDebounceTime(5000)
, _lastActionTime(0)
{
}

ESP8266UPnP::ESP8266UPnP(ESP8266WebServer* webServer, const char* actionEndpoint)
: _webServer(webServer)
, _actionEndpoint(actionEndpoint)
, _actionDebounceTime(5000)
, _lastActionTime(0)
{
}

ESP8266UPnP::~ESP8266UPnP() {   
}

void ESP8266UPnP::begin() {
    Log.trace("Binding UPnP Control Endpoint to URL: '%s'\n", _actionEndpoint);

    // Mark down which headers we need to capture
    const char * headerKeys[] = { "CALLBACK", "TIMEOUT", "SID" };
    size_t headerKeysSize = sizeof(headerKeys)/sizeof(char*);
    _webServer->collectHeaders(headerKeys, headerKeysSize);

    _webServer->on(_actionEndpoint, HTTP_POST, [&]() {
        Log.trace("Received HTTP POST, Processing...\n");
        this->_onHttpSoapMessage();
    });

    _webServer->on("/upnp/subscribe", [&]() {
        String callbackUrl = _webServer->header("CALLBACK");
        String timeout = _webServer->header("TIMEOUT");

        callbackUrl.replace(">", "");
        callbackUrl.replace("<", "");

        Log.trace("Registering Subscription: %s for %s\n", callbackUrl.c_str(), timeout.c_str());

        // Check if we've already registered this CALLBACK
        for(int i = 0; i < _subscriptions.size(); i++) {
            RegisteredSubscription subscription = _subscriptions.at(i);
            if (subscription.callbackUrl.equals(callbackUrl))
                return;
        }

        byte uuidNumber[16];
        ESP8266TrueRandom.uuid(uuidNumber);
        String uuidStr = ESP8266TrueRandom.uuidToString(uuidNumber);

        RegisteredSubscription subscription = { callbackUrl, uuidStr };
        this->_subscriptions.push_back(subscription);

        _webServer->sendHeader("Server", "Arduino/1.0 UPnP/1.1 ESP8266UPnP/1.0");
        _webServer->sendHeader("SID", uuidStr);
        _webServer->sendHeader("TIMEOUT", timeout);
        _webServer->send(200);
    });

    _webServer->on("/upnp/attributes", [&]() {
        _webServer->sendHeader("Server", "Arduino/1.0 UPnP/1.1 ESP8266UPnP/1.0");
        _webServer->send(200, "text/xml; charset=\"utf-8\"", this->_getPropertySet());
    });
}

void ESP8266UPnP::_onHttpSoapMessage() {
    if ((_lastActionTime + _actionDebounceTime) > millis()) {
        Log.error("Debouncing Soap Control Message\n");

        _webServer->sendHeader("Server", "Arduino/1.0 UPnP/1.1 ESP8266UPnP/1.0");
        _webServer->send(429, "text/xml; charset=\"utf-8\"", "<error>Too Many Requests</error>");

        return;
    }

    _lastActionTime = millis();

    String xmlBody = _webServer->argName(0) + "=" + _webServer->arg(0);
    Log.trace("Processing SOAP Control Message: %s\n", xmlBody.c_str());
    
    XMLDocument xml;
    xml.Clear();

    char __xml[xmlBody.length() + 1];
    xmlBody.toCharArray(__xml, sizeof(__xml));
    XMLError eResult = xml.Parse(__xml, xmlBody.length());
    if (eResult != XML_SUCCESS)
        Log.error("Error Pasing XML: %d\n%s", eResult, __xml);

    // TODO: What if there's a header sent?

    XMLNode* actionNode = xml.RootElement()->FirstChild()->FirstChild();
    String action = String(actionNode->Value());
    int sepIndex = action.indexOf(':');
    String actionNs = action.substring(0, sepIndex);
    String actionName = action.substring(sepIndex + 1);

    String serviceType = String(actionNode->ToElement()->Attribute(("xmlns:" + actionNs).c_str()));

    Log.trace( "Action: %s\nServiceType: %s\n", actionName.c_str(), serviceType.c_str());

    int argCount = 0;           // TODO: Finish parsing arguments

    if (argCount == 0) {
        for(int i = 0; i < _zeroArgActions.size(); i++) {
            ZeroArgAction action = _zeroArgActions.at(i);
            if (action.actionName.equals(actionName.c_str())) {
                action.callback();
            }
        }
    }

    String response = "<?xml version=\"1.0\"?>\n"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
    "  <s:Body>\n"
    "    <u:" + actionName + "Response xmlns:u=\"" + serviceType + "\">\n"
    "      <argumentName>out arg value</argumentName>\n"            // TODO: How to get arguments?
    "    </u:" + actionName + "Response>\n"
    "  </s:Body>\n"
    "</s:Envelope>\n";

    _webServer->sendHeader("Server", "Arduino/1.0 UPnP/1.1 ESP8266UPnP/1.0");
    _webServer->send(200, "text/xml; charset=\"utf-8\"", response);
}

void ESP8266UPnP::onAction(const String &actionName, ZeroArgHandlerFunction handler) {
    ZeroArgAction action = { actionName, handler };
    _zeroArgActions.push_back(action);
}

void ESP8266UPnP::onAction(const String &actionName, OneArgHandlerFunction handler) {
    OneArgAction action = { actionName, handler };
    _oneArgActions.push_back(action);
}

void ESP8266UPnP::onAction(const String &actionName, TwoArgHandlerFunction handler) {
    TwoArgAction action = { actionName, handler };
    _twoArgActions.push_back(action);
}

void ESP8266UPnP::registerAttribute(const String &attributeName, String *attributeValue) {
    StringAttribute attribute = { attributeName, attributeValue, String(*attributeValue) };
    _stringAttributes.push_back(attribute);
}

void ESP8266UPnP::handleAttributeChange() {
    bool foundChanged = false;

    for(int i = 0; i < _stringAttributes.size(); i++) {
        StringAttribute *attrib = &(_stringAttributes.at(i));
        String newValue = *(attrib->attributeValue);

        if (!attrib->lastAttributeValue.equals(newValue)) {
            Log.trace("Found change in attribute %s.  Old value: %s, new value: %s\n", attrib->attributeName.c_str(), attrib->lastAttributeValue.c_str(), newValue.c_str());
            // TODO: Free Memory?
            attrib->lastAttributeValue = String(newValue.c_str());
            foundChanged = true;
        }
    }

    if (foundChanged) {
        String fullEventText = _getPropertySet();

        // Push out the notifications to the registered listeners
        for(int i = 0; i < _subscriptions.size(); i++) {
            RegisteredSubscription subscription = _subscriptions.at(i);

            Log.trace("Notifing subscription %s at %s\n", subscription.sid.c_str(), subscription.callbackUrl.c_str());

            HTTPClient http;
            http.begin(subscription.callbackUrl);
            http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");

            int httpCode = http.sendRequest("NOTIFY", fullEventText);
            if (httpCode > 299) {
                Log.error("Error notifying subscription.  Response Code: %s", httpCode);
            }

            http.end();
        }
    }
}

String ESP8266UPnP::_getPropertySet() {
    String changedAttributes = "";

    for(int i = 0; i < _stringAttributes.size(); i++) {
        StringAttribute *attrib = &(_stringAttributes.at(i));
        changedAttributes += _createAttributeChangeProperty(attrib->attributeName, *(attrib->attributeValue));
    }

    return "<?xml version=\"1.0\"?>\n"
        "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">\n"
        + changedAttributes +
        "</e:propertyset>";
}

String ESP8266UPnP::_createAttributeChangeProperty(String attributeName, String attributeValue) {
    return "  <e:property>\n"
    "    <" + attributeName + ">" + attributeValue + "</" + attributeName + ">\n"
    "  </e:property>\n";
}