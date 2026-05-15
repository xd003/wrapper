# wrapper
A tool to decrypt Apple Music's music. An active subscription is still needed.

Only support Linux x86_64 and arm64.

# Install
Get the pre-built version from this project's Actions.

Or you can refer to the Actions configuration file for compilation.

# Docker
Available for x86_64 and arm64. Need to download prebuilt version from releases or actions.

Build image: `docker build --tag wrapper .`

## Single account

Login: `docker run -v ./rootfs/data:/app/rootfs/data -p 10020:10020 -e args="-L username:password -F -H 0.0.0.0" wrapper`

Run: `docker run -v ./rootfs/data:/app/rootfs/data -p 10020:10020 -e args="-H 0.0.0.0" wrapper`

## Multi-account / multi-region

Pass comma-separated `user:password` pairs via the `ACCOUNTS` env var (or the `-L` flag if invoking `./wrapper` directly). The wrapper supervisor spawns one decoder process per account, each on its own port triple and state directory.

| Account index | Decrypt port | M3U8 port | Account port | State dir (inside chroot) |
|---|---|---|---|---|
| 0 | 10020 | 20020 | 30020 | `/data/data/com.apple.android.music/files/account_0` |
| 1 | 10021 | 20021 | 30021 | `/data/data/com.apple.android.music/files/account_1` |
| 2 | 10022 | 20022 | 30022 | `/data/data/com.apple.android.music/files/account_2` |
| … | 10020+N | 20020+N | 30020+N | `…/account_N` |

Via env var (with the bundled entrypoint):

```
docker run --privileged --rm -it \
  -v ./rootfs/data:/app/rootfs/data \
  -p 10020-10022:10020-10022 \
  -p 20020-20022:20020-20022 \
  -p 30020-30022:30020-30022 \
  -e ACCOUNTS="us@example.com:pw1,jp@example.com:pw2,uk@example.com:pw3" \
  wrapper
```

Via direct binary invocation (bypassing entrypoint.sh):

```
docker run --privileged --rm -it \
  -v ./rootfs/data:/app/rootfs/data \
  -p 10020-10022:10020-10022 \
  -p 20020-20022:20020-20022 \
  -p 30020-30022:30020-30022 \
  --entrypoint ./wrapper wrapper \
  -L "us@example.com:pw1,jp@example.com:pw2,uk@example.com:pw3" \
  -F -H 0.0.0.0
```

Single-account `-L "user:pass"` (no comma) still works exactly as before — same default ports, same state dir.

If any decoder process exits, the supervisor terminates the remaining ones so Docker can restart the container.

# Usage
```
Usage: wrapper [OPTION]...

  -h, --help              Print help and exit
  -V, --version           Print version and exit
  -H, --host=STRING         (default=`127.0.0.1')
  -D, --decrypt-port=INT    (default=`10020')
  -M, --m3u8-port=INT       (default=`20020')
  -A, --account-port=INT    (default=`30020')
  -P, --proxy=STRING        (default=`')
  -L, --login=STRING        (username:password, or comma-separated list
                             "u1:p1,u2:p2,..." for multi-account)
  -F, --code-from-file      (default=off)
  -B, --base-dir=STRING     (default=`/data/data/com.apple.android.music/files')
  -I, --device-info=STRING  (default=`Music/4.9/Android/10/Samsung S9/...')
```

When `-L` contains multiple comma-separated accounts, `-D`/`-M`/`-A` and `-B` are used as the **base** values. Account N is served on `port+N` and stored in `<base-dir>/account_N`.

# Special thanks
- Anonymous, for providing the original version of this project and the legacy Frida decryption method.
- chocomint, for providing support for arm64 arch.
