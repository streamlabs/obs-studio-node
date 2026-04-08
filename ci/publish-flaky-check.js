const fs = require('fs');
const path = require('path');
const https = require('https');

const DEFAULT_API_URL = 'https://api.github.com';
const DEFAULT_MAX_SUMMARY_ITEMS = 20;

function readFlakyTests(reportPath) {
  if (!reportPath || !fs.existsSync(reportPath)) {
    return [];
  }

  const rawReport = fs.readFileSync(reportPath, 'utf8');
  if (!rawReport.trim()) {
    return [];
  }

  return JSON.parse(rawReport);
}

function summarizeFlakyTests(flakyTests, maxItems) {
  const visibleTests = flakyTests.slice(0, maxItems);
  const summaryLines = visibleTests.map(test => {
    const fileLabel = test.file ? ` [${test.file}]` : '';
    return `- ${test.fullTitle} (${test.attempts} attempts)${fileLabel}`;
  });

  if (flakyTests.length > visibleTests.length) {
    summaryLines.push(`- ...and ${flakyTests.length - visibleTests.length} more`);
  }

  return summaryLines.join('\n');
}

function buildCheckOutput(flakyTests, testStepConclusion) {
  if (testStepConclusion === 'failure') {
    const summaryParts = [
      'The primary test step failed. See the job logs for the failing assertion details.'
    ];

    if (flakyTests.length > 0) {
      summaryParts.push('');
      summaryParts.push('These tests did pass after retrying before the job failed:');
      summaryParts.push(summarizeFlakyTests(flakyTests, DEFAULT_MAX_SUMMARY_ITEMS));
    }

    return {
      conclusion: 'failure',
      title: 'Test job failed',
      summary: summaryParts.join('\n')
    };
  }

  if (flakyTests.length === 0) {
    return {
      conclusion: 'success',
      title: 'No flaky tests detected',
      summary: 'All tests passed on their first attempt.'
    };
  }

  return {
    conclusion: 'neutral',
    title: `${flakyTests.length} test(s) passed after retry`,
    summary: [
      'The primary test job succeeded, but these tests only passed after retrying:',
      '',
      summarizeFlakyTests(flakyTests, DEFAULT_MAX_SUMMARY_ITEMS)
    ].join('\n')
  };
}

function createCheckRun({
  apiUrl,
  token,
  repository,
  sha,
  name,
  detailsUrl,
  output
}) {
  return new Promise((resolve, reject) => {
    const requestUrl = new URL(`/repos/${repository}/check-runs`, apiUrl);
    const payload = JSON.stringify({
      name,
      head_sha: sha,
      status: 'completed',
      conclusion: output.conclusion,
      details_url: detailsUrl,
      output: {
        title: output.title,
        summary: output.summary
      }
    });

    const request = https.request(
      requestUrl,
      {
        method: 'POST',
        headers: {
          'accept': 'application/vnd.github+json',
          'authorization': `Bearer ${token}`,
          'content-type': 'application/json',
          'content-length': Buffer.byteLength(payload),
          'user-agent': 'obs-studio-node-flaky-check-reporter',
          'x-github-api-version': '2022-11-28'
        }
      },
      response => {
        let responseBody = '';
        response.setEncoding('utf8');
        response.on('data', chunk => {
          responseBody += chunk;
        });
        response.on('end', () => {
          if (response.statusCode >= 200 && response.statusCode < 300) {
            resolve();
            return;
          }

          reject(
            new Error(
              `GitHub API request failed (${response.statusCode}): ${responseBody}`
            )
          );
        });
      }
    );

    request.on('error', reject);
    request.write(payload);
    request.end();
  });
}

async function main() {
  const reportPath = process.env.FLAKY_REPORT_PATH;
  const checkName = process.env.FLAKY_CHECK_NAME;
  const token = process.env.GITHUB_TOKEN;
  const repository = process.env.GITHUB_REPOSITORY;
  const sha = process.env.GITHUB_SHA;
  const apiUrl = process.env.GITHUB_API_URL || DEFAULT_API_URL;
  const serverUrl = process.env.GITHUB_SERVER_URL || 'https://github.com';
  const runId = process.env.GITHUB_RUN_ID;
  const testStepConclusion = process.env.TEST_STEP_CONCLUSION || 'success';

  if (!token) {
    throw new Error('GITHUB_TOKEN is required to publish the flaky test check.');
  }

  if (!repository || !sha || !checkName) {
    throw new Error('GITHUB_REPOSITORY, GITHUB_SHA, and FLAKY_CHECK_NAME are required.');
  }

  const flakyTests = readFlakyTests(reportPath);
  const output = buildCheckOutput(flakyTests, testStepConclusion);
  const detailsUrl = runId
    ? `${serverUrl}/${repository}/actions/runs/${runId}`
    : undefined;

  await createCheckRun({
    apiUrl,
    token,
    repository,
    sha,
    name: checkName,
    detailsUrl,
    output
  });

  console.log(
    `Published "${checkName}" with conclusion "${output.conclusion}"` +
      ` from ${flakyTests.length} flaky test(s).`
  );
}

main().catch(error => {
  console.error(error.message);
  process.exitCode = 1;
});
