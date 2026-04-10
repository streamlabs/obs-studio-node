// Stores the failure from Mocha's retry event so afterEach-based retry
// preparation can inspect why the previous attempt failed.
function getStore(): Map<string, Error> {
    const globalWithRetryStore = global as typeof global & {
        // Use a loudly namespaced global slot to avoid colliding with other
        // helpers while keeping the store alive across Mocha retry callbacks.
        __OSN_RETRY_FAILURES__?: Map<string, Error>;
    };

    if (!globalWithRetryStore.__OSN_RETRY_FAILURES__) {
        globalWithRetryStore.__OSN_RETRY_FAILURES__ = new Map();
    }

    return globalWithRetryStore.__OSN_RETRY_FAILURES__;
}

function getTestKey(test: any): string {
    if (!test) {
        return '';
    }

    // Retry preparation runs after Mocha has already emitted the retry event, so
    // we key the failure by a stable test identity instead of keeping the object.
    if (typeof test.retriedTest === 'function') {
        const retriedTest = test.retriedTest();
        if (retriedTest && retriedTest.id) {
            return retriedTest.id;
        }
    }

    if (test.id) {
        return test.id;
    }

    if (typeof test.fullTitle === 'function') {
        return test.fullTitle();
    }

    return test.title || '';
}

export function setRetryFailure(test: any, err: Error) {
    getStore().set(getTestKey(test), err);
}

export function getRetryFailure(test: any): Error | undefined {
    return getStore().get(getTestKey(test));
}

export function clearRetryFailure(test: any) {
    getStore().delete(getTestKey(test));
}
