# USB machine and setup modes

Firmware `0.3.1-dev` defaults to **storage mode** on every power-on. It presents
one USB mass-storage interface, with device class/subclass/protocol zero, and
does not start the CDC setup worker. Cloud and local Wi-Fi delivery still work.
Use an MBR/FAT32 card. The Brother NQ1700E diagnostic baseline required the
storage-only profile and a freshly formatted FAT32 card to recognize a design.
The exact underlying host failure was not isolated to a single descriptor field.

## Enter setup on a computer

1. Insert the FAT32 card and plug Link into a computer normally, without holding
   BOOT. Allow a few seconds for startup.
2. Press and release **BOOT twice quickly**, starting the second press within
   one second of the first release. Two normal short taps are enough.
3. Link briefly blinks blue and reboots into **setup mode**. If a local/cloud
   transfer or firmware update is active, the request waits for its operation
   gate to become available instead of interrupting the write.
4. In the Ember web app, click **Connect Ember Link** and select **Ember Link
   Setup**, or use Ember Bridge's USB setup page. Setup mode exposes both mass
   storage and CDC serial. The existing JSON protocol and USB IDs are unchanged.
5. Safely eject the drive before moving Link. Unplugging and powering it from the
   machine selects storage mode again. Do not enable setup while in the machine.

Setup is a powered session recorded only in RTC memory. Software reboots can
preserve the session (including in-place updates with a compatible RTC layout).
Power-on, brownout, watchdog, and other non-software reset reasons select storage
mode and clear the marker. No mode flag is saved in NVS. A pair of complementary
marker values guards against uninitialized/corrupted retained memory.

A single short BOOT press still opens local pairing after the double-press
window expires. A five-second hold still resets Wi-Fi/account settings according
to the existing reset policy. Holding BOOT **while connecting power** remains ROM
flash recovery; it is different from the two presses after normal startup.
Missing/unreadable cards still block storage initialization and the setup port.

USB `info`, LAN `/api/health`, and authenticated `/api/info` add `usbMode` with
`storage` or `setup`. There is no LAN/cloud command that enables USB setup.
A second double press when already in setup does not restart or change settings.

## Verification

Native sanitizer tests cover bounce, single/double presses, long holds, timer
rollover, corrupt markers, software restart retention and cold-reset clearing.
Hardware qualification must also check both USB descriptor profiles, actual
power-cycle behavior, busy-transfer deferral, unchanged enrollment, and design
preview after cloud/local delivery on the target machine. The earlier
EmberConnect diagnostic preview is not proof for this integrated Link image.
