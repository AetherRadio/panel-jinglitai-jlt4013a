# Jinglitai JLT4013A LCD Panel Linux Kernel Module Driver

This is a Linux Kernel Module that provides a driver for the Jinglitai JLT4013A
LCD Panel, which uses a Sitronix ST7701S controller (thus the filename and the
contents of the `struct of_device_id st7701s_of_match[]` array).

## Usage

You can build the module directly, or you can use the script
`generate_kernel_patch.py` to generate a patch file that can be used to patch a
Linux Kernel source tree. This will give you a KConfig option called
`DRM_PANEL_SITRONIX_ST7701S` next to the other related ones.

## Important information

I build the driver to be able to use custom kernels in the
[Xiegu X6100](https://www.radioddity.com/products/xiegu-x6100).

Xiegu did not comply with the GPL 2.0 mandate of the linux kernel, and did not
provide the kernel as they have modified it, including this driver, not even
upon request.

I built this driver from reverse engineering of the stock kernel image, using
Ghidra, plus looking at similar driver code.
Funnily enough, the original code used the `MODULE_LICENSE("GPL v2");`
macro.

## Credits

The original author from Xiegu was recorded in the `MODULE_AUTHOR` macro as
`MODULE_AUTHOR("Jet Yee <xieyi@cqxiegu.com>");`.
