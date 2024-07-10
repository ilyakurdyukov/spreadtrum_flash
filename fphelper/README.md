## Feature phone firmware helper

For SC6530/SC6531/SC6531E firmware dumps. Finds small sections init table and helps decode compressed sections.

### Usage

`./fphelper flash.bin scan`  
`./fphelper flash.bin {lzdec2|lzdec3|copy} src_offset dst_size output.bin`  
