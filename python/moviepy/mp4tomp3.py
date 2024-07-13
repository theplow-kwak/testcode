#!/usr/bin/python3

import os
import moviepy.editor as mp

dir_path = "~/video/"

mp4_files = []
for (root, dir, files) in os.walk(dir_path):
    for file in files:
        mp4_files.append(os.path.join(root, file))
mp4_files.sort()

for file in mp4_files:
    print(file) 

# MP4 파일들을 하나로 합칩니다.
combined_clip = mp.concatenate_videoclips([mp.VideoFileClip(file) for file in mp4_files])
combined_clip.write_videofile("combined.mp4", fps=15, audio_fps=44100)

# 합친 동영상에서 오디오만 추출하여 MP3 파일로 저장합니다.
combined_clip.audio.write_audiofile("output_audio.mp3", nbytes=1, fps=15, bitrate="16K")

print("소리만 추출하여 output_audio.mp3 파일로 저장되었습니다.")
