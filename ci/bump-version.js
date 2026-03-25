const fs = require('fs');
const path = require('path');

function log_info(msg) {
    console.log(msg);
}

function log_error(msg) {
    console.error(`ERROR: ${msg}`);
}

const newVersion = process.argv[2];
const packagePath = process.argv[3];
log_info('Script Arguments: ' + process.argv.splice(2));

try {
    const file = path.join(process.cwd(), packagePath, 'package.json');
    const jsonData = fs.readFileSync(file);
    const root = JSON.parse(jsonData.toString());
    const currentVersion = root['version'];

    log_info('Current version: ' + currentVersion);
    log_info('New version: ' + newVersion);

    root['version'] = newVersion;
    log_info('Bumping version number...');
    fs.writeFileSync(file, JSON.stringify(root, null, 2));
} catch (error) {
    log_error(error);
    process.exit(1);
}