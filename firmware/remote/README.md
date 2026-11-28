# Coach remote

Runs reps from the start hill, so nobody has to kneel at ST-1: ARM/GO and CANCEL, with one light that mirrors ST-1's state. DFRobot Beetle ESP32-C6 (DFR1117), the same board as FN, on wire contract v5 ([remote commands](../shared/README.md#remote-commands-v5)). The design mock-up is a separate canvas. This firmware has been built but not yet run on hardware.

```sh
pio run -d firmware/remote -e c6_remote
pio run -d firmware/remote -e c6_remote -t upload
pio device monitor -d firmware/remote -b 115200
```

Like FN's C6 builds, it uses the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform (55.03.313, Arduino-ESP32 3.3.13). If the port does not appear, hold BOOT, press and release RST, release BOOT and retry.

## Wiring

| Part | Beetle pin | Notes |
| --- | --- | --- |
| ARM / GO button | GPIO 6 to GND | Internal pull-up. GPIO 0–7 are the only pins that can wake the C6 |
| CANCEL button | GPIO 7 to GND | Internal pull-up |
| NeoPixel data | GPIO 22 | |
| NeoPixel power (+) | GPIO 23 | Switched off before sleep: a NeoPixel draws about 1 mA even when dark. Fine for one pixel at the default brightness; use a P-channel MOSFET if you raise it a lot |
| NeoPixel GND | GND | |
| LiPo | Battery connector, through the slide switch | Charged over USB-C by the Beetle |

GPIO 0 is read for battery voltage (assumed divider ratio 2; check one reading against a meter and adjust `JNS_BATTERY_SCALE` if needed). GPIO 4 and 5 are strapping pins, so the buttons use 6 and 7. For an RGBW pixel, set `-DJNS_PIXEL_RGBW=1` in `platformio.ini`. Remove the Beetle's power LED for longer battery life.

## Using it

| Action | Does |
| --- | --- |
| Switch on holding both buttons for 2 s | Pairing (light pulses cyan for up to 35 s): see the [ST-1 pairing steps](../controller/README.md#coach-remotes) |
| Hold ARM / GO 2 s while ST-1 is idle | Take control |
| Press ARM / GO | ARM; in Standing, press again when armed to GO. Flying: the rider's beam starts it |
| Hold ARM / GO 1 s while running | Early reveal |
| Press CANCEL | Stops the attempt. Always accepted |
| Any press after sleep | Only wakes it. Never ARM or GO |

| Light | Meaning |
| --- | --- |
| Off | Asleep (after 10 minutes without a press, or 2 minutes without hearing ST-1) |
| White, pulsing | Looking for ST-1, or not paired |
| Cyan, pulsing | Pairing |
| Purple | Not in control: hold ARM / GO 2 s |
| Amber | ST-1 not ready |
| Green, dim | Ready: press to ARM |
| Green, bright | Armed |
| Blue | Running |
| White | Times revealed |
| Red | ST-1 fault |
| Red, flashing for 3 s at wake | Battery low (15% or less) |
| Bright flash after a press | Accepted |
| Double red blink after a press | Refused, too late, or the press did nothing |

The light follows ST-1's Status, never its own guess: if it hears nothing for 2 s it goes back to pulsing white. A press is resent every 40 ms until ST-1 answers, for up to 250 ms (CANCEL: 1 s). ST-1 refuses anything pressed longer ago than that, so a GO can never act late.

## Native tests

```sh
sh firmware/remote/tests/run.sh
```

They cover the light for every ST-1 state, what each press and hold asks for, retries and expiry, the waking press being ignored, and the battery estimate. ST-1's side of the rules is tested in `firmware/controller/tests`.
