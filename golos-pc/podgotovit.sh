#!/bin/sh
# ПК-часть голосового диалога BF7: whisper.cpp (CUDA) + Piper (x86_64) + модели.
set -eu
cd "$(dirname "$0")"
[ -f piper/piper ] || { curl -sL -o piper.tar.gz https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz && tar xzf piper.tar.gz && rm piper.tar.gz; }
for f in ru_RU-dmitri-medium.onnx ru_RU-dmitri-medium.onnx.json; do
	[ -s "$f" ] || curl -sL -o "$f" "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/ru/ru_RU/dmitri/medium/$f"
done
[ -s ggml-large-v3-turbo-q5_0.bin ] || curl -sL -o ggml-large-v3-turbo-q5_0.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin
[ -d whisper.cpp ] || git clone -q --depth 1 https://github.com/ggml-org/whisper.cpp
cd whisper.cpp
PATH=/opt/cuda/bin:$PATH cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON -DWHISPER_BUILD_TESTS=OFF > ../cmake.log 2>&1
PATH=/opt/cuda/bin:$PATH cmake --build build -j 12 --config Release > ../build.log 2>&1
ls -la build/bin/whisper-cli
echo ГОТОВО
