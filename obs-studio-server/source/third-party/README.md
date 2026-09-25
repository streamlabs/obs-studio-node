# Vendored third-party headers

`stb_image_write.h` — single-header PNG/BMP/TGA/JPG writer from
https://github.com/nothings/stb (public domain / MIT dual licence, see the
bottom of the file). Used by `nodeobs_common.cpp` to encode canvas screenshots.
Copied unmodified; `.clang-format` in this directory disables formatting so
`ci/check-format.sh` leaves it alone.
