import { spawn } from "node:child_process";
import fs from "node:fs";
import { getCliPath, getRepoRoot } from "./paths.js";

export type CliResult = {
  exitCode: number;
  stdout: string;
  stderr: string;
  command: string;
  cwd: string;
};

/**
 * Run build/qemu-connect with args from the repo root.
 * Does not stream; collects full output (fine for guest/run).
 */
export function runQemuConnect(
  args: string[],
  options?: { timeoutMs?: number }
): Promise<CliResult> {
  const root = getRepoRoot();
  const cli = getCliPath(root);
  if (!fs.existsSync(cli)) {
    return Promise.resolve({
      exitCode: 4,
      stdout: "",
      stderr:
        `CLI not found: ${cli}\nRun: make plugin cli  (in ${root})`,
      command: `${cli} ${args.join(" ")}`,
      cwd: root,
    });
  }

  const timeoutMs = options?.timeoutMs ?? 180_000;
  const command = `${cli} ${args.map(shellQuote).join(" ")}`;

  return new Promise((resolve) => {
    const child = spawn(cli, args, {
      cwd: root,
      env: process.env,
      stdio: ["ignore", "pipe", "pipe"],
    });

    let stdout = "";
    let stderr = "";
    child.stdout?.on("data", (d: Buffer) => {
      stdout += d.toString("utf8");
    });
    child.stderr?.on("data", (d: Buffer) => {
      stderr += d.toString("utf8");
    });

    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      resolve({
        exitCode: 1,
        stdout,
        stderr: stderr + `\n(MCP: killed after ${timeoutMs}ms)`,
        command,
        cwd: root,
      });
    }, timeoutMs);

    child.on("error", (err) => {
      clearTimeout(timer);
      resolve({
        exitCode: 4,
        stdout,
        stderr: stderr + String(err),
        command,
        cwd: root,
      });
    });

    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({
        exitCode: code ?? 1,
        stdout,
        stderr,
        command,
        cwd: root,
      });
    });
  });
}

function shellQuote(s: string): string {
  if (/^[a-zA-Z0-9_./:@+-]+$/.test(s)) return s;
  return `'${s.replace(/'/g, `'\\''`)}'`;
}

export function formatCliResult(r: CliResult): string {
  const parts = [
    `command: ${r.command}`,
    `cwd: ${r.cwd}`,
    `exit_code: ${r.exitCode}`,
    "",
    "--- stderr (console / progress) ---",
    r.stderr.trimEnd() || "(empty)",
    "",
    "--- stdout (JSON summary) ---",
    r.stdout.trimEnd() || "(empty)",
  ];
  return parts.join("\n");
}
