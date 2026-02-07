# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
### Changed
- Decoupled `hem_hvac.ino` from MPC control logic.
- Removed MPC MQTT subscriptions and heartbeat watchdog.
- Reduced heating hysteresis for tighter temperature control.
- Reduced heating minimum runtime to 5 minutes to prevent temperature overshoot.
