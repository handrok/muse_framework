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

#pragma once

#include "global/modularity/ioc.h"
#include "global/io/ifilesystem.h"

#include "../iknownaudiopluginsregister.h"
#include "../iknownaudiopluginsmigrationregister.h"
#include "../iaudiopluginsconfiguration.h"

namespace muse::audioplugins {
class KnownAudioPluginsRegister : public IKnownAudioPluginsRegister
{
    GlobalInject<IAudioPluginsConfiguration> configuration;
    GlobalInject<IKnownAudioPluginsMigrationRegister> migrations;
    GlobalInject<io::IFileSystem> fileSystem;

    friend class AudioPlugins_KnownAudioPluginsRegisterTest;

public:
    KnownAudioPluginsRegister() = default;

    Ret load() override;

    AudioPluginInfoList pluginInfoList(PluginInfoAccepted accepted = PluginInfoAccepted()) const override;
    muse::async::Notification pluginInfoListChanged() const override;

    const io::path_t& pluginPath(const PluginResourceId& resourceId) const override;

    bool exists(const io::path_t& pluginPath) const override;
    bool exists(const PluginResourceId& resourceId) const override;

    Ret registerPlugins(const AudioPluginInfoList& list) override;
    Ret unregisterPlugins(const PluginResourceIdList& resourceIds) override;

    Ret setPluginsState(const PluginResourceIdList& resourceIds, AudioPluginState state) override;

    Ret removePluginsAtPath(const io::path_t& path) override;

private:
    Ret writePluginsInfo();
    async::Notification m_pluginInfoListChanged;
    bool m_loaded = false;
    std::multimap<PluginResourceId, AudioPluginInfo> m_pluginInfoMap;
    std::set<io::path_t> m_pluginPaths;
};
}
