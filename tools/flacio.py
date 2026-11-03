"""FLAC/WAV input and output through ffmpeg, so the IR tools need only numpy, scipy and ffmpeg."""
import subprocess
import numpy as np


def read(path, sr=48000):
    """Returns a float array shaped (samples, channels) at sample rate sr."""
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "f32le", "-ar", str(sr), "-"],
                         check=True, stdout=subprocess.PIPE).stdout
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=channels",
                            "-of", "csv=p=0", str(path)], check=True, stdout=subprocess.PIPE).stdout
    ch = int(probe.decode().strip().split(",")[0])
    return np.frombuffer(out, dtype=np.float32).reshape(-1, ch).copy()


def write(path, data, sr=48000):
    """Writes 24-bit FLAC, with the maximum compression."""
    data = np.atleast_2d(np.asarray(data, dtype=np.float32))
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "f32le", "-ar", str(sr), "-ac", str(data.shape[1]), "-i", "-",
                    "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24", "-compression_level", "12", str(path)],
                   input=data.tobytes(), check=True)
