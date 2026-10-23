# notepad420

English · [简体中文](README.zh-CN.md)

> A drop-in Notepad replacement for Windows 10 — a personal fork of [Notepad4](https://github.com/zufuliu/notepad4), cherry-picked on demand, not following upstream mainline.
>
> **Mission: make the text editor that your Harness (Codex / Claude Code / pi, etc.) opens with Ctrl+G painlessly handle screenshots for AI.** Paste a screenshot in, get back a `C:\...\Temp\notepad420-Paste\xxxx.png` file path — no more data-URI blobs, no more hunting for paths.

[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](License.txt) [![Latest release](https://img.shields.io/github/release/Sakyvo/notepad420.svg)](https://github.com/Sakyvo/notepad420/releases) [![Downloads](https://img.shields.io/github/downloads/Sakyvo/notepad420/total.svg)](https://github.com/Sakyvo/notepad420/releases)

## Image Paste — the reason this fork exists

- Copy any image, then just **Ctrl+V** in notepad420
- The image is saved to `%TEMP%\notepad420-Paste\<timestamp>.png` and the **file path** is pasted at your cursor — your document holds a path, not a giant base64 blob
- Inverse paste with **Ctrl+Shift+V**: paste the original image data-URI as-is, nothing written to disk
- Pasting files from Explorer (or dragging them in) inserts their path text directly
- Data-URI images embedded in Markdown text are decoded into the cache dir and replaced by file paths
- No artificial limits: the cache grows as needed, managed via a dedicated dialog (Settings → **Paste Image Cache Management**): manual cleanup by age with a confirmation box, plus optional silent auto-cleanup on first launch each day (off by default). Cache location is configurable (`PasteCacheDir`)

| Opened by Ctrl+G (before paste) | After pasting you get a cache path |
| --- | --- |
| ![Blank document summoned via Ctrl+G](.github/readme-images/ctrlg-workflow-before.png) | ![The pasted path in %TEMP%\notepad420-Paste](.github/readme-images/ctrlg-workflow-after.png) |

No more manual "Save As", no more leaving the terminal to share a screenshot.

## Markdown Auto-Formatting

The editor works for Markdown out of the box:

- **List auto-completion** (kedit-style, on by default): press Enter at the end of a list item and the next line continues the marker automatically — blockquotes `> `, bullets `* + -`, ordered `1. ` — including task checkboxes `[ ]` / `[x]`; ordered lists renumber themselves; pressing Enter on an empty marker line removes the marker and leaves a blank line
- Markdown (GitHub GFM) is the **default scheme**: blank or unknown-extension documents open with Markdown highlighting, NP3-style heading/bold/italic looks
- **Use Default Code Style** is enabled by default for every lexer

## Smart Editing Touches

- **Ctrl+Click opens paths**: existing file/folder paths (absolute, UNC, relative, quoted paths with spaces) get the same hotspot styling as URLs — Ctrl+Click opens them with the system default handler (folders in Explorer, images in your viewer)
- **Ctrl+J inserts a newline**, identical to Enter (auto-indent, list continuation, selection replacement), toggleable under Settings → Extra Esc Key Function; on by default
- **At Startup options**: choose where the caret lands when opening a file — start or end — and whether the view scrolls there, under Settings → At Startup
- **Middle-button auto-scroll** (NP3 semantics): hold the middle button and drag to scroll; a short click toggles continuous mode
- Retuned defaults: quadruple-buffered drawing, F60FastScroll for high-refresh displays

## Notepad Replacer

`notepad420-replacer.exe` — a one-shot tool (admin) to:

- Replace the system Notepad (physical file, original backed up as `notepad.exe.bak`)
- Associate 25 text-file extensions with notepad420
- Take over the terminal Ctrl+G external editor (`EDITOR` / `VISUAL`)

With a live progress bar and a compact four-line result report. Fully reversible (`--restore`), and the installer's finish page can run it for you.

## Download & Install

Grab the latest build from [Releases](https://github.com/Sakyvo/notepad420/releases):

| File | Purpose |
|---|---|
| `notepad420-setup-x64-<ver>.exe` | Installer (bilingual English/简体中文, follows system language; finish page optionally replaces system Notepad / associates text files / takes over Ctrl+G) |
| `notepad420_i18n_x64_v<ver>.zip` | Portable package (unzip and run, replacer included) |

Requires Windows 10 x64 or newer. Shipped locales: **English + 简体中文** only.

## Build from Source

See the [build-output contract](.docs/spec/build-output.md) (all generated artifacts are confined to `.target/`). Quick path:

```
build\VisualStudio\build_x64.bat            # main program + matepath, x64 Release
locale\build.bat Build x64 Release          # locale satellite dlls
build\VisualStudio\build_replacer.bat      # notepad replacer
build\VisualStudio\make_installer.bat      # Inno Setup installer
build\make_zip.bat MSVC x64 Release Locale  # portable zip
```

## Relationship to Upstream

This repository is a personal fork, not the official Notepad4. Upstream copyright and authorship remain honored in [License.txt](License.txt) and the About dialog (Zufu Liu and all contributors).

Credit: [linux.do](https://linux.do) Community
