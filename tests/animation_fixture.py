import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="cressida-animation-") as directory:
    root = pathlib.Path(directory)
    colors = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
              (255, 255, 0, 255), (0, 255, 255, 255), (255, 0, 255, 255)]
    frames = b"".join(bytes(color) * (32 * 24) for color in colors)
    for extension, codec in [("gif", "gif"), ("webp", "libwebp_anim"), ("mp4", "libx264"), ("webm", "libvpx")]:
        codec_options = ["-pix_fmt", "yuv420p"] if extension in ("mp4", "webm") else []
        subprocess.run([
            sys.argv[1], "-v", "error", "-nostdin", "-f", "rawvideo", "-pixel_format", "rgba",
            "-video_size", "32x24", "-framerate", "10", "-i", "pipe:0", "-an", "-c:v", codec,
            "-threads", "1", *codec_options, "-y", str(root / ("clip." + extension)),
        ], input=frames, check=True, timeout=15)
    # Portraits and non-vector-aligned rows exercise conversion buffer boundaries.
    # Include native odd widths and downscaling that produces an odd output width.
    for width, height in [(10, 2), (26, 2), (18, 30), (34, 50), (17, 23), (31, 47), (174, 512)]:
        for extension, codec in [("gif", "gif"), ("webp", "libwebp_anim"), ("mp4", "libx264"), ("webm", "libvpx")]:
            codec_options = ["-pix_fmt", "yuv444p" if width % 2 or height % 2 else "yuv420p"] if extension == "mp4" else []
            if extension == "webm":
                codec_options = ["-pix_fmt", "yuv420p"]
            subprocess.run([
                sys.argv[1], "-v", "error", "-nostdin", "-f", "rawvideo", "-pixel_format", "rgba",
                "-video_size", f"{width}x{height}", "-framerate", "10", "-i", "pipe:0", "-an",
                "-c:v", codec, "-threads", "1", *codec_options, "-y",
                str(root / f"unaligned-{width}x{height}.{extension}"),
            ], input=b"".join(bytes(color) * (width * height) for color in colors), check=True, timeout=15)
    # Encoded GIF exceeds the frame-count bound but is tiny on disk/in memory.
    subprocess.run([
        sys.argv[1], "-v", "error", "-nostdin", "-f", "lavfi", "-i",
        "testsrc2=size=8x8:rate=100:duration=12.1", "-c:v", "gif", "-threads", "1",
        "-y", str(root / "long.gif"),
    ], check=True, timeout=15)
    subprocess.run([
        sys.argv[1], "-v", "error", "-nostdin", "-f", "lavfi", "-i",
        "testsrc2=size=256x256:rate=30:duration=10", "-c:v", "gif", "-threads", "1",
        "-y", str(root / "large.gif"),
    ], check=True, timeout=15)
    subprocess.run([sys.argv[2], str(root)], check=True, timeout=30)
