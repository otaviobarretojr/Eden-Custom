# NSZ integration

Eden Custom converts NSZ -> NSP through official external NSZ tools. The
emulation core itself does not read NSZ directly.

## Primary converter

- Project: nicoboss/nsz
- Version: 5.0.0
- Asset: nsz-cli-windows-x64.exe
- SHA-256: 341b395c18679bf4c01f0bf2fb0e22e315a64b34fad51064781f5155a978883d
- Packaging: PyInstaller one-file
- License: MIT

The primary converter is downloaded on demand and accepted only when its
pinned SHA-256 digest matches.

## Windows compatibility fallback

Some Windows configurations reject the Python 3.11 DLL extracted by the
NSZ 5.0.0 PyInstaller one-file executable. Eden detects the specific
"Failed to load Python DLL" / _MEI runtime failure and automatically retries
with the official portable NSZ 4.6.1 package.

- Version: 4.6.1
- Asset: nsz_v4.6.1_win64_portable.zip
- Size: 16,553,922 bytes
- SHA-256: 9ebf9e717db54918cbae3db9ec8befe3c12b90bd8d5f3059991deeb2b659889e
- Packaging: portable one-directory Python runtime
- Managed executable: tools/nsz/compat/4.6.1/nsz_v4.6.1_win64_portable/nsz.exe
- License: MIT

The fallback ZIP is also checksum-verified before extraction. NSZ 4.6.1 does
not support the newer --keys or --minimal-output CLI options, so Eden adapts
the argument set. When the fallback is used, Eden temporarily copies the
existing Eden prod.keys to keys.txt beside the portable converter, then
removes that temporary copy when the process exits. The original key file is
never modified.

The source NSZ is never deleted by Eden's conversion flow.

Eden Custom does not ship console keys, firmware, games, updates or DLC.
