# Notices and credits

## djnice and signde: the dvfel kernel module

The kernel module in `src/fel/onn/` is a port of signde's Fire TV Cube 3 (SC2 / PS7702)
Dolby Vision FEL module from <https://github.com/signde/firetv-passthrough> (`src/fel/ps7702/`),
licensed GPL-2.0-or-later. That module is djnice's `dvfel` for the Dune HD S905X4 (its
`MODULE_AUTHOR` is djnice), from djnice's Android VLC FEL work
(<https://github.com/djnice/vlc-fel-android>), adapted by signde for the Fire TV. The VFM
provider, the `/dev/dvfel` interface and the pairing design come from that work; this project
adapts them to the onn 4K Pro (S7D, kernel 5.15) and adds the LLDV output, the Magisk module and
the settings app.

The FEL players for the onn (separate repository, ONN4k-FEL-Players) use djnice's libdvfelplay
(copyright Team CoreELEC, GPL-2.0-or-later) through signde's VLC FEL and Kodi FEL.

The whole repository is distributed under GPL-2.0-or-later (see `LICENSE`).

## avdvplus

The LLDV output follows the design of avdvplus's CoreELEC "Player Led (DV as HDR10 422 12 Bit)"
mode (<https://github.com/avdvplus>). It was used as a reference for behaviour only; no avdvplus
code is included.

## libdovi

`player/build_player.sh` builds libdovi from quietvoid's dovi_tool (<https://github.com/quietvoid/dovi_tool>,
MIT) into the test player. It is not part of the Magisk module.

## Not included

No firmware, vendor kernel modules, Dolby libraries or video clips are in this repository or
in the module zip. The module hooks the box's own drivers at runtime and refuses to load on any
firmware build it was not checked against.

Dolby, Dolby Vision, onn, Amlogic, Kodi, VLC and Magisk names are used
only to describe compatibility. This project is not affiliated with or endorsed by any of them.
