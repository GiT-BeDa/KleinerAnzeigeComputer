# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.4.3.bin](kac-0.4.3.bin) | 0.4.3 | 396224 bytes | `48d4c516f89145434a3e26c36211ad10` | `39284cc29c893403969ada794e5f5e9786872abe2d7e4c98d2e08b3e15ab05b0` |

Check the download before uploading it:

```sh
md5sum kac-0.4.3.bin        # Linux
md5 kac-0.4.3.bin           # macOS
certutil -hashfile kac-0.4.3.bin MD5   # Windows
```
