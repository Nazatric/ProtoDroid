import path from "path";
import { fileURLToPath } from "url";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// The Android package gets one offline document: JavaScript, fonts, and artwork are embedded.
export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile()],
  resolve: { alias: { "@": path.resolve(__dirname, "src") } },
  build: {
    target: "chrome79",
    outDir: path.resolve(__dirname, "../android/assets/launcher"),
    emptyOutDir: true,
    sourcemap: false,
    chunkSizeWarningLimit: 12000,
  },
});
