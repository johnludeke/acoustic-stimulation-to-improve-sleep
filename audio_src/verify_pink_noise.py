#!/usr/bin/env python3
"""
Pink Noise Verifier
===================
Captures audio from a microphone (or reads a WAV file) and plots the
power spectral density to verify the ~3 dB/octave rolloff characteristic
of pink noise (1/f spectrum).

Usage:
  # Live capture from mic (10-second recording):
  python verify_pink_noise.py

  # Analyse an existing WAV file:
  python verify_pink_noise.py --file recording.wav

  # Save a plot image instead of showing interactively:
  python verify_pink_noise.py --save spectrum.png

Requirements:
  pip install numpy scipy matplotlib sounddevice soundfile
"""

import argparse
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from scipy.signal import welch

from typing import Optional

save_path: Optional[str] = None

RECORD_SECONDS  = 10        # duration for live capture
SAMPLE_RATE     = 44100     # must match ESP32 config
CHANNELS        = 1         # capture mono; we'll average if stereo
NPERSEG         = 16384      # Welch FFT window (higher = finer frequency resolution)
FREQ_MIN        = 20        # Hz — lower bound of plot
FREQ_MAX        = 20_000    # Hz — upper bound of plot


def record_audio(duration: float, fs: int) -> np.ndarray:
    """Record from the default microphone using sounddevice."""
    try:
        import sounddevice as sd
    except ImportError:
        sys.exit("sounddevice not installed. Run: pip install sounddevice")

    print(f"Recording {duration}s from default microphone at {fs} Hz …")
    audio = sd.rec(int(duration * fs), samplerate=fs, channels=1,
                   dtype="float32", blocking=True)
    print("Recording complete.")
    return audio.flatten()


def load_wav(path: str) -> tuple[np.ndarray, int]:
    """Load a WAV file, converting stereo to mono if needed."""
    try:
        import soundfile as sf
    except ImportError:
        sys.exit("soundfile not installed. Run: pip install soundfile")

    data, fs = sf.read(path, dtype="float32", always_2d=True)
    mono = data.mean(axis=1)  # average channels
    print(f"Loaded '{path}': {len(mono)/fs:.1f}s, {fs} Hz, "
          f"{data.shape[1]} channel(s)")
    return mono, fs


def compute_psd(signal: np.ndarray, fs: int, nperseg: int):
    """Compute power spectral density via Welch's method."""
    
    # Remove DC offset
    signal = signal - np.mean(signal)

    freqs, psd = welch(
        signal,
        fs=fs,
        nperseg=nperseg,
        noverlap=nperseg // 2,   # IMPORTANT
        window="hann",
        scaling="density"
    )

    # Avoid log(0)
    psd = np.maximum(psd, 1e-20)

    return freqs, psd


def fit_slope(freqs, psd, f_low=200, f_high=8000):
    """
    More robust slope fit for real-world audio.
    """
    mask = (freqs >= f_low) & (freqs <= f_high)

    log_f = np.log10(freqs[mask])
    log_psd = np.log10(psd[mask])

    # Remove NaNs / infs
    valid = np.isfinite(log_f) & np.isfinite(log_psd)
    log_f = log_f[valid]
    log_psd = log_psd[valid]

    coeffs = np.polyfit(log_f, log_psd, 1)

    slope_per_decade = coeffs[0] * 10
    slope_per_octave = slope_per_decade * np.log10(2)

    fitted = 10 ** np.polyval(coeffs, log_f)

    return slope_per_octave, 10**log_f, fitted


def plot_spectrum(freqs: np.ndarray, psd: np.ndarray,
                  slope_db: float, fit_freqs: np.ndarray,
                  fit_psd: np.ndarray, save_path: str) -> None:

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle("Pink Noise Spectrum Analysis", fontsize=14, fontweight="bold")

    # ── Left: Log-log PSD with 1/f reference ──────────────────────────────
    ax = axes[0]
    mask = (freqs >= FREQ_MIN) & (freqs <= FREQ_MAX)
    f_plot = freqs[mask]
    p_plot = 10 * np.log10(psd[mask] + 1e-20)   # convert to dBFS/Hz

    ax.semilogx(f_plot, p_plot, color="#4A90D9", linewidth=0.8,
                alpha=0.9, label="Measured PSD")

    # Overlay fitted slope line
    fit_dB = 10 * np.log10(fit_psd + 1e-20)
    ax.semilogx(fit_freqs, fit_dB, color="#E55C30", linewidth=2,
                linestyle="--", label=f"Fit: {slope_db:+.1f} dB/oct")

    # Reference ideal 1/f line (anchored at 1 kHz)
    ref_idx  = np.searchsorted(f_plot, 1000)
    ref_level = p_plot[ref_idx]
    f_ref    = np.array([FREQ_MIN, FREQ_MAX])
    ref_dB   = ref_level + np.log2(f_ref / 1000) * -3   # −3 dB/oct
    ax.semilogx(f_ref, ref_dB, color="#2CA02C", linewidth=1.5,
                linestyle=":", label="Ideal −3 dB/oct")

    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Power (dBFS/Hz)")
    ax.set_title("Power Spectral Density (log-log)")
    ax.set_xlim(FREQ_MIN, FREQ_MAX)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, _: f"{int(x):,}" if x >= 1000 else f"{int(x)}"))
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=9)

    # ── Right: Octave-band energy ──────────────────────────────────────────
    ax2 = axes[1]
    octave_centers = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    octave_energy  = []
    for fc in octave_centers:
        lo, hi = fc / np.sqrt(2), fc * np.sqrt(2)
        band   = (freqs >= lo) & (freqs <= hi)
        if band.sum() > 0:
            octave_energy.append(10 * np.log10(psd[band].mean() + 1e-20))
        else:
            octave_energy.append(np.nan)

    colors = ["#E55C30" if abs(e - octave_energy[5]) + 3 * np.log2(fc / 1000) > 3
              else "#4A90D9"
              for fc, e in zip(octave_centers, octave_energy)]

    bars = ax2.bar(range(len(octave_centers)), octave_energy,
                   color=colors, edgecolor="white", linewidth=0.5)
    ax2.set_xticks(range(len(octave_centers)))
    ax2.set_xticklabels([f"{int(f)}" if f < 1000 else f"{int(f//1000)}k"
                         for f in octave_centers], fontsize=9)
    ax2.set_xlabel("Octave band centre (Hz)")
    ax2.set_ylabel("Mean power (dBFS/Hz)")
    ax2.set_title("Octave Band Energy\n(should decrease ~3 dB per octave →)")
    ax2.grid(axis="y", alpha=0.3)

    # Draw expected −3 dB/oct trend line
    ref_oct = octave_energy[5]   # anchor at 1 kHz
    trend = [ref_oct + np.log2(fc / 1000) * -3 for fc in octave_centers]
    ax2.plot(range(len(octave_centers)), trend, "g--",
             linewidth=1.5, label="Ideal −3 dB/oct")
    ax2.legend(fontsize=9)

    plt.tight_layout()

    # ── Print verdict ──────────────────────────────────────────────────────
    print("\n── Spectrum Analysis Results ──")
    print(f"  Fitted slope:       {slope_db:+.2f} dB/octave")
    print(f"  Target (pink noise): −3.01 dB/octave")
    deviation = abs(slope_db - (-3.01))
    verdict = "✅ PASS — looks like pink noise!" if deviation < 1.0 else \
              "⚠️  MARGINAL — slope close but check plot" if deviation < 2.0 else \
              "❌ FAIL — slope too far from −3 dB/oct"
    print(f"  Deviation:          {deviation:.2f} dB/oct  →  {verdict}\n")

    if save_path:
        fig.savefig(save_path, dpi=150, bbox_inches="tight")
        print(f"Plot saved to {save_path}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Verify pink noise from microphone or WAV file")
    parser.add_argument("--file", "-f", metavar="WAV",
                        help="Path to .wav file (default: live mic capture)")
    parser.add_argument("--duration", "-d", type=float, default=RECORD_SECONDS,
                        help=f"Record duration in seconds (default: {RECORD_SECONDS})")
    parser.add_argument("--save", "-s", metavar="IMG",
                        help="Save plot to this path instead of showing it")
    args = parser.parse_args()

    if args.file:
        signal, fs = load_wav(args.file)
    else:
        signal = record_audio(args.duration, SAMPLE_RATE)
        fs     = SAMPLE_RATE

    signal = signal / (np.max(np.abs(signal)) + 1e-9)
    freqs, psd = compute_psd(signal, fs, nperseg=NPERSEG)
    slope_db, fit_freqs, fit_psd = fit_slope(freqs, psd)
    plot_spectrum(freqs, psd, slope_db, fit_freqs, fit_psd, save_path=args.save)


if __name__ == "__main__":
    main()
