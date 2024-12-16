# You'd Better Run!

Lets you know if you can walk leisurely to catch the bus, or if you'd better run.

![Image of the completed project hanging on the wall](https://github.com/youdbetterrun/.github/raw/main/profile/image.jpg)

This was developed for the transit system in Augsburg, Germany,
but it should be possible to adapt it for other cities.

This repository only holds the firmware for the project.
There are separate repositories for the PCB and 3D models.
- [PCB](https://github.com/youdbetterrun/youdbetterrun-pcb): The design files for the printed circuit board.
- [3D](https://github.com/youdbetterrun/youdbetterrun-3d): 3D models that need to be 3D printed and assembly instructions.

## Setup

Create a file `include/secrets.h` with your Wi-Fi SSID and password:
```h
static const char WIFI_SSID[] = "LordOfThePings";
static const char WIFI_PASSWORD[] = "password123";
```

Also include in `include/secrets.h` the end of the URL where the data is queried.
To find this:
1. Go to [fahrtauskunft.avv-augsburg.de](https://fahrtauskunft.avv-augsburg.de/sl3+/departureMonitor?lng=en)
1. Open your console developer tools to the Networking tab. Filter for XHR requests.
1. In the search box, enter the stop you are interested in.
1. Look in the Networking tab for a request that starts with `XML_DM_REQUEST`.
1. Copy this URL and paste it into `include/secrets.h` with the following format. Do not include `https://fahrtauskunft.avv-augsburg.de`.
    ```h
    static const String uri = "/efa/XML_DM_REQUEST?...";
    ```

Install [PlatformIO](https://platformio.org/). Run `pio run -t upload` to flash the board.
