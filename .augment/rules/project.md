# Project Rules

## Privacy

- Never read, write or execute command to access files outside this repository

## Repository Scope

- Consider only the esphome-ocpp repository and not any other repositories unless explicitly mentioned.
- You are allowed to run esphome-wrapper with compile and config command only unless explicily authorized.

## Units and Configuration Values

- Use these units of measure consistently in documentation and option descriptions: power in Watts (`W`), current in
  Amperes (`A`), voltage in Volts (`V`), and energy in kilowatt-hours (`kWh`).
- YAML configuration examples should use plain numeric values without unit suffixes.
- If an electricity provider states a limit in kilowatts, convert it to a numeric Watt value in YAML examples. For
  example, `10 kW` becomes `10000`.

## Markdown Documentation

### User vs Developer Documentation

- The root `README.md` should contain only user-facing documentation: features, configuration reference, YAML examples,
  and operational hints useful to someone installing or using the component.
- Developer-facing implementation notes, design rationale, charger quirks observed during development, and future
  engineering ideas should go under `docs/`, starting with `docs/development.md`.
- If developer documentation grows too large, split `docs/development.md` into focused files under `docs/`.

### Tables

- Keep Markdown tables fixed-width/aligned with padded columns for readability.
- Align the header row, separator row, and body rows so non-final pipe characters line up vertically.
- Use exactly three dashes (`---`) in each separator cell, padded with spaces as needed to preserve alignment.
- The last column may be shorter than the other columns.
- Markdown pipe table rows must stay on a single physical line; do not wrap a table cell onto following source lines
  because this breaks rendering in common Markdown parsers.
- Do not worry too much about long source lines in Markdown tables. Prefer correct rendering over strict source line
  length.
- If a table cell becomes too long to read comfortably as rendered text, use `<br>` inside the cell to add visible line
  breaks. Otherwise, keep the cell as a continuous paragraph.

### Configuration Reference Tables

- Configuration reference tables should follow ESPHome's style: use `Option` and `Description` columns.
- Put `(Required)`, `(Optional)`, or `(Conditionally required)` in the option name column.
- Put default values in the description and list available values in the description.
- Do not use separate `Required` or `Default` columns.
- Do not add bold or italic Markdown markers in the option column; use plain text like `` `mode` (Optional)``.
