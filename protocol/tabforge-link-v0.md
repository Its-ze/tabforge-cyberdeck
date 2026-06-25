# TabForge Link v0

TabForge Link is a newline-delimited JSON protocol for companion devices that choose to run a TabForge bridge firmware. It is not required for official Meshtastic or MeshCore mode.

## Framing

- Encoding: UTF-8 JSON.
- Delimiter: newline.
- Direction: bidirectional.
- Max frame size: 2048 bytes unless negotiated.

## Common Envelope

```json
{
  "type": "tabforge.device.status",
  "protocol": "tabforge-link-v0",
  "id": "unit-c6l",
  "ts": 1767225600,
  "payload": {}
}
```

## Message Types

### `tabforge.device.hello`

Sent by a bridge device after boot or when probed.

Payload:

```json
{
  "name": "Unit C6L",
  "firmware": "0.1.0",
  "mode": "tabforge-bridge",
  "capabilities": ["oled", "rgb", "buzzer", "button", "lora"]
}
```

### `tabforge.device.status`

Periodic status packet.

Payload:

```json
{
  "batteryMv": null,
  "rssi": null,
  "uptimeSec": 120,
  "button": "idle",
  "mode": "meshtastic"
}
```

### `tabforge.mesh.command`

Command from Tab5 to a bridge device.

Payload:

```json
{
  "driver": "meshcore",
  "command": "start ota",
  "confirm": true
}
```

### `tabforge.ir.event`

IR learn or send result.

Payload:

```json
{
  "action": "learned",
  "protocolName": "NEC",
  "address": "0x00FF",
  "command": "0x02FD"
}
```

### `tabforge.audio.marker`

Metadata for a local SD audio recording.

Payload:

```json
{
  "path": "/tabforge/audio/2026-06-25T120000Z.wav",
  "durationMs": 4200,
  "intent": "voice-note"
}
```

## Safety Rules

- Bridge firmware must reject firmware-write commands unless `confirm` is true and the command is allowlisted.
- The Tab5 UI must show the target device and action before sending destructive commands.
- Mesh secrets must not be sent in status frames.

