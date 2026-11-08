package com.protodroid;

/** Pure launcher cheat-code recognition; Android input and tour execution remain outside this class. */
final class DiagnosticTourCode {
    static final String PHRASE = "MERCER";
    static final int LOGO_TAPS = 7;
    static final long LOGO_WINDOW_MS = 4000L;

    private String trailingLetters = "";
    private int logoTaps;
    private long logoWindowStart;

    boolean onLetter(char letter) {
        if (!Character.isLetter(letter)) return false;
        trailingLetters += Character.toUpperCase(letter);
        if (trailingLetters.length() > PHRASE.length()) {
            trailingLetters = trailingLetters.substring(trailingLetters.length() - PHRASE.length());
        }
        if (!trailingLetters.equals(PHRASE)) return false;
        reset();
        return true;
    }

    boolean onLogoTap(long nowMs) {
        if (logoTaps == 0 || nowMs - logoWindowStart > LOGO_WINDOW_MS || nowMs < logoWindowStart) {
            logoTaps = 1;
            logoWindowStart = nowMs;
        } else {
            ++logoTaps;
        }
        if (logoTaps < LOGO_TAPS) return false;
        reset();
        return true;
    }

    void reset() {
        trailingLetters = "";
        logoTaps = 0;
        logoWindowStart = 0L;
    }
}
