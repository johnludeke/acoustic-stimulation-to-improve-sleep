import numpy as np
import soundfile as sf

def generate_pink(n):
    white = np.random.randn(n)
    fft = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n)
    fft /= np.sqrt(freqs + 1e-6)
    return np.fft.irfft(fft)

fs = 8000      # sample rate
duration = 0.1 # seconds

x = generate_pink(int(fs * duration))
sf.write("pink_noise.wav", x, fs)
