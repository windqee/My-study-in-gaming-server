# Third-party dependencies

NetArena prepares dependencies locally through `deps.lock.json` and `tools/bootstrap_dependencies.py`. Download caches, extracted source trees, the Godot editor and compiled libraries are not tracked in this repository. This inventory does not assign a license to NetArena itself.

| Dependency | Pinned version | License and upstream source |
| --- | --- | --- |
| ENet | 1.3.18 | MIT; Copyright (c) 2002–2024 Lee Salzman. [License](https://github.com/lsalzman/enet/blob/v1.3.18/LICENSE) |
| nlohmann/json | 3.12.0 | MIT; Copyright (c) 2013–2025 Niels Lohmann. [License](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) |
| godot-cpp | e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77 (godot-4.5-stable) | MIT; Copyright (c) 2017–present Godot Engine contributors. [License](https://github.com/godotengine/godot-cpp/blob/e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77/LICENSE.md) |
| Godot Engine editor (optional local tool) | 4.5.1-stable, official macOS universal archive | [Engine license](https://github.com/godotengine/godot/blob/4.5.1-stable/LICENSE.txt); bundled third-party components have additional notices in [COPYRIGHT.txt](https://github.com/godotengine/godot/blob/4.5.1-stable/COPYRIGHT.txt) |

The bootstrap preserves ENet's `LICENSE` and godot-cpp's `LICENSE.md` from their source archives and separately fetches nlohmann/json's `LICENSE.MIT` with its own locked checksum. Keep the complete applicable copyright and permission notices with redistributed source or compiled distributions. When distributing Godot-based executables, include the engine license and the relevant bundled third-party notices as well; the links in this inventory are not a replacement for distribution notices.

No publishable multi-platform release package is produced by WP-02. Release packaging and its license inventory remain part of WP-19.
