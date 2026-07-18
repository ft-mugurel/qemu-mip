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

function shellQuote(s: string): string {
  if (/^[a-zA-Z0-9_./:@+-]+$/.test(s)) return s;
  return `'${s.replace(/'/g, `'\\''`)}'`;
}

function runCommand(
  bin: string,
  args: string[],
  cwd: string,
  timeoutMs: number
): Promise<CliResult> {
  const command = `${bin} ${args.map(shellQuote).join(" ")}`;
  return new Promise((resolve) => {
    const child = spawn(bin, args, {
      cwd,
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
        cwd,
      });
    }, timeoutMs);

    child.on("error", (err) => {
      clearTimeout(timer);
      resolve({
        exitCode: 4,
        stdout,
        stderr: stderr + String(err),
        command,
        cwd,
      });
    });

    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({
        exitCode: code ?? 1,
        stdout,
        stderr,
        command,
        cwd,
      });
    });
  });
}

/** Run build/qemu-connect with args from the repo root. */
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
      stderr: `CLI not found: ${cli}\nRun: make plugin cli  (in ${root})`,
      command: `${cli} ${args.join(" ")}`,
      cwd: root,
    });
  }
  return runCommand(cli, args, root, options?.timeoutMs ?? 180_000);
}

/** Run make (or other) in the repo root. */
export function runMake(
  makeArgs: string[],
  options?: { timeoutMs?: number; cwd?: string }
): Promise<CliResult> {
  const root = options?.cwd ?? getRepoRoot();
  return runCommand("make", makeArgs, root, options?.timeoutMs ?? 300_000);
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
    "--- stdout (JSON / build log) ---",
    r.stdout.trimEnd() || "(empty)",
  ];
  return parts.join("\n");
}
