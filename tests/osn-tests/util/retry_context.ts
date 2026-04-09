function getStore(): Map<string, Error> {
    const globalWithRetryStore = global as typeof global & {
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
