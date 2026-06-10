# Terrax Launcher

A small standalone launcher for Terrax. It checks the GitHub **Releases** page,
installs or updates the game to the latest version, and launches it — wrapped in
an animated voxel cover-art scene with a frosted "liquid glass" theme.

It is a separate program from the game (its own `main()`), built as its own
MSBuild project so it never collides with `Terrax.exe`. It reuses the same
vendored Dear ImGui + vcpkg GLFW the game uses; it is **Windows-only** (it uses
WinHTTP for the GitHub API and DWM for the acrylic window).

![Launcher](.) <!-- run it to see the live, animated version -->

## Building

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    TerraxLauncher.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

- Output: `build\windows\<Configuration>\launcher\TerraxLauncher.exe`
  (with `glfw3.dll` copied next to it).
- `Debug|x64` / `Release|x64`, toolset `v145`, C++17 — same as the game.
- It is also part of `Terrax.sln`, so opening the solution builds both.

## What it does

On launch it queries `https://api.github.com/repos/Alli1223/Terrax/releases/latest`
(on a background thread) and compares the latest tag with the locally installed
version:

| State | Button | Action |
|-------|--------|--------|
| Nothing installed | **INSTALL** | download + extract the latest release |
| Installed < latest | **UPDATE** | re-download + replace |
| Installed == latest | **PLAY** | launch the installed game |
| Network / 404 error | **RETRY** | check again |

Installing downloads the release's `*-win64.zip` asset, extracts it with the
built-in `tar.exe` (bsdtar, Windows 10 1803+), and records the version. Play
runs `Terrax.exe` with its working directory set to the install folder so it
finds its `shaders\` folder.

### Install locations

| Path | Purpose |
|------|---------|
| `%LOCALAPPDATA%\Terrax\current\` | the installed game (`Terrax.exe`, `shaders\`, `glfw3.dll`) |
| `%LOCALAPPDATA%\Terrax\installed_version.txt` | the installed release tag |
| `%LOCALAPPDATA%\Terrax\launcher.log` | a short diagnostic log of checks/installs |

## Requirements & notes

- **The repository (or at least its releases) must be public.** The launcher
  makes *unauthenticated* GitHub requests; a private repo returns HTTP 404 and
  the launcher shows *"No public Terrax release found (is the repo public?)"*.
  It starts working the moment the repo/releases go public — no rebuild needed.
- Windows 10 1803+ (for `tar.exe`) and a desktop with DWM composition (for the
  acrylic backdrop; without it the window simply renders its own dark glass).

## Command line

- `TerraxLauncher.exe` — normal windowed launcher.
- `TerraxLauncher.exe --install` — **silent install/update** (no window); useful
  for first-run bootstrap or automation. Exit code `0` on success.

## Environment overrides

Point the launcher at a different repo (a fork, mirror, or a public test repo)
without rebuilding:

```powershell
$env:TERRAX_REPO_OWNER = "Alli1223"
$env:TERRAX_REPO_NAME  = "Terrax"
```
