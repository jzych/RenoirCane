# Android Launch Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a calibration APK that includes its startup dependency and targets Android API 36.

**Architecture:** The cross-platform startup module keeps importing its serial backends. Buildozer packages `pyserial` so the Android Python runtime can import the shared module. The Buildozer target becomes API 36 while the Android 7 minimum remains API 24.

**Tech Stack:** Buildozer, python-for-android, Kivy, PyJNIus, pyserial, Android SDK 36.

## Global Constraints

- Keep `android.minapi = 24`.
- Set `android.api = 36`.
- Keep both `arm64-v8a` and `armeabi-v7a` APK ABIs.
- Do not target Android 17 API 37 in this change.

---

### Task 1: Package the startup serial dependency and target API 36

**Files:**
- Modify: `calibration_app/buildozer.spec:8-16`
- Test: Build output inspected with Android `aapt`.

**Interfaces:**
- Consumes: Buildozer's comma-separated `requirements` configuration.
- Produces: an APK whose Python bundle contains `serial` and whose manifest has `targetSdkVersion=36`.

- [ ] **Step 1: Record the failing package inspection**

Run:

```bash
aapt dump badging calibration_app/bin/canecalibration-0.1.0-arm64-v8a_armeabi-v7a-debug.apk
```

Expected: the existing APK reports `targetSdkVersion:'33'` and Buildozer's requirements do not contain `pyserial`.

- [ ] **Step 2: Update the minimal Buildozer configuration**

```ini
requirements = python3,kivy,pyjnius,pyserial
android.api = 36
android.minapi = 24
```

- [ ] **Step 3: Build the Android debug APK**

Run:

```bash
cd calibration_app
source .venv/bin/activate
buildozer android debug
```

Expected: Buildozer downloads API 36 if missing and exits with code 0.

- [ ] **Step 4: Verify the packaged artifact**

Run:

```bash
aapt dump badging bin/canecalibration-0.1.0-arm64-v8a_armeabi-v7a-debug.apk
```

Expected: `targetSdkVersion:'36'`, `sdkVersion:'24'`, and native code for `arm64-v8a` and `armeabi-v7a`.

- [ ] **Step 5: Commit**

```bash
git add calibration_app/buildozer.spec docs/superpowers/specs/2026-07-28-android-launch-compatibility-design.md docs/superpowers/plans/2026-07-28-android-launch-compatibility.md
git commit -m "fix: package Android serial dependency"
```
