import fs = require('fs');
import path = require('path');
import dotenv = require('dotenv');

let loaded = false;

export function loadTestEnv() {
    if (loaded) {
        return;
    }

    loaded = true;

    const envPaths = [
        path.join(process.cwd(), '.env'),
        path.join(__dirname, '..', '..', '..', '.env'),
    ].filter((envPath, index, paths) => paths.indexOf(envPath) === index);

    const envPath = envPaths.find(candidate => fs.existsSync(candidate));
    if (!envPath) {
        return;
    }

    dotenv.config({ path: envPath, override: false, quiet: true });
}
