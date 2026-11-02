import { useEffect, useRef } from "react";
import { settingsStore } from "../lib/settings";

type Props = { density?: number; from?: "right" | "left" | "both"; className?: string; hue?: "red" | "ash" };

/**
 * Procedural Blacklight biomass: writhing tapered tendrils + rising embers.
 * Pure canvas, no assets. Disabled when Android reduced motion is enabled.
 */
export function Biomass({ density = 1, from = "both", className = "", hue = "red" }: Props) {
  const ref = useRef<HTMLCanvasElement>(null);
  const motion = settingsStore.use().animations;

  useEffect(() => {
    const c = ref.current;
    if (!c || motion === "off") return;
    const g = c.getContext("2d");
    if (!g) return;
    let w = 0,
      h = 0;
    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    const resize = () => {
      w = c.clientWidth;
      h = c.clientHeight;
      c.width = Math.max(1, w * dpr);
      c.height = Math.max(1, h * dpr);
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    resize();
    const ro = new ResizeObserver(resize);
    ro.observe(c);

    const reduced = motion !== "full";
    const N = Math.max(3, Math.round((reduced ? 5 : 11) * density));
    const rnd = Math.random;
    const tendrils = Array.from({ length: N }, (_, i) => {
      const left = from === "left" || (from === "both" && i % 3 === 0);
      return {
        ox: left ? -0.03 : 0.5 + rnd() * 0.55,
        oy: left ? 0.2 + rnd() * 0.8 : 1.05,
        ang: left ? -0.35 + rnd() * 0.5 : -Math.PI / 2 + (rnd() - 0.5) * 1.4,
        len: 0.3 + rnd() * 0.45,
        seg: 28,
        amp: 0.35 + rnd() * 0.6,
        freq: 0.12 + rnd() * 0.25,
        sp: 0.25 + rnd() * 0.55,
        ph: rnd() * 10,
        wid: 0.8 + rnd() * 3.2,
      };
    });
    const embers = Array.from({ length: reduced ? 16 : 44 }, () => ({
      x: rnd(),
      y: rnd(),
      r: 0.4 + rnd() * 1.7,
      v: 0.015 + rnd() * 0.05,
      ph: rnd() * 6.28,
    }));
    const [cr, cg, cb] = hue === "red" ? [255, 40, 30] : [220, 210, 205];

    let raf = 0,
      last = 0;
    const draw = (t: number) => {
      raf = requestAnimationFrame(draw);
      const minDt = reduced ? 66 : 16;
      if (t - last < minDt) return;
      const dt = Math.min(0.08, (t - last) / 1000);
      last = t;
      const time = t / 1000;
      g.clearRect(0, 0, w, h);
      g.globalCompositeOperation = "lighter";
      g.lineCap = "round";
      const diag = Math.hypot(w, h);

      for (const td of tendrils) {
        let x = td.ox * w,
          y = td.oy * h,
          a = td.ang;
        const step = (td.len * diag * 0.55) / td.seg;
        const pts: [number, number][] = [[x, y]];
        for (let i = 0; i < td.seg; i++) {
          a += Math.sin(time * td.sp + i * td.freq + td.ph) * td.amp * 0.11;
          x += Math.cos(a) * step;
          y += Math.sin(a) * step;
          pts.push([x, y]);
        }
        const pulse = 0.65 + 0.35 * Math.sin(time * 1.7 + td.ph);
        // soft glow
        g.strokeStyle = `rgba(${cr * 0.7},${cg * 0.2},${cb * 0.3},${0.05 * pulse})`;
        g.lineWidth = td.wid * 5;
        g.beginPath();
        g.moveTo(pts[0][0], pts[0][1]);
        for (let i = 1; i < pts.length - 1; i++) {
          const mx = (pts[i][0] + pts[i + 1][0]) / 2,
            my = (pts[i][1] + pts[i + 1][1]) / 2;
          g.quadraticCurveTo(pts[i][0], pts[i][1], mx, my);
        }
        g.stroke();
        // tapered core
        for (let i = 0; i < pts.length - 1; i++) {
          const k = 1 - i / pts.length;
          g.strokeStyle = `rgba(${cr},${(cg * (0.4 + 0.6 * k)) | 0},${cb},${(0.12 + 0.5 * k) * pulse})`;
          g.lineWidth = td.wid * k + 0.25;
          g.beginPath();
          g.moveTo(pts[i][0], pts[i][1]);
          g.lineTo(pts[i + 1][0], pts[i + 1][1]);
          g.stroke();
        }
      }

      for (const e of embers) {
        e.y -= e.v * dt;
        e.x += Math.sin(time + e.ph) * 0.0004;
        if (e.y < -0.02) {
          e.y = 1.02;
          e.x = rnd();
        }
        const a = 0.25 + 0.55 * Math.abs(Math.sin(time * 2 + e.ph));
        g.fillStyle = `rgba(${cr},${cg + 30},${cb},${a})`;
        g.beginPath();
        g.arc(e.x * w, e.y * h, e.r, 0, 6.283);
        g.fill();
      }
      g.globalCompositeOperation = "source-over";
    };
    raf = requestAnimationFrame(draw);
    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
    };
  }, [motion, density, from, hue]);

  if (motion === "off") return null;
  return <canvas ref={ref} className={`pointer-events-none absolute inset-0 h-full w-full ${className}`} aria-hidden />;
}
