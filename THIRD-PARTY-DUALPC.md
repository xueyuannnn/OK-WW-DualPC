# Sources and licenses

- OK-WW Enhanced: https://github.com/zzc-tongji/ok-ww-enhanced ; commit `310d8d9428ec78e62f11f429de75baf47d189add`. AGPL-3.0, see `LICENSE.txt`.
- Selected character updates: https://github.com/zzc-tongji/ok-ww-enhanced/tree/726b10e65cbeb1303214774664d0277a71b7e18a ; Aalto, Lingyang, Lumi, Yangyang, Suoming and the bounded `continues_click_until` helper. Python character implementations are retained and rotations are ported to C++; registration and resource provenance are recorded in `native/resources/character-updates.json` and `assets/upstream-characters/manifest.json`. This does not replace the overall source baseline above.
- OK-WW original: https://github.com/ok-oldking/ok-wuthering-waves . Original upstream of the enhanced game logic.
- ok-script: https://github.com/ok-oldking/ok-script ; commit `428bcd563292ac6451c84c0222e6b790d468215f`. See `vendor/ok-script/LICENSE.txt` and its attribution requirements.
- Character workshop rescue mirror: https://github.com/yamadafz/ok-ww-char-code ; public snapshot of the unavailable original workshop repository. Downloaded teams retain their original authors and metadata. See `docs/project-links.md` and `docs/workshop.md` in the source tree.
- KMBox Net protocol reference: https://github.com/kvmaibox/kmboxnet/blob/main/c%2B%2B_demo/NetConfig/kmboxNet.cpp . The Python and C++ adapters implement the publicly documented packet layouts.
- Dual-PC architecture reference: https://github.com/ai123lj/DualPC-TensorRT-Aim-KMBOX . No FPS model, targeting logic or runtime DLL is included in this adapter.
- GC573 driver: https://www.avermedia.com/support/download?model_number=GC573 . Install the manufacturer's driver on the inference PC.

Project and reference URLs were checked on 2026-11-10. Current source, release, workshop and hardware documentation entry points are listed in `docs/project-links.md` in the source tree. Pinned commits above record the source baseline and are not changed by a link check.

## Runtime components

| Component | Use | License record |
| --- | --- | --- |
| Qt 6 | Native interface and platform services | `native/licenses/`; dynamic libraries in the full package |
| OpenCV 4.12 | Image processing | SDK license and third-party notices included under `licenses/opencv` in the full package |
| OpenVINO Runtime | Native inference | Package license and notices included under `licenses/openvino` in the full package |
| OnnxOCR / PP-OCRv5 | OCR preprocessing, postprocessing and models | Apache-2.0 text included under `licenses/ocr` in the full package |
| Clipper 6.4.2 | OCR polygon operations | Boost Software License 1.0; original copyright and source hashes in `native/third_party/clipper` |
| Python 3.12 and Python dependencies | Workshop subprocess | Runtime license and individual package notices retained in `workshop-engine/runtime` |
| PySide6 Fluent Widgets 1.11.3 | UI icon resources | GPLv3 text and resource provenance in `native/resources/fluent` |

The application code follows `LICENSE.txt`. Third-party components retain their own terms; they are not relicensed by the application. In particular, the vendored ok-script snapshot uses Apache-2.0 with Commons Clause and additional terms, including a visible attribution requirement. Read `vendor/ok-script/LICENSE.txt` for the complete text. The project homepage links to ok-script.

Character artwork is not licensed as application code. Source URLs and hashes are recorded in `native/resources/portraits/manifest.json`. Task symbols are rendered with the installed Segoe UI Emoji font; the font itself is not distributed. These resources retain the rights of their respective owners.

The original license and attribution files remain in the source snapshot. Reference downloads and local migration records are not runtime dependencies.
