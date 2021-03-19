/* esp32-lib
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

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
    bool is_action;
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

    String callCommand(String path, Config::ConfUpdate payload);

    const Config *getState(String path);

    void addCommand(String path, Config *config, std::initializer_list<String> keys_to_censor_in_debug_report, std::function<void(void)> callback, bool is_action);
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
