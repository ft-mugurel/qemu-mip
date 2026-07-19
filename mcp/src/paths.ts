import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import os from "node:os";

const LOCAL_KEYS = new Set([
  "QEMU_CONNECT_ROOT",
  "QEMU_CONNECT_HOME",
  "QEMU_CONNECT_MUNUX",
  "QEMU_CONNECT_ISO",
  "QEMU_CONNECT_DISK",
  "QEMU_CONNECT_PLUGIN",
  "QEMU_CONNECT_CLI",
  "QEMU_CONNECT_PROMPT",
]);

let localLoaded = false;

/**
 * Load KEY=VALUE from a file into process.env only for unset keys.
 * Same semantics as cli/paths.c load_env_file.
 */
function loadEnvFile(filePath: string): void {
  if (!fs.existsSync(filePath)) return;
  let text: string;
  try {
    text = fs.readFileSync(filePath, "utf8");
  } catch {
    return;
  }
  for (const raw of text.split(/\r?\n/)) {
    let line = raw.trim();
    if (!line || line.startsWith("#")) continue;
    if (line.startsWith("export ")) line = line.slice(7).trim();
    const eq = line.indexOf("=");
    if (eq <= 0) continue;
    const key = line.slice(0, eq).trim();
    let val = line.slice(eq + 1).trim();
    if (
      (val.startsWith('"') && val.endsWith('"')) ||
      (val.startsWith("'") && val.endsWith("'"))
    ) {
      val = val.slice(1, -1);
    }
    if (!LOCAL_KEYS.has(key)) continue;
    if (!process.env[key]) {
      process.env[key] = val;
    }
  }
}

/** Apply $ROOT/.qemu-connect.local and ~/.config/qemu-connect/env once. */
export function loadLocalConfigs(rootHint?: string): void {
  if (localLoaded) return;
  localLoaded = true;
  const root =
    rootHint ||
    process.env.QEMU_CONNECT_ROOT ||
    process.env.QEMU_CONNECT_HOME ||
    "";
  if (root) {
    loadEnvFile(path.join(path.resolve(root), ".qemu-connect.local"));
  }
  const home = os.homedir();
  if (home) {
    loadEnvFile(path.join(home, ".config/qemu-connect/env"));
  }
}

/**
 * qemu-connect tool repo (has plugin/Makefile).
 * QEMU_CONNECT_ROOT env, or walk up from mcp package.
 */
export function getRepoRoot(): string {
  // Try env first; then local file next to a guessed root; then walk up.
  if (process.env.QEMU_CONNECT_ROOT) {
    const r = path.resolve(process.env.QEMU_CONNECT_ROOT);
    if (fs.existsSync(r)) {
      loadLocalConfigs(r);
      if (process.env.QEMU_CONNECT_ROOT) {
        const r2 = path.resolve(process.env.QEMU_CONNECT_ROOT);
        if (fs.existsSync(r2)) return r2;
      }
      return r;
    }
  }

  const here = path.dirname(fileURLToPath(import.meta.url));
  // Installed: .../share/qemu-connect/mcp/dist → not a repo.
  // Dev: .../mcp/dist or .../mcp/src → parent is repo.
  for (const candidate of [
    path.resolve(here, "../.."), // mcp/dist → repo
    path.resolve(here, "../../.."), // share/qemu-connect/mcp/dist → share parent?
  ]) {
    if (
      fs.existsSync(path.join(candidate, "Makefile")) &&
      fs.existsSync(path.join(candidate, "plugin"))
    ) {
      loadLocalConfigs(candidate);
      if (process.env.QEMU_CONNECT_ROOT) {
        const r = path.resolve(process.env.QEMU_CONNECT_ROOT);
        if (fs.existsSync(r)) return r;
      }
      return candidate;
    }
  }

  // Last resort: local config under HOME may set ROOT
  loadLocalConfigs();
  if (process.env.QEMU_CONNECT_ROOT) {
    const r = path.resolve(process.env.QEMU_CONNECT_ROOT);
    if (fs.existsSync(r)) return r;
  }

  throw new Error(
    "Cannot find qemu-connect repo. Set QEMU_CONNECT_ROOT to the tool checkout."
  );
}

/** Your munux kernel tree (the one you develop). */
export function getMunuxRoot(): string | null {
  // Ensure local config applied (sets QEMU_CONNECT_MUNUX from .qemu-connect.local)
  try {
    getRepoRoot();
  } catch {
    loadLocalConfigs();
  }
  const env = process.env.QEMU_CONNECT_MUNUX;
  if (env && fs.existsSync(env)) {
    return path.resolve(env);
  }
  try {
    const bundled = path.join(getRepoRoot(), "test", "munux");
    if (fs.existsSync(bundled)) {
      return bundled;
    }
  } catch {
    /* ignore */
  }
  return null;
}

export function getMunuxIso(): string {
  if (process.env.QEMU_CONNECT_ISO) {
    return path.resolve(process.env.QEMU_CONNECT_ISO);
  }
  const m = getMunuxRoot();
  if (m) return path.join(m, "build", "kernel.iso");
  return path.join(getRepoRoot(), "test/munux/build/kernel.iso");
}

export function getMunuxDisk(): string {
  if (process.env.QEMU_CONNECT_DISK) {
    return path.resolve(process.env.QEMU_CONNECT_DISK);
  }
  const m = getMunuxRoot();
  if (m) return path.join(m, "build", "disk.img");
  return path.join(getRepoRoot(), "test/munux/build/disk.img");
}

export function getCliPath(root: string): string {
  if (process.env.QEMU_CONNECT_CLI && fs.existsSync(process.env.QEMU_CONNECT_CLI)) {
    return path.resolve(process.env.QEMU_CONNECT_CLI);
  }
  const installed = path.join(os.homedir(), ".local/bin/qemu-connect");
  if (fs.existsSync(installed)) return installed;
  return path.join(root, "build", "qemu-connect");
}

export function getPluginPath(root: string): string {
  if (
    process.env.QEMU_CONNECT_PLUGIN &&
    fs.existsSync(process.env.QEMU_CONNECT_PLUGIN)
  ) {
    return path.resolve(process.env.QEMU_CONNECT_PLUGIN);
  }
  const installed = path.join(
    os.homedir(),
    ".local/lib/qemu-connect/libqemu-connect.so"
  );
  if (fs.existsSync(installed)) return installed;
  return path.join(root, "build", "libqemu-connect.so");
}
