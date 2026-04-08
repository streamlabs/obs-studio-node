var fs = require('fs');
var path = require('path');
var mocha = require('mocha');

function ListReporter(runner) {
    mocha.reporters.Base.call(this, runner);
    var passes = 0;
    var failures = 0;
    var failedTestCases = [];
    var flakyTestCases = [];
    var flakyReportPath = process.env.OSN_FLAKY_TESTS_FILE || 'flaky-tests.json';
    var testLine = "";

    runner.on('start', function() {
        console.log('');
    });

    runner.on('pass', function(test) {
        passes++;

        if (typeof test.currentRetry === 'function' && test.currentRetry() > 0) {
            flakyTestCases.push({
                suite: test.parent.title,
                title: test.title,
                fullTitle: typeof test.fullTitle === 'function'
                    ? test.fullTitle()
                    : test.parent.title + ' ' + test.title,
                file: test.file || '',
                duration: test.duration,
                attempts: test.currentRetry() + 1
            });
        }

        console.log('%s: [TEST CASE] %s [PASS] %dms', test.parent.title, test.title, test.duration);
    });

    runner.on('fail', function(test, err) {
        failures++;

        // Getting test line with the expect check that failed
        var regex = /(?<=src\\)(.*?)(?=\n)/;
        var str = err.stack.match(regex);
        if (str != null) {
            testLine = str[0].substr(0, str[0].lastIndexOf(":"));
        }

        // Formatting failure message
        var testCaseFailMsg = test.parent.title + ': [TEST CASE] ' + test.title + ' | [FAIL] ' + testLine;

        console.log(testCaseFailMsg);
        console.log('%s: [FAIL REPORT] Error - %s', test.parent.title, err.message);

        failedTestCases.push(testCaseFailMsg);
    });

    runner.on('end', function() {
        console.log('%d passing', passes);

        if (failures >= 1) {
            console.log('%d failing', failures);

            failedTestCases.forEach(testCase => {
                console.log('  - %s', testCase);
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
