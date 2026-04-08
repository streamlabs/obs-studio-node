var fs = require('fs');
var path = require('path');
var mocha = require('mocha');

function ListReporter(runner) {
    mocha.reporters.Base.call(this, runner);
    var passes = 0;
    var failures = 0;
    var retries = 0;
    var failedTestCases = [];
    var flakyTestCases = [];
    var flakyReportPath = process.env.OSN_FLAKY_TESTS_FILE || 'flaky-tests.json';
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

    runner.on('start', function() {
        console.log('');
    });

    runner.on('retry', function(test, err) {
        retries++;

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

        if (getRetryCount(test) > 0) {
            flakyTestCases.push({
                suite: test.parent.title,
                title: test.title,
                fullTitle: typeof test.fullTitle === 'function'
                    ? test.fullTitle()
                    : test.parent.title + ' ' + test.title,
                file: test.file || '',
                duration: test.duration,
                attempts: getRetryCount(test) + 1
            });
        }

        console.log('%s: [TEST CASE] %s [PASS] %dms', test.parent.title, test.title, test.duration);
    });

    runner.on('fail', function(test, err) {
        failures++;

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
            fs.mkdirSync(path.dirname(flakyReportPath), { recursive: true });
            fs.writeFileSync(flakyReportPath, JSON.stringify(flakyTestCases, null, 2));
        } catch (err) {
            console.log('Unable to write flaky test report: %s', err.message);
        }
    });
}

module.exports = ListReporter;
