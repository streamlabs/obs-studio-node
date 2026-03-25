import * as fs from 'fs';
import * as path from 'path';
import { S3Client } from '@aws-sdk/client-s3';
import { Upload } from '@aws-sdk/lib-storage';
import * as archiver from 'archiver';
import { logInfo } from '../util/logger';

export class CacheUploader {
    private testName: string;
    private cachePath: string;
    private dateStr = new Date().toISOString();
    private releaseName: string;

    constructor(test:string, path: string) {
        this.testName = test;
        this.cachePath = path;

        if ("RELEASE_NAME" in process.env) {
            this.releaseName = process.env.RELEASE_NAME;
        } else {
            this.releaseName = 'local';
        }
    }

    uploadCache() {
        return new Promise((resolve, reject) => {
            const cacheFile = path.join(path.normalize(__dirname), '..', this.testName + '-test-cache-' + this.releaseName + '.zip');
            const output = fs.createWriteStream(cacheFile);
            const archive = archiver('zip', { zlib: { level: 9 } });

            if (!("OSN_ACCESS_KEY_ID" in process.env)) {
                return reject('Failed to upload cache. The environment variable OSN_ACCESS_KEY_ID does not exist');
            }

            if (!("OSN_SECRET_ACCESS_KEY" in process.env)) {
                return reject('Failed to upload cache. The environment variable OSN_SECRET_ACCESS_KEY does not exist');
            }

            output.on('close', () => {
                const file = fs.createReadStream(cacheFile);
                const keyname = this.dateStr + '-' + this.testName + '-test-cache-' + this.releaseName + '.zip';
        
                // This is a restricted cache upload account
                const s3 = new S3Client({
                    region: 'us-east-2',
                    credentials: {
                        accessKeyId: process.env.OSN_ACCESS_KEY_ID,
                        secretAccessKey: process.env.OSN_SECRET_ACCESS_KEY,
                    },
                });

                const upload = new Upload({
                    client: s3,
                    params: {
                        Bucket: 'obs-studio-node-tests-cache',
                        Key: keyname,
                        Body: file,
                    },
                });

                upload.done().then(() => {
                    logInfo(this.testName, 'Finished uploading cache');
                    logInfo(this.testName, keyname);
                    resolve(keyname);
                }).catch((err) => reject(err));
            });

            // Modify the stream key in service.json in a reversible way when uploading user caches
            // to avoid accidentally streaming to a customer's account when debugging
            const serviceJson = JSON.parse(
                fs.readFileSync(path.join(this.cachePath, 'service.json'), 'utf8'),
            );
            serviceJson.settings.key = `[delete_me]${serviceJson.settings.key}`;
            fs.writeFileSync(
                path.join(this.cachePath, 'service-protected.json'),
                JSON.stringify(serviceJson, null, 2),
            );

            archive.pipe(output);
            this.addDirIfExists(archive, 'node-obs');
            this.addDirIfExists(archive, 'SceneConfigs');
            this.addDirIfExists(archive, 'SceneCollections');
            this.addDirIfExists(archive, 'Streamlabels');
            this.addFileIfExists(archive, 'log.log');
            this.addFileIfExists(archive, 'crash-handler.log');
            this.addFileIfExists(archive, 'crash-handler.log.old');
            archive.file(path.join(this.cachePath, 'basic.ini'), { name: 'basic.ini' });
            archive.file(path.join(this.cachePath, 'global.ini'), { name: 'global.ini' });
            archive.file(path.join(this.cachePath, 'service-protected.json'), { name: 'service.json' });
            archive.file(path.join(this.cachePath, 'streamEncoder.json'), { name: 'streamEncoder.json' });
            archive.file(path.join(this.cachePath, 'recordEncoder.json'), { name: 'recordEncoder.json' });
            archive.file(path.join(this.cachePath, 'window-state.json'), { name: 'window-state.json' });
            archive.finalize();
        });
    }

    private addDirIfExists(archive: archiver.Archiver, name: string) {
        const dirPath = path.join(this.cachePath, name);
    
        if (fs.existsSync(dirPath)) {
        archive.directory(dirPath, name);
        }
    }
    
    private addFileIfExists(archive: archiver.Archiver, name: string) {
        const dirPath = path.join(this.cachePath, name);
    
        if (fs.existsSync(dirPath)) {
          archive.file(dirPath, { name });
        }
      }
}