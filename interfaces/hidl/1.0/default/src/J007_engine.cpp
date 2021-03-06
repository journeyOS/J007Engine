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

#include "J007_engine.h"

#include "log.h"
#include "utils.h"
#include "factors.h"
#include "json/json_object.h"
#include "global_scene.h"
#include "policy/cpu_policy_agent.h"


J007Engine *J007Engine::sInstance = NULL;

J007Engine::J007Engine() {
    initAgent();
}

J007Engine::~J007Engine() {
}

J007Engine *J007Engine::getInstance() {
    if (sInstance == NULL) {
        sInstance = new J007Engine();
    }

    return sInstance;
}

void J007Engine::initAgent() {
    LOGD("init agent");
    CpuPolicyAgent *cpuAgent = new CpuPolicyAgent();
    addAgents(CPU_POLICY_AGENT, cpuAgent);
}

void J007Engine::addAgents(string flag, PolicyAgent *agent) {
    LOGD("add agents, flag = %s", flag.c_str());
    mAgentMap.insert(pair<string, PolicyAgent *>(flag, agent));
}

bool J007Engine::notifyCpuAgentAppSwitch() {
    if (!mAgentMap.count(CPU_POLICY_AGENT)) {
        return false;
    }
    App app = GlobalScene::getInstance()->getApp();
    SourceScene sourceScene = GlobalScene::getInstance()->getSourceScene();
    string status = sourceScene.status;
    string packageName = sourceScene.packageName;
    mAgentMap[CPU_POLICY_AGENT]->onAppSwitch(app, status, packageName);
    return true;
}

Return<void> J007Engine::registerCallback(const sp <IJ007EngineCallback> &callback) {
    LOGI("registerCallback(callback:%p).", &callback);
    if (callback == nullptr) {
        ALOGE("can't registerCallback null ptr");
        return Return<void>();
    }

    {
        lock_guard<decltype(mCallbacksLock)> lock(mCallbacksLock);
        mCallbacks.push_back(callback);
        // unlock
    }

    auto linkRet = callback->linkToDeath(this, 0u /* cookie */);
    if (!linkRet.withDefault(false)) {
        if (linkRet.isOk()) {
            ALOGW("Cannot link to death: linkToDeath returns false");
        }
        // else {
        //     ALOGW("Cannot link to death: %s", linkRet.description());
        // }
        // ignore the error
    }
    return Return<void>();
}

Return<void> J007Engine::unregisterCallback(const sp <IJ007EngineCallback> &callback) {
    LOGI("unregisterCallback(callback:%p).", &callback);
    unregisterCallbackInternal(callback);
    return Return<void>();
}

bool J007Engine::unregisterCallbackInternal(const sp <IBase> &callback) {
    if (callback == nullptr) return false;

    bool removed = false;
    lock_guard<decltype(mCallbacksLock)> lock(mCallbacksLock);
    for (auto it = mCallbacks.begin(); it != mCallbacks.end();) {
        if (interfacesEqual(*it, callback)) {
            it = mCallbacks.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    (void) callback->unlinkToDeath(this).isOk();  // ignore errors
    return removed;
}

void J007Engine::serviceDied(uint64_t /* cookie */, const wp <IBase> &who) {
    (void) unregisterCallbackInternal(who.promote());
}

void J007Engine::onResponse(TCode code, string messages) {
    if (DEBUG) {
        LOGI("on response code = %d , messages = %s\n", code, messages.c_str());
    }
    struct J007EngineResponse response;
    response.status = Status::SUCCESS;
    response.code = code;
    response.messages = messages;

    lock_guard<decltype(mCallbacksLock)> lock(mCallbacksLock);
    for (auto it = mCallbacks.begin(); it != mCallbacks.end();) {
        auto ret = (*it)->onResponse(response);
        if (!ret.isOk() && ret.isDeadObject()) {
            it = mCallbacks.erase(it);
        } else {
            ++it;
        }
    }
}

Return<bool>
J007Engine::notifySceneChanged(const int32_t factors, const hidl_string &status, const hidl_string &packageName) {
    if (DEBUG) {
        ALOGI("notify scene changed, factors = %d , status = %s , packageName = %s\n", factors, status.c_str(),
              packageName.c_str());
    }
    GlobalScene::getInstance()->updateScene(factors, status.c_str(), packageName.c_str());
    if (SCENE_FACTOR_APP == factors) {
        notifyCpuAgentAppSwitch();
    }

    return true;
}

Return<bool> J007Engine::setConfig(const TCode code, const hidl_string &val) {
    if (DEBUG) {
        LOGI("set config code = %d , messages = %s\n", code, val.c_str());
    }
    switch (code) {
        case TCode::SET_XXX:
            mConfigs = val.c_str();
            //ALOGI("performance cpu auto = %d , cpu level = %d", (cpu_auto_ ? 1 : 0), cpu_level_);
            onResponse(TCode::SET_XXX, mConfigs);
            break;
    }
    return true;
}

Return<void> J007Engine::getConfig(const TCode code, IJ007Engine::getConfig_cb _hidl_cb) {
    switch (code) {
        case TCode::GET_XXX:
            _hidl_cb(mConfigs);
            break;

        default:
            break;
    }
    return Return<void>();
}

Return<void> J007Engine::read(const hidl_string &path, IJ007Engine::read_cb _hidl_cb) {
    _hidl_cb(Utils::readFile(path.c_str()));
    return Return<void>();
}

Return<bool> J007Engine::write(const hidl_string &path, const hidl_string &val) {
    return Utils::writeFile(path.c_str(), val.c_str());
}

Return<void> J007Engine::readProperty(const hidl_string &key, const hidl_string &defaultVaule,
                                      IJ007Engine::readProperty_cb _hidl_cb) {
    char buf[PROPERTY_VALUE_MAX];
    if (property_get(key.c_str(), buf, defaultVaule.c_str()) != 0) {
        goto success;
    }

    success:
    LOGI("read property %s = %s ", key.c_str(), buf);
    _hidl_cb(buf);
    return Return<void>();
}

Return<bool> J007Engine::writeProperty(const hidl_string &key, const hidl_string &val) {
    bool success = false;
    if (property_set(key.c_str(), val.c_str()) < 0) {
        success = false;
    } else {
        success = true;
    }
    return success;
}

Return<void>
J007Engine::getPackageName(const int32_t pid, IJ007Engine::getPackageName_cb _hidl_cb) {
    char path[DEFAULT_PATH_SIZE];
    sprintf(path, "cat /proc/%d/cmdline", pid);
    //sprintf(path, "ps -A | grep %d | awk '{print $9}'", pid);
    if (DEBUG) {
        LOGI("path %s\n", path);
    }
    char package_name[DEFAULT_PATH_SIZE];
    if (!Utils::read_process_str(path, package_name)) {
        if (DEBUG) {
            LOGI("get package name %s\n", package_name);
        }
        _hidl_cb(package_name);
    }

    return Return<void>();
}
