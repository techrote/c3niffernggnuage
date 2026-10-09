# 03 — Sensing and fusion

## CSI V1 objective

Detect repeatable change in the Wi-Fi propagation channel, not absolute distance.

ESP CSI samples encode per-subcarrier complex values. V1 should begin with magnitude-derived features because they are useful without requiring phase coherence between independent radios.

For an I/Q pair:

```text
magnitude = sqrt(I^2 + Q^2)
```

Squared magnitude may be used internally when it avoids unnecessary square roots, provided normalization and tests reflect the representation.

## NGN-003 acquisition status

NGN-003 now implements the acquisition/normalization boundary before baseline processing. The pinned ESP32-C3 representation is decoded as signed `[imaginary, real]` byte pairs. Raw bytes and receive metadata are retained, ESP-IDF's invalid first word is excluded when flagged, and squared magnitude `I^2 + Q^2` is produced deterministically on the host and firmware.

Only CSI from station MACs already bound to logical A/B/C by the accepted NGN protocol runtime enters the raw queue. Records are correlated with the nearest accepted probe from the same source inside a configurable time window; a known-source record without a match remains explicitly unattributed diagnostic evidence.

The acquisition layer deliberately performs **no gain normalization, baseline update, perturbation score or activity inference**. Those start in NGN-005. See `docs/09-CSI-ACQUISITION.md`.

## CSI conditioning

The pipeline should be explicit and inspectable:

1. validate source and packet metadata;
2. decode subcarriers according to the pinned ESP-IDF/CSI format;
3. reject malformed or clearly unusable samples;
4. derive magnitude vectors;
5. normalize against packet-level gain/RSSI effects where evidence supports the method;
6. update an empty-field baseline only under permitted calibration conditions;
7. derive bounded perturbation features;
8. aggregate over a short time window.

Candidate V1 features:

- normalized mean/median absolute deviation from baseline;
- short-window variance;
- correlation distance between current magnitude vector and baseline vector;
- RSSI change retained as a separate diagnostic feature.

Do not create a black-box composite without retaining its component evidence.

## Baseline

A baseline represents the local RF field under a declared calibration condition.

Required states:

- uncalibrated;
- collecting;
- ready;
- stale/degraded.

Calibration must have an explicit user/system transition. Do not continuously absorb large disturbances into the baseline so quickly that the sensor erases the event it is meant to show.

A slow adaptive component may be added after a fixed-baseline path is proven. Adaptation rate must be configurable and replay-testable.

## Directed versus undirected links

CSI is measured at a receiver, so A->B and B->A are separate observations.

Retain directed metrics internally. For the first triangle visualization, derive an undirected edge score from the two available directions using a documented robust combination such as median/mean/max with age/quality handling.

Do not assume perfect RF reciprocity.

## Activity vector

The primary V1 spatial evidence is:

```text
E = [AB, BC, CA]
```

where each element is a normalized perturbation score with its own quality/age.

This vector is meaningful even when no position is inferred.

## Abstract field geometry

A display centroid or heat pattern may be produced by weighting triangle edge midpoints with the edge activity values.

This is a visualization mapping only.

- Strong AB activity may brighten the AB edge/midpoint region.
- Strong BC activity may brighten BC.
- Strong CA activity may brighten CA.
- Similar strong activity on all links may pull the visual field toward the center.

Do not label the result as metres, coordinates or person position.

## Motion versus static presence

Motion should generally create stronger temporal changes than a stationary object/person.

The first release should characterize:

- empty-field stability;
- walking/arm/body motion;
- step-in/step-out transitions;
- stationary presence as an exploratory case.

Static occupancy must not be a required claim for V1.

## BLE V1

Use passive scanning.

Each normalized observation contributes:

- session-scoped observation key;
- node ID;
- RSSI;
- advertisement metadata summary;
- timestamp.

Local/coordinator tracks use finite TTLs. A track may transition:

```text
NEW -> ACTIVE -> AGING -> EXPIRED
```

RSSI filtering should be simple and bounded, for example median or EWMA, and must preserve raw/latest values for debugging.

## Multi-node BLE representation

When two or three nodes report the same session-scoped key within a correlation window, form an RSSI vector:

```text
R = [rssi_A, rssi_B, rssi_C]
```

This may guide an abstract display placement toward the strongest receiver, but is not calibrated distance or location.

Missing node observations are normal due to advertising timing and Wi-Fi/BLE coexistence.

## BLE/CSI coincidence

A coincidence event means only:

- a BLE track became newly visible or changed materially;
- a significant CSI activity event occurred within a configured time window.

Record:

- event time;
- BLE track key;
- relevant edge activity vector;
- temporal offset;
- quality/confidence based on evidence availability.

Never convert this directly into a statement that the BLE transmitter caused the CSI event or belongs to a person.

## Tuning strategy

Thresholds must come from captured data.

Avoid embedding unexplained magic values. Defaults should be named, documented, serializable in session metadata and exercised by replay tests.

## Deferred work

Explicitly outside V1 unless a later issue adds it:

- phase sanitization across independent oscillators;
- AoA;
- calibrated ranging;
- ML classifiers;
- persistent device fingerprinting;
- camera/ground-truth integration.
