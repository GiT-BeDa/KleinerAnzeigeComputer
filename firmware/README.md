# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

Older images are in the Git history.

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.8.10.bin](kac-0.8.10.bin) | 0.8.10 | 467952 bytes | `38421f5006d0ba3e5a6f4cc68b76ff5b` | `1534b09e6c74f057577117f2ec2751f1ed87d3529a7906a46b72d3e126937b88` |

Check the download before uploading it:

```sh
md5sum kac-0.8.10.bin        # Linux
md5 kac-0.8.10.bin           # macOS
certutil -hashfile kac-0.8.10.bin MD5   # Windows
```
