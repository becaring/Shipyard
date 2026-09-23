# LibUltraShip + Fast3D Xbox 360 platform base

Shared, versioned Xbox 360 platform code and mechanical Fast3D transforms.
The modern 9.x Ship of Harkinian port is the target runtime. Smash360 is a
consumer and a source of proven platform techniques; application behavior,
resource formats, and version-specific Fast3D APIs remain owned by each port.

The base contains only code that both consumers actually use. Each consumer
pins the same base revision and runs contract tests against its own upstream
source before producing an XEX. See `BASELINE.md` for the capability ledger.
