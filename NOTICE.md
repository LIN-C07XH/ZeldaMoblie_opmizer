This source overlay incorporates modified files from
[OJZen/totk-mobile](https://github.com/OJZen/totk-mobile), based on commit
`510d95b8da159c0a7da467ad51b10a5a7c7ac133`, under the Apache License 2.0.
See [LICENSE](LICENSE) and the upstream [third-party notices](THIRD_PARTY_NOTICES.md).

Modified source paths are listed in `payload-files.txt`. Changes include native
camera and input integration, UI prompt bridging, Metal 3 rendering corrections,
session diagnostics, Mac/iOS build compatibility, folder-based cloud saves,
and phone/controller haptics. The upstream `core/` tree
is neither modified nor redistributed in this overlay; installation validates
the required files in the user's upstream source checkout.

Game modules, RomFS, fonts, shaders extracted from game data, keys, saves and
compiled applications are not included. Derived resources are generated locally
from the user's own game data.
