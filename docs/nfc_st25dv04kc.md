# NFC / ISO15693 Tag (ST25DV04KC, SO8) – Wake Strategies and Notes

## Wake strategies
- **V_EH wake (RF-powered, no VCC):** harvested V_EH drives an RC → low-Vth NMOS that pulls `GATE` low via Schottky. Standby ≈0; needs near-contact (≤5 mm); any strong 13.56 MHz source can trigger—filter in firmware if no valid NFC follows.
- **Always-on + GPO:** VCC tied to switched 3V3; GPO pulse on RF/mailbox wakes MCU. Standby ≈3–6 µA (ICC1_PON @3.3 V). Better range (≈10–20 mm typical, best case 30–40 mm with ferrite backing) but costs µA budget.

## Antenna and layout
- Target L≈4.8 µH (28.5 pF internal cap). Use planar loop sized to the window; back with ferrite sheet to push flux forward. Avoid ferrite rods (tiny coupling area, lossy at 13.56 MHz).
- Keep series R pad (1–2 Ω) and optional series C trim; keep loop clear of ground/metal. Twist feed pair; keep short to tag pins.

## V_EH wake hookup
- V_EH → RC (tens of ms) → gate of low-Vth NMOS; source to GND, drain to `GATE` via Schottky (prevents reverse bias).
- Clamp V_EH with ~3.3 V TVS/Zener (unregulated). Threshold ~1–1.5 V to beat 1–3.3 MΩ gate bias.
- Isolation rule: do not create DC path from V_EH/GPO into 3V3 or `PBx_GPIO`.

## GPO path
- Open-drain; pull-up 10–47 kΩ to 3V3 (negligible leakage when idle). Use for RF field + mailbox PUT interrupts when powered; unreliable for wake when VCC=0.

## Range expectations
- V_EH wake: contact to a few mm; >1 cm unlikely.
- Always-on GPO: ~10–20 mm typical; best case 30–40 mm with ferrite and alignment; >50 mm unrealistic.

## Test checklist
- Measure V_EH open/loaded (100 k / 1 M) vs distance/orientation with real phones in the enclosure.
- Verify `GATE` is pulled low by V_EH→NMOS path; confirm no rise in off-state current.
- Check for back-power on SDA/SCL when VCC=0 and RF active; add small series resistors or a bus switch if needed.
- If range/reliability insufficient, fall back to always-on mode and accept the ~3–6 µA standby hit.
