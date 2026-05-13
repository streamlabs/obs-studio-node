## About
Analyzes the long_calls.txt file produced by obs-studio-node and helps identify which methods take the longest.

## Detailed breakdown
* Mass download hundreds of latest user caches from AWS bucket streamlabs-obs-user-cache
* Extracts long_calls.txt and load its data into a database, with minimal filtering; for example, avoiding counting files from the same user multiple times.
* Run some basic statistics to identify which IPC calls cause the most trouble and are worth checking if we want to improve performance.

## Requirements
Node 22, Access to AWS S3 bucket

## How to run
```
yarn install
yarn build
node dist/long-calls-analyzer.js --age 1w
```