# wayland-protocols (vendored subset)

`presentation-time.xml` is the stable presentation-time protocol from the
`wayland-protocols` package (Debian 1.47-1~bpo13+1~rpt1; upstream
https://gitlab.freedesktop.org/wayland/wayland-protocols), licensed as
described in `copyright` (from the same package).

It is used by `src/swapsync.cpp` for ground-truth frame-presentation
timestamps under Wayland compositors (cage, Weston, labwc).  The build
prefers the system copy when the `wayland-protocols` package is installed;
this vendored copy keeps flip feedback working on build hosts (including
the release CI containers) that only have `libwayland-dev`.
