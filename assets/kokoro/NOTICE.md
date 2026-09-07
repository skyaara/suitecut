# Bundled Kokoro assets

SuiteCut redistributes three Kokoro v1.0 model, tokenizer, and voice assets from
[`onnx-community/Kokoro-82M-v1.0-ONNX`](https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX)
at revision `1939ad2a8e416c0acfeecc08a694d14ef25f2231`. SuiteCut adds an ONNX graph
output for the model's existing token-duration tensor; it does not change the model weights. The
model repository declares these assets under Apache-2.0. The license text is in this directory as
`LICENSE`.

| Packaged file | Upstream path | SHA-256 |
| --- | --- | --- |
| `model_quantized.onnx` | `onnx/model_quantized.onnx` with the existing duration tensor exposed | `85f4d4457d42b525d5d4b12d4d4d14d3ba55ae09f2381726860698ab43e1db09` |
| `tokenizer.json` | `tokenizer.json` | `77a02c8e164413299b4b4c403b14f8e0e1c1b727db4d46a09d6327b861060a34` |
| `af_heart.bin` | `voices/af_heart.bin` | `d583ccff3cdca2f7fae535cb998ac07e9fcb90f09737b9a41fa2734ec44a8f0b` |

SuiteCut uses these files for local inference through `onnxruntime-web/wasm`. The bundled default
voice does not contact the model repository while recording or rendering. Other voice profiles
download on first use, as described in the README.
