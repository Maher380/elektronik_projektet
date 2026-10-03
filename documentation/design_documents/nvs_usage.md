# Using NVS for car settings

NVS is a small key-value store in flash that survives restarts and reflashing.
We use it for settings that belong to one car, such as which car it is, its
last drive style, and known Wi-Fi networks and MQTT servers.

## How it works

- There is **one NVS driver**, owned by the driver factory: `factory.nvs()`.
- Settings are grouped in **namespaces**, one per feature. Keys and namespace
  names are at most 15 characters.
- `nvs.open(Namespace::X)` returns a **handle** that reads and writes only that
  namespace. Every set and erase is saved to flash immediately.

## Rules

1. **One owner class per namespace.** Only that class reads or writes it; others
   use the owner's functions, never raw keys.
2. **Register every namespace** in `driver/nvs/namespaces.h`.
3. **Keep key names private** to the owner's `.cpp` file.
4. **Open once and keep the handle.** A namespace can only be open once at a time,
   so if two classes claim the same namespace, the second `open()` fails.
5. **A missing key is normal.** Reads return `false`; use a default value.
6. **Store a `ver` key** (u8) in each namespace, and reset or migrate the
   namespace when the stored layout changes.

## Namespaces

| Namespace | Owner | Status |
|---|---|---|
| `test` | Host tests | In use |
| `car` | Car settings (which car, last drive style) | Planned |
| `wifi` | Known Wi-Fi networks | Planned |
| `mqtt` | Known MQTT servers per Wi-Fi network | Planned |
| `phy`, `nvs.net80211` | ESP-IDF itself | Reserved, never use |

## Good to know

- If NVS is full or from an incompatible ESP-IDF version, `init()` erases it and
  all stored settings are lost. That is one reason for rule 5.
- NVS is not encrypted. Anyone with the car and a USB cable can read stored
  passwords, just as they could read the passwords compiled into the firmware
  before. Only store credentials where that is acceptable.
