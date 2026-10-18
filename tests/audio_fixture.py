import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="cressida-audio-") as directory:
    path = pathlib.Path(directory) / "preview.ogg"
    subprocess.run([sys.argv[1], "-v", "error", "-nostdin", "-f", "lavfi", "-i", "sine=frequency=440:duration=0.8", "-c:a", "libvorbis", "-y", str(path)], check=True, timeout=15)
    subprocess.run([sys.argv[2], str(path)], check=True, timeout=20)
