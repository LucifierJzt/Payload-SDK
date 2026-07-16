# AGENTS.md

## Workspace Intent

This workspace is for **Manifold 3 development preparation**, not for rebuilding the Manifold system image itself.

Current contents:

- `Payload-SDK/`: DJI Payload SDK (often a symlink to Desktop clone / fork LucifierJzt/Payload-SDK).
- `docs/`: project documentation index (WS cloud integration, deploy, network).
- `manifold-ws-hub/`: optional Node reference WS hub.
- `tools/`: aarch64 cross toolchain helpers.

Default assumption for future work:

- The primary goal is to build **Linux user-space applications that run on Manifold 3**.
- `Payload-SDK` is used only when the app needs to integrate with the DJI aircraft / payload ecosystem.
- Do **not** assume this workspace contains Manifold firmware, BSP, kernel, rootfs, or system-source build infrastructure.

## Development Boundary

Treat these as different tracks:

### Track A: Manifold 3 Linux app development

Use this track when building:

- AI inference services
- vision / media pipelines
- network services
- local data processing
- background daemons

Default approach:

- build a normal `aarch64` Linux application
- deploy to Manifold 3
- manage startup with `systemd` when appropriate

### Track B: PSDK application development

Use this track only when the app needs:

- aircraft communication
- payload identity / app identity
- DJI Pilot / app-management integration
- `.dpk` packaging and installation
- DJI payload capability access

Default approach:

- integrate with `Payload-SDK`
- use `app.json`
- build `.dpk`
- let DJI app management own install/start/version behavior

### Track C: Manifold system-level customization

Examples:

- kernel / driver changes
- boot flow changes
- rootfs / image customization
- BSP or firmware packaging

This workspace does **not** currently provide the required sources or toolchains for that work. Do not pretend `Payload-SDK` is enough for this track.

## Decision Rules

When starting new work, choose the lightest correct path:

1. If the task is a normal Linux app concern, stay outside PSDK.
2. If the task must connect to DJI aircraft / payload workflows, use PSDK.
3. If the task asks for system-image or kernel-level changes, call out that the workspace lacks the necessary system sources.

## Startup Rules

Use `systemd` when the program is a normal Linux service and needs:

- auto restart
- boot-time startup
- service supervision
- journal-based diagnostics

Use `.dpk` / DJI app management when the program is a formal PSDK app and needs:

- DJI app identity
- Pilot-visible app metadata
- package install / uninstall
- version enforcement

Do not default to `systemd` for formal PSDK app delivery.
Do not default to `.dpk` for ordinary Linux daemons.

## Repository Handling

`Payload-SDK/` should usually be treated as:

- an upstream vendor dependency
- a reference implementation
- an integration target

Avoid turning the upstream SDK repo into the main business application repo unless explicitly requested.

Preferred long-term structure:

```text
workspace/
├── AGENTS.md
├── Payload-SDK/
└── your-manifold-app/
```

Where:

- `Payload-SDK/` remains close to upstream
- `your-manifold-app/` owns app code, deployment scripts, configs, and service definitions

## Build Assumptions

From the current SDK repo, Manifold 3 samples assume:

- `cmake`
- `make`
- `python3`
- `aarch64-linux-gnu-gcc`
- `aarch64-linux-gnu-g++`

Do not start PSDK integration work before confirming the local `aarch64` toolchain works for a minimal app.

## Useful Local References

Project docs (preferred entry):

- [docs/README.md](docs/README.md)
- [docs/03-ws-cloud-integration.md](docs/03-ws-cloud-integration.md) — WS telemetry / gimbal / live RTMP
- [docs/04-deploy-dpk.md](docs/04-deploy-dpk.md) — build and install `.dpk`
- [docs/02-network.md](docs/02-network.md) — SSH / Wi‑Fi / cellular notes
- [README-manifold3-wifi-8852bu.zh-CN.md](README-manifold3-wifi-8852bu.zh-CN.md)

Upstream Manifold notes inside SDK tree:

- [Payload-SDK/README-manifold3-system-dev.zh-CN.md](Payload-SDK/README-manifold3-system-dev.zh-CN.md)
- [Payload-SDK/README-manifold3-linux-app-dev.zh-CN.md](Payload-SDK/README-manifold3-linux-app-dev.zh-CN.md)
- [Payload-SDK/README-manifold3-quickstart.zh-CN.md](Payload-SDK/README-manifold3-quickstart.zh-CN.md)

Recommended reading order:

1. `docs/README.md` + `docs/03-ws-cloud-integration.md` (current product path)
2. system-dev / linux-app-dev / quickstart (SDK background)
3. AGENTS track decision (A vs B vs C)

## Working Style For Future Agents

- Prefer building a clean Linux app first, then adding DJI integration only when needed.
- Prefer a separate app repository/directory over stuffing product code into DJI samples.
- Keep deployment concerns explicit: build, copy, config, logs, service, restart.
- Be concrete about whether a recommendation applies to Linux app delivery or PSDK app delivery.
- If a user says “develop Manifold system,” verify whether they really mean:
  - Linux app development on Manifold
  - PSDK app development on Manifold
  - true system-level customization

## Non-Goals

Unless the user explicitly provides the required system sources and asks for it, do not claim support for:

- rebuilding Manifold OS images
- modifying DJI firmware internals
- kernel / BSP release engineering
- rootfs packaging for Manifold platform firmware
