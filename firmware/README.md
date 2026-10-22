# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.8.11.bin](kac-0.8.11.bin) | 0.8.11 | 468352 bytes | `a59a2958512506b1c1ab8f4d3ae35363` | `0ba9a4808bb53a3ab0aec8a035b3dfbe270ffeb5519661a2c0e68a471ccf0e55` |

Check the download before uploading it:

```sh
md5sum kac-0.8.11.bin        # Linux
md5 kac-0.8.11.bin           # macOS
certutil -hashfile kac-0.8.11.bin MD5   # Windows
```
