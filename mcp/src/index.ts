#!/usr/bin/env node
/**
 * qemu-connect MCP server (stdio) v0.5
 * Session tools + guest console always + disk-lock errors + vi script batch.
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import {
  getRepoRoot,
  getCliPath,
  getPluginPath,
  getGuestRoot,
  getGuestIso,
  getGuestDisk,
} from "./paths.js";
import { runMake, runQemuConnect, type CliResult } from "./run-cli.js";

const server = new McpServer({
  name: "qemu-connect",
  version: "0.5.0",
});

/** Serialize all session mutations in this MCP process (extra safety on top of CLI flock). */
const sessionChains = new Map<string, Promise<unknown>>();

async function withSessionLock<T>(
  sessionId: string,
  fn: () => Promise<T>
): Promise<T> {
  const id = sessionId || "default";
  const prev = sessionChains.get(id) ?? Promise.resolve();
  let release!: () => void;
  const gate = new Promise<void>((r) => {
    release = r;
  });
  const chained = prev.then(() => gate);
  sessionChains.set(
    id,
    chained.catch(() => {
      /* keep chain alive */
    })
  );
  await prev.catch(() => {
    /* ignore prior errors */
  });
  try {
    return await fn();
  } finally {
    release();
  }
}

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

function absPath(p: string | undefined, root: string): string | undefined {
  if (!p) return undefined;
  return path.isAbsolute(p) ? p : path.join(root, p);
}

const VI_RECIPE = [
  "Mini vi recipe (shell prompt is often $):",
  "1) qemu_session_start({ iso, disk, prompt: '$' })",
  "2) qemu_session_cmd({ cmd: 'vi hello.txt', wait: false })  # do not wait for $",
  "3) qemu_session_key({ qcode: 'i' })                       # insert mode",
  "4) qemu_session_type({ text: 'hello', enter: false })",
  "5) qemu_session_key({ qcode: 'esc' })",
  "6) qemu_session_type({ text: ':wq', enter: true, expect: '$' })",
  "7) qemu_session_stop({})",
  "",
  "Or one shot: qemu_session_script({ steps: [",
  "  { op:'cmd', text:'vi hello.txt', wait:false },",
  "  { op:'key', qcode:'i' },",
  "  { op:'type', text:'hello', enter:false },",
  "  { op:'key', qcode:'esc' },",
  "  { op:'type', text:':wq', enter:true, expect:'$' },",
  "] })",
  "",
  "Prefer j/k over arrow keys in vi (arrows may be private-bytes on guest).",
  "Console is glyphs only (no inverse cursor). type/cmd send Enter by default.",
  "Inside vi: omit console_lines (or 0 = full). Tail of non-blank lines is mostly '~'.",
].join("\n");

const TOOL_LIST = [
  "qemu_connect_info",
  "qemu_build_guest",
  "qemu_guest",
  "qemu_run",
  "qemu_session_start",
  "qemu_session_cmd",
  "qemu_session_type",
  "qemu_session_key",
  "qemu_session_script",
  "qemu_session_console",
  "qemu_session_status",
  "qemu_session_stop",
] as const;

server.registerTool(
  "qemu_connect_info",
  {
    description:
      "Report qemu-connect paths and artifact presence. Includes mini vi recipe. Structured JSON.",
    inputSchema: {},
  },
  async () => {
    try {
      const root = getRepoRoot();
      const cli = getCliPath(root);
      const plugin = getPluginPath(root);
      const guest = getGuestRoot();
      const iso = getGuestIso();
      const disk = getGuestDisk();
      return toolJson({
        ok: true,
        repo_root: root,
        cli: { path: cli, exists: fs.existsSync(cli) },
        plugin: { path: plugin, exists: fs.existsSync(plugin) },
        guest_root: { path: guest, exists: !!(guest && fs.existsSync(guest)) },
        guest_iso: { path: iso, exists: fs.existsSync(iso) },
        guest_disk: { path: disk, exists: fs.existsSync(disk) },
        env: {
          QEMU_CONNECT_ROOT: process.env.QEMU_CONNECT_ROOT ?? null,
          QEMU_CONNECT_GUEST: process.env.QEMU_CONNECT_GUEST ?? null,
          QEMU_CONNECT_ISO: process.env.QEMU_CONNECT_ISO ?? null,
          QEMU_CONNECT_DISK: process.env.QEMU_CONNECT_DISK ?? null,
          QEMU_CONNECT_PLUGIN: process.env.QEMU_CONNECT_PLUGIN ?? null,
          QEMU_CONNECT_PROMPT: process.env.QEMU_CONNECT_PROMPT ?? null,
        },
        notes: {
          type_sends_enter:
            "session_type / CLI type send Enter by default; use enter:false or --no-enter for partial (vi).",
          prompt:
            "session_start prompt defaults from QEMU_CONNECT_PROMPT. many shells use '$'.",
          char_map:
            "type maps : ! and common shell/vi punctuation (US QWERTY).",
          console:
            "Console is glyphs only; inverse-video cursor is not visible. guest/run always include console in JSON.",
          console_lines:
            "Pass console_lines=N for last N non-blank lines (shell/help). " +
            "For vi screens omit or use 0 (full console): vi fills the screen with '~' " +
            "lines which count as non-blank, so tail can drop the real buffer.",
          disk_lock:
            "If two boots share disk.img, error is 'disk locked by session X' with stop hint.",
          arrows:
            "Prefer j/k for vi motion; arrow qcodes may map to private bytes on guest kernel.",
          wait_false:
            "session_cmd wait:false for vi/top (no prompt wait). session_type already no-waits by default.",
        },
        vi_recipe: VI_RECIPE,
        tools: [...TOOL_LIST],
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
      "Build guest kernel ISO+disk and/or qemu-connect binaries via make. " +
      "Uses QEMU_CONNECT_GUEST (or guest_path) — not only test/guest.",
    inputSchema: {
      what: z
        .enum(["all", "guest", "tool"])
        .optional()
        .describe("all (default) | guest | tool"),
      guest_path: z
        .string()
        .optional()
        .describe(
          "Absolute or repo-relative path to guest kernel tree (overrides QEMU_CONNECT_GUEST)"
        ),
    },
  },
  async ({ what, guest_path }) => {
    const mode = what ?? "all";
    const root = getRepoRoot();
    const steps: Record<string, unknown>[] = [];

    if (mode === "all" || mode === "tool") {
      const r = await runMake(["plugin", "cli"], { timeoutMs: 120_000 });
      steps.push({
        step: "make plugin cli",
        ...parseCliJson(r),
        raw_exit: r.exitCode,
      });
      if (r.exitCode !== 0) {
        return toolJson({ ok: false, steps }, true);
      }
    }
    if (mode === "all" || mode === "guest") {
      let guest = absPath(guest_path, root) ?? getGuestRoot() ?? undefined;
      if (!guest || !fs.existsSync(guest)) {
        return toolJson(
          {
            ok: false,
            error: "guest tree not found",
            hint:
              "Pass guest_path, or set MCP env QEMU_CONNECT_GUEST=/abs/path/to/your-kernel " +
              "(or pin $QEMU_CONNECT_ROOT/.qemu-connect.local)",
            steps,
          },
          true
        );
      }
      const r = await runMake(["iso", "disk"], {
        cwd: guest,
        timeoutMs: 600_000,
      });
      steps.push({
        step: `make -C ${guest} iso disk`,
        guest_path: guest,
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
      "One-shot: boot guest, optional shell cmd + Enter, teardown. " +
      "JSON always includes console (even on success). Prefer qemu_session_* for multi-cmd/vi.",
    inputSchema: {
      cmd: z.string().optional().describe('e.g. "help" or "ls bin"'),
      iso: z.string().optional(),
      disk: z.string().optional(),
      prompt: z
        .string()
        .optional()
        .describe('Shell prompt to wait for, e.g. "$" or "$"'),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe(
          "Last N non-blank lines (0/omit=full). Use full console for vi (~ lines are non-blank)."
        ),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ cmd, iso, disk, prompt, console_lines, timeout_ms }) => {
    const root = getRepoRoot();
    const args = [
      "guest",
      "--iso",
      absPath(iso, root) ?? getGuestIso(),
      "--disk",
      absPath(disk, root) ?? getGuestDisk(),
      "--plugin",
      getPluginPath(root),
    ];
    if (prompt) {
      args.push("--prompt", prompt);
    }
    if (console_lines && console_lines > 0) {
      args.push("--console-lines", String(console_lines));
    }
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
      "One-shot custom expect/type script (boots then quits). " +
      "Each type step sends Enter. JSON always includes console.",
    inputSchema: {
      iso: z.string().optional(),
      disk: z.string().optional(),
      steps: z.array(stepSchema).min(1),
      show: z.boolean().optional(),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe("Last N non-blank lines; omit/0 for full (prefer full inside vi)"),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ iso, disk, steps, show, console_lines, timeout_ms }) => {
    const root = getRepoRoot();
    const defaultIso = getGuestIso();
    const defaultDisk = getGuestDisk();
    const isoPath = absPath(iso, root) ?? defaultIso;
    let diskPath: string | undefined;
    if (disk) {
      diskPath = absPath(disk, root);
    } else if (fs.existsSync(defaultDisk)) {
      diskPath = defaultDisk;
    }
    const args: string[] = [
      "run",
      "--iso",
      isoPath!,
      "--plugin",
      getPluginPath(root),
    ];
    if (diskPath) {
      args.push("--disk", diskPath);
    }
    if (timeout_ms) {
      args.push("--timeout", String(timeout_ms));
    }
    if (console_lines && console_lines > 0) {
      args.push("--console-lines", String(console_lines));
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

/* ---- Persistent session tools ---- */

server.registerTool(
  "qemu_session_start",
  {
    description:
      "Start a long-lived QEMU session (boot once). Pass iso/disk/prompt for your " +
      "dev kernel (e.g. shell prompt \"$\"). Fails clearly if disk is locked by another session. " +
      "Defaults from QEMU_CONNECT_GUEST / .qemu-connect.local.",
    inputSchema: {
      session_id: z.string().optional().describe("Session name (default)"),
      iso: z.string().optional().describe("Path to kernel.iso"),
      disk: z.string().optional().describe("Path to disk.img"),
      prompt: z
        .string()
        .optional()
        .describe('Shell prompt substring: "$" for shell guests, "$" for classic'),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe("Last N non-blank lines; omit/0 full (use full for vi)"),
      timeout_ms: z.number().int().positive().optional(),
      no_wait: z
        .boolean()
        .optional()
        .describe("If true, do not wait for shell prompt"),
    },
  },
  async ({
    session_id,
    iso,
    disk,
    prompt,
    console_lines,
    timeout_ms,
    no_wait,
  }) => {
    const sid = session_id ?? "default";
    return withSessionLock(sid, async () => {
      const root = getRepoRoot();
      const args = [
        "session",
        "start",
        "--iso",
        absPath(iso, root) ?? getGuestIso(),
        "--disk",
        absPath(disk, root) ?? getGuestDisk(),
        "--plugin",
        getPluginPath(root),
        "--id",
        sid,
      ];
      if (prompt) {
        args.push("--prompt", prompt);
      }
      if (console_lines && console_lines > 0) {
        args.push("--console-lines", String(console_lines));
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
    });
  }
);

server.registerTool(
  "qemu_session_cmd",
  {
    description:
      "Run a shell command + Enter in an existing session. " +
      "wait defaults true (wait for prompt). Set wait:false for vi/top. " +
      "Serialized per session. For vi keystrokes prefer session_key/type/script.",
    inputSchema: {
      cmd: z.string().describe('Shell line, e.g. "help" or "vi file.txt"'),
      session_id: z.string().optional(),
      prompt: z
        .string()
        .optional()
        .describe("Override wait prompt for this cmd (default: session prompt)"),
      wait: z
        .boolean()
        .optional()
        .describe(
          "Wait for shell prompt after cmd (default true). false for vi/top."
        ),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe("Last N non-blank lines; omit/0 full (use full after launching vi)"),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ cmd, session_id, prompt, wait, console_lines, timeout_ms }) => {
    const sid = session_id ?? "default";
    return withSessionLock(sid, async () => {
      const args = ["session", "cmd", "--id", sid];
      if (prompt) {
        args.push("--prompt", prompt);
      }
      if (wait === false) {
        args.push("--no-wait");
      }
      if (console_lines && console_lines > 0) {
        args.push("--console-lines", String(console_lines));
      }
      if (timeout_ms) {
        args.push("--timeout", String(timeout_ms));
      }
      args.push(...cmd.trim().split(/\s+/));
      const r = await runQemuConnect(args, {
        timeoutMs: (timeout_ms ?? 30_000) + 15_000,
      });
      return toolFromCli(r);
    });
  }
);

server.registerTool(
  "qemu_session_type",
  {
    description:
      "Type text into the guest (no shell-prompt wait by default). " +
      "enter defaults true. Use enter:false for vi insert. Supports : ! punctuation. " +
      "If expect is set and times out, console is still returned. " +
      "Prefer j/k over arrow keys for vi. " +
      VI_RECIPE,
    inputSchema: {
      text: z.string().describe('Text to type, e.g. ":wq" or "hello"'),
      enter: z
        .boolean()
        .optional()
        .describe("Press Enter after text (default true)"),
      expect: z
        .string()
        .optional()
        .describe("Optional console substring to wait for after typing"),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe(
          "Last N non-blank lines; omit/0 for full. Prefer full while in vi (~ dominates tail)."
        ),
      session_id: z.string().optional(),
      timeout_ms: z.number().int().positive().optional(),
    },
  },
  async ({ text, enter, expect, console_lines, session_id, timeout_ms }) => {
    const sid = session_id ?? "default";
    return withSessionLock(sid, async () => {
      const args = ["session", "type", text, "--id", sid];
      if (enter === false) {
        args.push("--no-enter");
      } else {
        args.push("--enter");
      }
      if (expect) {
        args.push("--expect", expect);
      }
      if (console_lines && console_lines > 0) {
        args.push("--console-lines", String(console_lines));
      }
      if (timeout_ms) {
        args.push("--timeout", String(timeout_ms));
      }
      const r = await runQemuConnect(args, {
        timeoutMs: (timeout_ms ?? 30_000) + 10_000,
      });
      return toolFromCli(r);
    });
  }
);

server.registerTool(
  "qemu_session_key",
  {
    description:
      "Send a single QMP key (or repeat) — for vi (esc, j, k, ret). " +
      "qcode: esc, ret, backspace, tab, j, k, a-z, 0-9. " +
      "Prefer j/k over up/down for vi (arrows may be private bytes on guest).",
    inputSchema: {
      qcode: z
        .string()
        .describe('QEMU qcode, e.g. "esc", "j", "ret" (prefer j/k over arrows)'),
      repeat: z
        .number()
        .int()
        .min(1)
        .max(100)
        .optional()
        .describe("How many times to send (default 1)"),
      session_id: z.string().optional(),
    },
  },
  async ({ qcode, repeat, session_id }) => {
    const sid = session_id ?? "default";
    return withSessionLock(sid, async () => {
      const args = ["session", "key", qcode, "--id", sid];
      if (repeat && repeat > 1) {
        args.push("--repeat", String(repeat));
      }
      const r = await runQemuConnect(args, { timeoutMs: 20_000 });
      return toolFromCli(r);
    });
  }
);

const scriptStepSchema = z.object({
  op: z
    .enum(["type", "key", "cmd", "expect", "console"])
    .describe("Step kind"),
  text: z
    .string()
    .optional()
    .describe("For type/cmd: text or shell command"),
  qcode: z.string().optional().describe("For key: qcode e.g. esc, j"),
  enter: z.boolean().optional().describe("For type: press Enter (default true)"),
  expect: z
    .string()
    .optional()
    .describe("For type: wait for this console substring after"),
  wait: z
    .boolean()
    .optional()
    .describe("For cmd: wait for prompt (default true)"),
  prompt: z.string().optional().describe("For cmd: prompt override"),
  repeat: z.number().int().min(1).max(100).optional(),
  timeout_ms: z.number().int().positive().optional(),
});

server.registerTool(
  "qemu_session_script",
  {
    description:
      "Run a batch of session steps under one lock (fewer round-trips for vi scripts). " +
      "Each step: {op:'type'|'key'|'cmd'|'expect'|'console', ...}. " +
      VI_RECIPE,
    inputSchema: {
      steps: z.array(scriptStepSchema).min(1),
      session_id: z.string().optional(),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe(
          "Last N non-blank lines on step consoles; omit/0 full. Use full for vi scripts."
        ),
    },
  },
  async ({ steps, session_id, console_lines }) => {
    const sid = session_id ?? "default";
    return withSessionLock(sid, async () => {
      const results: Record<string, unknown>[] = [];
      let lastConsole: string | null = null;
      let allOk = true;

      for (let i = 0; i < steps.length; i++) {
        const s = steps[i]!;
        const args: string[] = ["session"];
        if (s.op === "type") {
          if (!s.text) {
            return toolJson(
              { ok: false, error: `step ${i}: type needs text`, results },
              true
            );
          }
          args.push("type", s.text, "--id", sid);
          if (s.enter === false) args.push("--no-enter");
          else args.push("--enter");
          if (s.expect) args.push("--expect", s.expect);
          if (s.timeout_ms) args.push("--timeout", String(s.timeout_ms));
          if (console_lines && console_lines > 0) {
            args.push("--console-lines", String(console_lines));
          }
        } else if (s.op === "key") {
          if (!s.qcode) {
            return toolJson(
              { ok: false, error: `step ${i}: key needs qcode`, results },
              true
            );
          }
          args.push("key", s.qcode, "--id", sid);
          if (s.repeat && s.repeat > 1) {
            args.push("--repeat", String(s.repeat));
          }
        } else if (s.op === "cmd") {
          if (!s.text) {
            return toolJson(
              { ok: false, error: `step ${i}: cmd needs text`, results },
              true
            );
          }
          args.push("cmd", "--id", sid);
          if (s.wait === false) args.push("--no-wait");
          if (s.prompt) args.push("--prompt", s.prompt);
          if (s.timeout_ms) args.push("--timeout", String(s.timeout_ms));
          if (console_lines && console_lines > 0) {
            args.push("--console-lines", String(console_lines));
          }
          args.push(...s.text.trim().split(/\s+/));
        } else if (s.op === "expect") {
          if (!s.text && !s.expect) {
            return toolJson(
              { ok: false, error: `step ${i}: expect needs text`, results },
              true
            );
          }
          args.push(
            "expect",
            s.text ?? s.expect!,
            "--id",
            sid
          );
          if (s.timeout_ms) args.push("--timeout", String(s.timeout_ms));
        } else if (s.op === "console") {
          args.push("console", "--id", sid);
          if (console_lines && console_lines > 0) {
            args.push("--console-lines", String(console_lines));
          }
        }

        const r = await runQemuConnect(args, {
          timeoutMs: (s.timeout_ms ?? 30_000) + 15_000,
        });
        const payload = parseCliJson(r);
        results.push({
          step: i,
          op: s.op,
          ok: payload.ok ?? r.exitCode === 0,
          ...payload,
        });
        if (typeof payload.console === "string") {
          lastConsole = payload.console;
        }
        if (payload.ok === false || r.exitCode !== 0) {
          allOk = false;
          break;
        }
      }

      return toolJson(
        {
          ok: allOk,
          exit_code: allOk ? 0 : 1,
          session_id: sid,
          steps_run: results.length,
          results,
          console: lastConsole,
        },
        !allOk
      );
    });
  }
);

server.registerTool(
  "qemu_session_console",
  {
    description:
      "Read current VGA console text (glyphs only; cursor not shown). " +
      "console_lines=N returns last N non-blank lines (good for shell). " +
      "Inside vi, omit console_lines or pass 0 — full screen; otherwise '~' lines dominate the tail.",
    inputSchema: {
      session_id: z.string().optional(),
      console_lines: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe("Last N non-blank lines; omit/0=full (prefer full in vi)"),
    },
  },
  async ({ session_id, console_lines }) => {
    const args = ["session", "console"];
    if (session_id) {
      args.push("--id", session_id);
    }
    if (console_lines && console_lines > 0) {
      args.push("--console-lines", String(console_lines));
    }
    const r = await runQemuConnect(args, { timeoutMs: 15_000 });
    return toolFromCli(r);
  }
);

server.registerTool(
  "qemu_session_status",
  {
    description:
      "Session liveness + guest plugin status. Includes prompt and last_expect for stuck-session debugging.",
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
    const sid = session_id ?? "default";
    return withSessionLock(sid, async () => {
      const args = ["session", "stop", "--id", sid];
      const r = await runQemuConnect(args, { timeoutMs: 30_000 });
      return toolFromCli(r);
    });
  }
);

async function main(): Promise<void> {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error(
    "qemu-connect MCP server running on stdio (v0.5 console+lock+script)"
  );
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
