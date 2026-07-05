# Project Rules & Coding Guidelines (C++ & ESPHome)

This document defines the strict architectural, development, and code-reuse rules for the ESPHome C++ codebase and YAML configurations. The AI assistant must strictly adhere to these guidelines at all times.

---

## 📚 Documentation Maintenance (Dual-Docs System)

The project maintains two distinct documentation environments. You must understand the difference and maintain both appropriately:

1. **`llm-wiki/` (Internal/Agent Knowledge Base)**: An agent-readable, structured map of the projects's architecture, custom C++ logic, and internal code references.
2. **`docs/` (End-User Technical Documentation)**: Human-readable documentation, user guides, and YAML configuration references meant for the final user or deployer.

### 1. Docs-First Workflow (Mandatory)

* **Read Before Acting**: Before **ANY** action or analysis, you **MUST** first consult the internal wiki, starting from `llm-wiki/README.md`, and then drill down into the specific `llm-wiki/` file(s) relevant to the task.
* **Index-Driven Navigation**: Use `llm-wiki/README.md` as the map: identify which component/hardware-bus/yaml-node docs are relevant, read them, and follow their `path/to/file.cpp:line` or `path/to/config.yaml:line` references to reach the right code directly instead of blindly searching the whole repository.
* **Docs Guide the Search**: Treat `llm-wiki/` as the first search surface. Codebase-wide exploration is a **fallback** for what the wiki does not yet cover.
* **Bootstrap on Absence**: If a specific doc does not exist or lacks relevant content, note the gap and create/extend it as part of the task. A missing doc is never an excuse to skip the docs-first step.

### 2. Mandatory Dual-Synchronization

* **Code & Docs Are One Change**: Whenever you **add, modify, or remove** any relevant element of the application, you **MUST** update the documentation **within the same task**.
* **Assess the Impact (Internal-Only Changes)**: For pure C++ refactoring with no observable effect, private class helpers, or internal hardware timing tweaks, update **`llm-wiki/`** only.
* **Assess the Impact (User-Facing Changes)**: For anything a user/operator can observe or rely on, update **`llm-wiki/`** AND **`docs/`**. This includes YAML configuration nodes, custom sensor states, MQTT payloads, exposed API services, OTA procedures, or hardware wiring requirements.
* **`docs/`-Coverage Test (decisive)**: If the change affects a topic **already documented by any file under `docs/`**, it is User-Facing by definition and that file **MUST** be updated. When unsure, treat the change as User-Facing.
* **No Stale Docs**: Leaving either `llm-wiki/` or `docs/` out of sync with the codebase is **STRICTLY PROHIBITED**. If you cannot fully document a change in both places, the task is **not complete**.

### 3. Directory Structures

* **LLM-Wiki (`llm-wiki/`) - Agent Optimized**: Includes `README.md` (index), `architecture.md` (firmware overview/bus topology), `components/<name>.md` (C++ custom components, `setup()`/`loop()` responsibilities), `hardware.md` (pinouts, I2C/SPI addresses), and `decisions.md` (log of design decisions).
* **User Docs (`docs/`) - Human Optimized**: Includes `README.md` (index summarizing all user docs). Always keep it in sync when adding/renaming/removing a doc.
* **Topic Files Convention**: Topic files use the existing `UPPER_SNAKE_CASE.md` convention, one topic per file (e.g., `QUICKSTART.md`, `YAML_CONFIG.md`, `WIRING.md`, `SENSORS.md`, `OTA_UPDATES.md`).
* **Prefer Extending Existing Files**: Before adding a doc, check `docs/README.md` and the current files for an owning topic. Create a new `UPPER_SNAKE_CASE.md` only when no existing file covers the topic, and add it to the index in the same change.
* **No Ad-hoc Filenames**: Do **NOT** introduce ad-hoc filenames (e.g., `usage.md`, `api.md`) that duplicate content already owned by an existing file.

### 4. Content & Style Rules

* **For `llm-wiki/` (Concise & Factual)**: Write for an LLM consumer. Use bullet lists, tables, and clear headings. Reference the relevant source location using the `path/to/file.h:line` or `path/to/device.yaml:line` pattern. Do not duplicate large code blocks; describe intent and point to the code.
* **For `docs/` (Clear & Explanatory)**: Write for a human developer/deployer. Provide practical YAML configuration snippets, wiring examples, and clear step-by-step flashing guides. Do NOT expose internal C++ class hierarchies or pointer logic here; focus on the public configuration interface.
* **Keep the Indexes Honest**: When you add, rename, or delete a doc file in either directory, update the respective `README.md` in the same change.

### 5. Removal Discipline

* **Delete With the Code**: When a feature, YAML block, or C++ component is **removed**, delete or update its documentation in BOTH `llm-wiki/` and `docs/` in the same change. Consistent with the project's *Zero Backward Compatibility* stance, do **NOT** keep documentation describing removed functionality.

---

## 🚀 Evolution & Breaking Changes

* **Zero Backward Compatibility**: There are no existing production environments. **Backward compatibility is NOT required.** ALWAYS prioritize the ideal firmware architecture and optimal memory usage over supporting legacy YAML nodes or deprecated sensor states.
* **Clean Breaking Changes**: Treat every major intervention as a completely new version of the firmware. Implement rigid YAML contracts, mandatory component properties, and strict state types. **CATEGORICALLY REMOVE** legacy fallback logic, deprecated `#ifdef` wrappers, and old migration phases.

---

## ⚙️ C++ & ESPHome Standards (`*.cpp`, `*.h`, `*.yaml`)

### 1. Strict DRY & Duplication Prevention

* **Pre-implementation Check**: Before writing ANY new C++ function, custom component, or YAML package (e.g., data formatting, I2C polling, debouncing logic), you **MUST** scan the codebase and explicitly check `utils.h`, `helpers.h`, or existing ESPHome core components.
* **No Copy-Paste-Modify**: Copying, pasting, and slightly modifying existing C++ logic for a new use case is **STRICTLY PROHIBITED**. Use ESPHome's YAML substitutions, packages, or C++ templates/base classes instead.
* **Refactoring over Duplication**: If *similar* (but not identical) logic already exists, **DO NOT** create a duplicate. Instead, refactor the existing C++ code to make it more generic. Briefly explain your refactoring logic when doing so.
* **Rule of Three**: If you identify or need the same logic/block for a second time, treat it as a warning. If requested for a third time, you **MUST** extract it into a standalone utility function.
* **No Inline Lambdas**: Writing C++ lambdas directly inside ESPHome YAML files is **PROHIBITED**.

### 2. Typing, Memory & Data Models

* **Strict C++ Typing**: Strictly use standard C++ types and ESPHome's native classes (e.g., `esphome::sensor::Sensor`, `esphome::text_sensor::TextSensor`). Use `struct` or `class` definitions in header files for grouping data.
* **Memory Management**: Prioritize stack allocation. Avoid raw pointers (`*`) and manual `new`/`delete` calls. If dynamic allocation is strictly necessary, use smart pointers or ESPHome's component lifecycle management.
* **No Manual Parsing**: **DO NOT** perform manual byte-by-byte buffer parsing if a standard ESPHome component, I2C bus abstraction, or struct cast can safely validate and instantiate that specific sensor payload.

### 3. Modularity & Maintainability

* **Header and Implementation Split**: For C++ files, strictly separate declarations into `.h` files (using `#pragma once`) and implementations into `.cpp` files to reduce compilation overhead.
* **File Size Control**: Always evaluate the length of C++ files. If a C++ component handles too many responsibilities, split it into smaller, focused classes to maximize readability and simplify maintenance.


## ⚙️ Project Specific Standards & Overrides

### Formatting

- Use 4 spaces for indentation in both Python and C++ files.

### Units and Configuration Values

- Use these units of measure consistently in documentation and option descriptions: power in Watts (`W`), current in
  Amperes (`A`), voltage in Volts (`V`), and energy in kilowatt-hours (`kWh`).
- YAML configuration examples should use plain numeric values without unit suffixes.
- If an electricity provider states a limit in kilowatts, convert it to a numeric Watt value in YAML examples. For
  example, `10 kW` becomes `10000`.

### Markdown Tables

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

