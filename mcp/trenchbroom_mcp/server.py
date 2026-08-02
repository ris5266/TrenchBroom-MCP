from __future__ import annotations

import asyncio
import getpass
import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Any

import mcp.types as types
from mcp.server import Server
from mcp.server.stdio import stdio_server

CALL_TIMEOUT_SECONDS = 30.0

MAX_RESPONSE_BYTES = 64 * 1024 * 1024


def socket_name() -> str:
    user = os.environ.get("USER") or getpass.getuser()
    return f"trenchbroom-mcp-{user}"


def candidate_socket_paths() -> list[Path]:
    if override := os.environ.get("TB_MCP_SOCKET"):
        return [Path(override)]

    name = socket_name()
    directories = [Path(tempfile.gettempdir())]
    if runtime_dir := os.environ.get("XDG_RUNTIME_DIR"):
        directories.insert(0, Path(runtime_dir))

    return [directory / name for directory in directories]


def find_socket_path() -> Path | None:
    for path in candidate_socket_paths():
        if path.is_socket():
            return path
    return None


def schema_path() -> Path:
    if override := os.environ.get("TB_MCP_SCHEMA"):
        return Path(override)
    repo_root = Path(__file__).resolve().parents[2]
    return repo_root / "app" / "TrenchBroom" / "resources" / "mcp" / "tools.json"


def load_tools() -> list[types.Tool]:
    path = schema_path()
    if not path.is_file():
        raise SystemExit(
            f"Tool schema not found at {path}.\n"
            "Generate it with: cmake --build build --target TbMcpSchema"
        )

    with path.open(encoding="utf-8") as stream:
        entries = json.load(stream)

    return [
        types.Tool(
            name=entry["name"],
            description=entry["description"],
            inputSchema=entry["inputSchema"],
            annotations=types.ToolAnnotations(readOnlyHint=entry.get("readOnly", False)),
        )
        for entry in entries
    ]


class TrenchBroomConnection:

    def __init__(self) -> None:
        self._lock = asyncio.Lock()

    async def call(self, tool: str, params: dict[str, Any]) -> dict[str, Any]:
        request = json.dumps({"tool": tool, "params": params}, separators=(",", ":"))

        async with self._lock:
            socket_path = find_socket_path()
            if socket_path is None:
                searched = ", ".join(str(p) for p in candidate_socket_paths())
                raise RuntimeError(
                    f"TrenchBroom is not listening (looked in: {searched}).\n"
                    "Start TrenchBroom and enable the MCP server under Preferences."
                )

            try:
                reader, writer = await asyncio.open_unix_connection(
                    str(socket_path), limit=MAX_RESPONSE_BYTES
                )
            except (FileNotFoundError, ConnectionRefusedError, OSError) as error:
                raise RuntimeError(
                    f"Could not reach TrenchBroom on {socket_path}: {error}\n"
                    "Start TrenchBroom and enable the MCP server in Preferences."
                ) from error

            try:
                writer.write(request.encode("utf-8") + b"\n")
                await writer.drain()

                line = await asyncio.wait_for(
                    reader.readline(), timeout=CALL_TIMEOUT_SECONDS
                )
            except asyncio.TimeoutError as error:
                raise RuntimeError(
                    f"TrenchBroom did not respond within {CALL_TIMEOUT_SECONDS:.0f}s."
                ) from error
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except OSError:
                    pass

        if not line:
            raise RuntimeError("TrenchBroom closed the connection without responding.")

        return json.loads(line.decode("utf-8"))


def build_server(connection: TrenchBroomConnection, tools: list[types.Tool]) -> Server:
    server = Server("trenchbroom")

    @server.list_tools()
    async def list_tools() -> list[types.Tool]:
        return tools

    @server.call_tool()
    async def call_tool(name: str, arguments: dict[str, Any]) -> list[types.ContentBlock]:
        response = await connection.call(name, arguments or {})

        if not response.get("ok", False):
            error = response.get("error", {})
            code = error.get("code", "error")
            message = error.get("message", "unknown error")
            raise RuntimeError(f"{code}: {message}")

        result = response.get("result", {})

        if isinstance(result, dict) and result.get("format") == "png" and "data" in result:
            summary = (
                f"{result.get('view', 'viewport')} view, "
                f"{result.get('width')}x{result.get('height')}"
            )
            return [
                types.ImageContent(
                    type="image", data=result["data"], mimeType="image/png"
                ),
                types.TextContent(type="text", text=summary),
            ]

        return [types.TextContent(type="text", text=json.dumps(result, indent=2))]

    return server


async def main_async() -> None:
    tools = load_tools()
    connection = TrenchBroomConnection()
    server = build_server(connection, tools)

    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream, write_stream, server.create_initialization_options()
        )


def main() -> None:
    try:
        asyncio.run(main_async())
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
