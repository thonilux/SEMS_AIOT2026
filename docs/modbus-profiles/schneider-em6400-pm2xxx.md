# Schneider EM6400 / PM2xxx Bootstrap Profile

Source material:

- `docs/labiot2026.yaml`
- `docs/Public_EM6400_PM2xxx PMC Register List_v1050_6.xls`

This profile is the first concrete Modbus bootstrap for the PM1611 RS485 Reader project.
It is based on a previous ESPHome deployment that already talked to Schneider PM2xxx / EM6400-family meters.

## Notes

- The previous YAML used zero-based register addresses.
- The Excel register list uses the human register number.
- In practice, firmware code should treat the YAML values as `register_number - 1`.
- Transport is Modbus RTU over RS485.
- Initial function code should be `FC03` unless a specific register proves otherwise.

## Meter Family

Confirmed families in the register list:

- EM6400 NG
- PM2120
- PM2130
- PM2220
- PM2230

The old project used the same basic register family for live metering, so this is a good starting point for the new firmware.

## Recommended First Read Set

Start with these registers first because they validate the full decode path with minimal complexity:

| Field | Human Register | Zero-Based Address | Type | Unit |
| --- | ---: | ---: | --- | --- |
| Voltage A-N | 3028 | 3027 | FLOAT32 | V |
| Current A | 3000 | 2999 | FLOAT32 | A |
| Active Power A | 3054 | 3053 | FLOAT32 | kW |
| Frequency | 3110 | 3109 | FLOAT32 | Hz |
| Power Factor A | 3078 | 3077 | 4Q_FP_PF | - |
| Active Energy Delivered (Into Load) | 2676 | 2675 | FLOAT32 | kWh |

If you want the more “PM1611-like” phase split behavior, the previous YAML already showed the same pattern for:

| Field | Human Register | Zero-Based Address |
| --- | ---: | ---: |
| Current B | 3002 | 3001 |
| Current C | 3004 | 3003 |
| Voltage B-N | 3030 | 3029 |
| Voltage C-N | 3032 | 3031 |
| Active Power B | 3056 | 3055 |
| Active Power C | 3058 | 3057 |
| Reactive Power A | 3062 | 3061 |
| Reactive Power B | 3064 | 3063 |
| Reactive Power C | 3066 | 3065 |
| Power Factor B | 3080 | 3079 |
| Power Factor C | 3082 | 3081 |

## Bootstrap Reading Order

1. Read one float32 register, such as Voltage A-N.
2. Read one current register, such as Current A.
3. Read one power register, such as Active Power A.
4. Read one PF register and verify `4Q_FP_PF` decoding.
5. Read frequency.
6. Read energy.

That gives us:

- RS485 wiring proof
- direction control proof
- slave ID proof
- register addressing proof
- float decoding proof
- PF decoding proof
- stale/offline detection proof

## Suggested Firmware Mapping

The first implementation should map raw Schneider registers into normalized fields:

- `voltage`
- `current`
- `power`
- `frequency`
- `pf`
- `energy`

Once these are stable, the same pattern can be extended to MQTT payloads, LCD pages, and history buckets.

