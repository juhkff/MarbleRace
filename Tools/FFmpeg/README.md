# Recording encoder

FFmpeg 9.0.2 essentials static Windows build from https://www.gyan.dev/ffmpeg/builds/.
FFmpeg sources: https://github.com/FFmpeg/FFmpeg/tree/n9.0.2 .
This executable is distributed under GPLv3; see LICENSE. It runs as a separate process.

Downloaded from https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip and
checked against the accompanying SHA-256 file in Saved/Downloads.
The runtime uses ffmpeg.exe. ffprobe.exe is retained for verification only.
Both executables are optional local dependencies excluded from Git.

To enable recording, download the essentials ZIP linked above and copy the
archive's bin/ffmpeg.exe and bin/ffprobe.exe into this directory. Verify the ZIP
against its published SHA-256 before extracting. Keep LICENSE with ffmpeg.exe
when distributing a recorded-game build. Without the encoder the project still
builds and packages; enabling recording displays a missing-tool status.

The recorder reads only Unreal's game viewport and game audio mixer, encodes
H.264/AAC MP4 in Saved/Recordings, and never opens desktop or microphone inputs.
