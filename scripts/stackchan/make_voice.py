"""Make the StackChan speech clips with Windows speech synthesis.

    python make_voice.py                  # all lines below
    python make_voice.py hello "Hi!"      # one clip from your own text

Writes %USERPROFILE%\\stackchan-voice\\<name>.wav in the form NuttX's PCM
decoder and the CoreS3 speaker path play correctly:

- 16 kHz, 16-bit, **stereo** (a mono file plays at double speed on the
  ESP32-S3 I2S path),
- a plain 16-byte "fmt " chunk (System.Speech writes 18 bytes, which
  pcm_decode rejects as "Invalid PCM WAV file"),
- 0.5 s of silence at the end, so the last word is not cut off when the
  amplifier is switched off.

wg_setup.py copies the folder into the peer container, where it is served
at http://10.10.0.1:8000/ through the tunnel:

    stackchan say http://10.10.0.1:8000/hello.wav
"""

import array
import os
import subprocess
import sys
import tempfile
import wave

VOICE = "Microsoft Zira Desktop"
PITCH = "x-high"
RATE = 16000
TAIL_S = 0.5
OUT = os.path.join(os.path.expanduser("~"), "stackchan-voice")

LINES = {
    "hello": "Hello, Glasgow! I am Stack chan. I run Apache NuttX.",
    "tunnel": "You are talking to me through a WireGuard tunnel. "
              "Every packet is encrypted, all the way to my little body.",
    "thanks": "Thank you for listening. Community over code!",
}

PS = r"""
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo({rate},
  [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
  [System.Speech.AudioFormat.AudioChannel]::Mono)
$s.SetOutputToWaveFile('{out}', $fmt)
$s.SpeakSsml([IO.File]::ReadAllText('{ssml}'))
$s.SetOutputToNull()
"""


def synth(text, path):
    ssml = ("<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis'"
            " xml:lang='en-US'><voice name='%s'><prosody pitch='%s'>%s"
            "</prosody></voice></speak>" % (VOICE, PITCH, text))
    with tempfile.TemporaryDirectory() as d:
        sp = os.path.join(d, "s.xml")
        with open(sp, "w", encoding="utf-8") as f:
            f.write(ssml)
        script = PS.format(rate=RATE, out=path, ssml=sp)
        subprocess.run(["powershell.exe", "-NoProfile", "-Command", script],
                       check=True)


def fix(path):
    with wave.open(path, "rb") as r:
        p = r.getparams()
        mono = array.array("h", r.readframes(r.getnframes()))
    mono.extend([0] * int(TAIL_S * p.framerate))
    st = array.array("h", [0]) * (len(mono) * 2)
    st[0::2] = mono
    st[1::2] = mono
    with wave.open(path, "wb") as w:      # wave writes a 16-byte fmt chunk
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(p.framerate)
        w.writeframes(st.tobytes())
    return len(mono) / p.framerate


def main():
    os.makedirs(OUT, exist_ok=True)
    lines = {sys.argv[1]: sys.argv[2]} if len(sys.argv) == 3 else LINES
    for name, text in lines.items():
        path = os.path.join(OUT, name + ".wav")
        synth(text, path)
        print("%-8s %.1f s  %s" % (name, fix(path), path))


if __name__ == "__main__":
    main()
