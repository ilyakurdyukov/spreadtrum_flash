## Feature phone firmware helper

For SC6530/SC6531/SC6531E firmware dumps. Finds small sections init table and helps decode compressed sections.

### Usage

You can scan for compressed and/or relocatable sections in the firmware dump:

`./fphelper flash.bin scan`  

Use this command to run a scan and extract large sections:

`./fphelper flash.bin unpack`  

This command scans for data such as flash storage contents:

`./fphelper flash.bin scan_data`  

These commands decode compressed streams and require stream offset:

`./fphelper flash.bin {copy|lzdec2|lzdec3|lzmadec|lzmadec_sprd} src_offset dst_size output.bin`  
`./fphelper flash.bin drps_dec offset index output.bin`  

