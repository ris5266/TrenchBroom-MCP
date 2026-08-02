# TrenchBroom-MCP

[![TrenchBroom Icon](app/TrenchBroom/resources/graphics/images/AppIcon.png)](https://www.youtube.com/watch?v=shcAvnYp9ow)

A fork of [TrenchBroom](https://github.com/TrenchBroom/TrenchBroom) that adds an **MCP
server**, so an AI assistant can build geometry in the editor.

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

**→ See [mcp/README.md](mcp/README.md) for setup, the tool list and design notes.**

### Quick start

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTB_ENABLE_MCP=1
cmake --build build --target TrenchBroom
```

Enable it in `~/.TrenchBroom/Preferences.json` (off by default, it opens a socket that
can drive the editor):

```json
{ "mcp/Enable server": true }
```

Register the bridge, with `mcp/` on `PYTHONPATH`:

```bash
claude mcp add trenchbroom -- python3 -m trenchbroom_mcp.server
```


---

## About TrenchBroom

TrenchBroom is a modern cross-platform level editor for Quake-engine based games.

- Trailer:   https://www.youtube.com/watch?v=shcAvnYp9ow
- Website:   https://github.com/TrenchBroom/TrenchBroom
- Discord:   https://discord.gg/WGf9uve
- Mastodon:  https://mastodon.gamedev.place/@trenchbroom
- Bluesky:   https://bsky.app/profile/trenchbroom.bsky.social
- Video Tutorial Series:  https://www.youtube.com/playlist?list=PLgDKRPte5Y0AZ_K_PZbWbgBAEt5xf74aE
- Manual:    https://trenchbroom.github.io/manual/latest

## Features
* **General**
  - Full support for editing in 3D and in up to three 2D views
  - High performance renderer with support for huge maps
  - Unlimited Undo and Redo
  - Macro-like command repetition
  - Issue browser with automatic quick fixes
  - Point file support
  - Automatic backups
  - .obj file export
  - Free and cross platform
* **Brush Editing**
  - Robust vertex editing with edge and face splitting and manipulating multiple vertices together
  - Clipping tool with two and three points
  - Scaling and shearing tools
  - CSG operations: merge, subtract, intersect
  - UV view for easy texture manipulations
  - Precise texture lock for all brush editing operations
  - Multiple material collections
* **Entity Editing**
  - Entity browser with drag and drop support
  - Support for FGD and DEF files for entity definitions
  - Mod support
  - Entity link visualization
  - Displays 3D models in the editor
  - Smart entity property editors
* **Supported Games**
  - Quake (Standard and Valve 220 file formats)
  - Quake 2
  - Quake 3 (partial, no patches or brush primitives yet)
  - Hexen 2
  - Daikatana
  - Generic (for custom engines)
  - More games can be supported with custom game configurations


## Releases

Binary builds are available from [releases](https://github.com/kduske/TrenchBroom/releases).

## Compiling

Read [BUILD.md](BUILD.md) for instructions.

# Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for more information.

# Credits
- [Qt](https://www.qt.io/)
- [FreeType](https://www.freetype.org/)
- [FreeImage](https://freeimage.sourceforge.io/)
- [TinyXML](http://www.grinninglizard.com/tinyxml/)
- miniz
- [Assimp](https://www.assimp.org/)
- [Catch2](https://github.com/catchorg/Catch2)
- [CMake](https://cmake.org/)
- [Pandoc](https://www.pandoc.org/)
- Quake icons by [Th3 ProphetMan](https://www.deviantart.com/th3-prophetman)
- Hexen 2 icon by [thedoctor45](https://www.deviantart.com/thedoctor45)
- [Source Sans Pro](https://fonts.google.com/specimen/Source+Sans+Pro) font

## Changes

See [releases](https://github.com/TrenchBroom/TrenchBroom/releases) for latest changes.
