/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "registeraudiopluginsscenario.h"

#include <QCoreApplication>
#include <algorithm>
#include <map>
#include <vector>

#include "global/translation.h"

#include "audiopluginserrors.h"

#include "log.h"

using namespace muse;
using namespace muse::audioplugins;

namespace {
// Non-empty fallback id from a plugin path. completeBasename() is empty for LV2
// "<uri>@<bundle>/" composites, persisting an empty id that load() rejects; fall
// back to the full path for a non-empty, path-unique id.
std::string placeholderIdFromPath(const io::path_t& path)
{
    std::string id = io::completeBasename(path).toStdString();
    if (id.empty()) {
        id = path.toStdString();
    }
    return id;
}
}

void RegisterAudioPluginsScenario::init()
{
    TRACEFUNC;

    m_progress.canceled().onNotify(this, [this]() {
        m_aborted = true;
    });

    Ret ret = knownPluginsRegister()->load();
    if (!ret) {
        LOGE() << ret.toString();
    }
}

PluginScanResult RegisterAudioPluginsScenario::scanPlugins(Progress* progress) const
{
    TRACEFUNC;

    PluginScanResult result;

    // one binary path can host several plugin ids (shell bundles)
    std::map<io::path_t, std::vector<AudioPluginState> > registered;
    for (const auto& info : knownPluginsRegister()->pluginInfoList()) {
        registered[info.path].push_back(info.state);
    }

    for (const auto& scanner : scannerRegister()->scanners()) {
        for (const auto& path : scanner->scanPlugins(progress)) {
            auto it = registered.find(path);
            if (it == registered.end()) {
                result.newPluginPaths.push_back(path);
                continue;
            }

            const std::vector<AudioPluginState>& states = it->second;

            // Discovered placeholder: a prior run was interrupted, re-validate
            const bool hasDiscovered = std::any_of(states.cbegin(), states.cend(),
                                                   [](AudioPluginState state) {
                return state == AudioPluginState::Discovered;
            });
            if (hasDiscovered) {
                result.newPluginPaths.push_back(path);
                registered.erase(it);
                continue;
            }

            // a Missing path is back: re-validate rather than trust the cache,
            // the binary may have changed
            const bool hasMissing = std::any_of(states.cbegin(), states.cend(),
                                                [](AudioPluginState state) {
                return state == AudioPluginState::Missing;
            });
            if (hasMissing) {
                result.newPluginPaths.push_back(path);
            }
            registered.erase(it);
        }
    }

    // paths no scanner reports anymore are missing
    for (const auto& [path, states] : registered) {
        // skip paths already fully Missing, nothing to transition
        const bool hasTransition = std::any_of(states.cbegin(), states.cend(),
                                               [](AudioPluginState state) {
            return state != AudioPluginState::Missing;
        });
        if (hasTransition) {
            result.missingPluginPaths.push_back(path);
        }
    }

    return result;
}

Ret RegisterAudioPluginsScenario::updatePluginsRegistry()
{
    TRACEFUNC;

    PluginScanResult result = scanPlugins();

    Ret ret = knownPluginsRegister()->setPluginsState(result.missingPluginPaths, AudioPluginState::Missing);
    if (!ret) {
        LOGE() << "Failed to mark missing plugins: " << ret.toString();
        return ret;
    }

    ret = registerNewPlugins(result.newPluginPaths, /*validate*/ true);
    if (!ret) {
        LOGE() << "Failed to register new plugins: " << ret.toString();
        return ret;
    }

    return knownPluginsRegister()->load();
}

Ret RegisterAudioPluginsScenario::registerNewPlugins(const io::paths_t& pluginPaths, bool validate)
{
    TRACEFUNC;

    if (pluginPaths.empty()) {
        return make_ok();
    }

    Ret ret = persistDiscoveredPlaceholders(pluginPaths);
    if (!ret) {
        return ret;
    }

    if (validate) {
        processPluginsRegistration(pluginPaths);
    }

    return knownPluginsRegister()->load();
}

Ret RegisterAudioPluginsScenario::persistDiscoveredPlaceholders(const io::paths_t& pluginPaths)
{
    // Placeholders make scanPlugins() re-validate these paths next launch.
    // Clear any prior entry at the path first to avoid the same-id-same-path assert.
    AudioPluginInfoList placeholders;
    placeholders.reserve(pluginPaths.size());
    for (const io::path_t& path : pluginPaths) {
        Ret ret = knownPluginsRegister()->removePluginsAtPath(path);
        if (!ret) {
            return ret;
        }

        AudioPluginInfo info;
        info.meta.id = placeholderIdFromPath(path);
        info.meta.type = metaType(path);
        info.path = path;
        info.state = AudioPluginState::Discovered;
        placeholders.emplace_back(std::move(info));
    }
    return knownPluginsRegister()->registerPlugins(placeholders);
}

Ret RegisterAudioPluginsScenario::unregisterRemovedPlugins(const PluginResourceIdList& pluginIds)
{
    TRACEFUNC;

    if (pluginIds.empty()) {
        return make_ok();
    }

    Ret ret = knownPluginsRegister()->unregisterPlugins(pluginIds);
    if (!ret) {
        LOGE() << "Failed to unregister removed plugins: " << ret.toString();
    }

    return ret;
}

void RegisterAudioPluginsScenario::processPluginsRegistration(const io::paths_t& pluginPaths)
{
    interactive()->showProgress(muse::trc("audio", "Scanning audio plugins"), m_progress);

    m_aborted = false;
    m_progress.start();

    std::string appPath = globalConfiguration()->appBinPath().toStdString();
    int64_t pluginCount = static_cast<int64_t>(pluginPaths.size());

    for (int64_t i = 0; i < pluginCount; ++i) {
        if (m_aborted) {
            return;
        }

        const io::path_t& pluginPath = pluginPaths[i];
        std::string pluginPathStr = pluginPath.toStdString();

        m_progress.progress(i, pluginCount, io::filename(pluginPath).toStdString());
        qApp->processEvents();

        // The subprocess clears its own Discovered placeholder via
        // registerPlugin / registerFailedPlugin. Removing it here would
        // operate on the main process's stale in-memory register and
        // clobber the entries previous subprocesses already wrote.
        LOGD() << "--register-audio-plugin " << pluginPathStr;
        int code = process()->execute(appPath, { "--register-audio-plugin", pluginPathStr });
        if (code != 0) {
            code = process()->execute(appPath, { "--register-failed-audio-plugin", pluginPathStr, "--", std::to_string(code) });
        }

        if (code != 0) {
            LOGE() << "Could not register plugin: " << pluginPathStr << "\n error code: " << code;
        }
    }

    m_progress.finish(muse::make_ok());
}

Ret RegisterAudioPluginsScenario::registerPlugin(const io::path_t& pluginPath)
{
    TRACEFUNC;

    IF_ASSERT_FAILED(!pluginPath.empty()) {
        return false;
    }

    // Clear any prior Discovered placeholder at this path so the real
    // validated metadata can be registered without tripping the
    // same-id-same-path guard in registerPlugins(). Subprocess-side: the
    // process just load()ed, so the register reflects what's on disk.
    Ret ret = knownPluginsRegister()->removePluginsAtPath(pluginPath);
    if (!ret) {
        LOGE() << "Failed to clear existing entry at " << pluginPath.toStdString()
               << ": " << ret.toString();
        return ret;
    }

    const IAudioPluginMetaReaderPtr reader = metaReader(pluginPath);
    if (!reader) {
        return make_ret(Err::UnknownPluginType);
    }

    const RetVal<PluginMetaList> metaList = reader->readMeta(pluginPath);
    if (!metaList.ret) {
        LOGE() << metaList.ret.toString();
        return metaList.ret;
    }

    AudioPluginInfoList infoList;
    infoList.reserve(metaList.val.size());

    for (const PluginMeta& meta : metaList.val) {
        AudioPluginInfo info;
        info.meta = meta;
        info.path = pluginPath;
        info.state = AudioPluginState::Validated;
        infoList.emplace_back(std::move(info));
    }

    return knownPluginsRegister()->registerPlugins(infoList);
}

Ret RegisterAudioPluginsScenario::registerFailedPlugin(const io::path_t& pluginPath, int failCode)
{
    TRACEFUNC;

    IF_ASSERT_FAILED(!pluginPath.empty()) {
        return false;
    }

    // Same reason as registerPlugin: the failed entry uses the basename as
    // its id (matching the Discovered placeholder), so the placeholder must
    // be cleared first to avoid the same-id-same-path guard.
    Ret ret = knownPluginsRegister()->removePluginsAtPath(pluginPath);
    if (!ret) {
        LOGE() << "Failed to clear existing entry at " << pluginPath.toStdString()
               << ": " << ret.toString();
        return ret;
    }

    AudioPluginInfo info;
    info.meta.id = placeholderIdFromPath(pluginPath);
    info.meta.type = metaType(pluginPath);
    info.path = pluginPath;
    info.state = AudioPluginState::Error;
    info.errorCode = failCode;

    return knownPluginsRegister()->registerPlugins({ info });
}

IAudioPluginMetaReaderPtr RegisterAudioPluginsScenario::metaReader(const io::path_t& pluginPath) const
{
    for (const IAudioPluginMetaReaderPtr& reader : metaReaderRegister()->readers()) {
        if (reader->canReadMeta(pluginPath)) {
            return reader;
        }
    }

    return nullptr;
}

audioplugins::PluginType RegisterAudioPluginsScenario::metaType(const io::path_t& pluginPath) const
{
    const IAudioPluginMetaReaderPtr reader = metaReader(pluginPath);
    return reader ? reader->metaType() : audioplugins::PluginType();
}
