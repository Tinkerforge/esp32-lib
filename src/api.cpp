#include "api.h"

#include "SPIFFS.h"
#include "bindings/hal_common.h"
#include "bindings/errors.h"

#include "event_log.h"
#include "task_scheduler.h"

extern TF_HalContext hal;

extern TaskScheduler task_scheduler;
extern EventLog logger;

void API::setup()
{

}

void API::addCommand(String path, Config *config, std::initializer_list<String> keys_to_censor_in_debug_report, std::function<void(void)> callback)
{
    commands.push_back({path, config, callback, keys_to_censor_in_debug_report});

    for (auto* backend: this->backends) {
        backend->addCommand(commands[commands.size() - 1]);
    }
}

void API::addState(String path, Config *config, std::initializer_list<String> keys_to_censor, uint32_t interval_ms)
{
    // copy into vector, initializer list is temporary, but we want to capture the content
    std::vector<String> keys{keys_to_censor};

    task_scheduler.scheduleWithFixedDelay((String("state ") + path).c_str(), [this, path, config, keys]() {
        if(!config->was_updated())
            return;

        config->set_update_handled();

        String payload = config->to_string_except(keys);

        for (auto* backend: this->backends) {
            backend->pushStateUpdate(payload, path);
        }
    }, interval_ms, interval_ms);

    states.push_back({path, config, keys});
    for (auto* backend: this->backends) {
        backend->addState(states[states.size() - 1]);
    }
}

void API::addPersistentConfig(String path, Config *config, std::initializer_list<String> keys_to_censor, uint32_t interval_ms)
{
    addState(path, config, keys_to_censor, interval_ms);
    addCommand(path + String("_update"), config, keys_to_censor, [path, config](){
        String path_copy = path;
        path_copy.replace('/', '_');
        String tmp_path = String("/") + path_copy + ".json.tmp";
        String cfg_path = String("/") + path_copy + ".json";

        if (SPIFFS.exists(tmp_path))
            SPIFFS.remove(tmp_path);

        File file = SPIFFS.open(tmp_path, "w");
        config->save_to_file(file);
        file.close();

        if (SPIFFS.exists(cfg_path))
            SPIFFS.remove(cfg_path);

        SPIFFS.rename(tmp_path, cfg_path);
    });
}
/*
void API::addTemporaryConfig(String path, Config *config, std::initializer_list<String> keys_to_censor, uint32_t interval_ms, std::function<void(void)> callback)
{
    addState(path, config, keys_to_censor, interval_ms);
    addCommand(path + String("_update"), config, callback);
}
*/

void API::registerDebugUrl(AsyncWebServer *server) {
    server->on("/debug_report", HTTP_GET, [this](AsyncWebServerRequest *request) {

        String result = "{\"uptime\": ";
        result += String(millis());
        result += ",\n \"free_heap_bytes\":" ;
        result += ESP.getFreeHeap();
        result += ",\n \"largest_free_heap_block\":" ;
        result += ESP.getMaxAllocHeap();

        result += ",\n \"devices\": [";
        size_t i = 0;
        char uid[7] = {0};
        char pos = 0;
        uint16_t did = 0;
        while(tf_hal_get_device_info(&hal, i, uid, &pos, &did) == TF_E_OK) {
            char buf[100] = {0};
            snprintf(buf, sizeof(buf), "%c{\"UID\":\"%s\", \"DID\":%u, \"port\":\"%c\"}", i == 0 ? ' ': ',', uid, did, pos);
            result += buf;
            ++i;
        }
        result += "]";

        result += ",\n \"error_counters\": [";
        for(char c = 'A'; c <= 'F'; ++c) {
            uint32_t spitfp_checksum, spitfp_frame, tfp_frame, tfp_unexpected;

            tf_hal_get_error_counters(&hal, c, &spitfp_checksum, &spitfp_frame, &tfp_frame, &tfp_unexpected);
            char buf[100] = {0};
            snprintf(buf, sizeof(buf), "%c{\"port\": \"%c\", \"SpiTfpChecksum\": %u, \"SpiTfpFrame\": %u, \"TfpFrame\": %u, \"TfpUnexpected\": %u}", c == 'A' ? ' ': ',', c,
                spitfp_checksum,
                spitfp_frame,
                tfp_frame,
                tfp_unexpected);
            result += buf;
        }
        result += "]";

        for(auto &reg : states) {
            result += ",\n \"";
            result += reg.path;
            result += "\": ";
            result += reg.config->to_string_except(reg.keys_to_censor);
        }

        for(auto &reg: commands) {
            result += ",\n \"";
            result += reg.path;
            result += "\": ";
            result += reg.config->to_string_except(reg.keys_to_censor_in_debug_report);
        }
        result += "}";
        request->send(200, "application/json; charset=utf-8", result);
    });
}

void API::registerBackend(IAPIBackend *backend)
{
    backends.push_back(backend);
}

bool API::attemptReconnect(const char *caller) {
    if(reconnect_in_progress)
        return false;

    //logger.printfln("%s setting flag.", caller);
    reconnect_in_progress = true;
    last_attempt = millis();
    if(last_attempt == 0)
        last_attempt == 1;

    return true;
}

void API::loop()
{
    if (last_attempt == 0)
        return;
    if (!deadline_elapsed(last_attempt + 30000))
        return;

    reconnectDone("api loop fallback");
}

void API::reconnectDone(const char *caller)
{
    //logger.printfln("%s clearing flag.", caller);
    reconnect_in_progress = false;
    last_attempt = 0;
}

void API::wifiAvailable()
{
    task_scheduler.scheduleOnce("wifi_available", [this](){
        for (auto* backend: this->backends) {
            backend->wifiAvailable();
        }
    }, 1000);
}
