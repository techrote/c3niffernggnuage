# 06 — BLE privacy and project scope

## Purpose of BLE sensing

BLE advertisements are used as one ambient radio signal alongside Wi-Fi CSI.

The system is interested in events such as:

- an advertiser became newly observable;
- its received strength changed;
- multiple C3 nodes observed the same current advertiser;
- the event was temporally close to a CSI perturbation.

The project is not intended to identify people.

## Passive baseline

V1 scanning is passive.

The firmware must not:

- connect to discovered devices;
- enumerate GATT services by establishing connections;
- transmit directed requests to unknown BLE devices;
- attempt pairing;
- attempt to bypass address privacy.

Advertisement content that is already broadcast over the air may be summarized.

## Address handling

Bluetooth devices may use changing private/random addresses.

Treat rotation as a privacy feature.

- Do not attempt to reconstruct a persistent identity across rotations.
- Do not claim that two rotating addresses are the same physical device.
- Use session-scoped observation keys for correlation within a current session/address lifetime.
- Session keys/nonces should change across sessions so logs do not accidentally create durable identifiers.

## Logging

Default experiment logs should avoid raw BLE addresses.

Log:

- session-scoped observation key;
- address type;
- RSSI;
- bounded advertisement metadata;
- lifecycle timestamps;
- receiver node.

A developer diagnostic mode may expose a raw address transiently if required to debug acquisition, but it must be opt-in, obvious and not the default capture format.

## Interpretation

A new BLE observation plus a CSI disturbance is a **temporal RF coincidence**.

It is not evidence that:

- the BLE device belongs to the moving person;
- the BLE device caused the CSI disturbance;
- the person has been identified;
- the visualized BLE position is a calibrated physical location.

UI and documentation should use neutral terms such as advertiser, observation, track and coincidence.

## Spatial display

BLE RSSI differences across A/B/C may guide an abstract screen position or emphasis.

Do not display metres or a precise map position unless a future calibrated experiment establishes that capability.

## Retention

The repository should contain only small synthetic/curated fixtures.

Large real-world captures belong in user-controlled experiment storage unless a later issue explicitly establishes a dataset publication policy.
