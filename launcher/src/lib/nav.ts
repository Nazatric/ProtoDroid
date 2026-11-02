import { useEffect, useRef } from "react";
import { sfx } from "./sfx";

/** Back-stack: the top-most registered handler handles B / Escape / Android back. */
const backStack: (() => void)[] = [];

export function useBack(fn: () => void, active = true) {
  const ref = useRef(fn);
  ref.current = fn;
  useEffect(() => {
    if (!active) return;
    const h = () => ref.current();
    backStack.push(h);
    return () => {
      const i = backStack.lastIndexOf(h);
      if (i >= 0) backStack.splice(i, 1);
    };
  }, [active]);
}

export function goBack() {
  const h = backStack[backStack.length - 1];
  if (!h) return false;
  h();
  return true;
}

function scope(): HTMLElement {
  const modals = document.querySelectorAll<HTMLElement>("[data-modal]");
  if (modals.length) return modals[modals.length - 1];
  return document.querySelector<HTMLElement>("[data-screen]") ?? document.body;
}

export function focusables() {
  return Array.from(scope().querySelectorAll<HTMLElement>("[data-nav]")).filter(
    (el) => el.offsetParent !== null && !el.hasAttribute("disabled")
  );
}

export function moveFocus(dir: 1 | -1) {
  const els = focusables();
  if (!els.length) return;
  const i = els.indexOf(document.activeElement as HTMLElement);
  const n = i < 0 ? (dir > 0 ? 0 : els.length - 1) : (i + dir + els.length) % els.length;
  els[n].focus({ preventScroll: true });
  els[n].scrollIntoView({ block: "nearest" });
}

/**
 * Touch-first navigation helpers for hardware keyboards and TV remotes.
 * A / Enter activates, B / Escape goes back. No controller UI is shown anywhere.
 */
export function installNav() {
  const activate = () => {
    const el = document.activeElement as HTMLElement | null;
    if (!el || el === document.body) {
      focusables()[0]?.focus({ preventScroll: true });
      return;
    }
    if (el.matches("button, [role=button], a")) el.click();
  };

  const onKey = (e: KeyboardEvent) => {
    if (e.defaultPrevented || e.metaKey || e.ctrlKey || e.altKey) return;
    const t = e.target as HTMLElement;
    if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA") && (t as HTMLInputElement).type !== "range") return;
    const lr = (document.activeElement as HTMLElement | null)?.hasAttribute("data-lr");
    switch (e.key) {
      case "a":
      case "A":
        e.preventDefault();
        activate();
        break;
      case "b":
      case "B":
      case "Escape":
      case "Backspace":
        if (goBack()) {
          e.preventDefault();
          sfx.back();
        }
        break;
      case "ArrowDown":
        e.preventDefault();
        moveFocus(1);
        break;
      case "ArrowUp":
        e.preventDefault();
        moveFocus(-1);
        break;
      case "ArrowRight":
        if (!lr) {
          e.preventDefault();
          moveFocus(1);
        }
        break;
      case "ArrowLeft":
        if (!lr) {
          e.preventDefault();
          moveFocus(-1);
        }
        break;
      case "Enter":
        if (document.activeElement === document.body) {
          e.preventDefault();
          activate();
        }
        break;
    }
  };

  const onFocus = (e: FocusEvent) => {
    if ((e.target as HTMLElement)?.hasAttribute?.("data-nav")) sfx.move();
  };

  window.addEventListener("keydown", onKey);
  document.addEventListener("focusin", onFocus);
  return () => {
    window.removeEventListener("keydown", onKey);
    document.removeEventListener("focusin", onFocus);
  };
}
