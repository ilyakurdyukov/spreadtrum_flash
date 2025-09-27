## Feature phone firmware helper

For UMS9117 firmware dumps. Finds small sections init table and helps decode compressed sections.

### Usage

You can scan for compressed and/or relocatable segments in the firmware dump:

`./fphelper_t117 flash.bin scan`  

Use this command to run a firmware scan and extract segments:

`./fphelper_t117 flash.bin unpack`  

* `unpack` command can create: `init[%u]_%x.bin pinmap.bin keymap.bin`

These commands decode compressed streams and require stream offset:

`./fphelper_t117 flash.bin {copy|lzdec2|lzdec3} src_offset dst_size output.bin`  
