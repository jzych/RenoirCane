[app]
title = CaneCalibration
package.name = canecalibration
package.domain = org.renoircane
source.dir = .
source.include_exts = py,kv,txt
version = 0.1.0
requirements = python3,kivy,pyjnius,pyserial
orientation = all
fullscreen = 0
android.accept_sdk_license = True
android.api = 36
android.minapi = 24
android.ndk = 25b
android.extra_manifest_xml = android_manifest_features.xml
android.gradle_dependencies = com.github.mik3y:usb-serial-for-android:3.10.0
android.add_gradle_repositories = maven { url 'https://jitpack.io' }
android.private_storage = True

[buildozer]
log_level = 2
warn_on_root = 1
