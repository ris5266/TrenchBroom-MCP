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
| `translate` | write | Move objects by a delta |
| `rotate` | write | Rotate about an axis, in degrees |
| `scale` | write | Scale by factors, or fit into a target box |
| `csg_merge` | write | Merge brushes into their convex hull |
| `csg_subtract` | write | Carve brushes out of everything they touch |
| `csg_intersect` | write | Keep only the volume brushes share |
| `csg_hollow` | write | Turn solid brushes into shells with walls |
| `flip` | write | Mirror objects across a plane |
| `shear` | write | Slant objects by sliding one side |
| `select_objects` | write | Select objects in the editor by a query |
| `create_entity` | write | Create a point or brush entity from the game's definitions |
| `list_entity_definitions` | read | Classnames the current game defines |
| `invoke_action` | write | Run one of TrenchBroom's own named actions |
| `list_actions` | read | The named actions and whether each is enabled |
| `capture_viewport` | read | Render a view of the map as a PNG |
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

### What a transform acts on

There is no persistent handle for a brush, undo and redo replace nodes, so transforms
resolve their target fresh on every call, via `target`:

| `target` | Acts on |
|---|---|
| `"auto"` (default) | What this call created, if anything; otherwise the editor's selection |
| `"created"` | Everything earlier operations in this same call produced |
| `"last"` | Only what the previous operation produced |
| `"selection"` | Only what is selected in the editor |

That default is what makes building inside a `batch` read naturally — make a shape, then
move it, in one undo step:

```json
{"tool":"batch","params":{"name":"leaning pillar","ops":[
  {"tool":"create_brush","params":{"bounds":{"min":[0,0,0],"max":[96,96,384]}}},
  {"tool":"rotate","params":{"angle":20,"axis":"y"}}
]}}
```

Angles are **degrees**. `rotate` and `scale` default to the centre of the objects' own
bounds, so they turn and grow in place unless you pass a `center`. `scale` takes either
`factors` (a number for uniform scaling, or `[x, y, z]`) or a `bounds` box to fit into.

Transforms restore the editor's selection when they finish, so using one on your selection
does not change what you have selected.

### Selecting what to act on

Any tool that acts on existing geometry takes either `target` or a `select` query, and
`select` wins when both are given. Queries are resolved fresh on every call, so undo and
redo cannot invalidate them:

```json
{"tool":"translate","params":{"delta":[0,0,64],"select":{"material":"lava*"}}}
{"tool":"select_objects","params":{"select":{"classname":"func_*","layer":"Doors"}}}
```

| Field | Matches |
|---|---|
| `all` | Everything in the map |
| `classname` | Entities with that classname, and their brushes. Trailing `*` matches a prefix |
| `material` | Brushes with a face using that material. Trailing `*` matches a prefix |
| `layer` | Objects in the named layer |
| `bounds` + `mode` | Objects inside the box, or merely `touching` it |

All given fields must match. An empty query is rejected rather than quietly matching the
whole map.

### Entities

`create_entity` reads the game's own FGD files, so the classnames available are whatever
the game defines — 76 point and 24 brush classes for Quake. Point entities take an
`origin`; brush entities take over the targeted brushes instead.

```json
{"tool":"create_entity","params":{
  "classname":"light","origin":[256,256,200],"properties":{"light":"400"}}}
{"tool":"create_entity","params":{"classname":"func_door","target":"selection"}}
```

`list_entity_definitions` shows what the current game offers.

### CSG

CSG *consumes* what it acts on: the source brushes are deleted and the results take their
place. So unlike a transform, these do not put the previous selection back — the result is
left selected, which is what the editor does when you run the same command by hand.

`target: "last"` exists for exactly this. Carving a doorway means subtracting **only** the
cutting brush from the walls; with `"created"` the walls would be subtrahends too and
there would be nothing left to cut into. A sealed room with a door, as one undo step:

```json
{"tool":"batch","params":{"name":"room with doorway","ops":[
  {"tool":"create_brush","params":{"bounds":{"min":[0,0,0],"max":[512,512,256]}}},
  {"tool":"csg_hollow","params":{"thickness":16}},
  {"tool":"create_brush","params":{"bounds":{"min":[-32,200,0],"max":[32,312,160]}}},
  {"tool":"csg_subtract","params":{"target":"last"}}
]}}
```

`csg_hollow`'s `thickness` must be a power of two between 0.125 and 256, because
TrenchBroom applies it through the editor's grid. The tool pins the grid for the duration
and restores it, so the same call gives the same geometry regardless of the user's grid
setting; omit `thickness` to use whatever the grid currently is.

`csg_intersect` needs at least two brushes. All four fail cleanly, rolling the whole call
back, when the brushes do not overlap in a way the operation can use.

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

### TrenchBroom's own actions

`invoke_action` runs any of the editor's ~190 named actions by path, for whatever the
typed tools do not cover. `list_actions` reports each one's path, label and whether it is
currently enabled.

Enabled state is evaluated live rather than read off the menu widgets, whose state is
refreshed on a delayed timer and is stale immediately after a tool changes the selection.

**Many editing actions require the map view to have keyboard focus.** `Menu/Edit/Delete`,
for instance, is gated on `widgetOrChildHasFocus`, so it refuses while TrenchBroom is in
the background. That is the editor's own rule, not a limitation of the bridge: view and
grid actions work regardless, editing actions need the window focused.

### Capturing a view

`capture_viewport` renders a pane to a PNG, returned to the client as an image.

```json
{"tool":"capture_viewport","params":{"view":"top","fit":"map","width":800}}
```

`view` picks the pane by the axis its camera looks along, so `top`, `front` and `side`
require a layout that actually shows those panes; `3d` needs the 3D view. `fit` of
`"current"` leaves the camera exactly where the user left it.

**Known rough edge:** automatic framing for the 2D views is approximate. TrenchBroom's own
`focusCameraOnSelection` only centres a 2D camera and never changes its zoom, so the zoom
is computed here, and it currently lands tighter than the content. `fit: "current"` is
exact, and 3D framing uses the editor's own logic and is reliable.

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

Still to come: `create_entity` against the shipped FGDs, a richer selector engine for
addressing geometry by material, classname or layer, `invoke_action` over TrenchBroom's
~200 named actions, and orthographic viewport captures.

`flip` and `shear` exist in the model layer and would be short additions.
