# litratech_lidar

Viam camera module for LitraTech 2D lidars that speak [LDCP](https://github.com/LitraTech/ldcp_sdk) (LTME-02A, LT-R1, LT-R2, LT-I1, LT-I2, and other devices on the same protocol).

The lidar is a `camera` component. `GetPointCloud` returns one revolution as a binary PCD in Viam's frame: +y is forward, +x is right, and z is 0. Beam angles follow LitraTech's [ltme_node](https://github.com/LitraTech/ltme_node) ROS driver, including the 2 mm range counts. `GetImages` returns a top-down PNG of that scan, with forward at the top of the image.

## Configure

```json
{
  "modules": [
    {
      "type": "registry",
      "name": "litratech-lidar",
      "module_id": "viam-labs:litratech-lidar",
      "version": "latest"
    }
  ],
  "components": [
    {
      "name": "litratech",
      "api": "rdk:component:camera",
      "model": "viam-labs:lidar:litratech",
      "attributes": {
        "host": "192.168.1.230"
      }
    }
  ]
}
```

For a local build, set the module `type` to `local` and `executable_path` to `run.sh`.

| Attribute | Type | Default | Description |
| --- | --- | --- | --- |
| `host` | string | required | Device IPv4 address. A `host:port` value is also accepted. |
| `port` | int | `2105` | LDCP TCP port. Overrides a port written into `host`. |
| `scan_frequency` | int | device setting | Rotational frequency in Hz. Allowed values are 10, 15, 20, 25, and 30. Applied for the session and not written to flash. |
| `shadow_filter_strength` | int | device setting | Shadow filter strength from 0 to 100. |
| `range_min_m` | number | `0.05` | Near range, in meters. Closer returns are dropped. |
| `range_max_m` | number | `30` | Far range, in meters. Farther returns are dropped. |
| `invert_frame` | bool | `false` | Mirror the scan across the forward axis. |

`GetProperties` reports `supports_pcd: true` and the device scan frequency once a connection succeeds. `GetStatus` and `DoCommand` report connection state, model, serial, firmware, hardware, and the last error. The module keeps trying to connect until the resource is removed.

A raw range of 0 is a no-return and is left out of the cloud.

## Build

The LDCP SDK is the `third_party/ldcp_sdk` submodule.

```sh
git submodule update --init --recursive
```

Point-conversion tests need only a C++17 compiler:

```sh
make test
```

The module links the [Viam C++ SDK](https://github.com/viamrobotics/viam-cpp-sdk) v0.41.1, fetched by CMake. Install dependencies with `./setup.sh` (Homebrew on macOS, apt plus a gRPC source build on older Linux), then:

```sh
make module.tar.gz
```

`module.tar.gz` contains the binary and the shared libraries it needs. `run.sh` puts that directory on the library path.

Viam cloud build publishes `linux/amd64`, `linux/arm64`, and `darwin/arm64`. Intel Macs (`darwin/amd64`) are not a cloud-build target. A published GitHub release runs `.github/workflows/publish.yml`, which needs organization API key secrets named `viam_key_id` and `viam_key_value`.

## Model

`viam-labs:lidar:litratech` implements `rdk:component:camera`.
