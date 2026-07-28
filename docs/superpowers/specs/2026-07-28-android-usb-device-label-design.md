# Android USB Device Label Design

## Goal

Prevent the Android calibration application from crashing when Refresh discovers
an attached TinyPICO USB serial device.

## Cause

The Android bridge currently formats a device label by calling
`getSimpleName()` on the PyJNIus representation of a Java `Class`.  On the
target Android runtime that representation does not expose this method, raising
an `AttributeError` during Refresh.

## Design

Android USB discovery will label every probed device as:

`USB serial — <vendor-id>:<product-id>`

Both IDs are hexadecimal, zero-padded to four digits.  The label will be
derived solely from the Android `UsbDevice`; discovery will not inspect the
driver implementation class.

For the attached TinyPICO bridge this is expected to appear as:

`USB serial — 1a86:55d4`

## Error Handling

The change removes the unsupported class-name access from Refresh.  Other USB
discovery errors retain their existing behaviour and are out of scope.

## Tests and Verification

A unit test will exercise the bridge's driver-description path with a driver
object that provides only `getDevice()`, proving no class-name API is required.
The complete calibration-app test suite will run before an Android APK is
rebuilt and installed.  The installed APK will be verified on the connected
Samsung A03s by pressing Refresh with the TinyPICO attached and confirming the
device is listed without process termination.
