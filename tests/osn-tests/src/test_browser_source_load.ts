import 'mocha';
import { expect } from 'chai';
import * as fs from 'fs';
import * as http from 'http';
import * as path from 'path';
import { execSync } from 'child_process';
import { AddressInfo } from 'net';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';

const testName = 'browser-source-load';
const loadTimeoutMs = 30000;

interface IRequestRecord {
    path: string;
    time: number;
}

const pageHtml = '<!DOCTYPE html><html><body><script src="/app.js"></script></body></html>';
const appJs = "window.obsstudio.getControlLevel(level => fetch('/report?level=' + level));";

describe(testName, () => {
    let obs: OBSHandler;
    let server: http.Server;
    let pageUrl: string;
    let requests: IRequestRecord[] = [];
    let scene: osn.IScene;
    let source: osn.IInput;
    let hasTestFailed = false;

    const startTime = Date.now();

    function count(requestPath: string): number {
        return requests.filter((r) => r.path.split('?')[0] === requestPath).length;
    }

    function describeRequests(): string {
        if (requests.length === 0) {
            return 'no requests seen';
        }
        return requests.map((r) => `+${r.time - startTime}ms ${r.path}`).join(', ');
    }

    async function waitFor(condition: () => boolean, timeoutMs: number): Promise<boolean> {
        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline) {
            if (condition()) {
                return true;
            }
            await sleep(100);
        }
        return condition();
    }

    function runDiagnostics() {
        if (process.platform !== 'darwin') {
            return;
        }
        try {
            const ps = execSync('ps -axo pid,ppid,command', { encoding: 'utf8' });
            const lines = ps.split('\n').filter((line) => line.includes('obs64'));
            logInfo(testName, 'obs64 processes:\n' + (lines.join('\n') || '(none)'));
        } catch (e) {
            logInfo(testName, 'ps failed: ' + e);
        }

        const frameworkDirs = [
            path.join(osn.wd, 'Frameworks'),
            path.join(osn.wd, '..', '..', 'Contents', 'Frameworks'),
        ];
        for (const dir of frameworkDirs) {
            const resolved = path.resolve(dir);
            if (fs.existsSync(resolved)) {
                logInfo(testName, `${resolved}:\n` + fs.readdirSync(resolved).join('\n'));
            } else {
                logInfo(testName, `${resolved} does not exist`);
            }
        }
    }

    function failWithStage(stage: string, reached: string): never {
        runDiagnostics();
        throw new Error(`Browser source did not request ${stage}; reached stage: ${reached}. Requests: ${describeRequests()}`);
    }

    async function expectLoadSequence(documentCount: number) {
        if (!(await waitFor(() => count('/') >= documentCount, loadTimeoutMs))) {
            failWithStage('the document', 'none');
        }
        if (!(await waitFor(() => count('/app.js') >= documentCount, loadTimeoutMs))) {
            failWithStage('/app.js', 'document');
        }
        if (!(await waitFor(() => count('/report') >= documentCount, loadTimeoutMs))) {
            failWithStage('/report', 'app.js');
        }
        expect(requests.filter(r => r.path === '/report?level=1')).to.have.length.at.least(documentCount);
    }

    before(async function () {
        logInfo(testName, 'Starting ' + testName + ' tests');

        server = http.createServer((req, res) => {
            const reqPath = req.url || '';
            requests.push({ path: reqPath, time: Date.now() });
            const pathname = reqPath.split('?')[0];
            if (pathname === '/') {
                res.writeHead(200, { 'Content-Type': 'text/html', 'Cache-Control': 'no-store' });
                res.end(pageHtml);
            } else if (pathname === '/app.js') {
                res.writeHead(200, { 'Content-Type': 'application/javascript', 'Cache-Control': 'no-store' });
                res.end(appJs);
            } else if (pathname === '/codecs.html' || pathname === '/sample.mp4') {
                const video = pathname === '/sample.mp4';
                res.writeHead(200, { 'Content-Type': video ? 'video/mp4' : 'text/html', 'Cache-Control': 'no-store' });
                res.end(fs.readFileSync(path.join(__dirname, '..', 'media', video ? 'bigbuckbunny.mp4' : 'browser-codecs.html')));
            } else {
                res.writeHead(200, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' });
                res.end('ok');
            }
        });
        await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve));
        pageUrl = `http://127.0.0.1:${(server.address() as AddressInfo).port}/`;
        logInfo(testName, 'Fixture server listening at ' + pageUrl);

        deleteConfigFiles();
        obs = new OBSHandler(testName);
    });

    after(async function () {
        try {
            if (source) {
                source.release();
                source = null;
            }
            if (scene) {
                scene.getItems().forEach((item) => item.remove());
                osn.Global.setOutputSource(0, null as any);
                scene.release();
                scene = null;
            }

            if (obs) {
                obs.shutdown();

                if (hasTestFailed === true) {
                    logInfo(testName, 'One or more test cases failed. Uploading cache');
                    await obs.uploadTestCache();
                }
            }
        } finally {
            await new Promise<void>((resolve) => server.close(() => resolve()));
        }

        obs = null;
        deleteConfigFiles();
        logInfo(testName, 'Finished ' + testName + ' tests');
        logEmptyLine();
    });

    afterEach(function () {
        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    it('Uses a Windows GUI executable for the server and browser helpers', function () {
        if (process.platform !== 'win32') this.skip();

        const image = fs.readFileSync(path.join(osn.wd, 'obs64.exe'));
        const peOffset = image.readUInt32LE(0x3c);
        const optionalHeader = peOffset + 4 + 20; // PE signature and COFF file header
        const subsystem = image.readUInt16LE(optionalHeader + 68);
        expect(subsystem, 'obs64.exe PE subsystem').to.equal(2); // IMAGE_SUBSYSTEM_WINDOWS_GUI
    });

    it('Browser source loads its page after the first source is released during startup', async function () {
        this.timeout(120000);

        // Mocha retries rerun this test without rerunning before(), so create only once.
        if (!source) {
            const firstSource = osn.InputFactory.create('browser_source', 'browser-startup-source', { url: 'about:blank' });
            expect(firstSource).to.exist;
            firstSource.release();

            scene = osn.SceneFactory.create('browser-load-scene');
            source = osn.InputFactory.create('browser_source', 'browser-load-source', {
                url: pageUrl,
                width: 640,
                height: 360,
                webpage_control_level: 1,
            });
            expect(source).to.not.equal(undefined, 'Failed to create browser_source');
            scene.add(source);
            osn.Global.setOutputSource(0, scene);
        }

        await expectLoadSequence(1);
    });

    it('Browser source reloads on refresh', async function () {
        this.timeout(120000);
        expect(source, 'Browser source was not created by the previous test').to.exist;

        const documentsBefore = count('/');
        const refreshButton: any = source.properties.get('refreshnocache');
        if (refreshButton && typeof refreshButton.buttonClicked === 'function') {
            logInfo(testName, 'Refreshing via refreshnocache button');
            refreshButton.buttonClicked(source);
        } else {
            logInfo(testName, 'refreshnocache button not exposed, refreshing by changing url');
            source.update({ url: pageUrl + '?refresh=1' });
        }

        if (!(await waitFor(() => count('/') > documentsBefore, loadTimeoutMs))) {
            runDiagnostics();
            throw new Error(`Browser source did not request the document again after refresh. Requests: ${describeRequests()}`);
        }
    });

    it('Browser source decodes H.264 video and AAC audio from an MP4', async function () {
        this.timeout(60000);
        const attempt = Date.now();
        const reportPath = '/codec-result/' + attempt;
        const media = osn.InputFactory.create('browser_source', 'browser-codecs', {
            url: pageUrl + 'codecs.html?attempt=' + attempt,
            width: 640,
            height: 360,
        });
        expect(media).to.exist;
        try {
            osn.Global.setOutputSource(0, media);
            if (!(await waitFor(() => count(reportPath) > 0, loadTimeoutMs))) {
                runDiagnostics();
                throw new Error('No browser media result. Requests: ' + describeRequests());
            }
            const report = new URL(requests.find(r => r.path.startsWith(reportPath + '?'))!.path, pageUrl);
            logInfo(testName, 'Browser media result: ' + report.searchParams.toString());
            expect(report.searchParams.get('video'), report.search).to.equal('1280x720');
            expect(report.searchParams.get('audio'), report.search).to.equal('audible');
        } finally {
            osn.Global.setOutputSource(0, scene || null);
            media.release();
        }
    });
});
