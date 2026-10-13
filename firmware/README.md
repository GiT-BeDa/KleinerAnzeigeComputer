# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.5.2.bin](kac-0.5.2.bin) | 0.5.2 | 401280 bytes | `4b78b5bc62764a312fa9580807b41947` | `4ab06346ec1232f179c58f3861ac28b0874e0bf417dae97b04699902531038b0` |

Check the download before uploading it:

```sh
md5sum kac-0.5.2.bin        # Linux
md5 kac-0.5.2.bin           # macOS
certutil -hashfile kac-0.5.2.bin MD5   # Windows
```
