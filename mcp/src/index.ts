#!/usr/bin/env node
/**
 * qemu-connect MCP server (stdio).
 *
 * Step 1 scaffold: process starts, speaks MCP, exposes tools that wrap the
 * existing CLI. Phase 2 will expand tools; guest is the first real tool.
 *
 * Host config example (Cursor / Claude Desktop):
 * {
 *   "mcpServers": {
 *     "qemu-connect": {
 *       "command": "npx",
 *       "args": ["tsx", "/path/to/qemu-connect/mcp/src/index.ts"],
 *       "env": { "QEMU_CONNECT_ROOT": "/path/to/qemu-connect" }
 *     }
 *   }
 * }
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { getRepoRoot, getCliPath, getPluginPath } from "./paths.js";
import { formatCliResult, runQemuConnect } from "./run-cli.js";
import fs from "node:fs";

const server = new McpServer({
  name: "qemu-connect",
  version: "0.1.0",
});

/** Ping / diagnostics: resolve paths without booting QEMU. */
server.registerTool(
  "qemu_connect_info",
  {
    description:
      "Report qemu-connect install paths and whether the CLI/plugin binaries exist. Use before guest runs to diagnose setup.",
    inputSchema: {},
  },
  async () => {
    try {
      const root = getRepoRoot();
      const cli = getCliPath(root);
      const plugin = getPluginPath(root);
      const text = [
        `repo_root: ${root}`,
        `cli: ${cli}  exists=${fs.existsSync(cli)}`,
        `plugin: ${plugin}  exists=${fs.existsSync(plugin)}`,
        `QEMU_CONNECT_ROOT: ${process.env.QEMU_CONNECT_ROOT ?? "(unset)"}`,
        "",
        "Next: build with `make plugin cli` if binaries are missing,",
        "then call tool qemu_guest.",
      ].join("\n");
      return { content: [{ type: "text" as const, text }] };
    } catch (e) {
      return {
        content: [
          {
            type: "text" as const,
            text: `error: ${e instanceof Error ? e.message : String(e)}`,
          },
        ],
        isError: true,
      };
    }
  }
);

/**
 * Primary agent tool — wraps `./build/qemu-connect guest [cmd...]`.
 * Implemented in scaffold so the server is useful immediately; more tools later.
 */
server.registerTool(
  "qemu_guest",
  {
    description:
      "Boot munux under QEMU via qemu-connect guest, optionally type a shell command " +
      "(e.g. help, ls), return VGA console text + JSON summary. " +
      "Exit code 0 means success. Requires test/munux iso+disk and built CLI.",
    inputSchema: {
      cmd: z
        .string()
        .optional()
        .describe(
          "Optional munux shell command line, e.g. \"help\" or \"ls bin\". " +
            "Omit to only boot and show the console at munux>."
        ),
      timeout_ms: z
        .number()
        .int()
        .positive()
        .optional()
        .describe("Kill the guest process after this many ms (default 180000)"),
    },
  },
  async ({ cmd, timeout_ms }) => {
    const args = ["guest"];
    if (cmd && cmd.trim()) {
      // Split on spaces for simple multi-word commands (cat hello.txt)
      args.push(...cmd.trim().split(/\s+/));
    }
    const r = await runQemuConnect(args, { timeoutMs: timeout_ms ?? 180_000 });
    const text = formatCliResult(r);
    return {
      content: [{ type: "text" as const, text }],
      isError: r.exitCode !== 0,
    };
  }
);

async function main(): Promise<void> {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  // Logging must go to stderr — stdout is MCP JSON-RPC
  console.error("qemu-connect MCP server running on stdio");
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
