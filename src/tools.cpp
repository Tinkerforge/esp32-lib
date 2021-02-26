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

#include "tools.h"

#include "bindings/errors.h"

#include <Arduino.h>

#include "SPIFFS.h"
#include "esp_spiffs.h"

#include <soc/efuse_reg.h>
#include "bindings/base58.h"
#include "event_log.h"

extern EventLog logger;

bool deadline_elapsed(uint32_t deadline_ms) {
    uint32_t now = millis();

    if(now < deadline_ms) {
        uint32_t diff = deadline_ms - now;
        if (diff < UINT32_MAX / 2)
            return false;
        return true;
    }
    else {
        uint32_t diff = now - deadline_ms;
        if(diff > UINT32_MAX / 2)
            return false;
        return true;
    }
}

bool find_uid_by_did(TF_HalContext *hal, uint16_t device_id, char uid[7]) {
    char pos;
    uint16_t did;
    for (size_t i = 0; tf_hal_get_device_info(hal, i, uid, &pos, &did) == TF_E_OK; ++i) {
        if (did == device_id) {
            return true;
        }
    }
    return false;
}

bool send_event_allowed(AsyncEventSource *events) {
    // TODO: patch the library to get how many packets are waiting in the fullest client queue
    return events->count() > 0 && events->avgPacketsWaiting() < 8;
}

String update_config(Config &cfg, String config_name, JsonVariant &json) {
    String error = cfg.update_from_json(json);

    String tmp_path = String("/") + config_name + ".json.tmp";
    String path = String("/") + config_name + ".json";

    if (error == "") {
        if (SPIFFS.exists(tmp_path))
            SPIFFS.remove(tmp_path);

        File file = SPIFFS.open(tmp_path, "w");
        cfg.save_to_file(file);
        file.close();

        if (SPIFFS.exists(path))
            SPIFFS.remove(path);

        SPIFFS.rename(tmp_path, path);
    } else {
        logger.printfln("Failed to update %s: %s", path.c_str(), error.c_str());
    }
    return error;
}


void read_efuses(uint32_t *ret_uid_numeric, char *ret_uid_string, char *ret_passphrase_string) {
    uint32_t blocks[8] = {0};

    for(int32_t block3Address=EFUSE_BLK3_RDATA0_REG, i = 0; block3Address<=EFUSE_BLK3_RDATA7_REG; block3Address+=4, ++i)
    {
        blocks[i] = REG_GET_FIELD(block3Address, EFUSE_BLK3_DOUT0);
    }

    uint32_t passphrase[4] = {0};
    uint32_t uid = 0;

    /*
    EFUSE_BLK_3 is 256 bit (32 byte, 8 blocks) long and organized as follows:
    block 0:
        Custom MAC CRC + MAC bytes 0 to 2
    block 1:
        Custom MAC bytes 3 to 5
        byte 3 - Wifi passphrase chunk 0 byte 0
    block 2:
        byte 0 - Wifi passphrase chunk 0 byte 1
        byte 1 - Wifi passphrase chunk 0 byte 2
        byte 2 - Wifi passphrase chunk 1 byte 0
        byte 3 - Wifi passphrase chunk 1 byte 1
    block 3:
        ADC 1 calibration data
    block 4:
        ADC 2 calibration data
    block 5:
        byte 0 - Wifi passphrase chunk 1 byte 2
        byte 1 - Wifi passphrase chunk 2 byte 0
        byte 2 - Wifi passphrase chunk 2 byte 1
        byte 3 - Custom MAC Version
    block 6:
        byte 0 - Wifi passphrase chunk 2 byte 2
        byte 1 - Wifi passphrase chunk 3 byte 0
        byte 2 - Wifi passphrase chunk 3 byte 1
        byte 3 - Wifi passphrase chunk 3 byte 2
    block 7:
        UID
    */

    passphrase[0] = ((blocks[1] & 0xFF000000) >> 24) | ((blocks[2] & 0x0000FFFF) << 8);
    passphrase[1] = ((blocks[2] & 0xFFFF0000) >> 16) | ((blocks[5] & 0x000000FF) << 16);
    passphrase[2] = ((blocks[5] & 0x00FFFF00) >> 8)  | ((blocks[6] & 0x000000FF) << 16);
    passphrase[3] =  (blocks[6] & 0xFFFFFF00) >> 8;
    uid = blocks[7];

    char buf[7] = {0};
    for(int i = 0; i < 4; ++i) {
        if(i != 0)
            ret_passphrase_string[i * 5 - 1] = '-';

        tf_base58_encode(passphrase[i], buf);
        if(strnlen(buf, sizeof(buf)/sizeof(buf[0])) != 4) {
            logger.printfln("efuse error: malformed passphrase!");
        } else {
            memcpy(ret_passphrase_string + i * 5, buf, 4);
        }
    }
    tf_base58_encode(uid, ret_uid_string);
}

int check(int rc,const char *msg) {
    if (rc >= 0)
        return rc;
    logger.printfln("%lu Failed to %s rc: %s", millis(), msg, tf_hal_strerror(rc));
    delay(10);
    return rc;
}

bool mount_or_format_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 10,
      .format_if_mount_failed = false
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if(err == ESP_FAIL) {
        logger.printfln("SPIFFS is not formatted. Formatting now. This will take about 30 seconds.");
        SPIFFS.format();
    } else {
        esp_vfs_spiffs_unregister(NULL);
    }

    return SPIFFS.begin(false);
}

String read_or_write_config_version(String &firmware_version) {
    String spiffs_version = firmware_version;

    if (SPIFFS.exists("/spiffs.json")) {
        const size_t capacity = JSON_OBJECT_SIZE(1) + 60;
        StaticJsonDocument<capacity> doc;

        File file = SPIFFS.open("/spiffs.json", "r");
        deserializeJson(doc, file);
        file.close();

        spiffs_version = doc["spiffs"].as<char*>();
    } else {
        File file = SPIFFS.open("/spiffs.json", "w");
        file.printf("{\"spiffs\": \"%s\"}", firmware_version.c_str());
        file.close();
    }

    return spiffs_version;
}
