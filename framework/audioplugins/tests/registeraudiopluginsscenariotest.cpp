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
#include <gmock/gmock.h>

#include "audioplugins/internal/registeraudiopluginsscenario.h"

#include "global/tests/mocks/globalconfigurationmock.h"
#include "global/tests/mocks/processmock.h"
#include "interactive/tests/mocks/interactivemock.h"

#include "mocks/knownaudiopluginsregistermock.h"
#include "mocks/audiopluginsscannerregistermock.h"
#include "mocks/audiopluginsscannermock.h"
#include "mocks/audiopluginmetareaderregistermock.h"
#include "mocks/audiopluginmetareadermock.h"

#include "translation.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

using namespace muse;
using namespace muse::audioplugins;
using namespace muse::io;

namespace muse::audioplugins {
class AudioPlugins_RegisterAudioPluginsScenarioTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_scenario = std::make_shared<RegisterAudioPluginsScenario>(modularity::globalCtx());
        m_globalConfiguration = std::make_shared<NiceMock<GlobalConfigurationMock> >();
        m_interactive = std::make_shared<InteractiveMock>();
        m_process = std::make_shared<ProcessMock>();
        m_scannerRegister = std::make_shared<NiceMock<AudioPluginsScannerRegisterMock> >();
        m_knownPlugins = std::make_shared<NiceMock<KnownAudioPluginsRegisterMock> >();
        m_scanners = { std::make_shared<NiceMock<AudioPluginsScannerMock> >() };
        m_metaReaderRegister = std::make_shared<NiceMock<AudioPluginMetaReaderRegisterMock> >();

        const auto metaReaderMock = std::make_shared<NiceMock<AudioPluginMetaReaderMock> >();
        m_metaReaders = { metaReaderMock };

        m_scenario->globalConfiguration.set(m_globalConfiguration);
        m_scenario->interactive.set(m_interactive);
        m_scenario->process.set(m_process);
        m_scenario->knownPluginsRegister.set(m_knownPlugins);
        m_scenario->scannerRegister.set(m_scannerRegister);
        m_scenario->metaReaderRegister.set(m_metaReaderRegister);

        ON_CALL(*m_globalConfiguration, appBinPath())
        .WillByDefault(Return(m_appPath));

        ON_CALL(*m_scannerRegister, scanners())
        .WillByDefault(ReturnRef(m_scanners));

        ON_CALL(*m_metaReaderRegister, readers())
        .WillByDefault(ReturnRef(m_metaReaders));

        ON_CALL(*metaReaderMock, metaType())
        .WillByDefault(Return("VstPlugin"));

        ON_CALL(*metaReaderMock, canReadMeta(_))
        .WillByDefault(Return(true));

        ON_CALL(*m_knownPlugins, setPluginsState(_, _))
        .WillByDefault(Return(muse::make_ok()));

        ON_CALL(*m_knownPlugins, removePluginsAtPath(_))
        .WillByDefault(Return(muse::make_ok()));

        ON_CALL(*m_knownPlugins, registerPlugins(_))
        .WillByDefault(Return(muse::make_ok()));
    }

    std::shared_ptr<RegisterAudioPluginsScenario> m_scenario;
    std::shared_ptr<GlobalConfigurationMock> m_globalConfiguration;
    std::shared_ptr<InteractiveMock> m_interactive;
    std::shared_ptr<ProcessMock> m_process;
    std::shared_ptr<KnownAudioPluginsRegisterMock> m_knownPlugins;
    std::shared_ptr<AudioPluginsScannerRegisterMock> m_scannerRegister;
    std::vector<IAudioPluginsScannerPtr> m_scanners;
    std::shared_ptr<AudioPluginMetaReaderRegisterMock> m_metaReaderRegister;
    std::vector<IAudioPluginMetaReaderPtr> m_metaReaders;

    std::string m_appPath = "/some/path/to/MuseScore";
};

inline bool operator==(const AudioPluginInfo& info1, const AudioPluginInfo& info2)
{
    return info1.path == info2.path
           && info1.meta == info2.meta
           && info1.state == info2.state
           && info1.errorCode == info2.errorCode;
}
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, Init)
{
    // [THEN] The register is inited
    EXPECT_CALL(*m_knownPlugins, load())
    .WillOnce(Return(muse::make_ok()));

    // [WHEN] Init the scenario
    m_scenario->init();
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, UpdatePluginsRegistry)
{
    // [GIVEN] All found plugins
    paths_t foundPluginPaths {
        "/some/test/path/to/plugin/AAA.vst3", // already registered
        "/some/test/path/to/plugin/BBB.vst3", // already registered
        "/some/test/path/to/plugin/CCC.vst3",
        "/some/test/path/to/plugin/DDD.vst3",
        "/some/test/path/to/plugin/FFF.vst3", // incompatible (will crash)
    };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    AudioPluginInfo incompatiblePluginInfo;
    incompatiblePluginInfo.path = foundPluginPaths[4];
    incompatiblePluginInfo.meta.id = io::filename(incompatiblePluginInfo.path).toStdString();
    incompatiblePluginInfo.state = AudioPluginState::Error;
    incompatiblePluginInfo.errorCode = -1;

    // [GIVEN] Some plugins already exist in the register
    AudioPluginInfoList alreadyRegisteredPlugins;
    for (size_t i = 0; i < 2; ++i) {
        AudioPluginInfo info;
        info.path = foundPluginPaths[i];
        info.meta.id = io::completeBasename(foundPluginPaths[i]).toStdString();
        alreadyRegisteredPlugins.push_back(info);
    }

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(alreadyRegisteredPlugins));

    // [THEN] The progress bar is shown
    EXPECT_CALL(*m_interactive, showProgress(muse::trc("audio", "Scanning audio plugins"), _))
    .Times(1);

    // [THEN] Processes started only for unregistered plugins
    paths_t alreadyRegisteredPaths { foundPluginPaths[0], foundPluginPaths[1] };
    for (const path_t& pluginPath : foundPluginPaths) {
        std::vector<std::string> args = { "--register-audio-plugin", pluginPath.toStdString() };

        if (muse::contains(alreadyRegisteredPaths, pluginPath)) {
            // Ignore already registered plugins
            EXPECT_CALL(*m_process, execute(_, args))
            .Times(0);
        } else if (incompatiblePluginInfo.path == pluginPath) {
            // Incompatible plugin detected
            EXPECT_CALL(*m_process, execute(m_appPath, args))
            .WillOnce(Return(-1));

            args = { "--register-failed-audio-plugin", pluginPath.toStdString(), "--", "-1" };

            EXPECT_CALL(*m_process, execute(m_appPath, args))
            .WillOnce(Return(0));
        } else {
            // Successfully registered plugins
            EXPECT_CALL(*m_process, execute(m_appPath, args))
            .WillOnce(Return(0));
        }
    }

    // [THEN] All plugins remain in the register
    EXPECT_CALL(*m_knownPlugins, unregisterPlugins(_))
    .Times(0);

    // [THEN] The register is refreshed
    EXPECT_CALL(*m_knownPlugins, load())
    .Times(2)
    .WillRepeatedly(Return(muse::make_ok()));

    // [WHEN] Register new plugins
    Ret ret = m_scenario->updatePluginsRegistry();

    // [THEN] Plugins successfully registered
    EXPECT_TRUE(ret);
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, UpdatePluginsRegistry_NoNewPlugins)
{
    // [GIVEN] All found plugins (all are already registered)
    paths_t foundPluginPaths {
        "/some/test/path/to/plugin/AAA.vst3",
        "/some/test/path/to/plugin/BBB.vst3",
        "/some/test/path/to/plugin/CCC.vst3",
    };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    AudioPluginInfoList alreadyRegisteredPlugins;
    for (const path_t& pluginPath : foundPluginPaths) {
        AudioPluginInfo info;
        info.path = pluginPath;
        info.meta.id = io::completeBasename(pluginPath).toStdString();
        alreadyRegisteredPlugins.push_back(info);
    }

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(alreadyRegisteredPlugins));

    // [THEN] Don't register the plugins again
    EXPECT_CALL(*m_process, execute(_, _))
    .Times(0);

    EXPECT_CALL(*m_interactive, showProgress(_, _))
    .Times(0);

    EXPECT_CALL(*m_knownPlugins, load())
    .WillOnce(Return(muse::make_ok()));

    // [WHEN] Try to register the plugins again
    Ret ret = m_scenario->updatePluginsRegistry();

    // [THEN] No error
    EXPECT_TRUE(ret);
}

//! See: https://github.com/musescore/MuseScore/issues/16458
TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, UpdatePluginsRegistry_MarkUninstalledAsMissing)
{
    auto createPluginInfo = [](const io::path_t& path, AudioPluginState state = AudioPluginState::Validated) {
        AudioPluginInfo info;
        info.meta.id = io::completeBasename(path).toStdString();
        info.meta.type = "VstPlugin";
        info.path = path;
        info.state = state;
        return info;
    };

    // [GIVEN] Already registered plugins
    AudioPluginInfoList knownPlugins;
    knownPlugins.push_back(createPluginInfo("/some/path/AAA.vst3"));
    knownPlugins.push_back(createPluginInfo("/some/path/BBB.vst3"));
    knownPlugins.push_back(createPluginInfo("/some/path/CCC.vst3"));
    knownPlugins.push_back(createPluginInfo("/some/path/DDD.vst3"));

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(knownPlugins));

    // [GIVEN] Scanner only finds some plugins (AAA and BBB have been uninstalled)
    paths_t foundPluginPaths {
        "/some/path/CCC.vst3",
        "/some/path/DDD.vst3",
    };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    // [THEN] Uninstalled plugins transition to Missing (kept in cache)
    paths_t uninstalledPluginPaths {
        knownPlugins[0].path, knownPlugins[1].path
    };

    EXPECT_CALL(*m_knownPlugins, setPluginsState(uninstalledPluginPaths, AudioPluginState::Missing))
    .WillOnce(Return(make_ok()));

    EXPECT_CALL(*m_knownPlugins, unregisterPlugins(_))
    .Times(0);

    EXPECT_CALL(*m_knownPlugins, load())
    .WillOnce(Return(muse::make_ok()));

    // [WHEN] Update registry
    Ret ret = m_scenario->updatePluginsRegistry();

    // [THEN] Successfully transitioned
    EXPECT_TRUE(ret);
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, ScanPlugins_FormerlyMissingFoundAgainIsRevalidated)
{
    auto createPluginInfo = [](const io::path_t& path, AudioPluginState state) {
        AudioPluginInfo info;
        info.meta.id = io::completeBasename(path).toStdString();
        info.meta.type = "VstPlugin";
        info.path = path;
        info.state = state;
        return info;
    };

    // [GIVEN] One Missing entry that reappears, one untouched Validated entry
    AudioPluginInfoList knownPlugins;
    knownPlugins.push_back(createPluginInfo("/some/path/AAA.vst3", AudioPluginState::Missing));
    knownPlugins.push_back(createPluginInfo("/some/path/BBB.vst3", AudioPluginState::Validated));

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(knownPlugins));

    // [GIVEN] Scanner now finds both
    paths_t foundPluginPaths {
        "/some/path/AAA.vst3",
        "/some/path/BBB.vst3",
    };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    // [WHEN] Scanning
    const PluginScanResult result = m_scenario->scanPlugins();

    // [THEN] The reappeared plugin is re-validated rather than trusted back
    // to Validated; the untouched Validated plugin is left alone.
    EXPECT_TRUE(muse::contains(result.newPluginPaths, path_t("/some/path/AAA.vst3")));
    EXPECT_FALSE(muse::contains(result.newPluginPaths, path_t("/some/path/BBB.vst3")));
    EXPECT_TRUE(result.missingPluginPaths.empty());
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, UpdatePluginsRegistry_MissingMultiPluginBinaryReportedOnceByPath)
{
    auto createPluginInfo = [](const io::path_t& path, const PluginResourceId& id, AudioPluginState state) {
        AudioPluginInfo info;
        info.meta.id = id;
        info.meta.type = "VstPlugin";
        info.path = path;
        info.state = state;
        return info;
    };

    // [GIVEN] One path hosting two plugin ids (shell bundle), plus a standalone plugin
    const io::path_t shellPath = "/some/path/SHELL.vst3";
    const io::path_t soloPath = "/some/path/SOLO.vst3";

    AudioPluginInfoList knownPlugins;
    knownPlugins.push_back(createPluginInfo(shellPath, "Shell FxA", AudioPluginState::Validated));
    knownPlugins.push_back(createPluginInfo(shellPath, "Shell FxB", AudioPluginState::Validated));
    knownPlugins.push_back(createPluginInfo(soloPath, "Solo Fx", AudioPluginState::Validated));

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(knownPlugins));

    // [GIVEN] The scanner no longer finds the shell binary; the solo plugin stays.
    paths_t foundPluginPaths { soloPath };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    // [THEN] The vanished binary is reported once by path; flipping every id
    // under it is the register's job.
    paths_t expectedMissing { shellPath };

    EXPECT_CALL(*m_knownPlugins, setPluginsState(expectedMissing, AudioPluginState::Missing))
    .WillOnce(Return(make_ok()));

    EXPECT_CALL(*m_knownPlugins, load())
    .WillOnce(Return(make_ok()));

    Ret ret = m_scenario->updatePluginsRegistry();
    EXPECT_TRUE(ret);
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, ScanPlugins_FormerlyMissingMultiIdBinaryQueuedOnce)
{
    auto createPluginInfo = [](const io::path_t& path, const PluginResourceId& id, AudioPluginState state) {
        AudioPluginInfo info;
        info.meta.id = id;
        info.meta.type = "VstPlugin";
        info.path = path;
        info.state = state;
        return info;
    };

    // [GIVEN] A bundle whose two ids are both currently Missing
    const io::path_t shellPath = "/some/path/SHELL.vst3";

    AudioPluginInfoList knownPlugins;
    knownPlugins.push_back(createPluginInfo(shellPath, "Shell FxA", AudioPluginState::Missing));
    knownPlugins.push_back(createPluginInfo(shellPath, "Shell FxB", AudioPluginState::Missing));

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(knownPlugins));

    // [GIVEN] The scanner finds the binary again.
    paths_t foundPluginPaths { shellPath };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    // [WHEN] Scanning
    const PluginScanResult result = m_scenario->scanPlugins();

    // [THEN] The reappeared binary is queued once for re-validation; nothing
    // is trusted back to Validated, nothing is left Missing.
    EXPECT_EQ(result.newPluginPaths, paths_t { shellPath });
    EXPECT_TRUE(result.missingPluginPaths.empty());
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, UpdatePluginsRegistry_LeftoverDiscoveredRevalidates)
{
    // [GIVEN] A leftover Discovered entry; the scanner still sees the path on disk
    auto createPluginInfo = [](const io::path_t& path, AudioPluginState state) {
        AudioPluginInfo info;
        info.meta.id = io::completeBasename(path).toStdString();
        info.meta.type = "VstPlugin";
        info.path = path;
        info.state = state;
        return info;
    };

    AudioPluginInfoList knownPlugins;
    knownPlugins.push_back(createPluginInfo("/some/path/CRASHED.vst3", AudioPluginState::Discovered));

    ON_CALL(*m_knownPlugins, pluginInfoList(_))
    .WillByDefault(Return(knownPlugins));

    paths_t foundPluginPaths { "/some/path/CRASHED.vst3" };

    for (const IAudioPluginsScannerPtr& scanner : m_scanners) {
        AudioPluginsScannerMock* mock = dynamic_cast<AudioPluginsScannerMock*>(scanner.get());
        ASSERT_TRUE(mock);

        ON_CALL(*mock, scanPlugins(_))
        .WillByDefault(Return(foundPluginPaths));
    }

    // [THEN] The Discovered path is treated as new — registered as a fresh
    // placeholder and re-validated via subprocess. It is NOT marked Missing
    // (it's still on disk).
    EXPECT_CALL(*m_knownPlugins, setPluginsState(paths_t {}, AudioPluginState::Missing))
    .WillOnce(Return(make_ok()));

    // [THEN] registerNewPlugins writes a Discovered placeholder for the path
    // before spawning the subprocess.
    AudioPluginInfo expectedPlaceholder = createPluginInfo("/some/path/CRASHED.vst3",
                                                           AudioPluginState::Discovered);
    EXPECT_CALL(*m_knownPlugins, registerPlugins(AudioPluginInfoList { expectedPlaceholder }))
    .WillOnce(Return(make_ok()));

    // [THEN] The path is cleared once, inside persistDiscoveredPlaceholders,
    // so a leftover Discovered entry from a previous interrupted run doesn't
    // trip registerPlugins's same-id-same-path assertion. The subprocess
    // takes care of clearing the placeholder again before persisting its
    // Validated/Error result (see RegisterPlugin / RegisterFailedPlugin
    // tests); the main process must NOT do it again here because that would
    // operate on its stale in-memory register and clobber the accumulated
    // results of previous subprocesses.
    EXPECT_CALL(*m_knownPlugins, removePluginsAtPath(io::path_t("/some/path/CRASHED.vst3")))
    .Times(1)
    .WillRepeatedly(Return(make_ok()));

    EXPECT_CALL(*m_process, execute(m_appPath,
                                    std::vector<std::string> { "--register-audio-plugin", "/some/path/CRASHED.vst3" }))
    .WillOnce(Return(0));

    // [THEN] register loaded twice (once after registerNewPlugins, once at end of updatePluginsRegistry)
    EXPECT_CALL(*m_knownPlugins, load())
    .Times(2)
    .WillRepeatedly(Return(make_ok()));

    Ret ret = m_scenario->updatePluginsRegistry();
    EXPECT_TRUE(ret);
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, RegisterPlugin)
{
    // [GIVEN] Some plugin we want to register
    path_t pluginPath = "/some/test/path/to/plugin/AAA.vst3";

    PluginMetaList metaList;

    PluginMeta pluginMeta1;
    pluginMeta1.id = "Mono plugin";
    pluginMeta1.attributes.insert({ String(u"categories"), u"Fx|Mono" });
    metaList.push_back(pluginMeta1);

    PluginMeta pluginMeta2;
    pluginMeta2.id = "Stereo plugin";
    pluginMeta2.attributes.insert({ String(u"categories"), u"Fx|Stereo" });
    metaList.push_back(pluginMeta2);

    ASSERT_FALSE(m_metaReaders.empty());
    AudioPluginMetaReaderMock* mock = dynamic_cast<AudioPluginMetaReaderMock*>(m_metaReaders[0].get());
    ASSERT_TRUE(mock);

    ON_CALL(*mock, readMeta(pluginPath))
    .WillByDefault(Return(RetVal<PluginMetaList>::make_ok(metaList)));

    // [THEN] The plugin has been registered
    AudioPluginInfoList expectedInfoList;
    expectedInfoList.reserve(metaList.size());

    for (const PluginMeta& meta : metaList) {
        AudioPluginInfo expectedPluginInfo;
        expectedPluginInfo.meta = meta;
        expectedPluginInfo.path = pluginPath;
        expectedPluginInfo.state = AudioPluginState::Validated;
        expectedPluginInfo.errorCode = 0;
        expectedInfoList.emplace_back(std::move(expectedPluginInfo));
    }

    // [THEN] Any prior Discovered placeholder at this path is cleared before
    // the real validated entries are persisted. Subprocess-side, this is what
    // lets a single path with N sub-effects replace the 1 placeholder entry
    // with N Validated ones without tripping the same-id-same-path guard.
    ::testing::InSequence seq;
    EXPECT_CALL(*m_knownPlugins, removePluginsAtPath(pluginPath))
    .WillOnce(Return(make_ok()));
    EXPECT_CALL(*m_knownPlugins, registerPlugins(expectedInfoList))
    .WillOnce(Return(true));

    // [WHEN] Register the plugin
    Ret ret = m_scenario->registerPlugin(pluginPath);

    // [THEN] The plugin successfully registered
    EXPECT_TRUE(ret);
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, RegisterFailedPlugin)
{
    // [GIVEN] Some incompatible plugin we want to register
    path_t pluginPath = "/some/test/path/to/plugin/AAA.vst3";

    // [THEN] The plugin has been registered
    AudioPluginInfo expectedPluginInfo;
    expectedPluginInfo.meta.id = io::completeBasename(pluginPath).toStdString();
    expectedPluginInfo.meta.type = "VstPlugin";
    expectedPluginInfo.path = pluginPath;
    expectedPluginInfo.state = AudioPluginState::Error;
    expectedPluginInfo.errorCode = -42;

    // [THEN] Same as RegisterPlugin: clear the prior placeholder first. Here
    // it's load-bearing — the failed entry uses the basename as its id, which
    // is the same id the Discovered placeholder used, so without the prior
    // remove registerPlugins would hit the same-id-same-path guard.
    ::testing::InSequence seq;
    EXPECT_CALL(*m_knownPlugins, removePluginsAtPath(pluginPath))
    .WillOnce(Return(make_ok()));
    EXPECT_CALL(*m_knownPlugins, registerPlugins(AudioPluginInfoList { expectedPluginInfo }))
    .WillOnce(Return(true));

    // [WHEN] Register the incompatible plugin
    Ret ret = m_scenario->registerFailedPlugin(pluginPath, expectedPluginInfo.errorCode);

    // [THEN] The plugin successfully registered
    EXPECT_TRUE(ret);
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, RegisterNewPlugins_ValidateFalsePersistsDiscoveredOnly)
{
    // [GIVEN] Two paths recorded without validating ("Skip this time")
    paths_t paths {
        "/some/path/AAA.vst3",
        "/some/path/BBB.vst3",
    };

    // [THEN] One removePluginsAtPath per path, then a single batch
    // registerPlugins of the Discovered placeholders.
    EXPECT_CALL(*m_knownPlugins, removePluginsAtPath(io::path_t("/some/path/AAA.vst3")))
    .WillOnce(Return(make_ok()));
    EXPECT_CALL(*m_knownPlugins, removePluginsAtPath(io::path_t("/some/path/BBB.vst3")))
    .WillOnce(Return(make_ok()));
    EXPECT_CALL(*m_knownPlugins, registerPlugins(_))
    .WillOnce(Return(make_ok()));

    // [THEN] No out-of-process validation: no subprocess, no progress dialog.
    EXPECT_CALL(*m_process, execute(_, _))
    .Times(0);
    EXPECT_CALL(*m_interactive, showProgress(_, _))
    .Times(0);

    // [THEN] Final load() resyncs the register.
    EXPECT_CALL(*m_knownPlugins, load())
    .WillOnce(Return(make_ok()));

    // [WHEN] Register with validation deferred
    EXPECT_TRUE(m_scenario->registerNewPlugins(paths, /*validate*/ false));
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, RegisterNewPlugins_NoPerIterationClobber)
{
    // [GIVEN] Three new plugin paths to scan
    paths_t paths {
        "/some/path/AAA.vst3",
        "/some/path/BBB.vst3",
        "/some/path/CCC.vst3",
    };

    // [THEN] removePluginsAtPath is called exactly once per path — inside
    // persistDiscoveredPlaceholders only. The subprocess loop in
    // processPluginsRegistration must NOT call it again on the main process's
    // register; doing so would write the main's stale in-memory state to disk
    // and clobber the Validated entries previous subprocesses had written.
    // (The subprocess-side clearing now lives in registerPlugin /
    // registerFailedPlugin and runs against a freshly-loaded register.)
    EXPECT_CALL(*m_knownPlugins, removePluginsAtPath(_))
    .Times(3)
    .WillRepeatedly(Return(make_ok()));

    EXPECT_CALL(*m_knownPlugins, registerPlugins(_))
    .Times(1)
    .WillOnce(Return(make_ok()));

    // [THEN] One subprocess invocation per path
    for (const path_t& path : paths) {
        std::vector<std::string> args = { "--register-audio-plugin", path.toStdString() };
        EXPECT_CALL(*m_process, execute(m_appPath, args))
        .WillOnce(Return(0));
    }

    EXPECT_CALL(*m_interactive, showProgress(_, _))
    .Times(1);

    EXPECT_CALL(*m_knownPlugins, load())
    .WillOnce(Return(make_ok()));

    // [WHEN] Register with validation enabled
    EXPECT_TRUE(m_scenario->registerNewPlugins(paths, /*validate*/ true));
}

TEST_F(AudioPlugins_RegisterAudioPluginsScenarioTest, RegisterNewPlugins_TrailingSlashPathGetsNonEmptyId)
{
    // [GIVEN] An LV2 "<uri>@<bundle>/" composite path: completeBasename() is
    // empty, and load() would reject a row with an empty id.
    const path_t lv2Path = "http://calf.sourceforge.net/plugins/Analyzer@file:///usr/lib/lv2/calf.lv2/";
    paths_t paths { lv2Path };

    ON_CALL(*m_knownPlugins, removePluginsAtPath(_))
    .WillByDefault(Return(make_ok()));
    ON_CALL(*m_knownPlugins, load())
    .WillByDefault(Return(make_ok()));

    // [THEN] Capture the persisted Discovered placeholder
    AudioPluginInfoList persisted;
    EXPECT_CALL(*m_knownPlugins, registerPlugins(_))
    .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&persisted), Return(make_ok())));

    // [WHEN] Register with validation deferred (persistDiscoveredPlaceholders only)
    EXPECT_TRUE(m_scenario->registerNewPlugins(paths, /*validate*/ false));

    // [THEN] The placeholder id falls back to the full path, not empty
    ASSERT_EQ(persisted.size(), size_t(1));
    EXPECT_FALSE(persisted.front().meta.id.empty());
    EXPECT_EQ(persisted.front().meta.id, lv2Path.toStdString());
    EXPECT_EQ(persisted.front().state, AudioPluginState::Discovered);
}
