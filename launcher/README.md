# ProtoDroid launcher

This is the supplied Prototype-styled launcher adapted to the real Android host. It is not a standalone game or mod manager.

- Android owns game detection, `GameSettings` and atomic INI writes, Play, updates, drivers, mods, and the full touch-layout editor.
- Drivers, Mods, and Layout deliberately open the existing native screens. The diagnostic-tour icon gesture arms the real native gameplay tour.
- Artwork and fonts are embedded. The production document is fully offline; update networking stays in `UpdateManager`.

## Rebuild

Requires Node.js 22:

```sh
npm ci --prefix launcher
npm run check --prefix launcher
```

`npm run build --prefix launcher` writes the single-file `android/assets/launcher/index.html`. CI rebuilds it before Android packaging. Do not edit that generated file directly.
