# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.7.2.bin](kac-0.7.2.bin) | 0.7.2 | 407712 bytes | `312c730c86f6e23beb872585fa71df4d` | `432863298b044f5f1bc8661bc6a570ee4fdf385d8096feaf0eaf45c4f01c212b` |

Check the download before uploading it:

```sh
md5sum kac-0.7.2.bin        # Linux
md5 kac-0.7.2.bin           # macOS
certutil -hashfile kac-0.7.2.bin MD5   # Windows
```
