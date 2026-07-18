#!/usr/bin/env node
/**
 * qemu-connect MCP server (stdio) v0.3
 * Structured JSON tool results + session tools for multi-cmd without reboot.
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import { getRepoRoot, getCliPath, getPluginPath } from "./paths.js";
import { runMake, runQemuConnect, type CliResult } from "./run-cli.js";

const server = new McpServer({
  name: "qemu-connect",
  version: "0.3.0",
});

/** Prefer parsing CLI JSON line from stdout; fall back to raw text. */
function parseCliJson(r: CliResult): Record<string, unknown> {
  const lines = r.stdout
    .split("\n")
    .map((l) => l.trim())
    .filter(Boolean);
  for (let i = lines.length - 1; i >= 0; i--) {
    try {
      const o = JSON.parse(lines[i]!) as Record<string, unknown>;
      if (o && typeof o === "object") {
        return {
          ...o,
          _mcp: {
            exit_code: r.exitCode,
            command: r.command,
            cwd: r.cwd,
          },
        };
      }
    } catch {
      /* keep looking */
    }
  }
  return {
    ok: r.exitCode === 0,
    exit_code: r.exitCode,
    command: r.command,
    cwd: r.cwd,
    stdout: r.stdout,
    stderr: r.stderr,
  };
}

function toolJson(payload: Record<string, unknown>, isError = false) {
  const text = JSON.stringify(payload, null, 2);
  return {
    content: [{ type: "text" as const, text }],
    isError,
  };
}

function toolFromCli(r: CliResult) {
  const payload = parseCliJson(r);
  const ok =
    typeof payload.ok === "boolean" ? payload.ok : r.exitCode === 0;
  return toolJson(payload, !ok || r.exitCode !== 0);
}

server.registerTool(
  "qemu_connect_info",
  {
    description:
      "Report qemu-connect paths and artifact presence. Structured JSON.",
    inputSchema: {},
  },
  async () => {
    try {
      const root = getRepoRoot();
      const cli = getCliPath(root);
      const plugin = getPluginPath(root);
      const iso = path.join(root, "test/munux/build/kernel.iso");
      const disk = path.join(root, "test/munux/build/disk.img");
      return toolJson({
        ok: true,
        repo_root: root,
        cli: { path: cli, exists: fs.existsSync(cli) },
        plugin: { path: plugin, exists: fs.existsSync(plugin) },
        munux_iso: { path: iso, exists: fs.existsSync(iso) },
        munux_disk: { path: disk, exists: fs.existsSync(disk) },
        env: {
          QEMU_CONNECT_ROOT: process.env.QEMU_CONNECT_ROOT ?? null,
        },
        tools: [
          "qemu_connect_info",
          "qemu_build_guest",
          "qemu_guest",
          "qemu_run",
          "qemu_session_start",
          "qemu_session_cmd",
          "qemu_session_console",
          "qemu_session_status",
          "qemu_session_stop",
        ],
      });
    } catch (e) {
      return toolJson(
        {
          ok: false,
          error: e instanceof Error ? e.message : String(e),
        },
        true
      );
    }
  }
);

server.registerTool(
  "qemu_build_guest",
  {
    description:
      "Build munux ISO/disk and/or qemu-connect binaries via make. Structured JSON.",
    inputSchema: {
      what: z
        .enum(["all", "munux", "tool"])
        .optional()
        .describe("all (default) | munux | tool"),
    },
  },
  async ({ what }) => {
    const mode = what ?? "all";
    const root = getRepoRoot();
    const steps: Record<string, unknown>[] = [];

    if (mode === "all" || mode === "tool") {
      const r = await runMake(["plugin", "cli"], { timeoutMs: 120_000 });
      steps.push({ step: "make plugin cli", ...parseCliJson(r), raw_exit: r.exitCode });
      if (r.exitCode !== 0) {
        return toolJson({ ok: false, steps }, true);
      }
    }
    if (mode === "all" || mode === "munux") {
      const munux = path.join(root, "test/munux");
      if (!fs.existsSync(munux)) {
        return toolJson(
          {
            ok: false,
            error: `missing ${munux}`,
            hint: "git clone git@github.com:ft-mugurel/munux.git test/munux",
            steps,
          },
          true
        );
      }
      const r = await runMake(["iso", "disk"], {
        cwd: munux,
        timeoutMs: 600_000,
      });
      steps.push({
        step: "make -C test/munux iso disk",
        ...parseCliJson(r),
        raw_exit: r.exitCode,
      });
      if (r.exitCode !== 0) {
        return toolJson({ ok: false, steps }, true);
      }
    }
    return toolJson({ ok: true, steps });
  }
);

server.registerTool(
  "qemu_guest",
  {
    description:
      "One-shot: boot munux, optional shell cmd, teardown. Prefer qemu_session_* for multiple commands. Returns structured JSON including console.",
    inputSchema: {
      cmd: z.string().optional().describe('e.g. "help" or "ls bin"'),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ cmd, timeout_ms }) => {
    const args = ["guest"];
    if (cmd?.trim()) {
      args.push(...cmd.trim().split(/\s+/));
    }
    const r = await runQemuConnect(args, {
      timeoutMs: timeout_ms ?? 180_000,
    });
    return toolFromCli(r);
  }
);

const stepSchema = z.object({
  op: z.enum(["expect", "type"]),
  text: z.string(),
});

server.registerTool(
  "qemu_run",
  {
    description:
      "One-shot custom expect/type script (boots then quits). Returns structured JSON.",
    inputSchema: {
      iso: z.string().optional(),
      disk: z.string().optional(),
      steps: z.array(stepSchema).min(1),
      show: z.boolean().optional(),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ iso, disk, steps, show, timeout_ms }) => {
    const root = getRepoRoot();
    const defaultIso = path.join(root, "test/munux/build/kernel.iso");
    const defaultDisk = path.join(root, "test/munux/build/disk.img");
    const isoPath = iso
      ? path.isAbsolute(iso)
        ? iso
        : path.join(root, iso)
      : defaultIso;
    let diskPath: string | undefined;
    if (disk) {
      diskPath = path.isAbsolute(disk) ? disk : path.join(root, disk);
    } else if (fs.existsSync(defaultDisk)) {
      diskPath = defaultDisk;
    }
    const args: string[] = [
      "run",
      "--iso",
      isoPath,
      "--plugin",
      getPluginPath(root),
    ];
    if (diskPath) {
      args.push("--disk", diskPath);
    }
    if (timeout_ms) {
      args.push("--timeout", String(timeout_ms));
    }
    for (const s of steps) {
      args.push(s.op === "expect" ? "--expect" : "--type", s.text);
    }
    if (show !== false) {
      args.push("--show");
    }
    const expects = steps.filter((s) => s.op === "expect").length;
    const overall =
      (timeout_ms ?? 60_000) * Math.max(1, expects) + 60_000;
    const r = await runQemuConnect(args, { timeoutMs: overall });
    return toolFromCli(r);
  }
);

/* ---- Persistent session tools (P0) ---- */

server.registerTool(
  "qemu_session_start",
  {
    description:
      "Start a long-lived munux QEMU session (boot once). Use qemu_session_cmd for further commands. Default session_id=default.",
    inputSchema: {
      session_id: z.string().optional().describe("Session name (default)"),
      timeout_ms: z.number().int().positive().optional(),
      no_wait: z
        .boolean()
        .optional()
        .describe("If true, do not wait for munux> prompt"),
    },
  },
  async ({ session_id, timeout_ms, no_wait }) => {
    const args = ["session", "start"];
    if (session_id) {
      args.push("--id", session_id);
    }
    if (timeout_ms) {
      args.push("--timeout", String(timeout_ms));
    }
    if (no_wait) {
      args.push("--no-wait");
    }
    const r = await runQemuConnect(args, {
      timeoutMs: (timeout_ms ?? 60_000) + 30_000,
    });
    return toolFromCli(r);
  }
);

server.registerTool(
  "qemu_session_cmd",
  {
    description:
      "Run a shell command in an existing session (no reboot). Requires qemu_session_start first.",
    inputSchema: {
      cmd: z.string().describe('Shell line, e.g. "help" or "ls bin"'),
      session_id: z.string().optional(),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ cmd, session_id, timeout_ms }) => {
    const args = ["session", "cmd"];
    args.push(...cmd.trim().split(/\s+/));
    if (session_id) {
      args.push("--id", session_id);
    }
    if (timeout_ms) {
      args.push("--timeout", String(timeout_ms));
    }
    const r = await runQemuConnect(args, {
      timeoutMs: (timeout_ms ?? 30_000) + 15_000,
    });
    return toolFromCli(r);
  }
);

server.registerTool(
  "qemu_session_console",
  {
    description: "Read current VGA console text from an open session.",
    inputSchema: {
      session_id: z.string().optional(),
    },
  },
  async ({ session_id }) => {
    const args = ["session", "console"];
    if (session_id) {
      args.push("--id", session_id);
    }
    const r = await runQemuConnect(args, { timeoutMs: 15_000 });
    return toolFromCli(r);
  }
);

server.registerTool(
  "qemu_session_status",
  {
    description: "Session liveness + guest plugin status JSON.",
    inputSchema: {
      session_id: z.string().optional(),
    },
  },
  async ({ session_id }) => {
    const args = ["session", "status"];
    if (session_id) {
      args.push("--id", session_id);
    }
    const r = await runQemuConnect(args, { timeoutMs: 15_000 });
    return toolFromCli(r);
  }
);

server.registerTool(
  "qemu_session_stop",
  {
    description: "Stop session: QMP quit, remove session file.",
    inputSchema: {
      session_id: z.string().optional(),
    },
  },
  async ({ session_id }) => {
    const args = ["session", "stop"];
    if (session_id) {
      args.push("--id", session_id);
    }
    const r = await runQemuConnect(args, { timeoutMs: 30_000 });
    return toolFromCli(r);
  }
);

async function main(): Promise<void> {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("qemu-connect MCP server running on stdio (v0.3 session+json)");
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
