# Changelog

## Unreleased

- Target native CMake builds, continuous integration, and packaged releases at
  Windows x64.
- Add Groq account settings with OS-protected credential storage, authenticated
  connection/model tool-support verification, model refresh, and explicit key removal.
  AI Camera Director requests now retrieve credentials from the vault on demand;
  plaintext `.env` and environment-variable credential loading has been removed.
- Add an optional Groq-powered AI Camera Director with explicit cloud-sharing
  consent, typed and locally validated camera/effect proposals, approval and
  auto-apply controls, in-memory undo/reset, and asynchronous HTTPS requests.
- Add deterministic local grayscale, brightness, contrast, saturation, warm/cool,
  and mild-sharpening effects shared by the preview and explicitly saved photos.
  Camera images are never uploaded, and capture is not available as an AI tool.
- Add mocked Groq-client, tool-validation, and image-effect tests while retaining
  the native camera backend and existing diagnostics.

## 0.2.1 — 2026-11-03

- Add Windows video rotation for external USB webcams through the camera
  pipeline, with an administrator prompt when needed, profile support, and a
  `rotate` CLI command. Reopen camera streams after changing orientation.

- Reject process IDs outside the native operating system range before attempting
  termination. Previously, large IDs could wrap and target a different process
  or, on Linux/macOS, a process group.
- Reject malformed, fractional, out-of-range, and duplicate profile entries
  before applying settings. Preserve full signed 64-bit integer values.
- Use V4L2 integer-menu indexes when reading, selecting, and saving choices,
  while retaining the advertised integer values as display labels.
- Restore the remembered camera before opening a default device, so startup
  no longer overwrites the previous selection.

Profiles containing invalid entries now fail with an error instead of partially
loading or silently replacing invalid numbers with zero.
