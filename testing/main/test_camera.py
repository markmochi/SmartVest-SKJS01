"""Exercise the production AVI writer; external FFmpeg playback is optional.

Keyed event/clip certification is covered by test_camera_integrity.py and
production persistence/recovery by test_camera_storage.py.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[2]/'Main_and_Cameras'
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    base = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-I' + str(root.parent / 'testing/system/platform'),
            '-I' + str(root / '../testing/main/camera')]
    subprocess.run(base + [str(root / '../testing/main/camera/policy_test.cpp'), '-o', str(p / 'policy')], check=True)
    subprocess.run([str(p / 'policy')], check=True)
    subprocess.run(base + [str(root / '../testing/main/camera/avi_test.cpp'), '-lcrypto', '-o', str(p / 'writer')], check=True)
    Image.new('RGB', (640, 480), (10, 100, 150)).save(p / 'frame.jpg')
    (p / 'clip_system').mkdir()
    subprocess.run([str(p / 'writer'), str(p / 'frame.jpg'), str(p / 'clip')], check=True)
    assert not (p / 'clip_system/active.part').exists()
    assert not list(p.rglob('*.idx'))
    assert (p / 'clip_interrupted.part').exists()
    assert not (p / 'clip_interrupted.avi').exists()
    assert not (p / 'clip_empty.avi').exists() and not (p / 'clip_bad.avi').exists()
    assert (p / 'clip_resumed.avi').exists() and (p / 'clip_LOCKED.avi').exists()
    print('PASS production writer and window policy: finalization, interrupted source retention, restart, empty/invalid frame rejection, no index sidecars')

    if ffmpeg and ffprobe:
        result = subprocess.check_output([ffprobe, '-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', str(p / 'clip.avi')], text=True)
        data = json.loads(result)
        stream = data['streams'][0]
        assert stream['codec_name'] == 'mjpeg' and stream['width'] == 640 and stream['height'] == 480
        assert stream['nb_read_frames'] == '24' and abs(float(data['format']['duration']) - 3) < .01
        for name in ['clip.avi', 'clip_LOCKED.avi', 'clip_resumed.avi']:
            subprocess.run([ffmpeg, '-v', 'error', '-xerror', '-i', str(p / name), '-f', 'null', '-'], check=True)
        print('PASS FFmpeg playback: 640x480 MJPEG, 24 frames, 3.000 seconds; protected and resumed clips decoded')
    else:
        print('SKIP external FFmpeg playback: ffmpeg and ffprobe must both be on PATH; writer checks passed, external playback was not run')
