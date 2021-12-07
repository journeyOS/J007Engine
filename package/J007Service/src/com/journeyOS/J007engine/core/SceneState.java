/*
 * Copyright (c) 2021 anqi.huang@outlook.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.journeyOS.J007engine.core;

import com.journeyOS.J007engine.core.detect.Monitor;

public class SceneState {
    public long app = Monitor.SCENE_FACTOR_APP_DEFAULT;
    public Battery battery;

    public static class Battery {
        public int level = -1;
        public int pluggedIn = -1;
        public int status = -1;
        public int health = -1;
        public int temperature = -1;
    }

    public long brightness = -1;
}
