# Synthetic NVS fixture

`synthetic-settings.bin` was generated with Espressif's
`esp-idf-nvs-partition-gen` 0.1.9 (ESP-IDF 6.0.2 environment). It contains only
invented receipt IDs, a transport setting, display preferences and repeated test
bytes spanning multiple pages. It is not a device dump and contains no credentials.

Regenerate with `python installer/tests/fixtures/generate.py` in that environment.
The test suite reads this fixture without needing Python or ESP-IDF installed.
