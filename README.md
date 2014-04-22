## Application multi protocol feature in nrf51822 s110 version 7.0.0-1.alpha

    The latest alpha release of the Nordic nrf51822 softdevice
    (s110\_nrf51822\_7.0.0-2.alpha\_softdevice.hex) introduce a feature to
    Do concurrent radio activity in the app it self. This sample is a
    simple application taking advantage of this.

    The sample application implementes a very simple retail beacon
    module which will advertise a UUID, major and minor number when
    activated. It will run concurrent with any other activity on the
    device so it will run when flashing, advertising other data or even
    when in a connection with a master.

## To compile

    The sample relies on the header files distributed as part of the
    7.0.0 release, which cannot be distributed freely at this point.

    To compile, just
    - Download the alpha release (with the .hex and the include folder).
    - Flash the .hex file on your device
    - Put the header files in include\s110
    - Build the project using Keil
    - Flash the application and start up you beacon scanner
