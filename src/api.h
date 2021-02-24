#pragma once

#include "Arduino.h"

#include <functional>
#include <initializer_list>
#include <vector>

#include "ESPAsyncWebServer.h"

#include "config.h"

struct StateRegistration {
    String path;
    Config *config;
    std::vector<String> keys_to_censor;
};

struct CommandRegistration {
    String path;
    Config *config;
    std::function<void(void)> callback;
    std::vector<String> keys_to_censor_in_debug_report;
};


class IAPIBackend {
public:
    virtual void addCommand(CommandRegistration reg) = 0;
    virtual void addState(StateRegistration reg) = 0;
    virtual void pushStateUpdate(String payload, String path) = 0;
    virtual void wifiAvailable() = 0;
};


class API {
public:
    API() {}

    void setup();
    void loop();

    void addCommand(String path, Config *config, std::initializer_list<String> keys_to_censor_in_debug_report, std::function<void(void)> callback);
    void addState(String path, Config *config, std::initializer_list<String> keys_to_censor, uint32_t interval_ms);
    void addPersistentConfig(String path, Config *config, std::initializer_list<String> keys_to_censor, uint32_t interval_ms);
    //void addTemporaryConfig(String path, Config *config, std::initializer_list<String> keys_to_censor, uint32_t interval_ms, std::function<void(void)> callback);

    void registerDebugUrl(AsyncWebServer *server);

    void registerBackend(IAPIBackend *backend);

    bool reconnect_in_progress;
    uint32_t last_attempt = 0;
    bool attemptReconnect(const char*);
    void reconnectDone(const char*);
    void wifiAvailable();

    std::vector<StateRegistration> states;
    std::vector<CommandRegistration> commands;

    std::vector<IAPIBackend *> backends;
};
