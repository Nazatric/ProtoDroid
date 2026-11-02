/**
 * Minimal, quiet UI feedback — short filtered thumps, no sweeping synths.
 * Sounds start only after a user gesture. Everything is generated locally;
 * there are no audio files and nothing is fetched.
 */
let ctx: AudioContext | null = null;

function ac(): AudioContext | null {
  if (!ctx) {
    try {
      const C = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      ctx = new C();
    } catch {
      return null;
    }
  }
  if (ctx.state === "suspended") ctx.resume().catch(() => {});
  return ctx;
}

const levels = () => 0.35;

/** One soft, percussive thump. Short attack, fast decay, slightly filtered. */
function thump(from: number, to: number, durMs: number, gain: number, delayMs = 0, type: OscillatorType = "sine") {
  const c = ac();
  const v = levels();
  if (!c || !v) return;
  const t = c.currentTime + delayMs / 1000;
  const dur = durMs / 1000;
  const o = c.createOscillator();
  o.type = type;
  o.frequency.setValueAtTime(from, t);
  o.frequency.exponentialRampToValueAtTime(Math.max(to, 24), t + dur);
  const f = c.createBiquadFilter();
  f.type = "lowpass";
  f.frequency.value = 900;
  const g = c.createGain();
  g.gain.setValueAtTime(0.0001, t);
  g.gain.exponentialRampToValueAtTime(gain * v, t + 0.004);
  g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
  o.connect(f).connect(g).connect(c.destination);
  o.start(t);
  o.stop(t + dur + 0.03);
  void startStopGuard(c);
}
function startStopGuard(_c: AudioContext) {
  /* keep reference alive for lint clarity; no-op */
}

function haptic(_pattern: number | number[]) {
  // Launcher haptics stay off; the native ui_vibrate setting belongs to gameplay controls.
}

export const sfx = {
  /** Called on the first user gesture so nothing ever plays uninvited. */
  unlock: () => ac(),
  /** Cursor / hover over rows → silent (visual focus is enough). */
  move() {},
  /** Main activation — a soft, low thump. */
  select() {
    thump(190, 70, 70, 0.55, 0, "sine");
    haptic(10);
  },
  /** Leaving a screen — a slightly deeper reverse thump. */
  back() {
    thump(90, 160, 55, 0.42, 0, "sine");
    haptic(6);
  },
  /** On/off toggles. */
  toggle(on: boolean) {
    thump(on ? 820 : 520, on ? 1100 : 420, 22, 0.12, 0, "triangle");
    haptic(4);
  },
  /** Slider / choice nudge — a tiny tick. */
  tick() {
    thump(1500, 1150, 12, 0.05, 0, "triangle");
  },
  /** Denied / missing. */
  error() {
    thump(140, 70, 80, 0.35, 0, "sine");
    thump(130, 60, 90, 0.3, 55, "sine");
    haptic([18, 24, 18]);
  },
  // Kept for call sites; quiet on purpose — no long dramatic sweeps.
  impact() {},
  riser() {},
  launch() {},
  type() {},
};
