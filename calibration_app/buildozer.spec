[app]
title = CaneCalibration
package.name = canecalibration
package.domain = org.renoircane
source.dir = .
source.include_exts = py,kv,txt
version = 0.1.0
requirements = python3,kivy,pyjnius
orientation = all
fullscreen = 0
android.accept_sdk_license = True

[buildozer]
log_level = 2
warn_on_root = 1

[app:android]
android.api = 33
android.minapi = 24
android.ndk = 25b
android.permissions = android.permission.USB_PERMISSION
android.features = android.hardware.usb.host
android.private_storage = True
