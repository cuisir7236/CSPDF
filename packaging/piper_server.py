#!/usr/bin/env python3
"""常驻 Piper 服务：预加载模型，通过 stdin/stdout 与主程序通信。
协议：每行一个 JSON 请求 {"text": "...", "wav": "/path/out.wav"}
响应：{"ready": true, "sample_rate": N} 或 {"wav": "/path"} 或 {"error": "..."}
优化：句间不加额外静音（连续性）、语速略快、降噪（清亮）。
"""
import sys, json, wave

def main():
    model = sys.argv[1] if len(sys.argv) > 1 else ""
    config = sys.argv[2] if len(sys.argv) > 2 else ""
    if not model or not config:
        print(json.dumps({"error": "missing model args"}), flush=True)
        return 1
    try:
        from piper import PiperVoice
        from piper.config import SynthesisConfig
        voice = PiperVoice.load(model, config_path=config)
        sample_rate = voice.config.sample_rate
        # 合成参数：语速略快(0.95)、降噪(0.5)、更干净(noise_w 0.6)
        syn_config = SynthesisConfig(
            length_scale=0.95,
            noise_scale=0.5,
            noise_w_scale=0.6,
            normalize_audio=True,
            volume=1.0,
        )
    except Exception as e:
        print(json.dumps({"error": f"load fail: {e}"}), flush=True)
        return 1
    print(json.dumps({"ready": True, "sample_rate": sample_rate}), flush=True)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            text = req.get("text", "")
            wav_path = req.get("wav", "/tmp/cspdf_page.wav")
            # 整页合成：所有句子音频连续拼接，句间不加额外静音
            audio = b""
            for chunk in voice.synthesize(text, syn_config):
                audio += chunk.audio_int16_bytes
            if not audio:
                print(json.dumps({"error": "empty audio"}), flush=True)
                continue
            wf = wave.open(wav_path, "wb")
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            wf.writeframes(audio)
            wf.close()
            print(json.dumps({"wav": wav_path}), flush=True)
        except Exception as e:
            print(json.dumps({"error": str(e)}), flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())
