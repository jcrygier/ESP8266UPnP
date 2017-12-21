#ifndef ESP8266UPNP_H
#define ESP8266UPNP_H

#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <QList.h>
#include "QList.cpp" // Use only if IDE shows compile error

class ESP8266UPnP;

struct RegisteredSubscription {
    String callbackUrl;
    String sid;
    // TODO: Subscription Timeout
};

struct StringAttribute {
    String attributeName;
    String *attributeValue;
    String lastAttributeValue;
};

typedef std::function<void(void)> ZeroArgHandlerFunction;
typedef std::function<void(String)> OneArgHandlerFunction;
typedef std::function<void(String,String)> TwoArgHandlerFunction;

struct ZeroArgAction {
    String actionName;
    ZeroArgHandlerFunction callback;
};

struct OneArgAction {
    String actionName;
    OneArgHandlerFunction callback;
};

struct TwoArgAction {
    String actionName;
    TwoArgHandlerFunction callback;
};

class ESP8266UPnP {
public:
    ESP8266UPnP(ESP8266WebServer* webServer);
    ESP8266UPnP(ESP8266WebServer* webServer, const char* actionEndpoint);
    ~ESP8266UPnP();

    void begin();
    
    void onAction(const String &actionName, ZeroArgHandlerFunction handler);
    void onAction(const String &actionName, OneArgHandlerFunction handler);
    void onAction(const String &actionName, TwoArgHandlerFunction handler);

    void registerAttribute(const String &attributeName, String *attributeValue);
    void handleAttributeChange();

protected:
    ESP8266WebServer*  _webServer;
    const char* _actionEndpoint;
    unsigned long _actionDebounceTime;
    unsigned long _lastActionTime;

    QList<RegisteredSubscription> _subscriptions;
    QList<StringAttribute> _stringAttributes;
    QList<ZeroArgAction> _zeroArgActions;
    QList<OneArgAction> _oneArgActions;
    QList<TwoArgAction> _twoArgActions;

    void _onHttpSoapMessage();
    String _createAttributeChangeProperty(String attributeName, String attributeValue);
    String _getPropertySet();
};

#endif //ESP8266UPNP_H