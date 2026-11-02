import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { cn } from "../utils/cn";
import { sfx } from "../lib/sfx";
import { useBack, focusables } from "../lib/nav";
import { toastStore } from "../lib/data";
import { settingsStore } from "../lib/settings";
import { Biomass } from "./Biomass";
import { ART } from "../lib/art";

export function Logo({ size = "3rem", className = "", onClick }: { size?: string; className?: string; onClick?: () => void }) {
  return (
    <div className={cn("logo", className)} style={{ fontSize: size }} onClick={onClick} aria-label="Prototype">
      <span className="logo-bracket">[</span>
      <span className="logo-word" data-text="PROTOTYPE">
        PROTOTYPE
      </span>
      <span className="logo-bracket">]</span>
    </div>
  );
}

/** The app icon taken from the ProtoDroid repository. */
export function AppIcon({ size = 36, className = "", onClick }: { size?: number; className?: string; onClick?: () => void }) {
  const image = (
    <img
      src={ART.icon}
      width={size}
      height={size}
      alt="ProtoDroid"
      draggable={false}
      className={cn("shrink-0 rounded-[9px] shadow-[0_2px_10px_rgba(0,0,0,.6)] ring-1 ring-white/15", className)}
    />
  );
  if (!onClick) return image;
  return (
    <button data-nav aria-label="ProtoDroid icon" onClick={onClick} className="btn grid h-12 w-12 shrink-0 place-items-center">
      {image}
    </button>
  );
}

type BtnProps = {
  children: ReactNode;
  onClick?: () => void;
  variant?: "ghost" | "primary" | "danger" | "plain";
  small?: boolean;
  disabled?: boolean;
  className?: string;
  silent?: boolean;
  title?: string;
};
export function Btn({ children, onClick, variant = "ghost", small, disabled, className, silent, title }: BtnProps) {
  return (
    <button
      data-nav
      title={title}
      disabled={disabled}
      onClick={() => {
        if (disabled) return;
        if (!silent) sfx.select();
        onClick?.();
      }}
      className={cn(
        "btn inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap font-ui font-medium uppercase tracking-[0.16em]",
        small ? "h-9 px-3 text-[11px]" : "h-11 px-4 text-[13px]",
        variant === "primary" && "bg-blood text-white hover:bg-blood-hi",
        variant === "ghost" && "border border-white/15 bg-black/40 text-bone hover:border-blood hover:bg-blood/25",
        variant === "danger" && "border border-blood/60 bg-blood-lo/40 text-blood-hi hover:bg-blood hover:text-white",
        variant === "plain" && "text-ash hover:text-white",
        disabled && "pointer-events-none opacity-35",
        className
      )}
    >
      {children}
    </button>
  );
}

export function Panel({ children, className }: { children: ReactNode; className?: string }) {
  return <div className={cn("frame", className)}>{children}</div>;
}

export function Tag({ children, tone = "ash" }: { children: ReactNode; tone?: "ash" | "red" | "ok" }) {
  return (
    <span
      className={cn(
        "inline-flex h-5 items-center px-1.5 font-mono text-[10px] uppercase tracking-wider",
        tone === "ash" && "bg-white/8 text-ash",
        tone === "red" && "bg-blood/25 text-blood-hi",
        tone === "ok" && "bg-white/90 text-black"
      )}
    >
      {children}
    </span>
  );
}

/** Rotating, locally bundled cinematic art with a real crossfade instead of a hard swap. */
export function Backdrop({
  img,
  position = "center",
  dim = 0.55,
  tendrils = 0,
  fade = "left",
  interval = 10000,
}: {
  img: string | string[];
  position?: string;
  dim?: number;
  tendrils?: number;
  fade?: "left" | "none";
  interval?: number;
}) {
  const imgs = useMemo(() => (Array.isArray(img) ? img : [img]), [img]);
  const motion = settingsStore.use().animations;
  const rotate = imgs.length > 1 && motion !== "off";
  const [cur, setCur] = useState(0);
  const [prev, setPrev] = useState(0);
  const [entering, setEntering] = useState(false);
  const curRef = useRef(0);
  useEffect(() => {
    if (!rotate) return;
    let fadeTimer = 0;
    const id = setInterval(
      () => {
        const old = curRef.current;
        const next = (old + 1) % imgs.length;
        curRef.current = next;
        setPrev(old);
        setCur(next);
        setEntering(true);
        window.clearTimeout(fadeTimer);
        fadeTimer = window.setTimeout(() => setEntering(false), 1450);
      },
      interval
    );
    return () => {
      clearInterval(id);
      window.clearTimeout(fadeTimer);
    };
  }, [imgs.length, rotate, interval, motion]);

  const period = `${(interval) / 1000}s`;
  return (
    <div className="pointer-events-none absolute inset-0 overflow-hidden bg-ink">
      {rotate && <img src={imgs[prev]} alt="" className="bg-layer bg-previous" style={{ objectPosition: position }} draggable={false} />}
      <img
        key={cur}
        src={imgs[cur]}
        alt=""
        className={cn("bg-layer", rotate ? "bg-current" : "bg-static", entering && rotate && "bg-enter")}
        style={{ "--bg-period": period, objectPosition: position } as React.CSSProperties}
        draggable={false}
      />
      {fade === "left" && <div className="absolute inset-0 bg-gradient-to-r from-ink via-ink/60 to-ink/5" />}
      <div className="absolute inset-0 bg-gradient-to-t from-ink/90 via-transparent to-ink/45" />
      {dim > 0 && <div className="absolute inset-0" style={{ background: `rgba(7,4,5,${Math.min(0.45, dim * 0.32)})` }} />}
      {tendrils > 0 && <Biomass density={tendrils} />}
      <div className="vignette" />
    </div>
  );
}

export function Header({ title, onBack, right, icon }: { title: string; onBack: () => void; right?: ReactNode; icon?: boolean }) {
  useBack(onBack);
  return (
    <header className="relative z-10 flex h-[max(46px,11.5vh)] max-h-16 min-w-0 shrink-0 items-center gap-2 border-b border-white/8 bg-black/55 px-[max(12px,env(safe-area-inset-left))] pr-[max(12px,env(safe-area-inset-right))] backdrop-blur-[2px]">
      <button
        data-nav
        onClick={() => {
          sfx.back();
          onBack();
        }}
        className="btn flex h-10 items-center gap-2 pr-2 font-ui text-xs uppercase tracking-[0.22em] text-ash hover:text-white"
        aria-label="Back"
      >
        <svg width="14" height="14" viewBox="0 0 14 14" className="text-blood-hi">
          <path d="M10 1 3 7l7 6" fill="none" stroke="currentColor" strokeWidth="2.2" />
        </svg>
        Back
      </button>
      <div className="h-6 w-px bg-white/15" />
      {icon && <AppIcon size={26} className="rounded-md" />}
      <h1 className="anim-rise min-w-0 flex-1 truncate font-dirt text-[clamp(18px,4.6vh,28px)] leading-none text-bone">
        <span className="font-ui font-light text-blood-hi">[</span>
        {title}
        <span className="font-ui font-light text-blood-hi">]</span>
      </h1>
      <div className="ml-auto flex max-w-[48vw] shrink-0 items-center justify-end gap-1.5 overflow-x-auto">{right}</div>
    </header>
  );
}

/** Touch-language footer. No controller prompts. */
export function HintBar({ text, back = true, backLabel = "Back", onBack }: { text?: string; back?: boolean; backLabel?: string; onBack?: () => void }) {
  return (
    <footer className="relative z-10 flex h-9 shrink-0 items-center gap-4 border-t border-white/8 bg-black/65 px-[max(14px,env(safe-area-inset-left))] font-ui text-[11px] tracking-wide text-ash">
      <span className="min-w-0 flex-1 truncate text-bone/70">{text}</span>
      <span className="hidden shrink-0 uppercase tracking-[0.18em] text-white/40 sm:flex">Tap to select</span>
      {back && (
        <button data-nav onClick={onBack} className="btn flex min-h-9 items-center gap-1.5 whitespace-nowrap uppercase tracking-[0.2em] text-ash hover:text-white">
          <span className="grid h-4 w-4 place-items-center rounded-full border border-white/35 text-white">
            <svg width="7" height="9" viewBox="0 0 8 12">
              <path d="M7 0 0 6l7 6z" fill="currentColor" />
            </svg>
          </span>
          {backLabel}
        </button>
      )}
    </footer>
  );
}

export function Modal({ title, children, onClose, actions, wide }: { title: string; children: ReactNode; onClose: () => void; actions?: ReactNode; wide?: boolean }) {
  useBack(onClose);
  useEffect(() => {
    const t = setTimeout(() => focusables()[0]?.focus({ preventScroll: true }), 60);
    return () => clearTimeout(t);
  }, []);
  return (
    <div data-modal className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 backdrop-blur-[2px]" onClick={onClose}>
      <div onClick={(e) => e.stopPropagation()} className={cn("frame anim-fade flex max-h-[92vh] w-full flex-col", wide ? "max-w-3xl" : "max-w-md")}>
        <div className="flex items-center gap-3 border-b border-white/8 px-4 py-2.5">
          <span className="h-3 w-1 bg-blood-hi" />
          <h2 className="font-ui text-sm font-medium uppercase tracking-[0.22em]">{title}</h2>
        </div>
        <div className="scroll-y min-h-0 flex-1 px-4 py-3 text-sm text-bone/85">{children}</div>
        {actions && <div className="flex flex-wrap justify-end gap-2 border-t border-white/8 px-4 py-2.5">{actions}</div>}
      </div>
    </div>
  );
}

export function Toast() {
  const t = toastStore.use();
  if (!t.msg) return null;
  return (
    <div key={t.id} className="anim-fade pointer-events-none fixed bottom-11 left-1/2 z-[70] flex -translate-x-1/2 items-center gap-2 bg-black/90 py-1.5 pl-2 pr-3 font-ui text-[11px] uppercase tracking-wider text-bone shadow-lg shadow-black">
      <span className={cn("h-4 w-1", t.tone === "ok" ? "bg-blood-hi" : "bg-amber-400")} />
      {t.msg}
    </div>
  );
}

export function Progress({ value, label, stripes }: { value: number; label?: string; stripes?: boolean }) {
  return (
    <div className="w-full">
      {label && (
        <div className="mb-1 flex justify-between gap-3 font-ui text-[10px] uppercase tracking-wider text-ash">
          <span className="truncate">{label}</span>
          <span>{Math.round(value)}%</span>
        </div>
      )}
      <div className="h-1.5 w-full overflow-hidden bg-white/10">
        <div className={cn("h-full bg-blood-hi transition-[width] duration-200", stripes && "bar-stripes")} style={{ width: `${Math.min(100, value)}%` }} />
      </div>
    </div>
  );
}
