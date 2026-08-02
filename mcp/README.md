# TrenchBroom MCP

Lets an MCP client build geometry in a running TrenchBroom, so edits appear in the
editor as they happen.

```
Claude  <--MCP stdio-->  trenchbroom-mcp (Python)
                              |
                    /tmp/trenchbroom-mcp-<user>
                              |
                   [ TrenchBroom, TB_ENABLE_MCP ]
                     QLocalServer on the GUI thread
                              |
                     mdl::Map + CommandProcessor
```

## How it is put together

| Piece | Where | Depends on                |
|---|---|---------------------------|
| Tool implementations, dispatch, selectors | `lib/TbMcpCoreLib` | `TbMdlLib` only, no Qt    |
| Socket, document targeting, guards | `lib/TbMcpLib` | `TbUiLib`, `TbMcpCoreLib` |
| Committed tool schema | `app/TrenchBroom/resources/mcp/tools.json` | generated                 |
| Bridge process | `mcp/trenchbroom_mcp` | `mcp` Python SDK          |

The core library takes an `mdl::Map&` and knows nothing about the GUI, so its tests drive
real JSON against a headless map through `mdl::MapFixture`, no window, no GL context.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DTB_ENABLE_MCP=1
cmake --build build --target TrenchBroom
```

`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` is needed with CMake 4.x: several fetched
dependencies declare a minimum older than CMake 4 accepts.

Then enable it at runtime: it stays off until you ask for it, because it opens a socket
that can drive the editor. In `~/.TrenchBroom/Preferences.json`:

```json
{ "mcp/Enable server": true }
```

## Register the bridge

```bash
claude mcp add trenchbroom -- python3 -m trenchbroom_mcp.server
```

with `mcp/` on `PYTHONPATH`, or install it (`pip install -e mcp/`) and use the
`trenchbroom-mcp` entry point.

The bridge reads `tools.json` at startup rather than asking the editor, so the tool list
is correct even when TrenchBroom is closed; calls made while it is closed fail with an
explanation. Override the socket with `TB_MCP_SOCKET` and the schema with
`TB_MCP_SCHEMA`.

## Tools

| Tool | Kind | What it does |
|---|---|---|
| `ping` | read | Confirms the editor is reachable, reports the open map |
| `get_scene` | read | Layers, groups, brush counts, entity classname counts, bounds, grid, selection |
| `create_brush` | write | One axis-aligned box brush |
| `create_cylinder` | write | Cylinder, or a hollow tube when given a `thickness` |
| `create_cone` | write | Cone tapering along an axis |
| `create_sphere` | write | Sphere, `uv` (stacked rings) or `ico` (subdivided triangles) |
| `create_arch` | write | Semicircular arch as a band of wedge brushes |
| `set_worldspawn_property` | write | Sets a worldspawn key, e.g. `wad` to attach a texture WAD |
| `batch` | write | Several operations as a single undo step |

Every shape is inscribed in a `bounds` box and takes an optional `material`. The curved
ones share two more:

- **`axis`** (`"x"`, `"y"`, `"z"`, default `"z"`) — what the shape is revolved around. For
  the arch it is the direction the opening runs through, as for a tunnel.
- **`circle`** — how the cross-section is approximated: `alignment` of `"edge"` (a flat
  face on the axes), `"vertex"` (a corner there) or `"scalable"` (subdivided so the shape
  survives non-uniform scaling), plus `sides` (default 8) or `precision`.

`create_cylinder` with a `thickness`, and `create_arch`, each produce **several** brushes
from one call. They still land as a single undo step, and `result.created` reports how
many.

### Guarantees

- **One call is one undo step.** A `batch` of twenty brushes is one Ctrl+Z, named after
  the `name` you pass (`MCP: build atrium`).
- **A batch is all or nothing.** If any operation fails the transaction is rolled back, so
  a half-built room never reaches the map.
- **Reads are inert.** Read-only tools restore the selection via `PushSelection` and add
  nothing to the undo stack, so inspecting the map cannot disturb work in progress.
- **Writes leave their output selected**, which is what you would see after making the
  same edit by hand.

### Refusals

Requests are rejected with `editor_busy` while a modal dialog is open or an interactive
tool (clip, vertex, scale…) is mid-gesture, since those hold uncommitted state that
changing the map underneath would corrupt. `no_document` means no map is open.

## Example

```json
{"tool":"batch","params":{"name":"build room","ops":[
  {"tool":"create_brush","params":{"bounds":{"min":[0,0,0],"max":[256,256,16]}}},
  {"tool":"create_brush","params":{"bounds":{"min":[0,0,240],"max":[256,256,256]}}}
]}}
```

Talk to it directly for debugging:

```bash
printf '{"tool":"ping"}\n' | nc -U /tmp/trenchbroom-mcp-$USER
```

## Getting textures to show up

Brushes render untextured until TrenchBroom has a material source, which for Quake means
a palette *and* a WAD. The palette is read from `gfx/palette.lmp` relative to the
configured game path, so with no Quake install there is no palette and nothing resolves.

Point `Games/Quake/Path` at a directory laid out like the game's search path:

```
<game path>/
  id1/
    gfx/palette.lmp              256 RGB triples, 768 bytes
    your.wad                     WAD2, miptex entries
```

Since the palette is just a file at a path you control, a fully self-contained set is
possible without owning Quake: author the palette and index the WAD into it.

Then attach the WAD. Every brush already placed picks up its material at that point,
because `create_brush` records the material name whether or not it resolved at the time:

```json
{"tool":"set_worldspawn_property","params":{"key":"wad","value":"/abs/path/your.wad"}}
```

## Changing the tool surface

The C++ tool table in `lib/TbMcpCoreLib/src/Tools.cpp` is the single source of truth.
After editing it:

```bash
cmake --build build --target TbMcpSchema   # regenerate tools.json
```

`tst_ToolSchema` fails if the committed artifact and the dispatcher disagree, so drift is
a red test rather than a runtime surprise.

## Tests

```bash
cmake --build build --target TbMcpCoreLibTest
./build/lib/TbMcpCoreLib/test/TbMcpCoreLibTest
```

## Not yet implemented

Still to come: `create_entity` against the shipped FGDs, transforms (translate, rotate,
scale), CSG, the selector engine for addressing geometry that already exists, `invoke_action`
over TrenchBroom's ~200 named actions, and orthographic viewport captures.

Until transforms land, everything is axis-aligned, so sloped surfaces have to be built as
stepped geometry.
