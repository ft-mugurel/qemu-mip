import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

/**
 * Resolve qemu-connect repo root.
 * Priority: QEMU_CONNECT_ROOT env → walk up from this package.
 */
export function getRepoRoot(): string {
  const fromEnv = process.env.QEMU_CONNECT_ROOT;
  if (fromEnv && fs.existsSync(path.join(fromEnv, "build"))) {
    return path.resolve(fromEnv);
  }

  // mcp/ is inside the repo: .../qemu-connect/mcp
  const here = path.dirname(fileURLToPath(import.meta.url));
  // dist/ or src/
  const mcpDir = path.resolve(here, "..");
  const repo = path.resolve(mcpDir, "..");
  if (fs.existsSync(path.join(repo, "Makefile")) &&
      fs.existsSync(path.join(repo, "plugin"))) {
    return repo;
  }

  throw new Error(
    "Cannot find qemu-connect repo root. Set QEMU_CONNECT_ROOT to the checkout path."
  );
}

export function getCliPath(root: string): string {
  return path.join(root, "build", "qemu-connect");
}

export function getPluginPath(root: string): string {
  return path.join(root, "build", "libqemu-connect.so");
}
