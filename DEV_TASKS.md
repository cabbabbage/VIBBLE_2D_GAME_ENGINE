# Dev Tasks

- Asset drag/drop parallax: update ENGINE/dev_mode/room_editor.cpp (hit testing + drag loop) to lock cursor to selected asset anchor; unify world↔screen conversions via active camera to remove drift; validate by dragging while panning/zooming across room boundaries.
<!--
assignee: Any
assigner: Cal
status: pending
-->

- Light drag parallax: update ENGINE/dev_mode/asset_info_ui.cpp (light drag handler) to account for camera parallax in screen↔world conversions; ensure light positions remain consistent during camera movement; validate by dragging lights while panning/zooming.
<!--
assignee: Any
assigner: Cal
status: pending
-->

- Light blend mode: add blend mode option to ENGINE/utils/light_source.hpp and ENGINE/asset/asset_info.hpp; implement norm blend rendering in scene renderer; allow switching between mod blend and norm blend for light objects.
<!--
assignee: Any
assigner: Cal
status: pending
-->

- Tiled asset spawning: design grid-based tile spawn system in ENGINE/spawn/ for large assets; implement tile subdivision logic; add spawn method for tiled assets with grid alignment.
<!--
assignee: Cal
assigner: Cal
status: pending
-->

