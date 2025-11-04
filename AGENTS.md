# Repository Guidelines

## Project Structure & Module Organization
- `src/`: ESP32 firmware (`main.cpp`) handling Wi-Fi AP setup, WebSocket signaling, OLED UI, and hardware control. Generated bundles such as `web_bundle.h` are stored here and should not be edited manually.
- `web/`: Human-readable web client (`index.html`) served to browsers. Update this file before rebuilding the compressed bundle.
- `scripts/`: Automation utilities. `build_web_bundle.py` compresses the web UI into `src/web_bundle.h` during PlatformIO’s pre-build step or when run manually.
- `platformio.ini`: Target board configuration for PlatformIO; modify this if boards, build flags, or extra scripts change.

## Build, Test, and Development Commands
- `python scripts/build_web_bundle.py`: Regenerate the gzip-compressed web bundle after editing `web/index.html`.
- `pio run`: Compile the firmware and regenerate bundles (pre-build hook). Fails fast if PlatformIO cache permissions are incorrect.
- `pio run -t upload`: Build and flash the firmware to the connected ESP32-C3 board.
- `pio device monitor`: Open the serial console at 115200 baud for runtime logs.

## Coding Style & Naming Conventions
- **Firmware (C++/Arduino)**: Two-space indentation, `camelCase` for variables/functions, `PascalCase` for classes. Prefer `constexpr` for constants and avoid blocking loops; use event-driven updates.
- **Web client (HTML/JS/CSS)**: Two-space indentation, `camelCase` for JavaScript, semantic HTML, and modern CSS. Keep inline scripts self-contained so the gzip bundle stays deterministic.
- Generated files (`web_bundle.h`) must be treated as artifacts—never hand-edit them.

## Testing Guidelines
- No automated test suite exists yet. Validate changes by:
  1. Running `python scripts/build_web_bundle.py` to ensure asset generation succeeds.
  2. Building with `pio run` and monitoring for compiler warnings.
  3. Flashing (`pio run -t upload`) and exercising WebSocket reconnects, OLED screens, and hardware inputs.
- When adding tests, follow PlatformIO conventions (e.g., `test/` directory with Unity-based cases) and document invocation here.

## Commit & Pull Request Guidelines
- Use concise, imperative commit subjects (e.g., `Improve WebSocket heartbeat handling`). Group unrelated changes into separate commits.
- Pull requests should summarize key changes, note firmware/web client impacts, list manual verification steps, and reference related issues. Include screenshots or serial logs when UI or connectivity behavior changes.

## Security & Configuration Tips
- The device runs as an open AP by default; update `AP_PASSWORD` in `src/main.cpp` for secured deployments.
- If PlatformIO reports permission errors, fix ownership of `~/.platformio` before retrying builds.
