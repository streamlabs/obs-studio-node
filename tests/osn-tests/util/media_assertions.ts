import { expect } from 'chai';

export type RGB = [number, number, number];

export interface IRgbFrame {
    data: Buffer;
    width: number;
    height: number;
}

export interface IRgbRegion {
    x: number;
    y: number;
    width?: number;
    height?: number;
}

/**
 * Asserts the rounded mean color of a region in an unscaled RGB24 frame.
 * Region coordinates start at the top-left pixel and default to an 8x8 patch.
 * Tolerance applies independently to each RGB channel and defaults to 25.
 * Throws RangeError for invalid dimensions, buffer length, region bounds or tolerance;
 * throws a Chai AssertionError when the mean color is outside the tolerance.
 */
export function expectRegionColor(frame: IRgbFrame, region: IRgbRegion, expected: RGB,
    description: string, tolerance = 25): void {
    const { data, width: frameWidth, height: frameHeight } = frame;
    const { x, y, width = 8, height = 8 } = region;
    if (!Number.isSafeInteger(frameWidth) || frameWidth <= 0 ||
        !Number.isSafeInteger(frameHeight) || frameHeight <= 0) {
        throw new RangeError(`${description}: frame dimensions must be positive integers`);
    }
    const expectedBytes = frameWidth * frameHeight * 3;
    if (data.length !== expectedBytes) {
        throw new RangeError(`${description}: expected ${expectedBytes} RGB24 bytes, got ${data.length}`);
    }
    if (!Number.isSafeInteger(x) || x < 0 || !Number.isSafeInteger(y) || y < 0 ||
        !Number.isSafeInteger(width) || width <= 0 || !Number.isSafeInteger(height) || height <= 0 ||
        width > frameWidth - x || height > frameHeight - y) {
        throw new RangeError(`${description}: region ${x},${y} ${width}x${height} must fit within ${frameWidth}x${frameHeight}`);
    }
    if (!Number.isFinite(tolerance) || tolerance < 0) {
        throw new RangeError(`${description}: color tolerance must be finite and non-negative`);
    }

    const sum: RGB = [0, 0, 0];
    for (let dy = 0; dy < height; dy++) {
        for (let dx = 0; dx < width; dx++) {
            const offset = ((y + dy) * frameWidth + x + dx) * 3;
            for (let channel = 0; channel < 3; channel++) sum[channel] += data[offset + channel];
        }
    }
    const actual = sum.map(value => Math.round(value / (width * height)));
    expected.forEach((value, channel) => {
        expect(actual[channel], `${description}: expected ${expected}, got ${actual}`).to.be.closeTo(value, tolerance);
    });
}
