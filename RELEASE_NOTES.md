First public build of Semitone, a real-time pitch shifter (±12 semitones plus cents) with constant latency and an exact stereo image.

## Downloads

| File | Contains |
| --- | --- |
| `Semitone-0.1.0-macOS.zip` | VST3, Audio Unit and standalone app. Universal (Apple Silicon and Intel), macOS 11 or later. |
| `Semitone-0.1.0-Windows.zip` | VST3 and standalone app. 64-bit, Windows 10 or later. |

Each zip has an `INSTALL.txt` with where to put the files.

## Before you install

- **macOS:** the build is not signed with an Apple Developer ID or notarised, so macOS quarantines it after download. `INSTALL.txt` has the one-line command that clears it.
- **Windows:** the build is not code-signed, so Windows may warn before running the standalone app.

## What is in it

- Three modes: Smooth (85 ms latency at 48 kHz) for vocals, bass, keys and full mixes; Balanced (43 ms); Tight (21 ms) for drums and percussion.
- Attack lock keeps drum hits and note attacks tight instead of smearing them.
- Dry/wet mix with the dry signal time-aligned, and an output trim.

## Known limits

- Tight mode cannot separate low notes that sit close together; keep bass and chords in Smooth.
- Steady noise comes out about 2 dB quieter at an octave down.
- Two instances on separate tracks do not stay phase-locked to each other.
