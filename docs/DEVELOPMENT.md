# Development

[Back to the README](../README.md)

## Source map

| Path | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Pinned JUCE dependency, font embedding, VST3/standalone targets and optional tests |
| `Source/PluginProcessor.*` | Parameters, filter processing, gain smoothing and state serialization |
| `Source/EqResponse.h` | Filter coefficients and the matching plotted frequency response |
| `Source/SpectrumAnalyzer.h` | FFT analysis and bounded audio-to-UI frame queue |
| `Source/SpectrumDisplay.*` | Spectrum drawing, cached response and interactive band points |
| `Source/PluginEditor.*` | Responsive EQ layout, parameter attachments and theme switch |
| `Source/UI/Theme.*` | Fonts, monochrome palettes, flat controls and vector icons |
| `Source/Standalone/StandaloneApp.cpp` | Application lifecycle, toolbar, device selectors, dialogs and preferences |
| `Source/Standalone/StandaloneEngine.*` | Device callback, file transport and mode switching |
| `Source/Standalone/SystemAudioSource.*` | Windows loopback capture, buffering and drift correction |
| `Source/Standalone/FileRenderer.*` | Background WAV export, cancellation and safe destination replacement |
| `Tests/VisualizationTests.cpp` | Audio regressions, DSP benchmark, device checks and editor previews |
| `Assets/Fonts/` | Inter Regular/SemiBold and their SIL OFL license |
| `docs/images/` | Actual UI screenshots referenced by GitHub Markdown |

## Build targets and configuration

Use the [Windows build instructions](../README.md#build) first. The project requires C++17, CMake 3.22+ and JUCE 8.0.15.

```bat
cmake --build build-vs18 --config Release --target TenBandEQ_Standalone --parallel 4
cmake --build build-vs18 --config Release --target TenBandEQ_VST3 --parallel 4
```

| CMake setting | Purpose |
| --- | --- |
| `JUCE_DIR` | Use a local JUCE checkout; otherwise FetchContent downloads the pinned release |
| `TENBAND_EQ_BUILD_TESTS=ON` | Build and register the regression executable; defaults to OFF |
| `CMAKE_BUILD_TYPE=Release` | Select Release with a single-configuration generator such as Ninja |
| `--config Release` | Select Release with a multi-configuration generator such as Visual Studio |

On macOS/Linux, the generic configuration below is a starting point, not a verified build recipe. Install the compiler, CMake, Git and JUCE's platform development dependencies first. System loopback remains Windows-only.

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel 4
```

For current platform dependency requirements, consult the pinned [JUCE Linux documentation](https://github.com/juce-framework/JUCE/blob/8.0.15/docs/Linux%20Dependencies.md) and [JUCE CMake documentation](https://github.com/juce-framework/JUCE/blob/8.0.15/docs/CMake%20API.md). macOS native system font rendering and Linux builds have not been validated here.

## Regression checks

From the repository root in Command Prompt:

```bat
cmake -S . -B build-vs18 -G "Visual Studio 18 2026" -A x64 -DTENBAND_EQ_BUILD_TESTS=ON
cmake --build build-vs18 --config Release --parallel 4
ctest --test-dir build-vs18 -C Release --output-on-failure
```

Coverage includes spectrum calibration/frequency detection, opposite stereo polarity, silence, queue ordering/overflow, sample rate changes, EQ response, output trim, bypass, neutral pass-through, coefficient caching, gain smoothing, disabled analysis and WAV export/cancellation. Test audio is written under the ignored build directory.

Optional checks:

```bat
build-vs18\Release\TenBandEQTests.exe --mp3 build-vs18/_deps/juce-src/examples/Assets/Notifications/sounds/solemn.mp3
build-vs18\Release\TenBandEQTests.exe --devices
build-vs18\Release\TenBandEQTests.exe --capture-smoke
```

If using `JUCE_DIR`, replace the MP3 fixture path with the matching path in that checkout. Device listing and capture are Windows-specific. The capture smoke test briefly opens a playback endpoint, reads and closes it without playback or saving captured audio. It does not verify a full virtual-source-to-speaker route.

## Reproduce the screenshots

Build the optional tests, then run these commands from the repository root:

```bat
if not exist docs\images mkdir docs\images
build-vs18\Release\TenBandEQTests.exe --preview docs/images/eq-dark.png
build-vs18\Release\TenBandEQTests.exe --preview docs/images/eq-compact.png --compact
build-vs18\Release\TenBandEQTests.exe --preview docs/images/eq-light.png --light
"build-vs18\TenBandEQ_artefacts\Release\Standalone\Ten Band EQ.exe" --preview docs/images/standalone.png
```

The editor preview generates synthetic stereo audio in memory. Its temporary window peer is positioned offscreen so the analyzer uses the normal visibility/timer path. The standalone preview opens no audio device and shows an idle app. Neither preview produces sound. The images are native UI renders, not mockups; live FFT smoothing can cause minor differences between runs.

Both preview commands support `--compact` and `--light`. Editor sizes are 1000 x 720 or 520 x 620; standalone sizes are 1040 x 930 or 560 x 700. A short standalone preview intentionally shows a vertical scroll area. These PNGs belong in `docs/images`, which is not ignored; temporary build files belong under `build-vs18`.

To render the complete standalone window, including its themed title bar and current-monitor startup dimensions, without opening an audio device:

```bat
"build-vs18\TenBandEQ_artefacts\Release\Standalone\Ten Band EQ.exe" --window-preview build-vs18/window-preview.png
"build-vs18\TenBandEQ_artefacts\Release\Standalone\Ten Band EQ.exe" --window-preview build-vs18/window-light-preview.png --light
```

Before updating screenshots, visually check wide/narrow layouts, dark/light contrast, labels, disabled buttons, keyboard focus, open menus and active Bypass/system processing states. The standalone title bar follows the app theme; native file dialogs follow the OS. Startup uses the pointer's monitor (primary display fallback), 60% of usable height and a width capped at 90% of usable width, in JUCE logical coordinates.

## Benchmark

```bat
build-vs18\Release\TenBandEQTests.exe --benchmark
```

The benchmark compares the previous all-band loop with the current processor at 48 kHz, stereo and 512 samples per block, with the analyzer disabled. It measures DSP work, not device latency or whole-app CPU.

Historical measurements on the development Windows machine, for 2,000 blocks representing 21.33 seconds of audio:

| Active bands | Previous loop | Current DSP | Observation |
| --- | ---: | ---: | --- |
| 0 | 18.74 ms | 0.21 ms | Neutral filters skipped |
| 3 | 19.11 ms | 9.63 ms | About 2x faster in this run |
| 10 | 18.78 ms | 19.32 ms | Roughly unchanged; approximately 3% slower |

Hardware was not recorded for this historical sample, so it is illustrative rather than a cross-machine performance claim. Run the benchmark locally for useful comparisons. Results depend on compiler, CPU, automation and active bands. Device I/O, decoding, export and GUI rendering are excluded.

## Parameter and state compatibility

Band parameter IDs are `band1` through `band10`; the remaining IDs are `output` and `bypass`. Preserve IDs and ranges when changing presentation so existing host automation and sessions remain valid. UI attachments keep graph points, sliders and host values synchronized.

Processor state includes EQ values and the light-mode preference. The standalone stores this state and the selected output device through JUCE `ApplicationProperties` under the `TenBandEQ` application settings folder. Settings are written at normal shutdown. The selected file and transport state are not restored automatically.

## Design and implementation notes

The black and optional light palettes use neutral shades. `drawControlSurface` is shared by button and selector rendering; no bevel, drop shadow or pressed-position offset is drawn. Slider thumbs are flat marks on thin tracks. Button active state uses a subtle underline and focus uses an outline. Font data and vector paths are local; the theme adds no network requirement or texture rendering.

Keep GUI allocation, file I/O and font work off the audio callback. Spectrum frames cross a bounded queue; slow UI consumers can drop visual frames without stalling audio. Response images are rebuilt only when their relevant inputs change. Preserve these boundaries when adding meters or visual effects.

## Contributions and release preparation

For a focused change, explain the behavior, include reproduction steps or a screenshot when relevant, and run the checks that exercise the affected audio path. Report the OS/toolchain and host version. Use the project license for original contributions; preserve third-party notices.

Before packaging a release:

1. Build and test the intended OS/CPU configurations; publish only the ones actually verified.
2. Load the VST3 in the intended Audacity version. Check playback, automation, state save/restore, resizing and bypass.
3. Check standalone file playback, seeking, export/cancel and device changes. Manually listen to a correctly configured Windows virtual route if claiming system-audio support for that release.
4. Regenerate screenshots if the UI changed. Keep build output and private audio out of the source repository.
5. Include the complete VST3 bundle, standalone executable, project `LICENSE`, Inter notice and applicable JUCE/dependency notices with the corresponding source and build information required by their licenses.
6. Document the version, tested platforms, known issues and installation procedure. Signing, installers and GitHub release automation are not currently configured.

No CI badge or automated host certification is claimed. The repository contains build/test commands; a hosted CI workflow has not been added.
