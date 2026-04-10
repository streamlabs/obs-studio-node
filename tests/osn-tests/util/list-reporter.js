var fs = require('fs');
var path = require('path');
var mocha = require('mocha');
var retryContext = require('./retry_context.ts');

var DEFAULT_MAX_SUMMARY_ITEMS = 20;
var DEFAULT_MAX_TEXT_LENGTH = 200;
var INTENTIONAL_FLAKY_FAILURE_PREFIX = 'Intentional flaky failure for CI validation:';

function ListReporter(runner) {
    mocha.reporters.Base.call(this, runner);
    var passes = 0;
    var failures = 0;
    var retries = 0;
    var failedTestCases = [];
    var flakyTestCases = [];
    var testLine = "";

    function getRetryCount(test) {
        if (typeof test.currentRetry === 'function') {
            return test.currentRetry();
        }

        return test.currentRetry || 0;
    }

    function getMaxRetries(test) {
        if (typeof test.retries === 'function') {
            return test.retries();
        }

        return test._retries || 0;
    }

    function getTestLineFromError(err) {
        if (!err || !err.stack) {
            return "";
        }

        var regex = /(?<=src\\)(.*?)(?=\n)/;
        var str = err.stack.match(regex);
        if (str != null) {
            return str[0].substr(0, str[0].lastIndexOf(":"));
        }

        return "";
    }

    function normalizeOutputText(value, maxLength) {
        if (typeof value !== 'string') {
            return '';
        }

        var normalized = value.replace(/[\r\n\t]/g, ' ').replace(/\s+/g, ' ').trim();
        if (normalized.length <= maxLength) {
            return normalized;
        }

        return normalized.substr(0, maxLength - 3) + '...';
    }

    function normalizeTestFilePath(filePath) {
        if (typeof filePath !== 'string' || !filePath.trim()) {
            return '';
        }

        var relativePath = path.relative(process.cwd(), filePath);
        var normalizedPath = relativePath && !relativePath.startsWith('..')
            ? relativePath
            : filePath;

        return normalizedPath.split(path.sep).join('/');
    }

    function isIntentionalCiValidationFailure(err) {
        return Boolean(
            err &&
            typeof err.message === 'string' &&
            err.message.indexOf(INTENTIONAL_FLAKY_FAILURE_PREFIX) === 0
        );
    }

    function summarizeFlakyTests(flakyTests, maxItems) {
        var visibleTests = flakyTests.slice(0, maxItems);
        var summaryLines = visibleTests.map(function(testCase) {
            var fullTitle = normalizeOutputText(testCase.fullTitle, DEFAULT_MAX_TEXT_LENGTH);
            var fileLabel = testCase.file
                ? ' [' + normalizeOutputText(testCase.file, DEFAULT_MAX_TEXT_LENGTH) + ']'
                : '';

            return '- ' + fullTitle + ' (' + testCase.attempts + ' attempts)' + fileLabel;
        });

        if (flakyTests.length > visibleTests.length) {
            summaryLines.push('- ...and ' + (flakyTests.length - visibleTests.length) + ' more');
        }

        return summaryLines.join('\n');
    }

    function appendStepOutput(name, value) {
        var githubOutputPath = process.env.GITHUB_OUTPUT;
        if (!githubOutputPath) {
            return;
        }

        var delimiter = '__OSN_FLAKY_' + name.toUpperCase() + '_' + Date.now() + '_' + Math.random().toString(16).slice(2);
        fs.appendFileSync(
            githubOutputPath,
            name + '<<' + delimiter + '\n' + value + '\n' + delimiter + '\n'
        );
    }

    function publishFlakyOutputs(flakyTests) {
        appendStepOutput('osn_flaky_count', String(flakyTests.length));
        appendStepOutput(
            'osn_flaky_summary',
            summarizeFlakyTests(flakyTests, DEFAULT_MAX_SUMMARY_ITEMS)
        );
    }

    runner.on('start', function() {
        console.log('');
    });

    runner.on('retry', function(test, err) {
        retries++;
        retryContext.setRetryFailure(test, err);

        var currentRetry = getRetryCount(test);
        var maxRetries = getMaxRetries(test);
        var retryTestLine = getTestLineFromError(err);
        var retryDetails = retryTestLine ? ' | [FAIL] ' + retryTestLine : '';

        console.log(
            '%s: [TEST CASE] %s [RETRY %d/%d]%s',
            test.parent.title,
            test.title,
            currentRetry,
            maxRetries,
            retryDetails
        );
        console.log('%s: [RETRY REPORT] Error - %s', test.parent.title, err.message);
    });

    runner.on('pass', function(test) {
        passes++;
        var retryFailure = retryContext.getRetryFailure(test);
        retryContext.clearRetryFailure(test);

        if (getRetryCount(test) > 0 && !isIntentionalCiValidationFailure(retryFailure)) {
            flakyTestCases.push({
                suite: test.parent.title,
                title: test.title,
                fullTitle: typeof test.fullTitle === 'function'
                    ? test.fullTitle()
                    : test.parent.title + ' ' + test.title,
                file: normalizeTestFilePath(test.file || ''),
                duration: test.duration,
                attempts: getRetryCount(test) + 1
            });
        }

        console.log('%s: [TEST CASE] %s [PASS] %dms', test.parent.title, test.title, test.duration);
    });

    runner.on('fail', function(test, err) {
        failures++;
        retryContext.clearRetryFailure(test);

        // Getting test line with the expect check that failed
        testLine = getTestLineFromError(err);

        // Formatting failure message
        var testCaseFailMsg = test.parent.title + ': [TEST CASE] ' + test.title + ' | [FAIL] ' + testLine;

        console.log(testCaseFailMsg);
        console.log('%s: [FAIL REPORT] Error - %s', test.parent.title, err.message);

        failedTestCases.push(testCaseFailMsg);
    });

    runner.on('end', function() {
        console.log('%d passing', passes);

        if (retries >= 1) {
            console.log('%d retried', retries);
        }

        if (failures >= 1) {
            console.log('%d failing', failures);

            failedTestCases.forEach(testCase => {
                console.log('  - %s', testCase);
            });

            console.log('');
        }

        if (flakyTestCases.length >= 1) {
            console.log('%d flaky', flakyTestCases.length);

            flakyTestCases.forEach(function(testCase) {
                console.log(
                    '  - %s: [TEST CASE] %s [FLAKY PASS] %d attempt(s)',
                    testCase.suite,
                    testCase.title,
                    testCase.attempts
                );
            });

            console.log('');
        }

        try {
            publishFlakyOutputs(flakyTestCases);
        } catch (err) {
            console.error('Unable to publish flaky test metadata: %s', err.message);
        }
    });
}

module.exports = ListReporter;
