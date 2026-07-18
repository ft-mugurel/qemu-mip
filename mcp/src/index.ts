#!/usr/bin/env node
/**
 * qemu-connect MCP server (stdio).
 * Wraps the qemu-connect CLI for AI hosts (Cursor, Claude Desktop, …).
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import fs from "node:fs";
import path from "node:path";
import { getRepoRoot, getCliPath, getPluginPath } from "./paths.js";
import {
  formatCliResult,
  runMake,
  runQemuConnect,
} from "./run-cli.js";

const server = new McpServer({
  name: "qemu-connect",
  version: "0.2.0",
});

function toolOk(text: string, isError = false) {
  return {
    content: [{ type: "text" as const, text }],
    isError,
  };
}

server.registerTool(
  "qemu_connect_info",
  {
    description:
      "Report qemu-connect paths and whether CLI/plugin/munux artifacts exist. Use first to diagnose setup.",
    inputSchema: {},
  },
  async () => {
    try {
      const root = getRepoRoot();
      const cli = getCliPath(root);
      const plugin = getPluginPath(root);
      const iso = path.join(root, "test/munux/build/kernel.iso");
      const disk = path.join(root, "test/munux/build/disk.img");
      const text = [
        `repo_root: ${root}`,
        `cli: ${cli}  exists=${fs.existsSync(cli)}`,
        `plugin: ${plugin}  exists=${fs.existsSync(plugin)}`,
        `munux_iso: ${iso}  exists=${fs.existsSync(iso)}`,
        `munux_disk: ${disk}  exists=${fs.existsSync(disk)}`,
        `QEMU_CONNECT_ROOT: ${process.env.QEMU_CONNECT_ROOT ?? "(unset)"}`,
        "",
        "Tools: qemu_connect_info | qemu_build_guest | qemu_guest | qemu_run",
      ].join("\n");
      return toolOk(text);
    } catch (e) {
      return toolOk(
        `error: ${e instanceof Error ? e.message : String(e)}`,
        true
      );
    }
  }
);

server.registerTool(
  "qemu_build_guest",
  {
    description:
      "Build munux guest artifacts (ISO + disk) and/or qemu-connect plugin+CLI. " +
      "Call before qemu_guest if ISO/disk are missing. Runs make in the repo.",
    inputSchema: {
      what: z
        .enum(["all", "munux", "tool"])
        .optional()
        .describe(
          "all = plugin+cli + munux iso/disk (default); munux = only ISO+disk; tool = only plugin+cli"
        ),
    },
  },
  async ({ what }) => {
    const mode = what ?? "all";
    const root = getRepoRoot();
    const chunks: string[] = [];

    if (mode === "all" || mode === "tool") {
      const r = await runMake(["plugin", "cli"], { timeoutMs: 120_000 });
      chunks.push("### make plugin cli\n" + formatCliResult(r));
      if (r.exitCode !== 0) {
        return toolOk(chunks.join("\n\n"), true);
      }
    }

    if (mode === "all" || mode === "munux") {
      const munux = path.join(root, "test/munux");
      if (!fs.existsSync(munux)) {
        chunks.push(
          `### munux\nerror: ${munux} missing — clone git@github.com:ft-mugurel/munux.git test/munux`
        );
        return toolOk(chunks.join("\n\n"), true);
      }
      const r = await runMake(["iso", "disk"], {
        cwd: munux,
        timeoutMs: 600_000,
      });
      chunks.push("### make -C test/munux iso disk\n" + formatCliResult(r));
      if (r.exitCode !== 0) {
        return toolOk(chunks.join("\n\n"), true);
      }
    }

    chunks.push("### done\nok");
    return toolOk(chunks.join("\n\n"), false);
  }
);

server.registerTool(
  "qemu_guest",
  {
    description:
      "Boot munux under QEMU (ISO+disk), wait for munux>, optionally type a shell command, " +
      "return console + JSON. Maps to: qemu-connect guest [cmd…]. Exit 0 = success.",
    inputSchema: {
      cmd: z
        .string()
        .optional()
        .describe(
          'Shell line, e.g. "help", "ls", "cat hello.txt". Omit to only boot and show console.'
        ),
      timeout_ms: z
        .number()
        .int()
        .positive()
        .optional()
        .describe("Overall timeout ms (default 180000)"),
    },
  },
  async ({ cmd, timeout_ms }) => {
    const args = ["guest"];
    if (cmd && cmd.trim()) {
      args.push(...cmd.trim().split(/\s+/));
    }
    const r = await runQemuConnect(args, {
      timeoutMs: timeout_ms ?? 180_000,
    });
    return toolOk(formatCliResult(r), r.exitCode !== 0);
  }
);

const stepSchema = z.object({
  op: z
    .enum(["expect", "type"])
    .describe("expect = wait for console text; type = type text + Enter"),
  text: z.string().describe("Substring for expect, or keys for type"),
});

server.registerTool(
  "qemu_run",
  {
    description:
      "Custom QEMU script: boot with ISO (optional disk), run ordered expect/type steps, " +
      "optionally show console. Maps to: qemu-connect run --iso … --disk … --expect/--type … --show",
    inputSchema: {
      iso: z
        .string()
        .optional()
        .describe(
          "Path to ISO relative to repo root or absolute. Default: test/munux/build/kernel.iso"
        ),
      disk: z
        .string()
        .optional()
        .describe(
          "Optional IDE disk path. Default for munux: test/munux/build/disk.img if it exists"
        ),
      steps: z
        .array(stepSchema)
        .min(1)
        .describe(
          'Ordered steps, e.g. [{"op":"expect","text":"munux>"},{"op":"type","text":"help"}]'
        ),
      show: z
        .boolean()
        .optional()
        .describe("Print guest console when done (default true)"),
      timeout_ms: z
        .number()
        .int()
        .positive()
        .optional()
        .describe("Per-expect timeout ms (default 60000)"),
      plugin: z.string().optional().describe("Plugin .so path override"),
    },
  },
  async ({ iso, disk, steps, show, timeout_ms, plugin }) => {
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

    const args: string[] = ["run", "--iso", isoPath];
    if (diskPath) {
      args.push("--disk", diskPath);
    }
    if (plugin) {
      args.push(
        "--plugin",
        path.isAbsolute(plugin) ? plugin : path.join(root, plugin)
      );
    } else {
      args.push("--plugin", getPluginPath(root));
    }
    if (timeout_ms) {
      args.push("--timeout", String(timeout_ms));
    }
    for (const s of steps) {
      if (s.op === "expect") {
        args.push("--expect", s.text);
      } else {
        args.push("--type", s.text);
      }
    }
    if (show !== false) {
      args.push("--show");
    }

    const overall =
      (timeout_ms ?? 60_000) * Math.max(1, steps.filter((s) => s.op === "expect").length) +
      60_000;
    const r = await runQemuConnect(args, { timeoutMs: overall });
    return toolOk(formatCliResult(r), r.exitCode !== 0);
  }
);

async function main(): Promise<void> {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("qemu-connect MCP server running on stdio (v0.2)");
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
