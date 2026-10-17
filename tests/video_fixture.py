import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="cressida-video-") as directory:
    clip = pathlib.Path(directory) / "clip.mp4"
    subprocess.run([
        sys.argv[1], "-v", "error", "-nostdin", "-f", "lavfi", "-i",
        "testsrc2=size=1280x720:rate=15:duration=3,drawbox=x=0:y=0:w=iw:h=ih/2:color=red:t=fill,drawbox=x=0:y=ih/2:w=iw:h=ih/2:color=blue:t=fill", "-f", "lavfi", "-i", "sine=frequency=440:duration=3",
        "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-threads", "1",
        "-c:a", "aac", "-movflags", "+faststart", "-y", str(clip),
    ], check=True, timeout=20)
    result = subprocess.run([sys.argv[2], str(clip)], timeout=25)
    sys.exit(result.returncode)
