# ESPHome OCPP Component

Component for controlling OCPP EV chargers from a local node.

The configuration is organized around three concepts: **site** (shared electrical installation), **charge_point** (OCPP device), and **connector** (individual charging outlet). The site owns electrical constraints, charge points represent chargers, and connectors represent the controlled outlets.

See [docs/](docs/README.md) for the full configuration reference, example YAML, and all available options.
