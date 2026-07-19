import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import os from "node:os";

/**
 * qemu-connect tool repo (has plugin/Makefile).
 * QEMU_CONNECT_ROOT env, or walk up from mcp package.
 */
export function getRepoRoot(): string {
  const fromEnv = process.env.QEMU_CONNECT_ROOT;
  if (fromEnv) {
    const r = path.resolve(fromEnv);
    if (fs.existsSync(r)) return r;
  }

  const here = path.dirname(fileURLToPath(import.meta.url));
  const mcpDir = path.resolve(here, "..");
  const repo = path.resolve(mcpDir, "..");
  if (
    fs.existsSync(path.join(repo, "Makefile")) &&
    fs.existsSync(path.join(repo, "plugin"))
  ) {
    return repo;
  }

  throw new Error(
    "Cannot find qemu-connect repo. Set QEMU_CONNECT_ROOT to the tool checkout."
  );
}

/** Your munux kernel tree (the one you develop). */
export function getMunuxRoot(): string | null {
  const env = process.env.QEMU_CONNECT_MUNUX;
  if (env && fs.existsSync(env)) {
    return path.resolve(env);
  }
  const bundled = path.join(getRepoRoot(), "test", "munux");
  if (fs.existsSync(bundled)) {
    return bundled;
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
