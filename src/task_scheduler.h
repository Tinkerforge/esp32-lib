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

#include <vector>
#include <queue>
#include <functional>

#include <time.h>
#include <iostream>

#include "tools.h"

#include "ArduinoJson.h"
#include "AsyncJson.h"

struct Task {
    const char *task_name;
    std::function<void(void)> fn;
    uint32_t next_deadline_ms;
    uint32_t delay_ms;
    bool once;

    Task(const char *task_name, std::function<void(void)> fn, uint32_t first_run_delay_ms, uint32_t delay_ms, bool once);
};

bool compare(const Task &a, const Task &b);

class TaskScheduler {
public:
    TaskScheduler() : tasks(&compare) {}
    void setup();
    void register_urls();
    void loop();

    bool initialized = false;

    void scheduleOnce(const char *task_name, std::function<void(void)> &&fn, uint32_t delay) ;
    void scheduleWithFixedDelay(const char *task_name, std::function<void(void)> &&fn, uint32_t first_delay, uint32_t delay);
private:
    std::priority_queue<Task, std::vector<Task>, decltype(&compare)> tasks;
};
