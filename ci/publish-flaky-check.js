const https = require('https');

const DEFAULT_API_URL = 'https://api.github.com';

function parseFlakyCount(rawCount) {
  if (!rawCount) {
    return 0;
  }

  const parsedCount = Number.parseInt(rawCount, 10);
  if (!Number.isFinite(parsedCount) || parsedCount < 0) {
    return 0;
  }

  return parsedCount;
}

function normalizeSummary(rawSummary) {
  if (typeof rawSummary !== 'string') {
    return '';
  }

  return rawSummary.trim();
}

function buildCheckOutput(flakyCount, flakySummary, testStepConclusion) {
  if (testStepConclusion === 'failure') {
    const summaryParts = [
      'The primary test step failed. See the job logs for the failing assertion details.',
      '',
      'This flaky check is informational only.'
    ];

    if (flakyCount > 0) {
      summaryParts.push('');
      summaryParts.push(
        `Detected ${flakyCount} test(s) that passed after retrying before the job failed:`
      );

      if (flakySummary) {
        summaryParts.push(flakySummary);
      }
    }

    return {
      conclusion: 'neutral', // This is done intentionally to not clutter the failed jobs list.
      title: 'Test job failed',
      summary: summaryParts.join('\n')
    };
  }

  if (flakyCount === 0) {
    return {
      conclusion: 'success',
      title: 'No flaky tests detected',
      summary: 'All tests passed on their first attempt.'
    };
  }

  return {
    conclusion: 'neutral',
    title: `${flakyCount} test(s) passed after retry`,
    summary: [
      'The primary test job succeeded, but these tests only passed after retrying:',
      '',
      flakySummary || `Detected ${flakyCount} flaky test(s).`
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
  const checkName = process.env.FLAKY_CHECK_NAME;
  const token = process.env.GITHUB_TOKEN;
  const repository = process.env.GITHUB_REPOSITORY;
  // pull_request workflows run against a synthetic merge commit; using the PR head
  // SHA keeps the custom check navigable from the PR Checks tab.
  const sha = process.env.FLAKY_CHECK_SHA || process.env.GITHUB_SHA;
  const apiUrl = process.env.GITHUB_API_URL || DEFAULT_API_URL;
  const serverUrl = process.env.GITHUB_SERVER_URL || 'https://github.com';
  const runId = process.env.GITHUB_RUN_ID;
  const testStepConclusion = process.env.TEST_STEP_CONCLUSION || 'success';
  // The reporter publishes bounded flaky metadata through GITHUB_OUTPUT so this
  // step does not need to read raw test artifacts before calling the Checks API.
  const flakyCount = parseFlakyCount(process.env.FLAKY_TEST_COUNT);
  const flakySummary = normalizeSummary(process.env.FLAKY_TEST_SUMMARY);

  if (!token) {
    throw new Error('GITHUB_TOKEN is required to publish the flaky test check.');
  }

  if (!repository || !sha || !checkName) {
    throw new Error('GITHUB_REPOSITORY, FLAKY_CHECK_SHA or GITHUB_SHA, and FLAKY_CHECK_NAME are required.');
  }

  const output = buildCheckOutput(flakyCount, flakySummary, testStepConclusion);
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
      ` from ${flakyCount} flaky test(s).`
  );
}

main().catch(error => {
  console.error(error.message);
  process.exitCode = 1;
});
