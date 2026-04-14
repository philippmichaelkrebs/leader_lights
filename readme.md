# Leader Lights
Leader lights for Carrera Digital 124 & 132 powered by an ATtiny85. The system reads the track's data signal and determines the vehicle position by identifying its ID via the decoder's IR signal.

Use the 5V power supply by the decoder or aternatively power the circuit via an LDO. Adjust the voltage divider for your needs.

Note: Decoders marked 'CAR107A' do not provide sufficient power for driving the circuit. An external LDO is required. Also be aware that the IR signal is 3.6V on these decoders.

Example:
![](https://github.com/philippmichaelkrebs/leader_lights/blob/main/img/img2.png?raw=true)

Schematic:  
![](https://github.com/philippmichaelkrebs/leader_lights/blob/main/img/schematic.png?raw=true)


# Use it

## Make
- make


## Flash and Fuses

### Executable
chmod +x fuse_gen.sh  
chmod +x flash_gen.sh

### Flash
- Arduino: ./flash_gen.sh arduino /dev/ttyACM0
- Programmer: ./flash_gen.sh usbasp usb

### Fuse
- Arduino: ./fuse_gen.sh arduino /dev/ttyACM0
- Programmer: ./fuse_gen.sh usbasp usb