# Muti-Frequency Fork

I have a bunch of ceiling fans that use 303.87mhz controllers so I forked the original tuya_rf and slopped in the support for it.
keep in mind while this does let you use whatever sub-ghz frequency you need you may need to replace the antenna to get better range.

I'm using this on a couple of cheap NAS-IR02W6-Pro ir/rf bridges from aliexpress [see here](https://www.aliexpress.com/item/1005006809864336.html), these use a 433mhz antenna which means that while it did work on my 303.87mhz devices, it only really worked when it was about 6in away. This device does have a standard UFl connector and an ok amount of space in the bottom of the case, so you can replace it with something better.

## Learning mode

I also replaced the basic receiver stuff in the original code with a (hopefully) better system that filters out irrelevant noise based on a configurable rssi threshold and tries it's best to clean up the signal and spit it out on the logs. 

In practice I found this worked probably about 75% of the time, sometimes it couldn't get a clean scan and it would only give you part of the full signal but you can just press the button again. When learning I also expose a entity you can use to "repeat" the last detected signal, which is useful for testing before writing it into your config and re-flashing to test out the new code. 

You can also tell if you got a good signal just by looking at how many segments it returned and if it's consistent, like all the codes on my 433Mhz remote were 66 segments long while my 303Mhz remote used 26 segments for it's commands.

Also note that if your RSSI floor is too high that it will log if it sees something just below the RSSI floor so you'll know if you need to change the floor.

## Configuration

To configure things like the RSSI floor, learning mode, default frequency, etc.. take a look at the options table below:

| Option | Default | Description |
|---|---|---|
| `id` | auto | Component ID |
| `sclk_pin` | `P14` | CMT2300A SPI clock pin |
| `mosi_pin` | `P16` | CMT2300A SPI MOSI/SDIO pin |
| `csb_pin` | `P6` | CMT2300A config chip-select |
| `fcsb_pin` | `P26` | CMT2300A FIFO chip-select |
| `tx_pin` | `P20` | RF transmit pin |
| `rx_pin` | `P22` | RF receive pin |
| `frequency` | `433.92MHz` | Carrier (127 MHz – 1020 MHz, validated) |
| `invert_signal` | `true` | Mark/space polarity (negative = mark when true) |
| `learn_mode` | `false` | Protocol-agnostic capture mode |
| `rssi_floor` | `-70` | dBm noise threshold (-128 to 20) |
| `receive_timeout` | `50ms` | Idle period that finalizes a learn capture |
| `raw_capture` | `false` | Bypass cleanup, dump raw burst |
| `dump` | `[]` | Dumper list (e.g. `raw`) |
| `tolerance` | `25%` | Match tolerance (`percentage` or `time` typed schema) |
| `buffer_size` | `1000b` | Receive buffer size in bytes |

## Actions

| Action | Parameters | Description |
|---|---|---|
| `tuya_rf.set_frequency` | `frequency` (required, templatable) | Change the carrier frequency at runtime (127 MHz – 1020 MHz) |
| `tuya_rf.replay_last_capture` | `repeat` (default `1`), `wait_time` (default `0s`) | Transmit the most recent learned code (both templatable) |
| `tuya_rf.turn_on_receiver` | — | Enable learn-mode capture (starts the receiver) |
| `tuya_rf.turn_off_receiver` | — | Disable learn-mode capture (stops the receiver) |

All actions accept an optional `receiver_id` to target a specific `tuya_rf` instance (auto-selected when only one is defined).


## Examples

```yaml
tuya_rf:
  id: rf
  learn_mode: true # Enable receiving and frequency processing
  rssi_floor: -70 # dBm; bursts weaker than this are treated as noise
  receive_timeout: 200ms # time block to treat as one code for learning, 200ms is fine, just don't spam the buttons.
  frequency: 303.87MHz # Default frequency, frequency does change as you use the tuya_rf.set_frequency command, but this is what it will initially set it to.
  dump: raw
```

```yaml
button:
  - platform: template
    name: "Light Down"
    on_press:
      - tuya_rf.set_frequency: # Setting frequency, not needed if the default frequency is what you need and your not mixing different frequencies
          frequency: 433.92MHz
      - switch.turn_on: status_led
      - remote_transmitter.transmit_raw:
          transmitter_id: rf
          # Code copied from the learning mode.
          code: [3438,-854,375,-218,1001,-228,979,-250,968,-229,1000,-218,979,-239,989,-843,364,-229,1000,-219,1000,-229,979,-239,968,-260,979,-823,385,-844,375,-229,990,-250,958,-250,979,-239,979,-844,374,-239,958,-865,374,-219,999,-833,385,-218,979,-250,979,-229,1000,-823,385,-229,980,-853,364,-218,1010,-823,385,-218]
          repeat:
            # How many times to repeat the signal, might need to increase if it's not reliable, though keep in mind this can't fix it if you have a weak antenna
            times: 7
            wait_time: 0s
      - switch.turn_off: status_led
```



# tuya_rf
Custom component to integrate a tuya rf433 hub into esphome.

The tuya device I'm using is a Moes ufo-r2-rf, it's both an IR and RF bridge.

It uses a [CBU module](https://docs.libretiny.eu/boards/cbu/) (BK7231N) and an [SH4 RF module](https://developer.tuya.com/en/docs/iot/sh4-module-datasheet?id=Ka04qyuydvubw) (which appears to be using a CMT2300A).

For more hardware details see [this forum post](https://www.elektroda.com/rtvforum/topic3975921.html).

There are several devices using the same CBU/SH4 combo.

My code is based on the remote_receiver/remote_transmitter components, adding the initialization sequence to put the CMT2300A in direct RX or TX mode.

The transmitter works, I got the codes using the original firmware and the tinytuya [RFRemoteControlDevice.py](https://github.com/jasonacox/tinytuya/blob/master/tinytuya/Contrib/RFRemoteControlDevice.py), the gencodes.py script uses the data to generate the `remote_transmitter.transmit_raw` codes.

The codes have been captured from a ceiling fan remote, the only rf remote I have.
I also used the codes dumped from the receiver to confirm that they also
work.

The receiver (both ir using the standard remote_receiver and rf using this component) should work once this [pull request](https://github.com/libretiny-eu/libretiny/pull/290) lands in libretiny.

For the IR part (to be used with the standard remote_receiver/remote_transmitter) the receiver is on P8 and the transmitter on P7.

Keep in mind that the CMT2300A cannot transmit and receive at the same time, so it is normally set in RX mode (unless the receiver has been disabled). To transmit a code the CMT2300A is switched
to TX mode, the code is sent, then it is switched back to RX mode (or to standby if the receiver has been disabled).

## rf filtering

The rf signal is quite noisy, so I do some filtering to receive the codes:

1. the starting pulse must be longer than 6ms but shorter than 10ms.
2. any time I see a starting pulse I discard the data and start again (usually the same code is sent more than once).
3. the pauses cannot last more than 6ms, if I see a pause longer than that I discard the data and start again.
4. the end pulse is around 90ms, I look for a pulse of at least 50ms to detect the end of the data.
5. if the end pulse never arrives, when the receiving buffer is about to overflow I discard the data and start again.

I don't know if these values are good only for my remote, but there are configuration variables to tune them.

## configuration variables

You can use the same configuration variables of the [remote_transmitter](https://esphome.io/components/remote_transmitter) and of the [remote_receiver](https://esphome.io/components/remote_receiver) (with the exception of the pin, here you can set the tx_pin and the rx_pin, but if you don't set them the defaults are used, idle also isn't used).

| variable | default | description |
|--|--|--|
|receiver_disabled|false|set it to true to disable the receiver|
|tx_pin|P20|pin used to transmit data, note that by default it is inverted, so a pulse is 1 and a space is 0 (the signal on my device is reversed: 0 for a pulse, 1 for a space)|
|rx_pin|P22|pin used to receive the data, note that by default it is inverted just like the tx_pin|
|start_pulse_min|6ms|the minimun duration of the starting pulse|
|start_pulse_max|10ms|the maximum duration of the starting pulse, must be greater than start_pulse_min|
|end_pulse|50ms|the minimum duration of the ending pulse, must be greater than start_pulse_max|
|sclk_pin|P14|clock of the spi communication with the CMT2300A. Only the number of the pin is used, the remaining parameters of the pin schema are ignored|
|mosi_pin|P16|the bidirectional data pin of the spi communication. Only the number of the pin is used, the remaining parameters of the pin schema are ignored|
|csb_pin|P6|spi chip select pin to read/write registers. Only the number of the pin is used, the remaining parameters of the pin schema are ignored|
|fcsb_pin|P26|spi chip select pin to read/write the fifo. It is not used|

## actions

Since the receiver is mostly useful to learn new buttons, it's better to
leave it off and turn it on only when you want to start learning.
There are two actions, `tuya_rf.turn_on_receiver` (to turn the receiver on)
and `tuya_rf.turn_off_receiver` (to turn it off).
Once the code is fixed to work with multiple instances of tuya_rf (currently
it only allows one instance) you shuould specify the `receiver_id` in the
action.
