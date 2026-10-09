# Upstream references

Programme bootstrap references were checked on 2026-10-03. The exact pinned NGN-002 API references below were checked on 2026-10-09.

Prefer pinned/versioned documentation when implementing against the v5.5.5 baseline.

## ESP-IDF

- ESP-IDF v5.5.5 release:
  https://github.com/espressif/esp-idf/releases/tag/v5.5.5
- ESP32-C3 ESP-IDF release-v5.5 documentation:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c3/
- ESP32-C3 RF coexistence documentation:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c3/api-guides/coexist.html
- ESP32-C3 Bluetooth LE overview:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c3/api-guides/ble/overview.html
- ESP32-C3 Wi-Fi API / CSI reference:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c3/api-reference/network/esp_wifi.html
- ESP-NOW API:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c3/api-reference/network/esp_now.html

## Espressif CSI reference implementation

- ESP-CSI repository:
  https://github.com/espressif/esp-csi
- get-started CSI examples:
  https://github.com/espressif/esp-csi/tree/master/examples/get-started
- current csi_recv example:
  https://github.com/espressif/esp-csi/tree/master/examples/get-started/csi_recv
- ESP-Radar examples:
  https://github.com/espressif/esp-csi/tree/master/examples/esp-radar

At bootstrap, Espressif's ESP-CSI CI explicitly covers ESP-IDF 5.4 and 5.5 and includes ESP32-C3 targets. The project therefore pins v5.5.5 rather than relying on newer CSI component compatibility without evidence.

## Implementation rule

Upstream examples are references, not code authority for this repository.

When copying or adapting upstream code:

- preserve applicable license notices;
- pin/record the source revision when material;
- wrap hardware-specific behavior behind this project's interfaces;
- add tests for the local contract;
- do not copy stale configuration from a different ESP-IDF release without verification.

## NGN-002 pinned implementation references

- ESP-NOW API, callback/task guidance and completion limitations:
  https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/network/esp_now.html
- ESP-IDF v5.5.5 ESP-NOW declarations, including `esp_now_send_info_t`:
  https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_wifi/include/esp_now.h
- Pinned Wi-Fi initialization/API declarations:
  https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_wifi/include/esp_wifi.h
- Random generation with an active Wi-Fi entropy source:
  https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c3/api-reference/system/random.html
- Pinned FreeRTOS tick configuration:
  https://github.com/espressif/esp-idf/blob/v5.5.5/components/freertos/Kconfig

The repository's protocol v1 is the NGN application format inside ESP-NOW data;
it is separate from Espressif's ESP-NOW v1/v2 frame version. Successful MAC-layer
send completion is not proof of remote application delivery. NGN-002 uses no
application acknowledgment or retransmission loop, and claims no measured RF timing.
