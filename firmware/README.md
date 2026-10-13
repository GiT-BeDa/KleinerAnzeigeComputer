# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.5.0.bin](kac-0.5.0.bin) | 0.5.0 | 398080 bytes | `a1796b1dd80d08960d2835a6be20f83e` | `f7a5f99f81eb90f91b05582267caca94d274955dc9cad275ff06720f63377988` |

Check the download before uploading it:

```sh
md5sum kac-0.5.0.bin        # Linux
md5 kac-0.5.0.bin           # macOS
certutil -hashfile kac-0.5.0.bin MD5   # Windows
```
