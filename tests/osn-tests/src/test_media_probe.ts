import 'mocha';
import { expect } from 'chai';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import { getVideoFrameRgb } from '../util/media_probe';

describe('Recorded RGB frame decoding', function () {
    this.timeout(15000);
    let directory: string;

    before(() => {
        directory = fs.mkdtempSync(path.join(os.tmpdir(), 'osn-media-probe-'));
    });

    after(() => {
        if (directory) fs.rmSync(directory, { recursive: true, force: true });
    });

    function createFrame(width: number, height: number, pixels = Buffer.alloc(width * height * 3, 255)): string {
        const file = path.join(directory, `${width}x${height}.ppm`);
        fs.writeFileSync(file, Buffer.concat([Buffer.from(`P6\n${width} ${height}\n255\n`), pixels]));
        return file;
    }

    it('preserves RGB pixels, including whitespace and hash bytes at the start of the raster', () => {
        const pixels = Buffer.from([10, 13, 32, 35, 0, 255, 9, 11, 12, 255, 0, 1]);
        expect(getVideoFrameRgb(createFrame(2, 2, pixels), 0, 2, 2)).to.deep.equal(pixels);
    });

    for (const [width, height] of [[180, 320], [640, 90]]) {
        it(`rejects a ${width}x${height} frame when 320x180 is requested, despite equal pixel counts`, () => {
            const file = createFrame(width, height);
            expect(() => getVideoFrameRgb(file, 0, 320, 180)).to.throw(`decoded ${width}x${height}`);
        });
    }

    it('rejects a seek that produces no frame', () => {
        expect(() => getVideoFrameRgb(createFrame(2, 2), 5, 2, 2)).to.throw();
    });
});
