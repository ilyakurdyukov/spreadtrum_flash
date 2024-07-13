## Feature phone firmware helper

For SC6530/SC6531/SC6531E firmware dumps. Finds small sections init table and helps decode compressed sections.

### Usage

You can scan for compressed and/or relocatable segments in the firmware dump:

`./fphelper flash.bin scan`  

Use this command to run a firmware scan and extract segments:

`./fphelper flash.bin unpack`  

* `unpack` command can create: `{ps,kern,user,rsrc,unknown}[%u].bin init[%u]_%x.bin`

This command scans for data such as flash storage contents:

`./fphelper flash.bin scan_data`  

Use this command to run a data scan and extract FAT disk images:

`./fphelper flash.bin extract_data`  

* `extract_data` command can create: `fat_%u.img`

These commands decode compressed streams and require stream offset:

`./fphelper flash.bin {copy|lzdec2|lzdec3|lzmadec|lzmadec_sprd} src_offset dst_size output.bin`  
`./fphelper flash.bin drps_dec offset index output.bin`  

