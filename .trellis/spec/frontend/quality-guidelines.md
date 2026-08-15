# Simulator Quality Guidelines

## Scenario: Firmware-Accurate Canvas Simulator

### 1. Scope / Trigger

Apply when `sim/index.html` previews firmware layout or generated bitmap fonts.
The simulator is a rendering adapter, not a second owner of protocol semantics.

### 2. Signatures

```sh
python3 tools/make_sim_font.py firmware/src sim/font_data.js
node sim/verify.js
```

`sim/font_data.js` is generated output. `sim/verify.js` must evaluate font data
before evaluating the inline simulator script.

### 3. Contracts

- Screen geometry is 960x320 and matches firmware region constants.
- Simulator-only metadata such as range value, filter count/type, or GPIB 16
  is explicitly labeled as scenario input and must not imply panel-protocol
  support.
- Generated ASCII and dedicated micro/ohm/degree/plus-minus glyphs are loaded
  before any headless render or glyph assertion.
- No-DOM evaluation must safely skip browser initialization while keeping pure
  constants, trace generation, and bitmap lookup testable.
- Authentic Keithley labels are allowed; DMM6500-only vocabulary is forbidden.

### 4. Validation & Error Matrix

| Condition | Required behavior |
| --- | --- |
| `document` is unavailable | Script loads without touching DOM APIs |
| Font data is not loaded | Verification must fail, not silently use fallback |
| Dedicated `Ω` glyph requested | Return generated symbol bitmap, not `?` |
| Layout regions do not total 320px | Verification fails |
| Simulator preset has unavailable target metadata | UI includes an explicit simulator-only note |

### 5. Good/Base/Bad Cases

- Good: verifier loads `font_data.js`, then page logic, and asserts the `Ω`
  symbol plus deterministic 240-column trace.
- Base: browser opens with the complete demonstration preset.
- Bad: verifier evaluates only `index.html`, causing generated symbol globals
  to be undefined or allowing font drift to go untested.

### 6. Tests Required

- Assert 960x320 dimensions and all vertical regions sum to 320.
- Assert fixed X-axis labels and the configured render-column budget.
- Assert deterministic, non-flat trend fixture.
- Assert required Keithley labels and absence of forbidden DMM6500 terms.
- Assert simulator-only metadata disclosure and dedicated `Ω` bitmap lookup.

### 7. Wrong vs Correct

#### Wrong

```js
vm.runInContext(pageScript, sandbox); // SYMBOL_BITMAPS may be undefined
```

#### Correct

```js
vm.runInContext(fontData + "\n" + pageScript, sandbox);
```

## Review Checklist

- [ ] Regenerate `font_data.js` after C bitmap changes; do not hand-edit it.
- [ ] Run `node sim/verify.js` after layout, labels, trace, or font changes.
- [ ] Keep target unknown-value behavior distinct from simulator presets.
