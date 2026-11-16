import 'mocha';
import { expect } from 'chai';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSOutputSignal, EOBSOutputType, EOBSSettingsCategories } from '../util/obs_enums';
import { getAudioStreamBitrates, getMeanVolumeDb, getVideoFrameRgb } from '../util/media_probe';

describe('Legacy replay save without an OBS hotkey', function () {
    let obs: OBSHandler;
    let directory: string;

    beforeEach(function () {
        deleteConfigFiles();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), 'osn-replay-save-'));
        obs = new OBSHandler('legacy-replay-save');
        obs.connectOutputSignals();
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Simple');
        obs.setSetting(EOBSSettingsCategories.Output, 'StreamEncoder', obs.os === 'win32' ? 'x264' : 'obs_x264');
        obs.setSetting(EOBSSettingsCategories.Output, 'FilePath', directory);
    });

    afterEach(function () {
        if (obs) obs.shutdown();
        obs = null;
        deleteConfigFiles();
        if (directory) fs.rmSync(directory, { recursive: true, force: true });
    });

    it('Saves decodable video and audio through the existing JavaScript method', async function () {
        expect(osn.NodeObs.OBS_API_QueryHotkeys().some(hotkey => hotkey.HotkeyName === 'ReplayBuffer.Save')).to.equal(false);
        osn.NodeObs.OBS_service_processReplayBufferHotkey();
        expect(osn.NodeObs.OBS_service_getLastReplay()).to.equal('');
        osn.NodeObs.OBS_service_startReplayBuffer();
        let isStarted = false;
        try {
            const started = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Start);
            isStarted = started.code === osn.EOutputCode.Success;
            expect(started.code).to.equal(osn.EOutputCode.Success);
            const initialFrames = obs.defaultVideoContext.encodedFrames;
            const deadline = Date.now() + 30000;
            while (obs.defaultVideoContext.encodedFrames - initialFrames < 60 && Date.now() < deadline) {
                await sleep(50);
            }
            expect(obs.defaultVideoContext.encodedFrames - initialFrames, 'Replay video did not become ready')
                .to.be.at.least(60);
            osn.NodeObs.OBS_service_processReplayBufferHotkey();
            const saved = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Wrote);
            expect(saved.code).to.equal(osn.EOutputCode.Success);
            const file: string = osn.NodeObs.OBS_service_getLastReplay();
            expect(path.normalize(path.dirname(file))).to.equal(path.normalize(directory));
            expect(fs.statSync(file).size).to.be.greaterThan(0);
            expect(getAudioStreamBitrates(file)).not.to.be.empty;
            expect(getMeanVolumeDb(file)).to.be.at.most(0);
            const video = obs.defaultVideoContext.video;
            expect(getVideoFrameRgb(file, 0, video.outputWidth, video.outputHeight).length)
                .to.equal(video.outputWidth * video.outputHeight * 3);
        } finally {
            if (isStarted) {
                osn.NodeObs.OBS_service_stopReplayBuffer(true);
                const stopped = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stop);
                expect(stopped.code).to.equal(osn.EOutputCode.Success);
            }
        }
    });
});
