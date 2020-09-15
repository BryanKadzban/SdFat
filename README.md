The Arduino SdFatAsync library provides read/write access to FAT16/FAT32
file systems on SD/SDHC flash cards, with async read capability.

SdFat requires Arduino 1.6x or greater.

Key changes:

FatFile::read returns a future, which can be polled if the SPI/SD/SDHC
transfer happens in the background.

Support for multiple SPI ports now uses a pointer to a SPIClass object.

See the STM32Test example.
```
explicit SdFat(SPIClass* spiPort);
\\ or
explicit SdFatEX(SPIClass* spiPort);
```

Open flags now follow POSIX conventions.  O_RDONLY is the same as O_READ and O_WRONLY is the
same as O_WRITE. One and only one of of the mode flags, O_RDONLY, O_RDWR, or
O_WRONLY is required.
 
Open flags for Particle Gen3 and ARM boards are now defined by fcntl.h.
See SdFatConfig.h for options.

Support for particle Gen3 devices.

New cluster allocation algorithm.

Please read changes.txt and the html documentation in the extras folder for more information.

Please report problems as issues.

A number of configuration options can be set by editing SdFatConfig.h
define macros.  See the html documentation for details

Please read the html documentation for this library.  Start with
html/index.html and read the Main Page.  Next go to the Classes tab and
read the documentation for the classes SdFat, SdFatEX, SdBaseFile,
SdFile, File, StdioStream, ifstream, ofstream, and others.
 
Please continue by reading the html documentation in SdFat/extras/html

Updated 14 September 2020
