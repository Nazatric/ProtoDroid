import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import ts from "typescript";

const source = readFileSync(new URL("../src/lib/updatePolicy.ts", import.meta.url), "utf8");
const output = ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS },
}).outputText;
const module = { exports: {} };
vm.runInNewContext(`(function (module, exports) { ${output}\n })`, { console })(module, module.exports);
const { mergeUpdate } = module.exports;

const base = {
  status: "hidden",
  visible: false,
  canPress: false,
  versionName: "",
  progress: -1,
  downloadedBytes: 0,
  totalBytes: 0,
  detail: "",
  requiresUninstall: false,
  warningVersion: "",
};
const state = (status, upToDate = false) => ({ ...base, status, visible: status !== "hidden", upToDate });

test("a hidden initial card does not claim a completed update check", () => {
  assert.equal(mergeUpdate(state("hidden"), state("hidden")).upToDate, false);
});

test("only a checking-to-hidden transition means no newer release", () => {
  const checking = mergeUpdate(state("hidden"), state("checking"));
  assert.equal(checking.upToDate, false);
  assert.equal(mergeUpdate(checking, state("hidden")).upToDate, true);
});

test("a real available/update state clears a previous no-newer result", () => {
  const previous = state("hidden", true);
  assert.equal(mergeUpdate(previous, state("available")).upToDate, false);
  assert.equal(mergeUpdate(previous, state("downloading")).upToDate, false);
  assert.equal(mergeUpdate(previous, state("ready")).upToDate, false);
  assert.equal(mergeUpdate(previous, state("checking")).upToDate, false);
});
