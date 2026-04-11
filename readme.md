# Make
- make


# Flash and Fuses

## Executable
chmod +x fuse_gen.sh  
chmod +x flash_gen.sh

## Flash
- Arduino: ./flash_gen.sh arduino /dev/ttyACM0
- Programmer: ./flash_gen.sh usbasp usb

## Fuse
- Arduino: ./fuse_gen.sh arduino /dev/ttyACM0
- Programmer: ./fuse_gen.sh usbasp usb