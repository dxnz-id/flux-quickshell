# Contributing

Thanks for considering contributing to flux-quickshell.

## How to Report Bugs

Open a [bug report](https://github.com/dxnz-id/flux-quickshell/issues/new?labels=&template=bug_report.md).
Include your GPU, Hyprland version, Quickshell version, and any logs.

## How to Suggest Features

Open a [feature request](https://github.com/dxnz-id/flux-quickshell/issues/new?labels=&template=feature_request.md).

## Pull Requests

1. Make sure your branch builds and runs in the sandbox.
2. If you change shaders, recompile with `qsb --glsl "440"`.
3. Update docs/ if needed.
4. Open a PR and reference the related issue.

## Development Setup

```bash
cd plugin
cmake -Bbuild
cmake --build build -j$(nproc)
```

See [docs/build.md](docs/build.md) and [docs/development.md](docs/development.md) for details.
