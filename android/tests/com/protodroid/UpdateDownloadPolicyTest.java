package com.protodroid;

public final class UpdateDownloadPolicyTest {
    private static int checks;

    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }

    private static void unknownOutcomeKeepsGrace(int outcome) {
        long since = UpdateDownloadPolicy.nextUnknownSince(outcome,
                UpdateDownloadPolicy.NO_UNKNOWN_QUERY, 1_000L);
        check(since == 1_000L, "first unknown query did not start grace");
        check(!UpdateDownloadPolicy.graceExpired(outcome, since, 1_000L), "first unknown query failed immediately");
        check(!UpdateDownloadPolicy.graceExpired(outcome, since, 10_999L), "unknown query failed before grace");
        check(UpdateDownloadPolicy.nextUnknownSince(outcome, since, 5_000L) == since,
                "recheck restarted the grace interval");
        check(UpdateDownloadPolicy.graceExpired(outcome, since, 11_000L), "persistent unknown query never surfaced");
        check(UpdateDownloadPolicy.nextUnknownSince(UpdateDownloadPolicy.ROW, since, 11_000L)
                        == UpdateDownloadPolicy.NO_UNKNOWN_QUERY,
                "a healthy row did not clear query recovery");
        check(!UpdateDownloadPolicy.graceExpired(UpdateDownloadPolicy.ROW, since, 100_000L),
                "a healthy row was treated as an error");
    }

    public static void main(String[] args) {
        unknownOutcomeKeepsGrace(UpdateDownloadPolicy.EMPTY);
        unknownOutcomeKeepsGrace(UpdateDownloadPolicy.ERROR);

        long emptySince = UpdateDownloadPolicy.nextUnknownSince(UpdateDownloadPolicy.EMPTY,
                UpdateDownloadPolicy.NO_UNKNOWN_QUERY, 50L);
        check(!UpdateDownloadPolicy.mayReplaceAfterUnknown(UpdateDownloadPolicy.EMPTY, emptySince, 9_000L),
                "transient empty query allowed a healthy download to be replaced");
        check(UpdateDownloadPolicy.mayReplaceAfterUnknown(UpdateDownloadPolicy.EMPTY, emptySince, 10_050L),
                "confirmed missing download could not be replaced");

        long errorSince = UpdateDownloadPolicy.nextUnknownSince(UpdateDownloadPolicy.ERROR,
                UpdateDownloadPolicy.NO_UNKNOWN_QUERY, 50L);
        check(!UpdateDownloadPolicy.mayReplaceAfterUnknown(UpdateDownloadPolicy.ERROR, errorSince, 100_000L),
                "a query exception was treated as permission to remove a healthy download");
        check(!UpdateDownloadPolicy.graceExpired(UpdateDownloadPolicy.ERROR, errorSince, 25L),
                "elapsed-time regression triggered a false failure");

        boolean rejected = false;
        try {
            UpdateDownloadPolicy.nextUnknownSince(99, UpdateDownloadPolicy.NO_UNKNOWN_QUERY, 0L);
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }
        check(rejected, "invalid query outcome was accepted");

        System.out.println("UpdateDownloadPolicyTest: " + checks + " checks passed");
    }
}
