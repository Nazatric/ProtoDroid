import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import "./index.css";
import App from "./App";
import { LOCAL_ASSETS } from "./lib/localAssets";

const bundledFontFaces = [
  ["Dirt", "font-rubik-dirt-latin-400-normal", "400"],
  ["Oswald", "font-oswald-latin-300-normal", "300"],
  ["Oswald", "font-oswald-latin-400-normal", "400"],
  ["Oswald", "font-oswald-latin-500-normal", "500"],
  ["Oswald", "font-oswald-latin-700-normal", "700"],
  ["TechMono", "font-share-tech-mono-latin-400-normal", "400"],
] as const;

const fontStyle = document.createElement("style");
fontStyle.textContent = bundledFontFaces
  .map(([family, key, weight]) => `@font-face{font-family:"${family}";src:url("${LOCAL_ASSETS[key]}") format("woff2");font-style:normal;font-weight:${weight};font-display:swap}`)
  .join("\n");
document.head.appendChild(fontStyle);

const iconLink = document.createElement("link");
iconLink.rel = "icon";
iconLink.type = "image/png";
iconLink.href = LOCAL_ASSETS.icon;
document.head.appendChild(iconLink);

const mount = () =>
  createRoot(document.getElementById("root")!).render(
    <StrictMode>
      <App />
    </StrictMode>
  );

Promise.all([
  document.fonts.load("400 36px Dirt"),
  document.fonts.load("400 16px Oswald"),
  document.fonts.load("400 12px TechMono"),
]).then(mount, mount);

// Block the browser context menu / long-press callout for a native feel.
window.addEventListener("contextmenu", (e) => e.preventDefault());
