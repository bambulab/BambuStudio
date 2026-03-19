# macOS Release Guide: Build, Sign, Notarize, Publish

Step-by-step guide for creating a signed and notarized macOS DMG release of BambuStudio-ZAA.

---

## Prerequisites

### One-time setup

1. **Apple Developer Account** ($99/year) at https://developer.apple.com

2. **Developer ID Application certificate**:
   - Go to https://developer.apple.com/account/resources/certificates/list
   - Click "+" → select **"Developer ID Application"**
   - Choose **G2 Sub-CA** (not "Previous Sub-CA")
   - Create a CSR in **Keychain Access** → Certificate Assistant → Request a Certificate From a Certificate Authority
   - Upload the `.certSigningRequest` file, download the `.cer`, double-click to install
   - Verify: `security find-identity -v -p codesigning` should show `Developer ID Application: Your Name (TEAMID)`

3. **App-specific password** for notarization:
   - Go to https://account.apple.com → Sign-In and Security → App-Specific Passwords
   - Generate one, name it "notarytool"

4. **dylibbundler** (for bundling external dylibs):
   ```bash
   brew install dylibbundler
   ```

### Our specific values

| Item | Value |
|------|-------|
| Certificate | `Developer ID Application: Matthias Nott (7KU642K5ZL)` |
| Apple ID | `mn@mnsoft.org` |
| Team ID | `7KU642K5ZL` |

---

## Step 1: Build

```bash
cd /path/to/BambuStudio
./build.sh
```

The app bundle will be at: `build/arm64/src/Release/BambuStudio.app`

---

## Step 2: Stage the app bundle

The build output has two issues for distribution:
- `Contents/Resources` is a **symlink** to the source tree
- The binary links against **external dylibs** (Homebrew zstd, Imath, X11 libGL)

```bash
# Create staging area
mkdir -p /tmp/bambu-dmg/staging

# Copy the app
cp -R build/arm64/src/Release/BambuStudio.app /tmp/bambu-dmg/staging/BambuStudio-ZAA.app

# Replace Resources symlink with actual files
rm /tmp/bambu-dmg/staging/BambuStudio-ZAA.app/Contents/Resources
cp -R resources /tmp/bambu-dmg/staging/BambuStudio-ZAA.app/Contents/Resources

# Create Applications symlink for drag-and-drop DMG
ln -s /Applications /tmp/bambu-dmg/staging/Applications
```

---

## Step 3: Bundle external dylibs

```bash
dylibbundler -od -b \
  -x /tmp/bambu-dmg/staging/BambuStudio-ZAA.app/Contents/MacOS/BambuStudio \
  -d /tmp/bambu-dmg/staging/BambuStudio-ZAA.app/Contents/Frameworks/ \
  -p @executable_path/../Frameworks/
```

Fix duplicate rpaths:

```bash
while install_name_tool -delete_rpath "@executable_path/../Frameworks/" \
  /tmp/bambu-dmg/staging/BambuStudio-ZAA.app/Contents/MacOS/BambuStudio 2>/dev/null; do :; done
install_name_tool -add_rpath "@executable_path/../Frameworks/" \
  /tmp/bambu-dmg/staging/BambuStudio-ZAA.app/Contents/MacOS/BambuStudio
```

---

## Step 4: Create entitlements

Create `/tmp/bambu-dmg/entitlements.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
    <key>com.apple.security.cs.allow-dyld-environment-variables</key>
    <true/>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
    <key>com.apple.security.device.camera</key>
    <true/>
    <key>com.apple.security.network.client</key>
    <true/>
</dict>
</plist>
```

---

## Step 5: Code sign

```bash
IDENTITY="Developer ID Application: Matthias Nott (7KU642K5ZL)"
APP="/tmp/bambu-dmg/staging/BambuStudio-ZAA.app"
ENTITLEMENTS="/tmp/bambu-dmg/entitlements.plist"

# Sign each bundled framework
for lib in "$APP/Contents/Frameworks/"*.dylib; do
  codesign --force --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$lib"
done

# Sign the app bundle
codesign --deep --force --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$APP"

# Verify
codesign --verify --deep --strict "$APP"
```

---

## Step 6: Create DMG

```bash
rm -f /tmp/bambu-dmg/BambuStudio-ZAA-macOS-arm64.dmg

hdiutil create \
  -volname "BambuStudio-ZAA" \
  -srcfolder /tmp/bambu-dmg/staging \
  -ov -format UDZO \
  /tmp/bambu-dmg/BambuStudio-ZAA-macOS-arm64.dmg
```

---

## Step 7: Notarize

```bash
xcrun notarytool submit /tmp/bambu-dmg/BambuStudio-ZAA-macOS-arm64.dmg \
  --apple-id "mn@mnsoft.org" \
  --team-id "7KU642K5ZL" \
  --password "xxxx-xxxx-xxxx-xxxx" \
  --wait
```

---

## Step 8: Staple

```bash
xcrun stapler staple /tmp/bambu-dmg/BambuStudio-ZAA-macOS-arm64.dmg
```

---

## Step 9: Upload to GitHub Release

```bash
gh release create v1.0.0-zaa \
  --repo mnott/BambuStudio \
  --target feature/zaa-contouring \
  --title "BambuStudio with ZAA Contouring (macOS arm64)" \
  --notes-file release-notes.md \
  /tmp/bambu-dmg/BambuStudio-ZAA-macOS-arm64.dmg
```

---

## Known Issues

### "Network Plugin Update Available" dialog
The app shares config with stock BambuStudio at `~/Library/Application Support/BambuStudio/`. Click **"Don't Ask Again"** — printer connectivity works fine.

### External dylib dependencies
The build links against Homebrew zstd/Imath and X11 libGL. The dylibbundler step handles this for distribution.

---

*Created: 2026-02-10*
