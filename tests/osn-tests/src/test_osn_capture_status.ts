import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';

describe('osn-capture-status', function () {
    let obs: OBSHandler;
    before(function () {
        if (process.platform !== 'win32') this.skip();
        obs = new OBSHandler('osn-capture-status');
    });
    after(() => { if (obs) obs.shutdown(); });

    it('retains per-source errors across IPC reads and clears them on removal', async () => {
        const sources: osn.IInput[] = [];
        const create = (type: string, name: string, settings: osn.ISettings) => {
            const source = osn.InputFactory.create(type, name, settings);
            sources.push(source);
            return source;
        };
        const snapshot = (name: string) => osn.InputFactory.getCaptureStatuses().find(s => s.sourceName === name);
        const waitFor = async (name: string, state: osn.ICaptureStatus['state'], afterGeneration = 0) => {
            for (let i = 0; i < 100; ++i) {
                const status = snapshot(name);
                if (status && status.state === state && status.generation > afterGeneration) return status;
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            throw new Error(`Capture status did not reach ${state}: ${JSON.stringify(snapshot(name))}`);
        };
        try {
            create('color_source', 'not-a-camera', {});
            create('dshow_input', 'no-device-selected', {});
            create('dshow_input', 'inactive-camera', { active: false, video_device_id: 'Missing:nonexistent' });
            const camera = create('dshow_input', 'missing-camera', {
                video_device_id: 'Missing:\\\\?\\usb#22vid_ffff&pid_ffff#22osn-missing#22{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global', active: true,
            });
            const error = await waitFor('missing-camera', 'error');
            expect(error.reason).to.equal('device_not_found');
            expect(error.errorCode).to.equal(0);
            expect(error.deviceName).to.equal('Missing');
            expect(snapshot('missing-camera')).to.deep.equal(error);
            // Cross at least one native presence-check interval. An absent interface must
            // retain the error without repeatedly restarting capture or clearing the warning.
            await new Promise(resolve => setTimeout(resolve, 2300));
            expect(snapshot('missing-camera')).to.deep.equal(error);
            expect(snapshot('not-a-camera')).to.equal(undefined);
            expect((await waitFor('no-device-selected', 'inactive')).reason).to.equal('');
            expect(snapshot('inactive-camera').state).to.equal('inactive');

            camera.update({ video_device_id: 'Other:osn-capture-status-other' });
            const updated = await waitFor('missing-camera', 'error', error.generation);
            expect(updated.generation).to.be.greaterThan(error.generation);
            expect(updated.deviceName).to.equal('Other');
            // A non-PnP device with unknown presence must not be diagnosed as unplugged
            // or enter an unbounded automatic retry loop merely because enumeration failed.
            expect(updated.reason).to.equal('start_failed');
            await new Promise(resolve => setTimeout(resolve, 2300));
            expect(snapshot('missing-camera')).to.deep.equal(updated);
            (camera.properties.get('activate') as osn.IButtonProperty).buttonClicked(camera);
            const inactive = await waitFor('missing-camera', 'inactive', updated.generation);
            expect(inactive.reason).to.equal('');
            expect(inactive.errorCode).to.equal(0);
            await new Promise(resolve => setTimeout(resolve, 2300));
            expect(snapshot('missing-camera')).to.deep.equal(inactive);
            camera.remove();
            sources.splice(sources.indexOf(camera), 1);
            expect(snapshot('missing-camera')).to.equal(undefined);
            // Snapshots remain independent values after the native source is removed.
            expect(error.deviceName).to.equal('Missing');
        } finally {
            for (const source of sources.reverse()) source.release();
        }
    });
});
