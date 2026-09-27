# Project Rules & Coding Guidelines (Python OCPP Central System)

This document defines the strict architectural, development, and code-reuse rules for this project's
Python codebase and configuration. The AI assistant must strictly adhere to these guidelines at all
times.

---

## 📚 Documentation Maintenance (Dual-Docs System)

The project maintains two distinct documentation environments. You must understand the difference and
maintain both appropriately:

1. **`llm-wiki/` (Internal/Agent Knowledge Base)**: An agent-readable, structured map of the project's
   architecture, Python module responsibilities, and internal code references.
2. **`docs/` (End-User Technical Documentation)**: Human-readable documentation, deployment guides, and
   configuration references meant for the final user/operator running the Central System.

### 1. Docs-First Workflow (Mandatory)

* **Read Before Acting**: Before **ANY** action or analysis, you **MUST** first consult the internal wiki, starting from `llm-wiki/README.md`, and then drill down into the specific `llm-wiki/` file(s) relevant to the task.
* **Index-Driven Navigation**: Use `llm-wiki/README.md` as the map: identify which module/protocol/deployment docs are relevant, read them, and follow their `path/to/file.py:line` references to reach the right code directly instead of blindly searching the whole repository.
* **Docs Guide the Search**: Treat `llm-wiki/` as the first search surface. Codebase-wide exploration is a **fallback** for what the wiki does not yet cover.
* **Bootstrap on Absence**: If a specific doc does not exist or lacks relevant content, note the gap and create/extend it as part of the task. A missing doc is never an excuse to skip the docs-first step.

### 2. Mandatory Dual-Synchronization

* **Code & Docs Are One Change**: Whenever you **add, modify, or remove** any relevant element of the application, you **MUST** update the documentation **within the same task**.
* **Assess the Impact (Internal-Only Changes)**: For pure Python refactoring with no observable effect, private helpers, or internal implementation tweaks, update **`llm-wiki/`** only.
* **Assess the Impact (User-Facing Changes)**: For anything a user/operator can observe or rely on, update **`llm-wiki/`** AND **`docs/`**. This includes CLI/config options, MQTT discovery payloads/topics, OCPP message handling behavior visible to a charger or to Home Assistant, deployment/systemd setup, or the persistence schema's on-disk shape.
* **`docs/`-Coverage Test (decisive)**: If the change affects a topic **already documented by any file under `docs/`**, it is User-Facing by definition and that file **MUST** be updated. When unsure, treat the change as User-Facing.
* **No Stale Docs**: Leaving either `llm-wiki/` or `docs/` out of sync with the codebase is **STRICTLY PROHIBITED**. If you cannot fully document a change in both places, the task is **not complete**.

### 3. Directory Structures

* **LLM-Wiki (`llm-wiki/`) - Agent Optimized**: Includes `README.md` (index), `architecture.md` (system overview: OCPP/MQTT/ESPHome protocol boundaries), `modules/<name>.md` (Python module responsibilities, as they're added), `deployment.md` (Raspberry Pi read-only-root setup, persistence partition, watchdog — once written), and `decisions.md` (log of design decisions).
* **User Docs (`docs/`) - Human Optimized**: Includes `README.md` (index summarizing all user docs). Always keep it in sync when adding/renaming/removing a doc.
* **Topic Files Convention**: Topic files use the existing `UPPER_SNAKE_CASE.md` convention, one topic per file (e.g., `CONFIGURATION.md`, `DEPLOYMENT.md`, `HOME_ASSISTANT.md`).
* **Prefer Extending Existing Files**: Before adding a doc, check `docs/README.md` and the current files for an owning topic. Create a new `UPPER_SNAKE_CASE.md` only when no existing file covers the topic, and add it to the index in the same change.
* **No Ad-hoc Filenames**: Do **NOT** introduce ad-hoc filenames (e.g., `usage.md`, `api.md`) that duplicate content already owned by an existing file.

### 4. Content & Style Rules

* **For `llm-wiki/` (Concise & Factual)**: Write for an LLM consumer. Use bullet lists, tables, and clear headings. Reference the relevant source location using the `path/to/file.py:line` pattern. Do not duplicate large code blocks; describe intent and point to the code.
* **For `docs/` (Clear & Explanatory)**: Write for a human deployer/operator. Provide practical configuration examples and clear step-by-step deployment instructions. Do NOT expose internal Python class hierarchies or implementation details here; focus on the public configuration/CLI interface and observable behavior.
* **Keep the Indexes Honest**: When you add, rename, or delete a doc file in either directory, update the respective `README.md` in the same change.

### 5. Removal Discipline

* **Delete With the Code**: When a feature, config option, or Python module is **removed**, delete or update its documentation in BOTH `llm-wiki/` and `docs/` in the same change. Consistent with the project's *Zero Backward Compatibility* stance, do **NOT** keep documentation describing removed functionality.

---

## 🚀 Evolution & Breaking Changes

* **Zero Backward Compatibility**: There are no existing production deployments. **Backward compatibility is NOT required.** ALWAYS prioritize the ideal service architecture and simplicity over supporting legacy config keys or deprecated message shapes.
* **Clean Breaking Changes**: Treat every major intervention as a completely new version of the service. Implement rigid configuration contracts, mandatory options, and strict types. **CATEGORICALLY REMOVE** legacy fallback logic, deprecated compatibility shims, and old migration phases.

---

## ⚙️ Python Standards (`*.py`)

### 1. Strict DRY & Duplication Prevention

* **Pre-implementation Check**: Before writing ANY new function or module (e.g. persistence access, MQTT payload building, OCPP message handling), you **MUST** scan the codebase and explicitly check for existing shared utilities, and check whether the `ocpp`, `ha-mqtt-discoverable`, or `aioesphomeapi` libraries already provide it (see `llm-wiki/decisions.md`) before writing it yourself.
* **No Copy-Paste-Modify**: Copying, pasting, and slightly modifying existing logic for a new use case is **STRICTLY PROHIBITED**. Extract a shared function/class instead.
* **Refactoring over Duplication**: If *similar* (but not identical) logic already exists, **DO NOT** create a duplicate. Instead, refactor the existing code to make it more generic. Briefly explain your refactoring logic when doing so.
* **Rule of Three**: If you identify or need the same logic/block for a second time, treat it as a warning. If needed for a third time, you **MUST** extract it into a standalone utility function/module.

### 2. Typing & Data Models

* **Strict Typing**: Use Python type hints on all function signatures and class attributes (PEP 484). Prefer `dataclasses` or the `ocpp` package's own typed `call`/`call_result` payload classes over untyped dicts for structured data.
* **Async Discipline**: This service is `asyncio`-based (per the `ocpp`/`websockets` stack in `llm-wiki/decisions.md`). Do not perform blocking I/O inside async handlers; use async-native libraries or `asyncio.to_thread` for anything blocking.
* **No Manual Parsing**: **DO NOT** hand-parse OCPP JSON-RPC frames, MQTT discovery payloads, or ESPHome API messages if the `ocpp`, `ha-mqtt-discoverable`, or `aioesphomeapi` library already validates and models that payload.

### 3. Modularity & Maintainability

* **Single Responsibility Modules**: Keep one clear responsibility per module (e.g. OCPP handlers, persistence, MQTT publishing, ESPHome client each in their own module). If a module accumulates too many responsibilities, split it.
* **File Size Control**: Always evaluate the length of Python files. If a module handles too many responsibilities, split it into smaller, focused modules to maximize readability and simplify maintenance.


## ⚙️ Project Specific Standards & Overrides

### Formatting

- Use 4 spaces for indentation in Python files.

### Units and Configuration Values

- Use these units of measure consistently in documentation and option descriptions: power in Watts (`W`), current in
  Amperes (`A`), voltage in Volts (`V`), and energy in kilowatt-hours (`kWh`).
- Configuration examples should use plain numeric values without unit suffixes.
- If an electricity provider states a limit in kilowatts, convert it to a numeric Watt value in examples. For
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
