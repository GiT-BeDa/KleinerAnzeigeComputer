# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.7.3.bin](kac-0.7.3.bin) | 0.7.3 | 407712 bytes | `a5c74eb506b3700dcd2c13f48df80fc6` | `90a42ec3f03f17d5d969cd95ce3eccb13c26f807402eaa9839fbdf4a8c1111d0` |

Check the download before uploading it:

```sh
md5sum kac-0.7.3.bin        # Linux
md5 kac-0.7.3.bin           # macOS
certutil -hashfile kac-0.7.3.bin MD5   # Windows
```
