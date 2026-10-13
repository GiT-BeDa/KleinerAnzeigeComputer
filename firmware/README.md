# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.7.1.bin](kac-0.7.1.bin) | 0.7.1 | 407696 bytes | `dde632e8ca0b1f97dc2a5627633ba1ca` | `e0f3276629ea7ce93c621ecaeba08f8ce093870490f00b94e7c5bf00d7196a81` |

Check the download before uploading it:

```sh
md5sum kac-0.7.1.bin        # Linux
md5 kac-0.7.1.bin           # macOS
certutil -hashfile kac-0.7.1.bin MD5   # Windows
```
