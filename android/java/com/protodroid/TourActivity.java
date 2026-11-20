package com.protodroid;

/**
 * The shader-precompile tour's engine host. It exists only to give the tour its own process
 * (android:process=":tour"): the tour's native side ends the run with an immediate process
 * exit, and SDL allows a single native main per process. The launcher process stays alive
 * behind this activity, watches the tour's result file, and relaunches normal Play when the
 * tour finishes — this activity and its process then die on their own.
 */
public class TourActivity extends ProtoActivity {
}
