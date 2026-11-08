package com.protodroid;

/** Pure timing policy for DownloadManager query recovery; kept separate for host-side regression tests. */
final class UpdateDownloadPolicy {
    static final int ROW = 0;
    static final int EMPTY = 1;
    static final int ERROR = 2;
    static final long NO_UNKNOWN_QUERY = -1L;
    static final long QUERY_GRACE_MS = 10_000L;

    private UpdateDownloadPolicy() {}

    static long nextUnknownSince(int outcome, long previous, long now) {
        if (outcome == ROW) return NO_UNKNOWN_QUERY;
        if (outcome != EMPTY && outcome != ERROR) throw new IllegalArgumentException("unknown query outcome");
        if (now < 0L) throw new IllegalArgumentException("negative elapsed time");
        return previous >= 0L ? previous : now;
    }

    static boolean graceExpired(int outcome, long unknownSince, long now) {
        if (outcome == ROW || unknownSince < 0L || now < unknownSince) return false;
        return now - unknownSince >= QUERY_GRACE_MS;
    }

    /** Only an empty result after grace is evidence that replacing the system download may be necessary. */
    static boolean mayReplaceAfterUnknown(int outcome, long unknownSince, long now) {
        return outcome == EMPTY && graceExpired(outcome, unknownSince, now);
    }
}
