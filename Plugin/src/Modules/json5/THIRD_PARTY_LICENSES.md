# Third-party licenses

The `json` module is licensed under the GPL v2.0 (see the project license).
It bundles the following third-party code.

## json5

- Project: https://github.com/P-i-N/json5
- Upstream commit: `11084bf6aeef779e115483c6d31330e8508c9a3b`
- License: MIT
- Location: `third_party/json5/`

Files used: `json5.hpp`, `json5_base.hpp`, `json5_builder.hpp`,
`json5_input.hpp`, `LICENSE`.

`json5.hpp`, `json5_base.hpp`, `json5_builder.hpp` and `LICENSE` are
byte-identical to upstream. `json5_input.hpp` carries two small fixes, described
in its header comment and in `third_party/json5/RAINJIT.patch`:

1. JSON `null` is stored as a null value (upstream stored the number `0`).
2. Quoted object keys accept any characters (upstream accepted only
   `[A-Za-z0-9_]`).

The MIT license is compatible with the GPL v2.0. The original copyright and
permission notice must stay with the files above and with any binary
distribution that includes them:

```
MIT License

Copyright (c) 2020 P-i-N

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
