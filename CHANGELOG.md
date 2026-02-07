# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
### Changed
- Decoupled `hem_hvac.ino` from MPC control logic.
- Removed MPC MQTT subscriptions and heartbeat watchdog.
- Adjusted heating hysteresis to [-0.5, +0.0] for tighter setpoint tracking and thermal lag compensation.
- Reduced heating minimum runtime to 5 minutes to prevent temperature overshoot.
