package com.protodroid;

/** Host-side contract tests for deliberate, repeatable one-shot tour activation. */
public final class DiagnosticTourCodeTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void phrase(DiagnosticTourCode code, String text, boolean expectedAtEnd) {
        boolean activated = false;
        for (int i = 0; i < text.length(); ++i) activated = code.onLetter(text.charAt(i));
        check(activated == expectedAtEnd, "unexpected phrase activation for " + text);
    }

    public static void main(String[] args) {
        DiagnosticTourCode typed = new DiagnosticTourCode();
        phrase(typed, "xxmErCeR", true);
        phrase(typed, "MERC", false);
        phrase(typed, "ER", true);  // the previous successful code reset its recognizer, this is a fresh MERCER
        phrase(typed, "123456", false);
        phrase(typed, "mercer", true);

        DiagnosticTourCode taps = new DiagnosticTourCode();
        for (int i = 0; i < DiagnosticTourCode.LOGO_TAPS - 1; ++i) {
            check(!taps.onLogoTap(1000L + i * 100L), "logo activated before seven taps");
        }
        check(taps.onLogoTap(1600L), "seven logo taps did not activate");
        for (int i = 0; i < DiagnosticTourCode.LOGO_TAPS - 1; ++i) {
            check(!taps.onLogoTap(2000L + i * 100L), "successful gesture did not reset");
        }
        check(!taps.onLogoTap(7000L), "an out-of-window tap completed an old gesture");
        for (int i = 0; i < 5; ++i) {
            check(!taps.onLogoTap(7100L + i * 100L), "restarted gesture reused old taps");
        }
        check(taps.onLogoTap(7600L), "restarted seven-tap gesture did not activate");

        System.out.println("diagnostic-tour cheat-code tests passed");
    }
}
