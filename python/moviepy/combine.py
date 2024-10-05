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
            mp4_files.append(os.path.join(root, file))
    mp4_files.sort()

    if len(mp4_files) <= 0:
        print("There is no files ", dir_path)
        sys.exit()

    # MP4 파일들을 하나로 합칩니다.
    clip = concatenate_videoclips([VideoFileClip(file) for file in mp4_files])
    return clip


dir_path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/hana/")

clip = combine_video(dir_path)

print(f"FPS: {clip.fps}, channels: {clip.audio.nchannels}")
print(f"Width x Height of clip 1 : {clip.w} x {clip.h}")
clip = clip.resize(0.2)
print(f"after resize Width x Height of clip 1 : {clip.w} x {clip.h}")

clip.audio.write_audiofile("combined.mp3", 22050, ffmpeg_params=["-ac", "1", '-metadata', 'creation_time=2021/02/16 14:00:33'])
clip.write_videofile("combined.mp4")
