# Ready-made KAC images

Built with `pio run` from the commit that added the file, **without** a key: every device creates its own
key on first start (see the main README, chapter 4).

| File | Version | Size | MD5 | SHA-256 |
|---|---|---|---|---|
| [kac-0.3.3.bin](kac-0.3.3.bin) | 0.3.3 | 394672 bytes | `cf0927dbc4e218216e7548fa29a6d861` | `c85fff08e65190f01ea6b4070f312db9e013156c7563f3db734857f61359546f` |

Check the download before uploading it:

```sh
md5sum kac-0.3.3.bin        # Linux
md5 kac-0.3.3.bin           # macOS
certutil -hashfile kac-0.3.3.bin MD5   # Windows
```
