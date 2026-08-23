# Bundled Kokoro assets

SuiteCut includes `model_quantized.onnx`, `tokenizer.json`, and `af_heart.bin` from
[`onnx-community/Kokoro-82M-v1.0-ONNX`](https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX).
The model repository identifies these assets as Apache-2.0 licensed.

The package uses the assets only for local inference through `onnxruntime-web/wasm`. It does not
contact the model repository while recording or rendering.
