# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.7.0.bin](kac-0.7.0.bin) | 0.7.0 | 407776 bytes | `9fc186a25a06952ce04fcfee9ba552c5` | `7491816fc0f6ca0c290a81f8cf83632c4e15a213cb23b626d11d638a70ae3ee5` |

Check the download before uploading it:

```sh
md5sum kac-0.7.0.bin        # Linux
md5 kac-0.7.0.bin           # macOS
certutil -hashfile kac-0.7.0.bin MD5   # Windows
```
