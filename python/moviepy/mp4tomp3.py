#!/usr/bin/python3

import os
import sys
from moviepy.editor import *


mp4_file = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("combined.mp4")

if not os.path.exists(mp4_file):
    print("There is no files ", mp4_file)
    sys.exit()


clip = VideoFileClip(mp4_file)
louder_clip = clip.audio.audio_normalize().volumex(10)

print(f"FPS: {clip.fps}, channels: {clip.audio.nchannels}, maxvol: {clip.audio.max_volume()}, louder_maxvol: {louder_clip.max_volume()}")

new_name = os.path.splitext(mp4_file)[0] + ".mp3"
# clip.audio.write_audiofile("output_audio.mp3", 22050, ffmpeg_params=["-ac", "1"])
louder_clip.write_audiofile(new_name, 22050, ffmpeg_params=["-ac", "1"])
