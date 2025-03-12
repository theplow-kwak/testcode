#!/usr/bin/python3

import os
import sys
import PIL
from moviepy.editor import *

PIL.Image.ANTIALIAS = PIL.Image.LANCZOS


def combine_video(dir_path):
    mp4_files = []
    for root, dir, files in os.walk(dir_path):
        for file in files:
            if file.endswith('.mp4'):
                mp4_files.append(os.path.join(root, file))
    mp4_files.sort()

    if len(mp4_files) <= 0:
        print("There is no MP4 files", dir_path)
        sys.exit()

    # MP4 파일들을 하나로 합칩니다.
    clip = concatenate_videoclips([VideoFileClip(file) for file in mp4_files])
    return clip


dir_path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/hana/")
clip = combine_video(dir_path)

new_name = "cc_" + os.path.basename(os.path.normpath(dir_path)) + ".mp4"

print(f"FPS: {clip.fps}, channels: {clip.audio.nchannels}")
print(f"Width x Height of clip 1 : {clip.w} x {clip.h}")

resize_clip = clip.resize(0.2)
print(f"after resize Width x Height of clip 1 : {resize_clip.w} x {resize_clip.h}")
clip.write_videofile(new_name)

# louder_clip = clip.audio.audio_normalize().volumex(10)
# # print(f"channels: {clip.audio.nchannels}, louder_maxvol: {louder_clip.max_volume()}")

# louder_clip.write_audiofile("louder_audio.mp3", 22050, codec='libmp3lame', ffmpeg_params=["-ac", "1"])
