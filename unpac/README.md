## PAC unpacker

Spreadtrum/Unisoc store firmware binaries inside archives with a `.pac` extension. This tool extracts files from these archives.

### Usage

To list files in an archive:

`./unpac list firmware.pac`

To extract files from an archive:

`./unpac [-d outdir] extract firmware.pac [names]`

* `names` can be IDs and wildcards (`*.xml`). All files are extracted if names are not specified.

To verify archive checksums:

`./unpac check firmware.pac`

